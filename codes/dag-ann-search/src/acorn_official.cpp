// 官方 ACORN（guestrin-lab/ACORN，FAISS 分支 faiss/IndexACORN.{h,cpp}），只实现 ACORN-gamma。
//
// 参数分层与论文一致：
//   查询期：ef（唯一在线旋钮）
//   构造期：gamma（决定"能服务的最低选择率" s_min ≈ 1/gamma）、M_beta（保留多少精确近邻边）、M
// 输出：按 (gamma, M_beta, ef) 一条 JSON，含总体 recall/QPS 与**按选择率分档**的 recall/QPS，
//       用于画"选择率-召回"塌陷边界，而不是只看总体均值。
//
// 另有 --thresh：实现 ACORN 方法本体的选择率回退（s < thresh 走 pre-filter），默认关闭（纯 ACORN 曲线）。
#include <faiss/IndexACORN.h>
#include <faiss/impl/ACORN.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

constexpr int DIM = 128;

// 与 B-scan 同源：遍历合格概念下的 item 列表，不扫全量位图。
void scan_query(const float* q, const std::vector<int>& scope,
                const std::unordered_map<int, std::vector<int>>& members,
                const float* base, int k, faiss::idx_t* ids, float* dis) {
    std::priority_queue<std::pair<float, int>> heap;
    for (int c : scope) {
        auto it = members.find(c);
        if (it == members.end()) continue;
        for (int id : it->second) {
            const float* x = base + size_t(id) * DIM;
            float s = 0.f;
            for (int d = 0; d < DIM; ++d) { const float t = q[d] - x[d]; s += t * t; }
            if (int(heap.size()) < k) heap.push({s, id});
            else if (s < heap.top().first) { heap.pop(); heap.push({s, id}); }
        }
    }
    const int m = int(heap.size());
    std::vector<std::pair<float, int>> out(m);
    for (int j = m - 1; j >= 0; --j) { out[j] = heap.top(); heap.pop(); }
    for (int j = 0; j < k; ++j) {
        if (j < m) { dis[j] = out[j].first; ids[j] = out[j].second; }
        else { dis[j] = NAN; ids[j] = -1; }
    }
}

inline void score(const std::vector<faiss::idx_t>& ids, const std::vector<int>& gt, int qi, int k,
                  long& hit, long& denom) {
    std::unordered_set<long> got;
    for (int j = 0; j < k; ++j) if (ids[size_t(j)] >= 0) got.insert(ids[size_t(j)]);
    int e = 0, h = 0;
    for (int j = 0; j < k; ++j) { const int g = gt[size_t(qi) * k + j]; if (g >= 0) { ++e; if (got.count(g)) ++h; } }
    hit += h; denom += std::min(k, e);
}

std::vector<int> split_ints(const std::string& s) {
    std::vector<int> v; std::stringstream ss(s); std::string t;
    while (std::getline(ss, t, ',')) if (!t.empty()) v.push_back(std::stoi(t));
    return v;
}

// 选择率分档：用它看"在哪一档开始塌"
const double BAND_LO[] = {0.0, 0.01, 0.02, 0.05, 0.10};
const double BAND_HI[] = {0.01, 0.02, 0.05, 0.10, 1.01};
constexpr int NBAND = 5;

}  // namespace

