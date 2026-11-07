// Attribute-Skeleton Index (ASI)：属性 DAG 上的骨架图索引与双队列检索。
//
// 索引三部分：
//   L1 属性内局部图 (ALG)   —— 每属性一张单层图（RNG 选边 + 反向边收缩 + 连通修补）
//   L2 代表点骨架 (RS)      —— 每属性按 1/p 取 farthest-first 代表点，代表点间建 MST；
//                              并向"直接子属性入口"与"≥2 跳后代入口"建单向跳转边
//   L3 闭包位图 (CL)        —— 每属性一张后代闭包位图，用于闭包求解与资格判定
//
// 检索：
//   q1（容量 ef，收所有节点）+ q2（容量 ef2，只收代表节点），各自维护
//   cand（最近优先，决定展开顺序）与 keep（容量上限，决定淘汰阈值）。
//   每次从两队列取更近者展开；邻居先试 q1，若为代表节点且进不了 q1 仍可进 q2。
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
};

static void shrink_list(const float* base, const Vec& ids, uint32_t bl, Vec& list, int M) {
    if (int(list.size()) <= M) return;
    std::sort(list.begin(), list.end(), [&](uint32_t a, uint32_t b) {
        return dist2(base + size_t(ids[a]) * DIM, base + size_t(ids[bl]) * DIM) <
               dist2(base + size_t(ids[b]) * DIM, base + size_t(ids[bl]) * DIM);
    });
    Vec keep;
    const float* pb = base + size_t(ids[bl]) * DIM;
    for (uint32_t c : list) {
        if (int(keep.size()) >= M) break;
        const float* pc = base + size_t(ids[c]) * DIM;
        const float dq = dist2(pc, pb);
        bool ok = true;
        for (uint32_t k : keep) if (dist2(pc, base + size_t(ids[k]) * DIM) < dq) { ok = false; break; }
        if (ok) keep.push_back(c);
    }
    list.swap(keep);
}

