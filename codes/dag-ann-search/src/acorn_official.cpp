// 官方 ACORN（guestrin-lab/ACORN，FAISS 分支 faiss/IndexACORN.{h,cpp}），只实现 ACORN-gamma。
//
// 方法本体包含选择率回退：s = |allowed|/N，若 s < thresh（默认 1/gamma）则对合格集做 pre-filter
// 精确扫描，否则用 ACORN-gamma 的图搜索。两条路径分别计时与统计，用于定位"低选择率下应回退变快"
// 是否成立，以及实测交叉点是否等于 1/gamma。
//
// 谓词：per-query filter_ids_map（DAG 闭包 = 包含；精确集合 = 相等）。
#include <faiss/IndexACORN.h>

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

// 与 B-scan 同源：直接遍历合格概念下的 item 列表，不扫全量位图。
inline void scan_query(const float* q, const std::vector<int>& scope,
                       const std::unordered_map<int, std::vector<int>>& members,
                       const float* base, int k, faiss::idx_t* ids, float* dis) {
    std::priority_queue<std::pair<float, int>> heap;  // max-heap on distance
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

struct Acc { long queries = 0; double secs = 0; long hit = 0; long denom = 0; };

// ids 与 dis 是单查询缓冲（长度 k），按位置索引。
inline void score(const std::vector<faiss::idx_t>& ids, const std::vector<int>& gt, int qi, int k, Acc& a) {
    std::unordered_set<long> got;
    for (int j = 0; j < k; ++j) if (ids[size_t(j)] >= 0) got.insert(ids[size_t(j)]);
    int e = 0, h = 0;
    for (int j = 0; j < k; ++j) { const int g = gt[size_t(qi) * k + j]; if (g >= 0) { ++e; if (got.count(g)) ++h; } }
    a.hit += h; a.denom += std::min(k, e);
}

}  // namespace

int main(int argc, char** argv) {
    std::string dir = "work/dag-ann", work = "work/dag-scope/workload-run1", vecfile = "orig.bin", mode = "containment";
    int k = 50, ef = 50, M = 32, gamma = 12, Mbeta = 32, nq_max = 200;
    double thresh = -1.0;               // <0 => 1/gamma
    std::vector<double> sweep;          // 非空则一次建图跑多阈值
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto nx = [&]() { return std::string(argv[++i]); };
        if (a == "--dir") dir = nx();
        else if (a == "--work") work = nx();
        else if (a == "--vec") vecfile = nx();
        else if (a == "--mode") mode = nx();
        else if (a == "--k") k = std::stoi(nx());
        else if (a == "--ef") ef = std::stoi(nx());
        else if (a == "--M") M = std::stoi(nx());
        else if (a == "--gamma") gamma = std::stoi(nx());
        else if (a == "--Mbeta") Mbeta = std::stoi(nx());
        else if (a == "--queries") nq_max = std::stoi(nx());
        else if (a == "--thresh") thresh = std::stod(nx());
        else if (a == "--sweep") {
            std::stringstream ss(nx()); std::string t;
            while (std::getline(ss, t, ',')) if (!t.empty()) sweep.push_back(std::stod(t));
        }
    }
    if (thresh < 0) thresh = 1.0 / gamma;
    if (sweep.empty()) sweep.push_back(thresh);

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

    std::vector<int> metadata(owner.begin(), owner.begin() + n);
    auto t0 = std::chrono::steady_clock::now();
    faiss::IndexACORNFlat index(DIM, M, gamma, metadata, Mbeta, faiss::METRIC_L2);
    index.add(n, base.data());
    const double build_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    std::unordered_map<int, std::vector<int>> members;   // concept -> item ids
    for (size_t i = 0; i < n; ++i) members[owner[i]].push_back(int(i));

    std::vector<std::vector<int>> qscope(nq);
    std::vector<char> filt(size_t(nq) * n, 0);
    for (int qi = 0; qi < nq; ++qi) {
        std::unordered_set<int> want;
        if (mode == "equality") want.insert(qc[qi].begin(), qc[qi].end());
        else {
            std::vector<int> st = qc[qi];
            while (!st.empty()) { const int c = st.back(); st.pop_back(); if (!want.insert(c).second) continue;
                auto it = children.find(c); if (it != children.end()) for (int w : it->second) st.push_back(w); }
        }
        qscope[qi].assign(want.begin(), want.end());
        for (size_t i = 0; i < n; ++i) filt[size_t(qi) * n + i] = want.count(owner[i]) ? 1 : 0;
    }

    faiss::SearchParametersACORN sp;
    sp.efSearch = ef;
    sp.check_relative_distance = true;

    std::vector<faiss::idx_t> ids(size_t(k), -1);
    std::vector<float> dis(size_t(k), NAN);

    for (double th : sweep) {
        Acc pf, ac;
        long n_acorn = 0;
        std::vector<long> sel(nq, 0);
        const auto t1 = std::chrono::steady_clock::now();
        for (int qi = 0; qi < nq; ++qi) {
            const char* allow = filt.data() + size_t(qi) * n;
            long cnt = 0;
            for (int c : qscope[qi]) { auto it = members.find(c); if (it != members.end()) cnt += long(it->second.size()); }
            sel[qi] = cnt;
            const double s = double(cnt) / double(n);
            const auto q0 = std::chrono::steady_clock::now();
            if (s < th) {
                scan_query(queries.data() + size_t(qi) * DIM, qscope[qi], members, base.data(), k, ids.data(), dis.data());
                pf.secs += std::chrono::duration<double>(std::chrono::steady_clock::now() - q0).count();
                pf.queries += 1;
                score(ids, gt, qi, k, pf);
            } else {
                index.search(1, queries.data() + size_t(qi) * DIM, k, dis.data(), ids.data(), const_cast<char*>(allow), &sp);
                ac.secs += std::chrono::duration<double>(std::chrono::steady_clock::now() - q0).count();
                ac.queries += 1;
                ++n_acorn;
                score(ids, gt, qi, k, ac);
            }
        }
        const double total_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
        const long hit = pf.hit + ac.hit, den = pf.denom + ac.denom;
        double mean_sel = 0; for (long v : sel) mean_sel += double(v) / double(n);
        mean_sel /= std::max<size_t>(1, sel.size());
        std::printf("{");
        std::printf("\"mode\":\"%s\",\"n\":%zu,\"queries\":%d,\"M\":%d,\"gamma\":%d,\"Mbeta\":%d,\"ef\":%d,\"k\":%d,",
                    mode.c_str(), n, nq, M, gamma, Mbeta, ef, k);
        std::printf("\"thresh\":%.4f,\"mean_sel\":%.5f,", th, mean_sel);
        std::printf("\"recall\":%.6f,\"qps\":%.1f,", double(hit) / double(std::max<long>(den, 1)), nq / total_s);
        std::printf("\"pf_q\":%ld,\"pf_qps\":%.1f,\"pf_recall\":%.6f,", pf.queries,
                    pf.queries / std::max(1e-9, pf.secs), double(pf.hit) / double(std::max<long>(pf.denom, 1)));
        std::printf("\"ac_q\":%ld,\"ac_qps\":%.1f,\"ac_recall\":%.6f,", ac.queries,
                    ac.queries / std::max(1e-9, ac.secs), double(ac.hit) / double(std::max<long>(ac.denom, 1)));
        std::printf("\"build_s\":%.1f}\n", build_s);
    }
    return 0;
}