int main(int argc, char** argv) {
    std::string dir = "work/dag-ann", work = "work/dag-scope/workload-run1", vecfile = "orig.bin", mode = "containment";
    int k = 50, M = 32, nq_max = 200;
    std::vector<int> gammas{12}, mbetas{32}, efs{50};
    bool nofilter = false;   // 纯 ANN（不传 filter map），用于拆"枚举开销 vs 过滤开销"
    double thresh = -1.0;   // >=0 则启用选择率回退（方法本体）；<0 纯 ACORN 曲线
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto nx = [&]() { return std::string(argv[++i]); };
        if (a == "--dir") dir = nx();
        else if (a == "--work") work = nx();
        else if (a == "--vec") vecfile = nx();
        else if (a == "--mode") mode = nx();
        else if (a == "--k") k = std::stoi(nx());
        else if (a == "--M") M = std::stoi(nx());
        else if (a == "--gamma") gammas = {std::stoi(nx())};
        else if (a == "--Mbeta") mbetas = {std::stoi(nx())};
        else if (a == "--ef") efs = {std::stoi(nx())};
        else if (a == "--gammas") gammas = split_ints(nx());
        else if (a == "--mbetas") mbetas = split_ints(nx());
        else if (a == "--efs") efs = split_ints(nx());
        else if (a == "--queries") nq_max = std::stoi(nx());
        else if (a == "--thresh") thresh = std::stod(nx());
        else if (a == "--nofilter") nofilter = true;
    }

    std::vector<float> base, queries;
    {
        FILE* f = std::fopen((dir + "/" + vecfile).c_str(), "rb");
        if (!f) { std::fprintf(stderr, "missing %s\n", vecfile.c_str()); return 2; }
        std::fseek(f, 0, SEEK_END); long b = std::ftell(f); std::fseek(f, 0, SEEK_SET);
        base.resize(size_t(b) / sizeof(float));
        if (std::fread(base.data(), sizeof(float), base.size(), f) != base.size()) return 2;
        std::fclose(f);
    }
    const size_t n = base.size() / DIM;
    {
        FILE* f = std::fopen((dir + "/queries.bin").c_str(), "rb");
        if (!f) { std::fprintf(stderr, "missing queries.bin\n"); return 2; }
        std::fseek(f, 0, SEEK_END); long b = std::ftell(f); std::fseek(f, 0, SEEK_SET);
        queries.resize(size_t(b) / sizeof(float));
        if (std::fread(queries.data(), sizeof(float), queries.size(), f) != queries.size()) return 2;
        std::fclose(f);
    }
    const int nq = std::min<int>(int(queries.size() / DIM), nq_max);

    std::vector<int> owner;
    { std::ifstream in(work + "/objects.txt"); std::string l;
      while (std::getline(in, l)) if (!l.empty()) owner.push_back(std::stoi(l)); }
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

    std::unordered_map<int, std::vector<int>> members;
    for (size_t i = 0; i < n; ++i) members[owner[i]].push_back(int(i));

    std::vector<std::vector<int>> qscope(nq);
    std::vector<char> filt(size_t(nq) * n, 0);
    std::vector<double> qsel(nq, 0.0);
    for (int qi = 0; qi < nq; ++qi) {
        std::unordered_set<int> want;
        if (mode == "equality") want.insert(qc[qi].begin(), qc[qi].end());
        else {
            std::vector<int> st = qc[qi];
            while (!st.empty()) { const int c = st.back(); st.pop_back(); if (!want.insert(c).second) continue;
                auto it = children.find(c); if (it != children.end()) for (int w : it->second) st.push_back(w); }
        }
        qscope[qi].assign(want.begin(), want.end());
        long cnt = 0;
        for (int c : qscope[qi]) { auto it = members.find(c); if (it != members.end()) cnt += long(it->second.size()); }
        qsel[qi] = double(cnt) / double(n);
        for (size_t i = 0; i < n; ++i) filt[size_t(qi) * n + i] = nofilter ? 1 : (want.count(owner[i]) ? 1 : 0);
    }

    std::vector<faiss::idx_t> ids(size_t(k), -1);
    std::vector<float> dis(size_t(k), NAN);

    for (int g : gammas) {
        for (int mb : mbetas) {
            if (mb > 2 * M * g) { std::fprintf(stderr, "skip Mbeta=%d > 2*M*gamma=%d\n", mb, 2 * M * g); continue; }
            std::vector<int> metadata(owner.begin(), owner.begin() + n);
            const auto tb = std::chrono::steady_clock::now();
            faiss::IndexACORNFlat index(DIM, M, g, metadata, mb, faiss::METRIC_L2);
            index.add(n, base.data());
            const double build_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - tb).count();

            for (int ef : efs) {
                faiss::SearchParametersACORN sp;
                sp.efSearch = ef;
                sp.check_relative_distance = true;

                long hit = 0, denom = 0, pf_q = 0;
                double secs = 0;
                long bhit[NBAND] = {0}, bden[NBAND] = {0}, bn[NBAND] = {0};
                double bsec[NBAND] = {0};

                const size_t ndis0 = faiss::acorn_stats.ndis, n10 = faiss::acorn_stats.n1, n20 = faiss::acorn_stats.n2;
                const auto t1 = std::chrono::steady_clock::now();
                for (int qi = 0; qi < nq; ++qi) {
                    const auto q0 = std::chrono::steady_clock::now();
                    const bool use_pf = thresh >= 0 && qsel[qi] < thresh;
                    if (use_pf) {
                        scan_query(queries.data() + size_t(qi) * DIM, qscope[qi], members, base.data(), k, ids.data(), dis.data());
                        ++pf_q;
                    } else {
                        index.search(1, queries.data() + size_t(qi) * DIM, k, dis.data(), ids.data(),
                                     filt.data() + size_t(qi) * n, &sp);
                    }
                    const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - q0).count();
                    secs += dt;

                    long h = 0, d = 0;
                    score(ids, gt, qi, k, h, d);
                    hit += h; denom += d;
                    for (int bi = 0; bi < NBAND; ++bi) {
                        if (qsel[qi] >= BAND_LO[bi] && qsel[qi] < BAND_HI[bi]) {
                            bhit[bi] += h; bden[bi] += d; bn[bi] += 1; bsec[bi] += dt; break;
                        }
                    }
                }
                const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();

                std::printf("{\"mode\":\"%s\",\"n\":%zu,\"queries\":%d,\"M\":%d,\"gamma\":%d,\"Mbeta\":%d,\"ef\":%d,\"k\":%d,",
                            mode.c_str(), n, nq, M, g, mb, ef, k);
                std::printf("\"thresh\":%.4f,\"pf_q\":%ld,\"recall\":%.6f,\"qps\":%.1f,\"build_s\":%.1f,\"bands\":[",
                            thresh, pf_q, double(hit) / double(std::max<long>(denom, 1)), nq / total, build_s);
                for (int bi = 0; bi < NBAND; ++bi) {
                    char rb[32] = "null", qb[32] = "null";
                    if (bden[bi] > 0) std::snprintf(rb, sizeof rb, "%.4f", double(bhit[bi]) / double(bden[bi]));
                    if (bsec[bi] > 0) std::snprintf(qb, sizeof qb, "%.1f", double(bn[bi]) / bsec[bi]);
                    std::printf("%s{\"lo\":%.2f,\"hi\":%.2f,\"n\":%ld,\"recall\":%s,\"qps\":%s}",
                                bi ? "," : "", BAND_LO[bi], BAND_HI[bi], bn[bi], rb, qb);
                }
                std::printf("],\"ndis_per_q\":%.1f,\"n1_per_q\":%.1f,\"n2_per_q\":%.1f}\n",
                            double(faiss::acorn_stats.ndis - ndis0) / std::max(1, nq),
                            double(faiss::acorn_stats.n1 - n10) / std::max(1, nq),
                            double(faiss::acorn_stats.n2 - n20) / std::max(1, nq));
                std::fflush(stdout);
            }
        }
    }
    return 0;
}
