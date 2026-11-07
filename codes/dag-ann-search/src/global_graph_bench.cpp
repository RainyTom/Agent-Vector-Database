// 单张全局图查询：块只是**构造期**的合并单位，查询期不存在"块"。
//
// 构造：
//   1) 每个概念建一张单层图（RNG 选边 + 反向边收缩）
//   2) 所有概念图**拼接成一张全局图**（局部 id 平移），概念内边一条不减
//   3) 跨概念：门户桥（父概念入口/代表点 → 子概念入口），作为全局图里的边
//   4) 每个概念记录代表点（farthest-first，K=8），作为查询入口候选
//
// 查询（单队列，无分块、无每块预算）：
//   - 位图求闭包 D；对 D 中每个概念，取"距查询最近的代表点"作为种子，全部压入**同一个**队列
//   - 全局唯一的结果堆，容量 ef
//   - 只有 own(o) ∈ D 的对象能进结果（谓词跳过）；其余对象仍可参与导航
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

struct ConceptGraph {
    Vec members;
    std::vector<Vec> adj;
    Vec reps;
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

static void build_concept(const float* base, const Vec& ids, int M, int efC, int K, int pdiv, ConceptGraph& g) {
    const int n = int(ids.size());
    g.members = ids;
    g.adj.assign(size_t(n), {});
    if (n == 1) { g.entry = 0; g.reps = {0}; return; }
    g.adj[0].push_back(1); g.adj[1].push_back(0);
    std::vector<uint32_t> vis(size_t(n), 0);
    uint32_t tok = 0;
    std::vector<std::pair<float, uint32_t>> cands;
    for (int i = 2; i < n; ++i) {
        ++tok;
        const float* q = base + size_t(ids[size_t(i)]) * DIM;
        std::priority_queue<std::pair<float, uint32_t>> fr;
        fr.push({-dist2(q, base + size_t(ids[g.entry]) * DIM), g.entry});
        vis[g.entry] = tok;
        cands.clear();
        while (!fr.empty()) {
            const auto [nd, v] = fr.top(); fr.pop();
            const float dv = -nd;
            if (cands.size() >= size_t(efC)) {
                float worst = 0; for (auto& c : cands) worst = std::max(worst, c.first);
                if (dv > worst) break;
            }
            cands.push_back({dv, v});
            for (uint32_t w : g.adj[v]) {
                if (vis[w] == tok) continue;
                vis[w] = tok;
                fr.push({-dist2(q, base + size_t(ids[w]) * DIM), w});
            }
        }
        std::sort(cands.begin(), cands.end());
        if (cands.size() > size_t(efC)) cands.resize(size_t(efC));
        Vec chosen;
        for (auto& c : cands) {
            if (int(chosen.size()) >= M) break;
            const float* p = base + size_t(ids[c.second]) * DIM;
            bool ok = true;
            for (uint32_t s2 : chosen) if (dist2(p, base + size_t(ids[s2]) * DIM) < c.first) { ok = false; break; }
            if (ok) chosen.push_back(c.second);
        }
        if (chosen.empty() && !cands.empty()) chosen.push_back(cands[0].second);
        g.adj[size_t(i)] = chosen;
        for (uint32_t w : chosen) {
            g.adj[w].push_back(uint32_t(i));
            if (int(g.adj[w].size()) > M) shrink_list(base, ids, w, g.adj[w], M);
        }
    }
    for (int i = 0; i < n; ++i) {
        if (!g.adj[size_t(i)].empty()) continue;
        const float* pi = base + size_t(ids[size_t(i)]) * DIM;
        float best = INFINITY; int bid = -1;
        for (int j = 0; j < n; ++j) {
            if (j == i) continue;
            const float d = dist2(pi, base + size_t(ids[size_t(j)]) * DIM);
            if (d < best) { best = d; bid = j; }
        }
        if (bid >= 0) { g.adj[size_t(i)].push_back(uint32_t(bid)); g.adj[size_t(bid)].push_back(uint32_t(i)); }
    }
    const int K2 = std::max(1, std::min({K, n, std::max(1, n / std::max(1, pdiv)) + 1}));
    g.reps.clear(); g.reps.push_back(0);
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
}

static bool readf(const std::string& p, std::vector<float>& v) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END); const long b = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    v.resize(size_t(b) / sizeof(float));
    const bool ok = std::fread(v.data(), sizeof(float), v.size(), f) == v.size();
    std::fclose(f);
    return ok;
}

