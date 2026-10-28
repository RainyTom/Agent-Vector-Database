// Freeze T_scan(d): exact scan vs hnswlib search per local-node size.
// Plan: 2026-10-29 nodeann-first-hnswlib-plan.md section 4.2.
// All raw distances go through RawL2Space (hnswlib L2Space::get_dist_func()).
#include "raw_l2_space.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;

static std::vector<float> load_f32(const std::string& path, int dim) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("cannot open " + path);
    size_t bytes = (size_t)f.tellg();
    if (bytes % (size_t(dim) * 4)) throw std::runtime_error("bad size " + path);
    f.seekg(0);
    std::vector<float> x(bytes / 4);
    f.read((char*)x.data(), bytes);
    return x;
}

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s BASE TEST DIM OUT [sizes]\n", argv[0]);
        return 2;
    }
    const std::string base_p = argv[1], test_p = argv[2];
    const int dim = std::atoi(argv[3]);
    std::vector<int> sizes = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512};
    if (argc >= 6) {
        sizes.clear();
        std::string s = argv[5];
        size_t p = 0;
        while (p <= s.size()) {
            size_t c = s.find(',', p);
            sizes.push_back(std::atoi(s.substr(p, c == std::string::npos ? std::string::npos : c - p).c_str()));
            if (c == std::string::npos) break;
            p = c + 1;
        }
    }
    auto base = load_f32(base_p, dim);
    auto test = load_f32(test_p, dim);
    const size_t nb = base.size() / dim, nq_all = test.size() / dim;
    RawL2Space l2(dim);
    std::mt19937 rng(20261029);

    const int reps = 5, warmup = 200, measure = 800;  // queries per rep
    std::ofstream out(argv[4]);
    out << "dim,n,scan_ns,hnsw_ns,hnsw_recall,scan_wins\n";

    volatile float sink = 0;
    for (int n : sizes) {
        // one fixed local set of n random distinct base rows
        std::vector<size_t> perm(nb);
        for (size_t i = 0; i < nb; ++i) perm[i] = i;
        std::shuffle(perm.begin(), perm.end(), rng);
        std::vector<float> buf((size_t)n * dim);
        for (int j = 0; j < n; ++j)
            std::copy_n(&base[perm[j] * dim], dim, &buf[(size_t)j * dim]);

        RawL2Space sp(dim);
        hnswlib::HierarchicalNSW<float> ix(&sp, n, 16, 200, 100);
        for (int j = 0; j < n; ++j) ix.addPoint(&buf[(size_t)j * dim], j);
        const int ef = 64, k = std::min(10, n);
        ix.setEf(std::max(ef, k));

        // fixed query subset (not the workload; threshold freeze is a kernel
        // tradeoff, not query-tuned)
        std::vector<size_t> qs(measure);
        std::uniform_int_distribution<size_t> uq(0, nq_all - 1);
        for (auto& v : qs) v = uq(rng);

        auto bench = [&](auto fn) {
            // warmup
            for (int i = 0; i < warmup; ++i) fn(&test[(qs[i % measure]) * dim]);
            std::vector<double> times;
            for (int r = 0; r < reps; ++r) {
                auto t0 = Clock::now();
                for (int i = 0; i < measure; ++i) fn(&test[qs[i] * dim]);
                double ns = std::chrono::duration<double, std::nano>(Clock::now() - t0).count();
                times.push_back(ns / measure);
            }
            std::sort(times.begin(), times.end());
            return times[reps / 2];
        };

        // hnsw path: searchKnn and consume results
        auto hnsw_fn = [&](const float* q) {
            auto res = ix.searchKnn(q, k);
            while (!res.empty()) { sink += res.top().first; res.pop(); }
        };
        // scan path: exact over n rows with top-k insertion
        auto scan_fn = [&](const float* q) {
            float best[10]; size_t have = 0;
            for (int j = 0; j < n; ++j) {
                float d = l2.dist(q, &buf[(size_t)j * dim]);
                if (have < (size_t)k) {
                    size_t t = have++;
                    while (t > 0 && best[t - 1] > d) { best[t] = best[t - 1]; --t; }
                    best[t] = d;
                } else if (d < best[k - 1]) {
                    size_t t = k - 1;
                    while (t > 0 && best[t - 1] > d) { best[t] = best[t - 1]; --t; }
                    best[t] = d;
                }
            }
            sink += best[k - 1];
        };

        double hnsw_ns = bench(hnsw_fn);
        double scan_ns = bench(scan_fn);

        // recall of hnsw at this size vs exact ids
        size_t hits = 0, total = 0;
        for (int i = 0; i < 200; ++i) {
            const float* q = &test[qs[i] * dim];
            auto res = ix.searchKnn(q, k);
            std::vector<size_t> got;
            while (!res.empty()) { got.push_back(res.top().second); res.pop(); }
            std::vector<std::pair<float, size_t>> all;
            for (int j = 0; j < n; ++j)
                all.push_back({l2.dist(q, &buf[(size_t)j * dim]), (size_t)j});
            std::sort(all.begin(), all.end(), [](auto& a, auto& b) {
                return a.first != b.first ? a.first < b.first : a.second < b.second;
            });
            all.resize(k);
            for (auto& [d, j] : all)
                if (std::find(got.begin(), got.end(), j) != got.end()) ++hits;
            total += k;
        }
        double recall = double(hits) / total;
        out << dim << ',' << n << ',' << scan_ns << ',' << hnsw_ns << ',' << recall
            << ',' << (scan_ns < hnsw_ns ? 1 : 0) << '\n';
        std::fprintf(stderr, "d=%d n=%d scan=%.1fns hnsw=%.1fns recall=%.4f\n",
                     dim, n, scan_ns, hnsw_ns, recall);
    }
    return sink > 1e30 ? 1 : 0;
}
