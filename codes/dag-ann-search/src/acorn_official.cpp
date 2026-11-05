// 官方 ACORN（guestrin-lab/ACORN，FAISS 分支 faiss/IndexACORN.{h,cpp}）在我们的 DAG 谓词上的基线。
// 注意：官方实现是 float32 + AVX，与"PQ 码 + 标量距离"的基线口径不同，报告时需分开。
// 谓词：per-query filter_ids_map（DAG 闭包 = 包含；精确集合 = 相等）。
#include <faiss/IndexACORN.h>
#include <faiss/index_io.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

int main(int argc, char** argv) {
    std::string dir = "work/dag-ann", work = "work/dag-scope/workload-run1", vecfile = "orig.bin", mode = "containment";
    int k = 50, ef = 50, M = 32, gamma = 12, Mbeta = 32, nq_max = 200;
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
    }

    std::vector<float> base, queries;
    {
        FILE* f = std::fopen((dir + "/" + vecfile).c_str(), "rb");
        if (!f) { std::fprintf(stderr, "missing %s\n", vecfile.c_str()); return 2; }
        std::fseek(f, 0, SEEK_END); long b = std::ftell(f); std::fseek(f, 0, SEEK_SET);
        base.resize(b / sizeof(float));
        if (std::fread(base.data(), sizeof(float), base.size(), f) != base.size()) return 2;
        std::fclose(f);
    }
    const size_t n = base.size() / 128;
    {
        FILE* f = std::fopen((dir + "/queries.bin").c_str(), "rb");
        if (!f) { std::fprintf(stderr, "missing queries.bin\n"); return 2; }
        std::fseek(f, 0, SEEK_END); long b = std::ftell(f); std::fseek(f, 0, SEEK_SET);
        queries.resize(b / sizeof(float));
        if (std::fread(queries.data(), sizeof(float), queries.size(), f) != queries.size()) return 2;
        std::fclose(f);
    }
    const int nq = std::min<int>(queries.size() / 128, nq_max);

    std::vector<int> owner;
    { std::ifstream in(work + "/objects.txt"); std::string l; while (std::getline(in, l)) if (!l.empty()) owner.push_back(std::stoi(l)); }
    std::unordered_map<int, std::vector<int>> children;
    { std::ifstream in(work + "/edges.tsv"); std::string l;
      while (std::getline(in, l)) { if (l.empty()) continue; std::stringstream ss(l); int c, p; ss >> c >> p; children[p].push_back(c); } }
    std::vector<std::vector<int>> qc;
    { std::ifstream in(work + "/queries.tsv"); std::string l;
      while (std::getline(in, l) && int(qc.size()) < nq) { if (l.empty()) continue; std::stringstream ss(l); int cnt; ss >> cnt; std::vector<int> v(cnt); for (int i = 0; i < cnt; ++i) ss >> v[i]; qc.push_back(v); } }

    std::vector<int> gt(size_t(nq) * k, -1);
    { FILE* f = std::fopen((dir + "/gt_" + std::to_string(k) + ".txt").c_str(), "rb");
      if (f) { if (std::fread(gt.data(), sizeof(int32_t), gt.size(), f) != gt.size()) {} std::fclose(f); } }

    std::vector<int> metadata(owner.begin(), owner.begin() + n);
    auto t0 = std::chrono::steady_clock::now();
    faiss::IndexACORNFlat index(128, M, gamma, metadata, Mbeta, faiss::METRIC_L2);
    index.add(n, base.data());
    double build_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    std::vector<char> filt(size_t(nq) * n, 0);
    for (int qi = 0; qi < nq; ++qi) {
        if (mode == "equality") {
            std::unordered_set<int> want(qc[qi].begin(), qc[qi].end());
            for (size_t i = 0; i < n; ++i) filt[size_t(qi) * n + i] = want.count(owner[i]) ? 1 : 0;
        } else {
            std::unordered_set<int> scope;
            std::vector<int> st = qc[qi];
            while (!st.empty()) { int c = st.back(); st.pop_back(); if (!scope.insert(c).second) continue;
                auto it = children.find(c); if (it != children.end()) for (int w : it->second) st.push_back(w); }
            for (size_t i = 0; i < n; ++i) filt[size_t(qi) * n + i] = scope.count(owner[i]) ? 1 : 0;
        }
    }

    std::vector<faiss::idx_t> ids(size_t(nq) * k);
    std::vector<float> dis(size_t(nq) * k);
    auto t1 = std::chrono::steady_clock::now();
    faiss::SearchParametersACORN sp;
    sp.efSearch = ef;
    sp.check_relative_distance = true;
    for (int qi = 0; qi < nq; ++qi) {
        index.search(1, queries.data() + size_t(qi) * 128, k, dis.data() + size_t(qi) * k,
                     ids.data() + size_t(qi) * k, filt.data() + size_t(qi) * n, &sp);
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();

    long hit = 0, denom = 0;
    for (int qi = 0; qi < nq; ++qi) {
        std::unordered_set<long> got;
        for (int j = 0; j < k; ++j) if (ids[size_t(qi) * k + j] >= 0) got.insert(ids[size_t(qi) * k + j]);
        int e = 0, h = 0;
        for (int j = 0; j < k; ++j) { int g = gt[size_t(qi) * k + j]; if (g >= 0) { ++e; if (got.count(g)) ++h; } }
        hit += h; denom += std::min(k, e);
    }
    std::printf("{\n  \"method\": \"acorn_official\", \"mode\": \"%s\", \"vectors\": \"%s\", \"n\": %zu, \"queries\": %d,\n",
                mode.c_str(), vecfile.c_str(), n, nq);
    std::printf("  \"M\": %d, \"gamma\": %d, \"Mbeta\": %d, \"ef\": %d, \"k\": %d,\n", M, gamma, Mbeta, ef, k);
    std::printf("  \"recall\": %.6f, \"qps\": %.1f, \"build_s\": %.1f\n}\n",
                double(hit) / double(std::max<long>(denom, 1)), nq / secs, build_s);
    return 0;
}