int main(int argc, char** argv) {
    std::string dir = "work/dag-ann", work = "work/dag-scope/workload-run1", vec = "recon.bin";
    int k = 50, M = 16, efC = 200, nq_max = 200, K = 8, repeat = 1, pdiv = 50, ef2 = 16;
    bool skip_links = true;
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
        else if (a == "--K") K = std::stoi(nx());
        else if (a == "--p") pdiv = std::max(1, std::stoi(nx()));
        else if (a == "--no-skip") skip_links = false;
        else if (a == "--ef2") ef2 = std::max(1, std::stoi(nx()));
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
    std::unordered_map<int, std::vector<int>> children;
    { std::ifstream in(work + "/edges.tsv"); std::string l;
      while (std::getline(in, l)) { if (l.empty()) continue; std::stringstream ss(l); int c, p; ss >> c >> p; children[p].push_back(c); } }
    std::vector<std::vector<int>> qc;
    { std::ifstream in(work + "/queries.tsv"); std::string l;
      while (std::getline(in, l) && int(qc.size()) < nq) { if (l.empty()) continue;
        std::stringstream ss(l); int cnt; ss >> cnt; std::vector<int> v(cnt); for (int i = 0; i < cnt; ++i) ss >> v[i]; qc.push_back(v); } }
    std::vector<int> gt(size_t(nq) * k, -1);
    { FILE* f = std::fopen((dir + "/gt_" + std::to_string(k) + ".txt").c_str(), "rb");
      if (f) { if (std::fread(gt.data(), sizeof(int32_t), gt.size(), f) != gt.size()) {} std::fclose(f); } }

    // 概念全集与位图
    std::vector<std::vector<int>> parents_of;
    std::vector<int> cids;
    std::unordered_map<int,int> cpos;
    auto addc = [&](int c) { if (cpos.emplace(c, int(cids.size())).second) cids.push_back(c); };
    for (auto& kv : children) { addc(kv.first); for (int ch : kv.second) addc(ch); }
    for (int o : owner) addc(o);
    const int C = int(cids.size());
    parents_of.assign(size_t(C), std::vector<int>{});
    for (auto& kv : children) {
        auto pit = cpos.find(kv.first);
        if (pit == cpos.end()) continue;
        for (int ch : kv.second) { auto cit = cpos.find(ch); if (cit != cpos.end()) parents_of[size_t(cit->second)].push_back(pit->second); }
    }
    std::vector<std::vector<uint64_t>> desc(size_t(C), std::vector<uint64_t>{});
    {
        const int words = (C + 63) / 64;
        for (auto& d : desc) d.assign(size_t(words), 0);
        std::vector<int> state(size_t(C), 0);
        std::function<void(int)> dfs = [&](int u) {
            if (state[size_t(u)] == 2) return;
            state[size_t(u)] = 1;
            auto it = children.find(cids[size_t(u)]);
            if (it != children.end()) for (int ch : it->second) {
                const int ci = cpos[ch];
                dfs(ci);
                for (int w = 0; w < words; ++w) desc[size_t(u)][size_t(w)] |= desc[size_t(ci)][size_t(w)];
            }
            desc[size_t(u)][size_t(u) / 64] |= (uint64_t(1) << (u % 64));
            state[size_t(u)] = 2;
        };
        for (int i = 0; i < C; ++i) dfs(i);
    }

    // 概念深度（从根 BFS）
    std::vector<int> cdepth(size_t(C), 0);
    {
        std::vector<int> indeg(size_t(C), 0);
        for (auto& kv : parents_of) for (size_t i2 = 0; i2 < parents_of.size(); ++i2) {}
        std::vector<int> q2;
        for (int i2 = 0; i2 < C; ++i2) if (parents_of[size_t(i2)].empty()) q2.push_back(i2);
        std::vector<char> vis(size_t(C), 0);
        for (size_t h = 0; h < q2.size(); ++h) {
            const int u = q2[h];
            if (vis[size_t(u)]) continue;
            vis[size_t(u)] = 1;
            auto it = children.find(cids[size_t(u)]);
            if (it == children.end()) continue;
            for (int ch : it->second) {
                auto ci2 = cpos.find(ch);
                if (ci2 == cpos.end()) continue;
                if (!vis[size_t(ci2->second)]) { cdepth[size_t(ci2->second)] = cdepth[size_t(u)] + 1; q2.push_back(ci2->second); }
            }
        }
    }
    // 逐概念建图，然后拼成一张全局图
    std::vector<ConceptGraph> cg(size_t(C), ConceptGraph{});
    {
        std::vector<Vec> mem(size_t(C), Vec{});
        for (size_t i = 0; i < n; ++i) mem[size_t(cpos[owner[i]])].push_back(uint32_t(i));
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < C; ++i) if (!mem[size_t(i)].empty()) build_concept(base.data(), mem[size_t(i)], M, efC, K, pdiv, cg[size_t(i)]);
        std::fprintf(stderr, "[build] %d concept graphs in %.1fs\n", C,
                     std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    }
    // 全局图：偏移 + 连续邻接
    std::vector<uint32_t> goff(size_t(C) + 1, 0), cbase(size_t(C), 0);
    std::vector<uint32_t> concept_of;   // 节点号 -> 概念稠密下标
    std::vector<uint32_t> obj_of;       // 节点号 -> 对象 id
    std::vector<uint32_t> obj2node;     // 对象 id -> 节点号
    {
        size_t tot = 0;
        for (int i = 0; i < C; ++i) { cbase[size_t(i)] = uint32_t(tot); tot += cg[size_t(i)].members.size(); }
        goff[C] = uint32_t(tot);
        concept_of.assign(tot, 0);
        obj_of.assign(tot, 0);
        obj2node.assign(n, 0);
        for (int i = 0; i < C; ++i)
            for (size_t j = 0; j < cg[size_t(i)].members.size(); ++j) {
                concept_of[cbase[size_t(i)] + j] = uint32_t(i);
                obj_of[cbase[size_t(i)] + j] = cg[size_t(i)].members[j];
                obj2node[cg[size_t(i)].members[j]] = cbase[size_t(i)] + uint32_t(j);
            }
    }
    std::vector<Vec> gadj(size_t(n), Vec{});
    std::vector<Vec> gbridge(size_t(n), Vec{});   // 门户桥（独立表：始终可走）
    for (int i = 0; i < C; ++i) {
        const uint32_t off = cbase[size_t(i)];
        for (size_t j = 0; j < cg[size_t(i)].adj.size(); ++j)
            for (uint32_t w : cg[size_t(i)].adj[size_t(j)]) gadj[off + j].push_back(off + w);
    }
    // 门户桥：父概念入口/代表点 → 子概念入口（双向）
    for (auto& kv : children) {
        const int p = kv.first;
        auto pit = cpos.find(p);
        if (pit == cpos.end()) continue;
        const int pi = pit->second;
        if (cg[size_t(pi)].members.empty()) continue;
        for (int ch : kv.second) {
            auto cit = cpos.find(ch);
            if (cit == cpos.end()) continue;
            const int ci = cit->second;
            if (cg[size_t(ci)].members.empty()) continue;
            const uint32_t pentry = cbase[size_t(pi)] + cg[size_t(pi)].entry;
            const uint32_t centry = cbase[size_t(ci)] + cg[size_t(ci)].entry;
            // 门户桥：父侧 K 个代表点 ↔ 子侧 K 个代表点（全连接，K=8 → 每对 8x8）
            // 理由：只连"最近的一个锚点"会导致搜索深入子图后找不到桥，无法继续下潜。
            for (uint32_t rp : cg[size_t(pi)].reps) {
                const uint32_t gp = cbase[size_t(pi)] + rp;
                for (uint32_t rc : cg[size_t(ci)].reps) {
                    const uint32_t gc = cbase[size_t(ci)] + rc;
                    gbridge[gp].push_back(gc);   // 单向：只允许父 → 子
                }
            }
        }
    }
    // 跳转边（skip links）：从祖先代表点直接连到"非直接后代"的入口，避免逐层线性下潜
    if (skip_links) {
        long nskip = 0;
        for (int i = 0; i < C; ++i) {
            if (cg[size_t(i)].members.empty()) continue;
            for (int anc : parents_of[size_t(i)]) {   // 只看两层以上的祖先
                (void)anc;
            }
            // 沿祖先链向上走 2 跳及以上
            std::unordered_set<int> seen_anc;
            std::vector<int> frontier = parents_of[size_t(i)];
            for (int lvl = 0; lvl < 3 && !frontier.empty(); ++lvl) {
                std::vector<int> nxt;
                for (int a : frontier) {
                    if (!seen_anc.insert(a).second) continue;
                    if (lvl == 0) {   // 距离 1 的父：已有直接门户桥，跳过
                        for (int pp : parents_of[size_t(a)]) nxt.push_back(pp);
                        continue;
                    }
                    if (cg[size_t(a)].members.empty()) { for (int pp : parents_of[size_t(a)]) nxt.push_back(pp); continue; }
                    const uint32_t target = cbase[size_t(i)] + cg[size_t(i)].entry;
                    for (uint32_t r : cg[size_t(a)].reps) {
                        gbridge[cbase[size_t(a)] + r].push_back(target);   // 单向：祖先 → 后代
                        ++nskip;
                    }
                    for (int pp : parents_of[size_t(a)]) nxt.push_back(pp);
                }
                frontier.swap(nxt);
            }
        }
        std::fprintf(stderr, "[skip] %ld skip links\n", nskip);
    }
    // 扁平化
    std::vector<uint32_t> adj_off(size_t(n) + 1, 0), adj_dat;
    {
        size_t tot = 0;
        for (size_t i = 0; i < n; ++i) { adj_off[i] = uint32_t(tot); tot += gadj[i].size(); }
        adj_off[n] = uint32_t(tot);
        adj_dat.resize(tot);
        size_t w = 0;
        for (size_t i = 0; i < n; ++i) for (uint32_t x : gadj[i]) adj_dat[w++] = x;
        std::vector<Vec>().swap(gadj);
    }
    std::vector<uint32_t> br_off(size_t(n) + 1, 0), br_dat;
    {
        size_t tot = 0;
        for (size_t i = 0; i < n; ++i) { br_off[i] = uint32_t(tot); tot += gbridge[i].size(); }
        br_off[n] = uint32_t(tot);
        br_dat.resize(tot);
        size_t w = 0;
        for (size_t i = 0; i < n; ++i) for (uint32_t x : gbridge[i]) br_dat[w++] = x;
    }
    std::fprintf(stderr, "[global] %zu nodes, %zu intra edges (%.1f/node), %zu portal edges\n", n, adj_dat.size(),
                 double(adj_dat.size()) / double(n), br_dat.size());

    std::vector<char> is_rep(size_t(n), 0);
    {
        size_t nrep_tot = 0;
        for (int i = 0; i < C; ++i)
            for (uint32_t r : cg[size_t(i)].reps) { is_rep[cbase[size_t(i)] + r] = 1; ++nrep_tot; }
        std::fprintf(stderr, "[reps] %zu rep nodes (p=%d)\n", nrep_tot, pdiv);
    }
    std::vector<uint32_t> vstamp(size_t(n), 0);
    std::vector<uint32_t> cstamp(size_t(C), 0);
    uint32_t ctoken = 0;
    uint32_t token = 0;
    std::vector<uint64_t> bm((size_t(C) + 63) / 64, 0);
    for (int ef : efs) {
        long hit = 0, denom = 0, dists = 0, elig_sum = 0, seeds_sum = 0, res_sum = 0, res_full = 0;
        long cov_elig = 0, cov_concepts = 0, entered_sum = 0, inD_sum = 0;
        const auto t1 = std::chrono::steady_clock::now();
        for (int round = 0; round < repeat; ++round)
        for (int qi = 0; qi < nq; ++qi) {
            std::fill(bm.begin(), bm.end(), 0ull);
            for (int c : qc[qi]) {
                auto it = cpos.find(c);
                if (it == cpos.end()) continue;
                const auto& d = desc[size_t(it->second)];
                for (size_t w = 0; w < bm.size(); ++w) bm[w] |= d[w];
            }
            auto eligible = [&](uint32_t gi) {
                const uint32_t ci = concept_of[gi];
                return ((bm[ci / 64] >> (ci % 64)) & 1ull) != 0;
            };
            // 参与概念（D）
            std::vector<int> inD;
            for (size_t w = 0; w < bm.size(); ++w) {
                uint64_t x = bm[w];
                while (x) { const int b = __builtin_ctzll(x); x &= x - 1;
                    const int ci = int(w) * 64 + b;
                    if (ci < C && !cg[size_t(ci)].members.empty()) inD.push_back(ci); }
            }
            if (inD.empty()) continue;
            inD_sum += long(inD.size());
            for (int ci : inD) elig_sum += long(cg[size_t(ci)].members.size());
            const float* q = queries.data() + size_t(qi) * DIM;
            ++token; ++ctoken;
            long entered = 0;
            // 每概念结果堆：容量 ef_c = max(1, round(ef * n_c / |E|))，按子树规模比例分配
            std::vector<std::vector<std::pair<float,uint32_t>>> pc(size_t(C), std::vector<std::pair<float,uint32_t>>{});
            std::vector<int> cap(size_t(C), 0);
            {
                long total_el = 0;
                for (int ci : inD) total_el += long(cg[size_t(ci)].members.size());
                for (int ci : inD) {
                    const long nc = long(cg[size_t(ci)].members.size());
                    cap[size_t(ci)] = int(std::max<long>(1, (long(ef) * nc) / std::max<long>(1, total_el)));
                    pc[size_t(ci)].reserve(size_t(cap[size_t(ci)]) + 1);
                }
            }
            // q1 预算 ef 的动态分配：
            //   初始额度 = max(k, ef/|D|)/2（保底，每个属性都有机会进入）
            //   剩余额度 = ef - Σ初始额度，按“该属性已产出结果数”最少者优先补发
            //   每当某属性结果数达到当前额度，就从余量里再拨一份给它
            const int dfull = std::max<int>(1, int(inD.size()));
            const int base_cap = std::max(k, ef / dfull) / 2;
            std::vector<int> acap(size_t(C), 0);
            long granted = 0;
            for (int ci : inD) { acap[size_t(ci)] = base_cap; granted += base_cap; }
            long pool = std::max<long>(0, long(ef) - granted);
            static std::vector<std::vector<std::pair<float,uint32_t>>> akeep;
            static std::vector<int> touched_a;
            if (akeep.size() < size_t(C)) akeep.resize(size_t(C));
            touched_a.clear();
            std::priority_queue<std::pair<float,uint32_t>> cand;
            std::priority_queue<std::pair<float,uint32_t>> q2_keep, q2_cand;
            std::priority_queue<std::pair<float,uint32_t>> best;
            auto topup = [&](int ci) {
                // 某属性达到当前额度：从余量里补发（按剩余额度大小给，最小 8）
                if (pool <= 0) return;
                const int add = int(std::min<long>(std::max<long>(8, pool / 4), pool));
                acap[size_t(ci)] += add;
                pool -= add;
            };
            auto offer = [&](uint32_t gi) {
                if (vstamp[gi] == token) return;
                vstamp[gi] = token;
                const float d = dist2(q, base.data() + size_t(obj_of[gi]) * DIM);
                ++dists;
                const int ci = int(concept_of[gi]);
                bool accepted = false;
                auto& h = akeep[size_t(ci)];
                if (h.empty()) touched_a.push_back(ci);
                if (int(h.size()) < acap[size_t(ci)]) { h.push_back({d, gi}); std::push_heap(h.begin(), h.end()); accepted = true; }
                else if (d < h.front().first) { std::pop_heap(h.begin(), h.end()); h.back() = {d, gi}; std::push_heap(h.begin(), h.end()); accepted = true; }
                if (is_rep[gi]) {
                    if (int(q2_keep.size()) < ef2) { q2_keep.push({d, gi}); q2_cand.push({-d, gi}); accepted = true; }
                    else if (d < q2_keep.top().first) { q2_keep.pop(); q2_keep.push({d, gi}); q2_cand.push({-d, gi}); accepted = true; }
                }
                if (!accepted) return;
                // 达到额度 → 申请补发
                if (int(h.size()) >= acap[size_t(ci)] && pool > 0) topup(ci);
                cand.push({-d, gi});
                if (cstamp[ci] != ctoken) { cstamp[ci] = ctoken; ++entered; }
                if (int(best.size()) < ef) best.push({d, gi});
                else if (d < best.top().first) { best.pop(); best.push({d, gi}); }
            };
            for (int c : qc[qi]) {
                auto it = cpos.find(c);
                if (it == cpos.end()) continue;
                const int ci = it->second;
                if (cg[size_t(ci)].members.empty()) continue;
                ++seeds_sum;
                offer(cbase[size_t(ci)] + cg[size_t(ci)].entry);
            }
            while (!cand.empty() || !q2_cand.empty()) {
                const float d1 = cand.empty() ? INFINITY : -cand.top().first;
                const float d2 = q2_cand.empty() ? INFINITY : -q2_cand.top().first;
                uint32_t gi;
                if (d1 <= d2) { gi = cand.top().second; cand.pop(); }
                else { gi = q2_cand.top().second; q2_cand.pop(); }
                for (uint32_t j = adj_off[gi]; j < adj_off[gi + 1]; ++j) offer(adj_dat[j]);
                for (uint32_t j = br_off[gi]; j < br_off[gi + 1]; ++j) offer(br_dat[j]);
            }
            // 结果只取合格对象
            std::vector<std::pair<float,int>> out;
            { auto bk = best; while (!bk.empty()) { auto pr = bk.top(); bk.pop(); out.push_back({pr.first, int(obj_of[pr.second])}); } }
            {   // 覆盖统计：本次查询访问到的不同合格概念数
                std::unordered_set<int> seen;
                for (uint32_t gi2 = 0; gi2 < 0; ++gi2) {}
                (void)seen;
            }
            cov_concepts += 0;
            entered_sum += entered;
            if (qi < 3 && round == 0) {
                int gt_visited = 0, gt_in_best = 0, gt_in_top = 0, gt_n = 0;
                std::unordered_set<uint32_t> bset;
                for (auto& b : pc) for (auto& pr : b) bset.insert(pr.second);
                std::unordered_set<int> topset;
                { auto tmp = out; const size_t kk = std::min<size_t>(k, tmp.size());
                  std::partial_sort(tmp.begin(), tmp.begin() + kk, tmp.end());
                  for (size_t i2 = 0; i2 < kk; ++i2) topset.insert(tmp[i2].second); }
                for (int j = 0; j < k; ++j) {
                    const int g2 = gt[size_t(qi) * k + j];
                    if (g2 < 0) continue;
                    ++gt_n;
                    const uint32_t node = obj2node[g2];
                    if (vstamp[node] == token) ++gt_visited;
                    if (bset.count(node)) ++gt_in_best;
                    if (topset.count(g2)) ++gt_in_top;
                }
                if (qi == 0) {
                    for (int j = 0; j < 3; ++j) {
                        const int g2 = gt[size_t(qi) * k + j];
                        const uint32_t node = obj2node[g2];
                        const float dnode = dist2(q, base.data() + size_t(node) * DIM);
                        const float dobj = dist2(q, base.data() + size_t(g2) * DIM);
                        (void)dnode; (void)dobj; (void)node;
                    }
                }
                {
                    // 真值点的距离 vs 我们返回的 top-50 的距离边界
                    float gt_max = -1, top_max = -1;
                    for (int j = 0; j < k; ++j) { const int g2 = gt[size_t(qi) * k + j]; if (g2 < 0) continue;
                        gt_max = std::max(gt_max, dist2(q, base.data() + size_t(g2) * DIM)); }
                    if (topset.size() == size_t(k)) {
                        auto tmp = out; std::partial_sort(tmp.begin(), tmp.begin() + k, tmp.end());
                        top_max = tmp[k - 1].first;
                    }
                    std::fprintf(stderr, "[q%d] gt=%d visited=%d in_best=%d in_top=%d | dists=%ld D=%zu\n",
                                 qi, gt_n, gt_visited, gt_in_best, gt_in_top, dists, inD.size());
                }
            }
            res_sum += long(out.size());
            if (int(out.size()) >= k) ++res_full;
            const size_t keep = std::min<size_t>(k, out.size());
            std::partial_sort(out.begin(), out.begin() + keep, out.end());
            std::unordered_set<int> topk;
            for (size_t i = 0; i < keep; ++i) topk.insert(out[i].second);
            int e = 0, h = 0;
            for (int j = 0; j < k; ++j) { const int g2 = gt[size_t(qi) * k + j]; if (g2 >= 0) { ++e; if (topk.count(g2)) ++h; } }
            hit += h; denom += std::min(k, e);
        }
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
        std::printf("{\"mode\":\"global_single_queue\",\"ef\":%d,\"ef2\":%d,\"k\":%d,\"M\":%d,\"efC\":%d,\"queries\":%d,\"repeat\":%d,"
                    "\"recall\":%.6f,\"qps\":%.1f,\"dists_per_q\":%.0f,\"eligible_per_q\":%.1f,"
                    "\"dist_over_eligible\":%.3f,\"seeds_per_q\":%.2f,\"results_per_q\":%.1f,\"full_result_q\":%ld,\"eligible_visited_per_q\":%.1f,\"concepts_entered\":%.2f,\"concepts_in_D\":%.2f}\n",
                    ef, ef2, k, M, efC, nq, repeat,
                    double(hit) / double(std::max<long>(denom, 1)),
                    double(nq) * repeat / s,
                    double(dists) / (double(nq) * repeat),
                    double(elig_sum) / (double(nq) * repeat),
                    double(dists) / std::max<double>(1.0, double(elig_sum)),
                    double(seeds_sum) / (double(nq) * repeat),
                    double(res_sum) / (double(nq) * repeat), res_full,
                    double(cov_elig) / (double(nq) * repeat),
                    double(entered_sum) / (double(nq) * repeat), double(inD_sum) / (double(nq) * repeat));
        std::fflush(stdout);
    }
    return 0;
}