// 用 hnswlib 建该属性的多层索引，然后取 layer 0 邻接作为属性内局部图。
// 这样建图质量与基线同源（同 RNG 剪边、同层次构建），不需要自写启发式。
static void build_attr_graph(const float* base, const Vec& ids, int M, int efC, int nrep,
                             int seed, AttrGraph& g) {
    const int n = int(ids.size());
    g.members = ids;
    g.adj.assign(size_t(n), {});
    if (n == 0) return;
    if (n == 1) { g.entry = 0; g.reps = {0}; return; }

    RawL2Space space(DIM);
    hnswlib::HierarchicalNSW<float> idx(&space, size_t(n), M, efC, seed);
    for (int i = 0; i < n; ++i) idx.addPoint(base + size_t(ids[size_t(i)]) * DIM, hnswlib::labeltype(i));

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
    std::string dir = "work/dag-ann", work = "work/dag-scope/workload-run1", vec = "recon.bin";
    int k = 50, M = 16, efC = 200, nq_max = 200, Kcap = 8, repeat = 1, pdiv = 50, ef2 = 16;
    bool skip_links = true;
    bool do_merge = true;
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
        else if (a == "--ef2") ef2 = std::max(1, std::stoi(nx()));
        else if (a == "--no-skip") skip_links = false;
        else if (a == "--no-merge") do_merge = false;
        else if (a == "--queries") nq_max = std::stoi(nx());
        else if (a == "--repeat") repeat = std::max(1, std::stoi(nx()));
        else if (a == "--efs") { efs.clear(); std::stringstream ss(nx()); std::string t;
            while (std::getline(ss, t, ',')) if (!t.empty()) efs.push_back(std::stoi(t)); }
    }

    std::vector<float> base, queries;
    if (!readf(dir + "/" + vec, base) || !readf(dir + "/queries.bin", queries)) return 2;
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
    { FILE* f = std::fopen((dir + "/gt_" + std::to_string(k) + ".txt").c_str(), "rb");
      if (f) { if (std::fread(gt.data(), sizeof(int32_t), gt.size(), f) != gt.size()) {} std::fclose(f); } }

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
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < A; ++i) {
            const int nrep = std::max(1, std::min(Kcap, int(mem[size_t(i)].size()) / std::max(1, pdiv) + 1));
            build_attr_graph(base.data(), mem[size_t(i)], M, efC, nrep, 100 + i, ag[size_t(i)]);
        }
        std::fprintf(stderr, "[L1] %d attribute graphs in %.1fs (M=%d efC=%d p=%d Kcap=%d)\n", A,
                     std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), M, efC, pdiv, Kcap);
    }

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
    // L2 的跨属性边：**按拓扑序自底向上合并**，合并时双向更新代表点邻居并做 RNG 调整。
    // 说明：不存在"跳转边"这一独立机制；祖先可达深层后代，靠的是合并过程中
    //       代表点邻居被反复更新（下层代表点不是建完就冻结）。
    const int Rdeg = 16;                        // 代表点跨属性邻居的度上界
    const int PPROT = 2;                        // 每个代表点至少保留的跨属性边数（防止属性成为孤岛）
    std::vector<uint32_t> rep_owner(size_t(n), UINT32_MAX);   // 代表点 -> 所属属性
    for (int i = 0; i < A; ++i)
        for (uint32_t r : ag[size_t(i)].reps) rep_owner[abase[size_t(i)] + r] = uint32_t(i);

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

    // 代表点的邻居表（跨属性），以全局节点号存储，合并时做 RNG 调整
    auto rep_prune = [&](uint32_t node) {
        Vec& L = gbr[node];
        if (int(L.size()) <= Rdeg) return;
        const float* pn = base.data() + size_t(node_objid(node)) * DIM;
        std::sort(L.begin(), L.end(), [&](uint32_t a, uint32_t b) {
            return dist2(pn, base.data() + size_t(node_objid(a)) * DIM) <
                   dist2(pn, base.data() + size_t(node_objid(b)) * DIM);
        });
        const uint32_t self_attr = rep_owner[node];
        Vec keep;
        int cross_kept = 0;
        for (uint32_t c : L) {
            if (int(keep.size()) >= Rdeg) break;
            const bool cross = (rep_owner[c] != self_attr);
            const float* pc = base.data() + size_t(node_objid(c)) * DIM;
            const float dq = dist2(pc, pn);
            bool ok = true;
            for (uint32_t k2 : keep) if (dist2(pc, base.data() + size_t(node_objid(k2)) * DIM) < dq) { ok = false; break; }
            if (ok) { keep.push_back(c); if (cross) ++cross_kept; }
            else if (cross && cross_kept < PPROT) { keep.push_back(c); ++cross_kept; }   // 保底额度
        }
        L.swap(keep);
    };

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
                rep_prune(gp);
            }
            for (uint32_t rc : ag[size_t(ci)].reps) rep_prune(abase[size_t(ci)] + rc);
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
    std::fprintf(stderr, "[L2-merge] %ld cross-attribute rep edges (+%ld repaired), RNG-pruned to %d\n",
                 n_merge_edges, n_repair, Rdeg);
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

    std::vector<uint32_t> vstamp(size_t(n), 0);
    uint32_t token = 0;
    std::vector<uint64_t> bm(size_t(words), 0);

    for (int ef : efs) {
        long hit = 0, denom = 0, dists = 0, elig_sum = 0, entered_sum = 0, inD_sum = 0, seeds_sum = 0;
        long miss_attr_not_entered = 0, miss_item_not_visited = 0, miss_ranked_out = 0, gt_total = 0, miss_rejected = 0, touched_sum = 0;
        const auto t1 = std::chrono::steady_clock::now();
        for (int round = 0; round < repeat; ++round)
        for (int qi = 0; qi < nq; ++qi) {
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

            std::priority_queue<std::pair<float,uint32_t>> q1_cand, q1_keep, q2_cand, q2_keep;
            std::unordered_set<uint32_t> entered, touched;
            auto offer = [&](uint32_t gi) {
                if (vstamp[gi] == token) return;      // 首次接触即标记，距离只算一次
                vstamp[gi] = token;
                if (!eligible(gi)) return;            // 范围外：连距离都不算，不占预算
                touched.insert(attr_of[gi]);
                const float d = dist2(q, base.data() + size_t(obj_of[gi]) * DIM);
                ++dists;
                bool any = false;
                if (int(q1_keep.size()) < ef) { q1_keep.push({d, gi}); q1_cand.push({-d, gi}); any = true; }
                else if (d < q1_keep.top().first) { q1_keep.pop(); q1_keep.push({d, gi}); q1_cand.push({-d, gi}); any = true; }
                if (is_rep[gi] || is_gw[gi]) {
                    if (int(q2_keep.size()) < ef2) { q2_keep.push({d, gi}); q2_cand.push({-d, gi}); any = true; }
                    else if (d < q2_keep.top().first) { q2_keep.pop(); q2_keep.push({d, gi}); q2_cand.push({-d, gi}); any = true; }
                }
                if (any) entered.insert(attr_of[gi]);
            };
            for (int c : qc[qi]) {
                auto it = apos.find(c);
                if (it == apos.end()) continue;
                const int a = it->second;
                if (ag[size_t(a)].members.empty()) continue;
                // 种子 = 该属性的**全部代表点**（单入口是任意点，质量差）
                for (uint32_t r : ag[size_t(a)].reps) {
                    ++seeds_sum;
                    offer(abase[size_t(a)] + r);
                }
            }
            while (!q1_cand.empty() || !q2_cand.empty()) {
                const float d1 = q1_cand.empty() ? INFINITY : -q1_cand.top().first;
                const float d2 = q2_cand.empty() ? INFINITY : -q2_cand.top().first;
                uint32_t gi;
                if (d1 <= d2) { gi = q1_cand.top().second; q1_cand.pop(); }
                else { gi = q2_cand.top().second; q2_cand.pop(); }
                for (uint32_t j = adj_off[gi]; j < adj_off[gi + 1]; ++j) offer(adj_dat[j]);
                for (uint32_t j = br_off[gi]; j < br_off[gi + 1]; ++j) offer(br_dat[j]);
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
                dump(q2_keep);
            }
            std::sort(out.begin(), out.end());
            out.erase(std::unique(out.begin(), out.end(),
                      [](const std::pair<float,int>& a, const std::pair<float,int>& b) { return a.second == b.second; }),
                      out.end());
            const size_t keep = std::min<size_t>(k, out.size());
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
        std::printf("{\"ef\":%d,\"ef2\":%d,\"p\":%d,\"k\":%d,\"M\":%d,\"efC\":%d,\"skip\":%d,\"queries\":%d,\"repeat\":%d,"
                    "\"recall\":%.6f,\"qps\":%.1f,\"dists_per_q\":%.1f,\"eligible_per_q\":%.1f,"
                    "\"dist_over_eligible\":%.3f,\"attrs_entered\":%.2f,\"attrs_in_D\":%.2f,\"miss_attr_not_entered\":%.4f,\"miss_item_not_visited\":%.4f,\"miss_ranked_out\":%.4f,\"miss_rejected\":%.4f,\"attrs_touched\":%.2f}\n",
                    ef, ef2, pdiv, k, M, efC, int(skip_links), nq, repeat,
                    double(hit) / double(std::max<long>(denom, 1)),
                    double(nq) * repeat / s,
                    double(dists) / (double(nq) * repeat),
                    double(elig_sum) / (double(nq) * repeat),
                    double(dists) / std::max<double>(1.0, double(elig_sum)),
                    double(entered_sum) / (double(nq) * repeat),
                    double(inD_sum) / (double(nq) * repeat),
                    double(miss_attr_not_entered) / std::max<long>(1, gt_total),
                    double(miss_item_not_visited) / std::max<long>(1, gt_total),
                    double(miss_ranked_out) / std::max<long>(1, gt_total),
                    double(miss_rejected) / std::max<long>(1, gt_total),
                    double(touched_sum) / (double(nq) * repeat));
        std::fflush(stdout);
    }
    return 0;
}
