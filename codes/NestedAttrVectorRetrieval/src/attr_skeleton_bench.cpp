// Attribute-Skeleton Index (ASI)：保留属性 DAG 的资格闭包，并将属性内对象图接成共享搜索图。
//
// 构建：
//   1. 每个非空直属属性独立构建 hnswlib HNSW，底层供 DQS，高层可选定位入口。
//   2. 属性内 farthest-first 代表点以 MST 双向连接；不补普通对象的连通边。
//   3. 逆拓扑处理属性 DAG，为父代表点连接子属性中的最近对象；跨属性边双向冻结，repair 另加边。
//   4. 为包含空属性的完整 DAG 构建后代闭包位图。
//
// 查询：全闭包 D 每个非空属性选代表点或高层入口作种子；offer 先去重、再检查资格。
// q1 收所有范围内对象，q2 独立尝试接纳代表点或网关；两条通道各有 cand 与 keep，
// cand 共同驱动展开，最终合并两个 keep、按 ID 去重并取 top-k。
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "hnswlib/hnswlib.h"
#include "raw_l2_space.h"
#include "native_graph_audit.h"

static constexpr int DIM = 128;
using Vec = std::vector<uint32_t>;

static RawL2Space g_space(DIM);
static inline float dist2(const float* a, const float* b) { return g_space.dist(a, b); }

// ---------------- L1：属性内局部图 ----------------
struct AttrGraph {
    Vec members;                 // 局部 id -> 全局对象 id
    std::vector<Vec> adj;        // 局部邻接
    Vec reps;                    // 代表点（局部 id）
    uint32_t entry = 0;
    uint32_t hnsw_entry = 0;
    int max_level = 0;
    Vec upper_row, upper_off, upper_dat;  // 节点 -> 高层行 -> 邻接，层号从 1 开始
};


// 导出原生 HNSW 的底层与高层；MST 只加入底层，高层仅供入口定位。
static void build_attr_graph(const float* base, const Vec& ids, int M, int efC, int nrep,
                             int seed, int concept, FILE* audit, AttrGraph& g) {
    const int n = int(ids.size());
    g.members = ids;
    g.adj.assign(size_t(n), {});
    if (n == 0) return;

    RawL2Space space(DIM);
    hnswlib::HierarchicalNSW<float> idx(&space, size_t(n), M, efC, seed);
    for (int i = 0; i < n; ++i) idx.addPoint(base + size_t(ids[size_t(i)]) * DIM, hnswlib::labeltype(i));
    if (audit) write_native_graph_audit(audit, concept, idx);
    g.hnsw_entry = idx.enterpoint_node_;
    g.max_level = idx.maxlevel_;
    g.upper_row.resize(size_t(n) + 1);
    g.upper_off.push_back(0);
    for (int i = 0; i < n; ++i) {
        g.upper_row[size_t(i)] = uint32_t(g.upper_off.size() - 1);
        for (int level = 1; level <= idx.element_levels_[size_t(i)]; ++level) {
            auto* links = idx.get_linklist(hnswlib::tableint(i), level);
            const size_t count = idx.getListCount(links);
            const auto* neighbors = reinterpret_cast<const hnswlib::tableint*>(links + 1);
            g.upper_dat.insert(g.upper_dat.end(), neighbors, neighbors + count);
            g.upper_off.push_back(uint32_t(g.upper_dat.size()));
        }
    }
    g.upper_row[size_t(n)] = uint32_t(g.upper_off.size() - 1);

    // 取 layer 0 邻接
    for (int i = 0; i < n; ++i) {
        int* data = (int*) idx.get_linklist0(hnswlib::tableint(i));
        const size_t cnt = idx.getListCount((hnswlib::linklistsizeint*) data);
        auto& L = g.adj[size_t(i)];
        L.reserve(cnt);
        for (size_t j = 0; j < cnt; ++j) {
            const int nb = data[j + 1];
            if (nb >= 0 && nb < n && nb != i) L.push_back(uint32_t(nb));
        }
    }

    // 代表点：farthest-first
    const int K2 = std::max(1, std::min(n, nrep));
    g.reps.clear();
    g.reps.push_back(0);
    std::vector<float> mind(size_t(n), INFINITY);
    for (int r = 1; r < K2; ++r) {
        const uint32_t last = g.reps.back();
        const float* pl = base + size_t(ids[last]) * DIM;
        float far = -1; uint32_t bid = 0;
        for (int i = 0; i < n; ++i) {
            const float d = dist2(pl, base + size_t(ids[size_t(i)]) * DIM);
            if (d < mind[size_t(i)]) mind[size_t(i)] = d;
            if (mind[size_t(i)] > far) { far = mind[size_t(i)]; bid = uint32_t(i); }
        }
        g.reps.push_back(bid);
    }
    // 代表点 MST，边并入局部图（保持双向）
    const int R = int(g.reps.size());
    if (R >= 2) {
        std::vector<char> in(size_t(R), 0);
        std::vector<float> key(size_t(R), INFINITY);
        std::vector<int> par(size_t(R), -1);
        key[0] = 0;
        for (int it = 0; it < R; ++it) {
            int u = -1;
            for (int k2 = 0; k2 < R; ++k2) if (!in[size_t(k2)] && (u < 0 || key[size_t(k2)] < key[size_t(u)])) u = k2;
            in[size_t(u)] = 1;
            for (int k2 = 0; k2 < R; ++k2) {
                if (in[size_t(k2)]) continue;
                const float d = dist2(base + size_t(ids[g.reps[size_t(u)]]) * DIM,
                                      base + size_t(ids[g.reps[size_t(k2)]]) * DIM);
                if (d < key[size_t(k2)]) { key[size_t(k2)] = d; par[size_t(k2)] = u; }
            }
        }
        for (int k2 = 1; k2 < R; ++k2) {
            if (par[size_t(k2)] < 0) continue;
            const uint32_t a = g.reps[size_t(k2)], b = g.reps[size_t(par[size_t(k2)])];
            g.adj[a].push_back(b);
            g.adj[b].push_back(a);
        }
    }
}

static std::vector<uint32_t> g_obj_of;   // 全局节点号 -> 对象 id
static inline uint32_t node_objid(uint32_t node) { return g_obj_of[node]; }

static bool readf(const std::string& p, std::vector<float>& v) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long b = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    v.resize(size_t(b) / sizeof(float));
    const bool ok = std::fread(v.data(), sizeof(float), v.size(), f) == v.size();
    std::fclose(f);
    return ok;
}

int main(int argc, char** argv) {
    std::string dir = "datasets/sift1m/ccs-attr", work = "datasets/sift1m/ccs-attr", vec = "base.bin";
    int k = 50, M = 16, efC = 200, nq_max = 200, Kcap = 8, repeat = 1, pdiv = 50, reps_topL = 32;
    bool skip_links = true;
    bool do_merge = true;
    bool diag = false;
    std::string trace_file, graph_audit_file, entry_mode = "representative";
    bool rerank_fp32 = false;
    int rerank_pool = 0;
    std::vector<int> efs{50};
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto nx = [&]() { return std::string(argv[++i]); };
        if (a == "--dir") dir = nx();
        else if (a == "--work") work = nx();
        else if (a == "--vec") vec = nx();
        else if (a == "--k") k = std::stoi(nx());
        else if (a == "--M") M = std::stoi(nx());
        else if (a == "--efC") efC = std::stoi(nx());
        else if (a == "--K") Kcap = std::stoi(nx());
        else if (a == "--p") pdiv = std::max(1, std::stoi(nx()));
        else if (a == "--no-skip") skip_links = false;
        else if (a == "--no-merge") do_merge = false;
        else if (a == "--diag") diag = true;
        else if (a == "--queries") nq_max = std::stoi(nx());
        else if (a == "--trace-file") trace_file = nx();
        else if (a == "--entry-mode") entry_mode = nx();
        else if (a == "--reps-topL") reps_topL = std::max(0, std::stoi(nx()));
        else if (a == "--graph-audit-file") graph_audit_file = nx();
        else if (a == "--rerank-fp32") rerank_fp32 = true;
        else if (a == "--rerank-pool") rerank_pool = std::stoi(nx());
        else if (a == "--repeat") repeat = std::max(1, std::stoi(nx()));
        else if (a == "--efs") { efs.clear(); std::stringstream ss(nx()); std::string t;
            while (std::getline(ss, t, ',')) if (!t.empty()) efs.push_back(std::stoi(t)); }
    }
    if (entry_mode != "representative" && entry_mode != "hnsw") {
        std::fprintf(stderr, "entry-mode must be representative or hnsw\n");
        return 2;
    }

    std::vector<float> base, queries;
    if (!readf(dir + "/" + vec, base) || !readf(work + "/queries.bin", queries)) return 2;
    const size_t n = base.size() / DIM;
    const int nq = std::min<int>(int(queries.size() / DIM), nq_max);

    std::vector<int> owner;
    { std::ifstream in(work + "/objects.txt"); std::string l; while (std::getline(in, l)) if (!l.empty()) owner.push_back(std::stoi(l)); }
    std::unordered_map<int, std::vector<int>> children, parents;
    { std::ifstream in(work + "/edges.tsv"); std::string l;
      while (std::getline(in, l)) { if (l.empty()) continue; std::stringstream ss(l); int c, p; ss >> c >> p;
        children[p].push_back(c); parents[c].push_back(p); } }
    std::vector<std::vector<int>> qc;
    { std::ifstream in(work + "/queries.tsv"); std::string l;
      while (std::getline(in, l) && int(qc.size()) < nq) { if (l.empty()) continue;
        std::stringstream ss(l); int cnt; ss >> cnt; std::vector<int> v(cnt); for (int i = 0; i < cnt; ++i) ss >> v[i]; qc.push_back(v); } }
    std::vector<int> gt(size_t(nq) * k, -1);
    { FILE* f = std::fopen((work + "/gt_" + std::to_string(k) + ".txt").c_str(), "rb");
      if (f) { if (std::fread(gt.data(), sizeof(int32_t), gt.size(), f) != gt.size()) {} std::fclose(f); } }
    std::vector<float> original;
    FILE* trace = nullptr;
    if (!trace_file.empty() || rerank_fp32) {
        // orig.bin 存在则用原始 FP32；缺省时与搜索向量同源（无量化误差）。
        if (!readf(dir + "/orig.bin", original)) original = base;
        if (original.size() != base.size()) {
            std::fprintf(stderr, "original vector dimensions do not match search vectors\n");
            return 2;
        }
    }
    if (!trace_file.empty()) {
        trace = std::fopen(trace_file.c_str(), "w");
        if (!trace) { std::perror(trace_file.c_str()); return 2; }
    }

    // 属性全集与编号
    std::vector<int> aids;
    std::unordered_map<int,int> apos;
    auto adda = [&](int a) { if (apos.emplace(a, int(aids.size())).second) aids.push_back(a); };
    for (auto& kv : children) { adda(kv.first); for (int ch : kv.second) adda(ch); }
    for (int o : owner) adda(o);
    const int A = int(aids.size());

    std::vector<int> depth(size_t(A), 0);
    {
        std::vector<int> q2;
        for (int i = 0; i < A; ++i) if (parents[aids[size_t(i)]].empty()) q2.push_back(i);
        std::vector<char> seen(size_t(A), 0);
        for (size_t h = 0; h < q2.size(); ++h) {
            const int u = q2[h];
            if (seen[size_t(u)]) continue;
            seen[size_t(u)] = 1;
            auto it = children.find(aids[size_t(u)]);
            if (it == children.end()) continue;
            for (int ch : it->second) {
                auto ci = apos.find(ch);
                if (ci == apos.end()) continue;
                depth[size_t(ci->second)] = depth[size_t(u)] + 1;
                if (!seen[size_t(ci->second)]) q2.push_back(ci->second);
            }
        }
    }

    // L1：属性内局部图
    std::vector<AttrGraph> ag(size_t(A), AttrGraph{});
    {
        std::vector<Vec> mem(size_t(A), Vec{});
        for (size_t i = 0; i < n; ++i) mem[size_t(apos[owner[i]])].push_back(uint32_t(i));
        FILE* audit = graph_audit_file.empty() ? nullptr : std::fopen(graph_audit_file.c_str(), "w");
        if (!graph_audit_file.empty() && !audit) { std::perror(graph_audit_file.c_str()); return 2; }
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < A; ++i) {
            const int nrep = std::max(1, std::min(Kcap, int(mem[size_t(i)].size()) / std::max(1, pdiv) + 1));
            build_attr_graph(base.data(), mem[size_t(i)], M, efC, nrep,
                             100 + aids[size_t(i)], aids[size_t(i)], audit, ag[size_t(i)]);
        }
        if (audit) std::fclose(audit);
        std::fprintf(stderr, "[L1] %d attribute graphs in %.1fs (M=%d efC=%d p=%d Kcap=%d)\n", A,
                     std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), M, efC, pdiv, Kcap);
    }
    size_t upper_index_bytes = 0;
    for (const auto& graph : ag)
        upper_index_bytes += sizeof(uint32_t) * (graph.upper_row.size() + graph.upper_off.size() + graph.upper_dat.size());

    // 全局图：属性图拼接 + 单向门户/跳转边（独立表）
    std::vector<uint32_t> abase(size_t(A), 0);
    std::vector<uint32_t> attr_of, obj_of, obj2node;
    {
        size_t tot = 0;
        for (int i = 0; i < A; ++i) { abase[size_t(i)] = uint32_t(tot); tot += ag[size_t(i)].members.size(); }
        attr_of.assign(tot, 0);
        obj_of.assign(tot, 0);
        obj2node.assign(n, 0);
        for (int i = 0; i < A; ++i)
            for (size_t j = 0; j < ag[size_t(i)].members.size(); ++j) {
                attr_of[abase[size_t(i)] + j] = uint32_t(i);
                obj_of[abase[size_t(i)] + j] = ag[size_t(i)].members[j];
                obj2node[ag[size_t(i)].members[j]] = abase[size_t(i)] + uint32_t(j);
            }
        g_obj_of = obj_of;
    }
    std::vector<Vec> gadj(size_t(n), Vec{}), gbr(size_t(n), Vec{});
    for (int i = 0; i < A; ++i) {
        const uint32_t off = abase[size_t(i)];
        for (size_t j = 0; j < ag[size_t(i)].adj.size(); ++j)
            for (uint32_t w : ag[size_t(i)].adj[size_t(j)]) gadj[off + j].push_back(off + w);
    }
    std::vector<char> is_rep(size_t(n), 0), is_gw(size_t(n), 0);
    // 跨属性网关边独立存储并冻结；原始局部边与代表点 MST 边均不剪除。
    // 拓扑序（子先于父）
    std::vector<int> topo;
    {
        std::vector<int> indeg(size_t(A), 0);
        for (auto& kv : children) {
            auto pit = apos.find(kv.first);
            if (pit == apos.end()) continue;
            for (int ch : kv.second) { auto cit = apos.find(ch); if (cit != apos.end()) ++indeg[size_t(cit->second)]; }
        }
        std::vector<int> q;
        for (int i = 0; i < A; ++i) if (indeg[size_t(i)] == 0) q.push_back(i);
        for (size_t h = 0; h < q.size(); ++h) {
            const int u = q[h];
            topo.push_back(u);
            auto it = children.find(aids[size_t(u)]);
            if (it == children.end()) continue;
            for (int ch : it->second) {
                auto cit = apos.find(ch);
                if (cit == apos.end()) continue;
                if (--indeg[size_t(cit->second)] == 0) q.push_back(cit->second);
            }
        }
    }
    // 自底向上：先处理深度大的属性（把子树的代表点合并进父侧）
    std::reverse(topo.begin(), topo.end());

    // Cross-attribute gateway adjacency is stored separately in gbr.
    long n_merge_edges = 0;
    for (int ci : topo) {
        if (!do_merge) break;
        if (ag[size_t(ci)].members.empty()) continue;
        const int cid = aids[size_t(ci)];
        // 候选父：直接父；若直接父无成员（无代表点），继续上溯至最近有成员的祖先
        std::vector<int> up;
        {
            std::vector<int> cur;
            for (int pch : parents[cid]) { auto it = apos.find(pch); if (it != apos.end()) cur.push_back(it->second); }
            std::unordered_set<int> seen;
            for (int lvl = 0; lvl < 8 && !cur.empty(); ++lvl) {
                std::vector<int> nxt;
                for (int a : cur) {
                    if (!seen.insert(a).second) continue;
                    if (!ag[size_t(a)].members.empty()) { up.push_back(a); continue; }   // 找到有代表的祖先
                    for (int pp : parents[aids[size_t(a)]]) { auto it = apos.find(pp); if (it != apos.end()) nxt.push_back(it->second); }
                }
                cur.swap(nxt);
            }
        }
        for (int pi : up) {
            // 合并：父代表点 ↔ 子代表点（双向），并立即更新双方的邻居表
            // 网关边：派给父代表点 → 子属性中**距它最近的对象**（几何相邻处接入）
            for (uint32_t rp : ag[size_t(pi)].reps) {
                const uint32_t gp = abase[size_t(pi)] + rp;
                const float* pp = base.data() + size_t(ag[size_t(pi)].members[rp]) * DIM;
                float best = INFINITY; uint32_t bid = 0;
                for (size_t j = 0; j < ag[size_t(ci)].members.size(); ++j) {
                    const float d = dist2(pp, base.data() + size_t(ag[size_t(ci)].members[j]) * DIM);
                    if (d < best) { best = d; bid = uint32_t(j); }
                }
                const uint32_t gc = abase[size_t(ci)] + bid;
                gbr[gp].push_back(gc);
                gbr[gc].push_back(gp);
                is_gw[gc] = 1;          // 网关节点也是骨架节点：可走 q2 通道
                ++n_merge_edges;
                // 合并边**冻结**：不参与任何剪枝
            }
        }
    }
    // 兜底修复：仍然没有入边的属性，强制连一条到最近有成员祖先的代表点
    long n_repair = 0;
    {
        std::vector<uint32_t> rep_attr0(size_t(n), UINT32_MAX);
        for (int i = 0; i < A; ++i)
            for (uint32_t r : ag[size_t(i)].reps) rep_attr0[abase[size_t(i)] + r] = uint32_t(i);
        std::vector<long> indeg0(size_t(A), 0);
        for (size_t node = 0; node < n; ++node) {
            if (rep_attr0[node] == UINT32_MAX) continue;
            for (uint32_t t : gbr[node])
                if (rep_attr0[t] != UINT32_MAX && rep_attr0[t] != rep_attr0[node]) ++indeg0[rep_attr0[t]];
        }
        for (int i = 0; i < A; ++i) {
            if (ag[size_t(i)].members.empty() || indeg0[size_t(i)] > 0) continue;
            // 向上找最近有成员的祖先
            std::vector<int> cur;
            for (int pp : parents[aids[size_t(i)]]) { auto it = apos.find(pp); if (it != apos.end()) cur.push_back(it->second); }
            std::unordered_set<int> seen;
            int found = -1;
            for (int lvl = 0; lvl < 8 && !cur.empty() && found < 0; ++lvl) {
                std::vector<int> nxt;
                for (int a : cur) {
                    if (!seen.insert(a).second) continue;
                    if (!ag[size_t(a)].members.empty()) { found = a; break; }
                    for (int pp : parents[aids[size_t(a)]]) { auto it = apos.find(pp); if (it != apos.end()) nxt.push_back(it->second); }
                }
                cur.swap(nxt);
            }
            if (found < 0) continue;
            for (uint32_t rc : ag[size_t(i)].reps)
                for (uint32_t rp : ag[size_t(found)].reps) {
                    gbr[abase[size_t(found)] + rp].push_back(abase[size_t(i)] + rc);
                    gbr[abase[size_t(i)] + rc].push_back(abase[size_t(found)] + rp);
                    is_gw[abase[size_t(i)] + rc] = 1;
                    ++n_repair;
                }
        }
    }
    std::fprintf(stderr, "[L2-merge] %ld cross-attribute edges (+%ld repaired); local and MST edges retained\n",
                 n_merge_edges, n_repair);
    // 统计：属性层入边（本属性任一代表点，是否有来自**其他属性**代表点的边）
    {
        std::vector<long> in_deg(size_t(A), 0);
        std::vector<uint32_t> rep_attr(size_t(n), UINT32_MAX);
        for (int i = 0; i < A; ++i)
            for (uint32_t r : ag[size_t(i)].reps) rep_attr[abase[size_t(i)] + r] = uint32_t(i);
        for (size_t node = 0; node < n; ++node) {
            if (rep_attr[node] == UINT32_MAX) continue;
            for (uint32_t t : gbr[node])
                if (rep_attr[t] != UINT32_MAX && rep_attr[t] != rep_attr[node]) ++in_deg[rep_attr[t]];
        }
        long with_mem = 0, no_in = 0, no_in_items = 0, tot_items = 0;
        std::vector<std::string> samples;
        for (int i = 0; i < A; ++i) {
            if (ag[size_t(i)].members.empty()) continue;
            ++with_mem;
            tot_items += long(ag[size_t(i)].members.size());
            if (in_deg[size_t(i)] == 0) {
                ++no_in;
                no_in_items += long(ag[size_t(i)].members.size());
                if (samples.size() < 8) samples.push_back("a" + std::to_string(aids[size_t(i)]) + "(n=" + std::to_string(ag[size_t(i)].members.size()) + ")");
            }
        }
        std::fprintf(stderr, "[coverage] attrs with members=%ld; zero-in-edge=%ld (%.1f%%); their items=%ld (%.1f%% of %ld)\n",
                     with_mem, no_in, 100.0 * double(no_in) / double(with_mem),
                     no_in_items, 100.0 * double(no_in_items) / double(tot_items), tot_items);
        std::fprintf(stderr, "[coverage] samples:");
        for (auto& x : samples) std::fprintf(stderr, " %s", x.c_str());
        std::fprintf(stderr, "\n");
    }

    // 扁平化
    std::vector<uint32_t> adj_off(size_t(n) + 1, 0), adj_dat, br_off(size_t(n) + 1, 0), br_dat;
    {
        size_t t1 = 0, t2 = 0;
        for (size_t i = 0; i < n; ++i) { adj_off[i] = uint32_t(t1); t1 += gadj[i].size(); br_off[i] = uint32_t(t2); t2 += gbr[i].size(); }
        adj_off[n] = uint32_t(t1); br_off[n] = uint32_t(t2);
        adj_dat.resize(t1); br_dat.resize(t2);
        size_t w1 = 0, w2 = 0;
        for (size_t i = 0; i < n; ++i) {
            for (uint32_t x : gadj[i]) adj_dat[w1++] = x;
            for (uint32_t x : gbr[i]) br_dat[w2++] = x;
        }
    }
    size_t nrep_tot = 0;
    for (int i = 0; i < A; ++i)
        for (uint32_t r : ag[size_t(i)].reps) { is_rep[abase[size_t(i)] + r] = 1; ++nrep_tot; }
    std::fprintf(stderr, "[L2] %zu rep nodes; graph %zu intra edges (%.1f/node)\n",
                 nrep_tot, adj_dat.size(), double(adj_dat.size()) / double(n));

    // L3：闭包位图（覆盖全部属性，含无对象的中间属性）
    const int words = (A + 63) / 64;
    std::vector<std::vector<uint64_t>> desc(size_t(A), std::vector<uint64_t>(size_t(words), 0));
    {
        std::vector<int> state(size_t(A), 0);
        std::function<void(int)> dfs = [&](int u) {
            if (state[size_t(u)] == 2) return;
            state[size_t(u)] = 1;
            auto it = children.find(aids[size_t(u)]);
            if (it != children.end()) for (int ch : it->second) {
                auto ci = apos.find(ch);
                if (ci == apos.end()) continue;
                dfs(ci->second);
                for (int w = 0; w < words; ++w) desc[size_t(u)][size_t(w)] |= desc[size_t(ci->second)][size_t(w)];
            }
            desc[size_t(u)][size_t(u) / 64] |= (uint64_t(1) << (u % 64));
            state[size_t(u)] = 2;
        };
        for (int i = 0; i < A; ++i) dfs(i);
    }
    std::fprintf(stderr, "[L3] %d bitmaps x %d words = %.2f MB\n", A, words,
                 double(A) * words * 8.0 / 1048576.0);

    // 诊断：对前若干查询做"范围内全图 BFS"，检验 D 导出子图是否连通
    if (diag) {
        std::vector<uint32_t> seen(size_t(n), 0);
        for (int qi = 0; qi < std::min(nq, 5); ++qi) {
            std::vector<uint64_t> bm(size_t(words), 0);
            for (int c : qc[qi]) { auto it = apos.find(c); if (it != apos.end()) {
                const auto& d = desc[size_t(it->second)];
                for (int w = 0; w < words; ++w) bm[size_t(w)] |= d[size_t(w)]; } }
            auto in_scope = [&](uint32_t gi) { const uint32_t a = attr_of[gi]; return ((bm[a/64] >> (a%64)) & 1ull) != 0; };
            std::vector<int> inD;
            for (int w = 0; w < words; ++w) { uint64_t x = bm[size_t(w)];
                while (x) { const int b = __builtin_ctzll(x); x &= x-1; const int a = w*64+b;
                    if (a < A && !ag[size_t(a)].members.empty()) inD.push_back(a); } }
            // BFS
            std::vector<uint32_t> st;
            for (int c : qc[qi]) { auto it = apos.find(c); if (it == apos.end()) continue;
                for (uint32_t r : ag[size_t(it->second)].reps) st.push_back(abase[size_t(it->second)] + r); }
            std::unordered_set<uint32_t> vis(st.begin(), st.end());
            std::unordered_set<int> attr_seen;
            while (!st.empty()) {
                const uint32_t u = st.back(); st.pop_back();
                if (!in_scope(u)) continue;
                attr_seen.insert(int(attr_of[u]));
                auto go = [&](uint32_t v) { if (in_scope(v) && vis.insert(v).second) st.push_back(v); };
                for (uint32_t j = adj_off[u]; j < adj_off[u+1]; ++j) go(adj_dat[j]);
                for (uint32_t j = br_off[u]; j < br_off[u+1]; ++j) go(br_dat[j]);
            }
            std::fprintf(stderr, "[diag q%d] |D|=%zu  BFS 可达属性=%zu", qi, inD.size(), attr_seen.size());
            int miss_attr = 0; std::string names;
            for (int a : inD) if (!attr_seen.count(a)) { ++miss_attr; if (miss_attr <= 4) names += " a" + std::to_string(aids[size_t(a)]); }
            std::fprintf(stderr, "  不可达属性=%d%s\n", miss_attr, names.c_str());
        }
    }

    std::vector<uint32_t> vstamp(size_t(n), 0);
    std::vector<uint32_t> expanded_stamp(size_t(n), 0), seed_score_stamp(size_t(n), 0);
    std::vector<float> seed_score(size_t(n), 0.f);
    uint32_t token = 0;
    std::vector<uint64_t> bm(size_t(words), 0);

    for (int ef : efs) {
        long hit = 0, denom = 0, dists = 0, elig_sum = 0, entered_sum = 0, inD_sum = 0, seeds_sum = 0;
        long miss_attr_not_entered = 0, miss_item_not_visited = 0, miss_ranked_out = 0, gt_total = 0, miss_rejected = 0, touched_sum = 0;
        long expanded_sum = 0, adjacency_slots_sum = 0, distance_cache_hits_sum = 0;
        long upper_distances_sum = 0, upper_slots_sum = 0, rerank_calls_sum = 0;
        long reps_scored_sum = 0, reps_expanded_sum = 0;
        double query_seconds = 0;
        const auto t1 = std::chrono::steady_clock::now();
        for (int round = 0; round < repeat; ++round)
        for (int qi = 0; qi < nq; ++qi) {
            const auto query_start = std::chrono::steady_clock::now();
            std::vector<uint32_t> evaluated, admitted, seed_evaluated, expanded, seed_nodes;
            const long distances_before = dists;
            long query_expanded = 0, query_adjacency_slots = 0, query_cache_hits = 0;
            long query_upper_distances = 0, query_upper_slots = 0, query_rerank_calls = 0;
            long query_reps_scored = 0, query_reps_expanded = 0;
            std::fill(bm.begin(), bm.end(), 0ull);
            for (int c : qc[qi]) {
                auto it = apos.find(c);
                if (it == apos.end()) continue;
                const auto& d = desc[size_t(it->second)];
                for (int w = 0; w < words; ++w) bm[size_t(w)] |= d[size_t(w)];
            }
            auto eligible = [&](uint32_t gi) {
                const uint32_t a = attr_of[gi];
                return ((bm[a / 64] >> (a % 64)) & 1ull) != 0;
            };
            std::vector<int> inD;
            for (int w = 0; w < words; ++w) {
                uint64_t x = bm[size_t(w)];
                while (x) {
                    const int b = __builtin_ctzll(x);
                    x &= x - 1;
                    const int a = w * 64 + b;
                    if (a < A && !ag[size_t(a)].members.empty()) inD.push_back(a);
                }
            }
            if (inD.empty()) continue;
            inD_sum += long(inD.size());
            for (int a : inD) elig_sum += long(ag[size_t(a)].members.size());
            const float* q = queries.data() + size_t(qi) * DIM;
            ++token;

            std::priority_queue<std::pair<float,uint32_t>> q1_cand, q1_keep;
            std::unordered_set<uint32_t> entered, touched;
            // 缓存评分不设置 first-touch：未选中的代表点仍须正常 offer。
            auto seed_distance = [&](uint32_t gi) {
                if (seed_score_stamp[gi] == token) {
                    ++query_cache_hits;
                    return seed_score[gi];
                }
                const float d = dist2(q, base.data() + size_t(obj_of[gi]) * DIM);
                ++dists;
                seed_score_stamp[gi] = token;
                seed_score[gi] = d;
                return d;
            };
            auto offer = [&](uint32_t gi) {
                if (vstamp[gi] == token) return;      // 首次接触即标记，距离只算一次
                vstamp[gi] = token;
                if (!eligible(gi)) return;            // 范围外：连距离都不算，不占预算
                touched.insert(attr_of[gi]);
                float d;
                if (seed_score_stamp[gi] == token) {
                    d = seed_score[gi];
                    ++query_cache_hits;
                } else {
                    d = dist2(q, base.data() + size_t(obj_of[gi]) * DIM);
                    ++dists;
                }
                if (trace && round == 0) evaluated.push_back(obj_of[gi]);
                bool any = false;
                if (int(q1_keep.size()) < ef) { q1_keep.push({d, gi}); q1_cand.push({-d, gi}); any = true; }
                else if (d < q1_keep.top().first) { q1_keep.pop(); q1_keep.push({d, gi}); q1_cand.push({-d, gi}); any = true; }
                if (any) entered.insert(attr_of[gi]);
                if (any && trace && round == 0) admitted.push_back(obj_of[gi]);
            };
            auto expand = [&](uint32_t gi) {
                if (expanded_stamp[gi] == token) return;
                expanded_stamp[gi] = token;
                ++query_expanded;
                query_adjacency_slots += long(adj_off[gi + 1] - adj_off[gi]);
                query_adjacency_slots += long(br_off[gi + 1] - br_off[gi]);
                if (trace && round == 0) expanded.push_back(obj_of[gi]);
                for (uint32_t j = adj_off[gi]; j < adj_off[gi + 1]; ++j) offer(adj_dat[j]);
                for (uint32_t j = br_off[gi]; j < br_off[gi + 1]; ++j) offer(br_dat[j]);
            };
            // 代表点独立检索：对闭包内全部代表点评分取 top-L，逐个放入独立队列并强制展开。
            if (reps_topL > 0) {
                std::priority_queue<std::pair<float,uint32_t>> top;
                for (int a : inD) {
                    const uint32_t off = abase[size_t(a)];
                    for (uint32_t r : ag[size_t(a)].reps) {
                        const uint32_t gi = off + r;
                        const float d = seed_distance(gi);
                        ++query_reps_scored;
                        if (trace && round == 0) seed_evaluated.push_back(obj_of[gi]);
                        if (int(top.size()) < reps_topL) top.push({d, gi});
                        else if (d < top.top().first) { top.pop(); top.push({d, gi}); }
                    }
                }
                std::vector<std::pair<float,uint32_t>> repq;
                while (!top.empty()) { repq.push_back(top.top()); top.pop(); }
                std::sort(repq.begin(), repq.end());
                for (const auto& pr : repq) {
                    if (expanded_stamp[pr.second] == token) continue;
                    expand(pr.second);
                    ++query_reps_expanded;
                    if (trace && round == 0) seed_nodes.push_back(obj_of[pr.second]);
                }
            } else
            // 对照：每个非空直属属性只选一个入口。
            for (int a : inD) {
                const auto& graph = ag[size_t(a)];
                const uint32_t offset = abase[size_t(a)];
                auto score_seed = [&](uint32_t node) {
                    if (trace && round == 0) seed_evaluated.push_back(obj_of[offset + node]);
                    return seed_distance(offset + node);
                };
                uint32_t bi;
                if (entry_mode == "hnsw") {
                    const long before = dists;
                    bi = graph.hnsw_entry;
                    float bd = score_seed(bi);
                    for (int level = graph.max_level; level > 0; --level) {
                        bool changed = true;
                        while (changed) {
                            changed = false;
                            const uint32_t row = graph.upper_row[bi] + uint32_t(level - 1);
                            query_upper_slots += long(graph.upper_off[row + 1] - graph.upper_off[row]);
                            for (uint32_t j = graph.upper_off[row]; j < graph.upper_off[row + 1]; ++j) {
                                const uint32_t node = graph.upper_dat[j];
                                const float distance = score_seed(node);
                                if (distance < bd) { bd = distance; bi = node; changed = true; }
                            }
                        }
                    }
                    query_upper_distances += dists - before;
                } else {
                    bi = graph.entry;
                    float bd = score_seed(bi);
                    for (uint32_t node : graph.reps) {
                        const float distance = score_seed(node);
                        if (distance < bd) { bd = distance; bi = node; }
                    }
                }
                ++seeds_sum;
                if (trace && round == 0) seed_nodes.push_back(obj_of[offset + bi]);
                offer(offset + bi);
            }
            while (!q1_cand.empty()) {
                const uint32_t gi = q1_cand.top().second;
                q1_cand.pop();
                expand(gi);   // pop 后去重，同一对象只展开一次
            }

            // 结果：合并两队列保留集，按距离取 top-k（只收合格对象）
            std::vector<std::pair<float,int>> out;
            {
                auto dump = [&](std::priority_queue<std::pair<float,uint32_t>> h) {
                    while (!h.empty()) {
                        const auto pr = h.top(); h.pop();
                        if (eligible(pr.second)) out.push_back({pr.first, int(obj_of[pr.second])});
                    }
                };
                dump(q1_keep);
            }
            std::sort(out.begin(), out.end());
            out.erase(std::unique(out.begin(), out.end(),
                      [](const std::pair<float,int>& a, const std::pair<float,int>& b) { return a.second == b.second; }),
                      out.end());
            const size_t keep = std::min<size_t>(k, out.size());
            // 可选：用原始 FP32 对同一候选集重排，距离计入查询成本。
            // --rerank-pool 限制两侧共同的重排预算；0 表示重排全部保留候选。
            if (rerank_fp32) {
                if (rerank_pool > 0 && out.size() > size_t(rerank_pool)) out.resize(size_t(rerank_pool));
                for (auto& item : out) {
                    item.first = dist2(q, original.data() + size_t(item.second) * DIM);
                    ++dists;
                    ++query_rerank_calls;
                }
                std::sort(out.begin(), out.end());
            }
            rerank_calls_sum += query_rerank_calls;
            reps_scored_sum += query_reps_scored;
            reps_expanded_sum += query_reps_expanded;
            expanded_sum += query_expanded;
            adjacency_slots_sum += query_adjacency_slots;
            distance_cache_hits_sum += query_cache_hits;
            adjacency_slots_sum += query_upper_slots;
            upper_distances_sum += query_upper_distances;
            upper_slots_sum += query_upper_slots;
            query_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - query_start).count();
            if (trace && round == 0) {
                // Reference scans are diagnostics only: never part of query throughput.
                std::vector<std::pair<float, uint32_t>> pq_exact, fp_exact;
                for (int a : inD) {
                    for (uint32_t id : ag[size_t(a)].members) {
                        pq_exact.emplace_back(dist2(q, base.data() + size_t(id) * DIM), id);
                        fp_exact.emplace_back(dist2(q, original.data() + size_t(id) * DIM), id);
                    }
                }
                const size_t ref_k = std::min<size_t>(k, pq_exact.size());
                std::partial_sort(pq_exact.begin(), pq_exact.begin() + ref_k, pq_exact.end());
                std::partial_sort(fp_exact.begin(), fp_exact.begin() + ref_k, fp_exact.end());
                const float pq_kth = ref_k ? pq_exact[ref_k - 1].first : 0.f;
                const float fp_kth = ref_k ? fp_exact[ref_k - 1].first : 0.f;
                auto ids_json = [&](const char* key, const std::vector<uint32_t>& values) {
                    std::fprintf(trace, ",\"%s\":[", key);
                    for (size_t j = 0; j < values.size(); ++j)
                        std::fprintf(trace, "%s%u", j ? "," : "", values[j]);
                    std::fprintf(trace, "]");
                };
                std::fprintf(trace, "{\"ef\":%d,\"reps_topL\":%d,\"p\":%d,\"qi\":%d,\"eligible\":%zu,\"pq_kth\":%.9g,\"fp_kth\":%.9g",
                             ef, reps_topL, pdiv, qi, pq_exact.size(), pq_kth, fp_kth);
                std::vector<uint32_t> pq_ids, fp_ids, result_ids, entered_attrs, touched_attrs;
                for (size_t j = 0; j < ref_k; ++j) {
                    pq_ids.push_back(pq_exact[j].second);
                    fp_ids.push_back(fp_exact[j].second);
                }
                for (size_t j = 0; j < keep; ++j) result_ids.push_back(uint32_t(out[j].second));
                for (uint32_t a : entered) entered_attrs.push_back(uint32_t(aids[a]));
                for (uint32_t a : touched) touched_attrs.push_back(uint32_t(aids[a]));
                ids_json("fp_top", fp_ids); ids_json("pq_top", pq_ids);
                ids_json("returned", result_ids); ids_json("evaluated", evaluated);
                ids_json("admitted", admitted); ids_json("seed_evaluated", seed_evaluated);
                ids_json("entered_attrs", entered_attrs); ids_json("touched_attrs", touched_attrs);
                ids_json("expanded", expanded);
                std::fprintf(trace, ",\"distance_calls\":%ld,\"distance_cache_hits\":%ld,\"adjacency_slots\":%ld",
                             dists - distances_before, query_cache_hits, query_adjacency_slots + query_upper_slots);
                ids_json("seed_nodes", seed_nodes);
                std::fprintf(trace, ",\"entry_mode\":\"%s\",\"upper_distance_calls\":%ld,\"upper_adjacency_slots\":%ld"
                             ",\"reps_scored\":%ld,\"reps_expanded\":%ld",
                             entry_mode.c_str(), query_upper_distances, query_upper_slots,
                             query_reps_scored, query_reps_expanded);
                std::fprintf(trace, ",\"missing_reference_distances\":[");
                bool first = true;
                std::unordered_set<uint32_t> result_set(result_ids.begin(), result_ids.end());
                for (uint32_t id : fp_ids) if (!result_set.count(id)) {
                    std::fprintf(trace, "%s[%u,%.9g]", first ? "" : ",", id,
                                 dist2(q, base.data() + size_t(id) * DIM));
                    first = false;
                }
                std::fprintf(trace, "]}\n");
            }
            std::unordered_set<int> topk;
            for (size_t i = 0; i < keep; ++i) topk.insert(out[i].second);
            int e = 0, h = 0;
            for (int j = 0; j < k; ++j) {
                const int g2 = gt[size_t(qi) * k + j];
                if (g2 < 0) continue;
                ++e; ++gt_total;
                if (topk.count(g2)) { ++h; continue; }
                // 未命中：归为三类
                const uint32_t node = obj2node[size_t(g2)];
                const bool attr_in = entered.count(attr_of[node]) > 0;
                const bool node_seen = (vstamp[node] == token);
                if (!attr_in && !node_seen)      ++miss_attr_not_entered;   // 结构不可达
                else if (!attr_in && node_seen)  ++miss_rejected;           // 访问过但准入拒收
                else if (attr_in && !node_seen)  ++miss_item_not_visited;   // 进了属性但没搜到它
                else                             ++miss_ranked_out;
            }
            hit += h; denom += std::min(k, e);
            entered_sum += long(entered.size());
            touched_sum += long(touched.size());
        }
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
        std::printf("{\"ef\":%d,\"reps_topL\":%d,\"p\":%d,\"k\":%d,\"M\":%d,\"efC\":%d,\"skip\":%d,\"queries\":%d,\"repeat\":%d,"
                    "\"recall\":%.6f,\"qps\":%.1f,\"dists_per_q\":%.1f,\"eligible_per_q\":%.1f,"
                    "\"dist_over_eligible\":%.3f,\"attrs_entered\":%.2f,\"attrs_in_D\":%.2f,\"miss_attr_not_entered\":%.4f,\"miss_item_not_visited\":%.4f,\"miss_ranked_out\":%.4f,\"miss_rejected\":%.4f,\"attrs_touched\":%.2f,"
                    "\"expanded_per_q\":%.3f,\"adjacency_slots_per_q\":%.3f,\"distance_cache_hits_per_q\":%.3f,"
                    "\"entry_mode\":\"%s\",\"upper_dists_per_q\":%.3f,\"upper_slots_per_q\":%.3f,\"upper_index_bytes\":%zu,\"rerank_fp32\":%d,\"rerank_calls_per_q\":%.3f,\"reps_scored_per_q\":%.3f,\"reps_expanded_per_q\":%.3f}\n",
                    ef, reps_topL, pdiv, k, M, efC, int(skip_links), nq, repeat,
                    double(hit) / double(std::max<long>(denom, 1)),
                    double(nq) * repeat / query_seconds,
                    double(dists) / (double(nq) * repeat),
                    double(elig_sum) / (double(nq) * repeat),
                    double(dists) / std::max<double>(1.0, double(elig_sum)),
                    double(entered_sum) / (double(nq) * repeat),
                    double(inD_sum) / (double(nq) * repeat),
                    double(miss_attr_not_entered) / std::max<long>(1, gt_total),
                    double(miss_item_not_visited) / std::max<long>(1, gt_total),
                    double(miss_ranked_out) / std::max<long>(1, gt_total),
                    double(miss_rejected) / std::max<long>(1, gt_total),
                    double(touched_sum) / (double(nq) * repeat),
                    double(expanded_sum) / (double(nq) * repeat),
                    double(adjacency_slots_sum) / (double(nq) * repeat),
                    double(distance_cache_hits_sum) / (double(nq) * repeat),
                    entry_mode.c_str(), double(upper_distances_sum) / (double(nq) * repeat),
                    double(upper_slots_sum) / (double(nq) * repeat), upper_index_bytes,
                    int(rerank_fp32), double(rerank_calls_sum) / (double(nq) * repeat),
                    double(reps_scored_sum) / (double(nq) * repeat),
                    double(reps_expanded_sum) / (double(nq) * repeat));
        std::fflush(stdout);
    }
    if (trace) std::fclose(trace);
    return 0;
}
