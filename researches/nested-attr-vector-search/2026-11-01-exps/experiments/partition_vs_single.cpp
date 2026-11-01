// Single graph (top-100) vs 10 even graphs (top-50 each, merged to top-100).
// All distances go through the pinned hnswlib L2Space via RawL2Space.
// efSearch is set once per arm before any worker starts; searches are read-only.
// Distance counts are collected in an untimed second pass so timing is clean.
#include "raw_l2_space.h"

#include "hnswlib/hnswlib.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;
using Id = uint32_t;

namespace {
struct Vectors {
    size_t dim = 0, count = 0;
    std::vector<float> data;
};

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "partition_vs_single: " << message << '\n';
    std::exit(2);
}

Vectors load_fvecs(const std::string& path, size_t limit) {
    std::ifstream in(path, std::ios::binary);
    if (!in) fail("cannot open " + path);
    Vectors out;
    std::vector<float> buffer;
    while (out.count < limit) {
        int32_t dim = 0;
        if (!in.read(reinterpret_cast<char*>(&dim), sizeof(dim))) break;
        if (dim <= 0) fail("bad fvecs header in " + path);
        if (!out.dim) out.dim = static_cast<size_t>(dim);
        if (static_cast<size_t>(dim) != out.dim) fail("inconsistent fvecs dim");
        buffer.assign(out.dim, 0.0f);
        if (!in.read(reinterpret_cast<char*>(buffer.data()), out.dim * sizeof(float)))
            fail("truncated fvecs in " + path);
        out.data.insert(out.data.end(), buffer.begin(), buffer.end());
        ++out.count;
    }
    if (!out.count) fail("empty fvecs " + path);
    return out;
}

// GT file: standard ivecs (int32 k, then k int32 ids per record).
std::vector<std::vector<Id>> load_gt(const std::string& path, size_t limit) {
    std::ifstream in(path, std::ios::binary);
    if (!in) fail("cannot open " + path);
    std::vector<std::vector<Id>> rows;
    while (rows.size() < limit) {
        int32_t k = 0;
        if (!in.read(reinterpret_cast<char*>(&k), sizeof(k))) break;
        if (k <= 0) fail("bad ivecs header in " + path);
        std::vector<Id> row(static_cast<size_t>(k));
        if (!in.read(reinterpret_cast<char*>(row.data()), k * sizeof(int32_t)))
            fail("truncated ivecs in " + path);
        rows.push_back(std::move(row));
    }
    if (rows.empty()) fail("empty ivecs " + path);
    return rows;
}

struct CountedSpace final : RawL2Space {
    std::atomic<uint64_t> calls{0};
    bool counting = false;
    explicit CountedSpace(size_t dim) : RawL2Space(dim) {}
    static float counted(const void* a, const void* b, const void* param) {
        auto* self = const_cast<CountedSpace*>(static_cast<const CountedSpace*>(param));
        if (self->counting) self->calls.fetch_add(1, std::memory_order_relaxed);
        return self->dist(static_cast<const float*>(a), static_cast<const float*>(b));
    }
    hnswlib::DISTFUNC<float> get_dist_func() override { return counted; }
    void* get_dist_func_param() override { return this; }
};

double micros(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}
double mean(const std::vector<double>& values) {
    if (values.empty()) return 0;
    double total = 0;
    for (double value : values) total += value;
    return total / values.size();
}
double pctl(std::vector<double> values, double q) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    return values[static_cast<size_t>(q * (values.size() - 1))];
}

struct Stats {
    std::vector<double> latency;
    size_t hits = 0, denominator = 0, candidates = 0;
    void add(double us, size_t hit, size_t denom, size_t cand) {
        latency.push_back(us);
        hits += hit;
        denominator += denom;
        candidates += cand;
    }
    void absorb(const Stats& other) {
        latency.insert(latency.end(), other.latency.begin(), other.latency.end());
        hits += other.hits;
        denominator += other.denominator;
        candidates += other.candidates;
    }
};

double recall(const Stats& stats) {
    return stats.denominator ? double(stats.hits) / double(stats.denominator) : 0.0;
}

std::vector<std::pair<float, Id>> take(
    std::priority_queue<std::pair<float, hnswlib::labeltype>> found, size_t k) {
    std::vector<std::pair<float, Id>> out;
    while (!found.empty() && out.size() < k) {
        out.emplace_back(found.top().first, static_cast<Id>(found.top().second));
        found.pop();
    }
    std::sort(out.begin(), out.end());
    return out;
}

void write_stats(std::ostream& out, const Stats& stats, uint64_t distance_calls) {
    out << "{\"queries\":" << stats.latency.size()
        << ",\"mean_us\":" << mean(stats.latency)
        << ",\"p50_us\":" << pctl(stats.latency, .50)
        << ",\"p95_us\":" << pctl(stats.latency, .95)
        << ",\"recall_at_100\":" << recall(stats)
        << ",\"hits\":" << stats.hits << ",\"denominator\":" << stats.denominator
        << ",\"candidates_merged\":"
        << (stats.latency.empty() ? 0 : stats.candidates / stats.latency.size())
        << ",\"distance_calls_per_query\":"
        << (stats.latency.empty() ? 0.0
                                  : double(distance_calls) / double(stats.latency.size()))
        << "}";
}
} // namespace

int main(int argc, char** argv) {
    std::string base_path, query_path, gt_path, out_path;
    size_t partitions = 10, k = 100, queries = 1000, seed = 20261101, threads = 16;
    std::string ef_list = "100,200,400,800,1600";
    std::string per_k_list = "10,50";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() {
            if (i + 1 >= argc) fail("missing value for " + arg);
            return std::string(argv[++i]);
        };
        if (arg == "--base") base_path = next();
        else if (arg == "--queries") query_path = next();
        else if (arg == "--gt") gt_path = next();
        else if (arg == "--out") out_path = next();
        else if (arg == "--partitions") partitions = std::stoull(next());
        else if (arg == "--k") k = std::stoull(next());
        else if (arg == "--nq") queries = std::stoull(next());
        else if (arg == "--seed") seed = std::stoull(next());
        else if (arg == "--threads") threads = std::stoull(next());
        else if (arg == "--ef") ef_list = next();
        else if (arg == "--per-partition-k") per_k_list = next();
        else fail("unknown argument " + arg);
    }
    if (base_path.empty() || query_path.empty() || gt_path.empty() || out_path.empty())
        fail("--base --queries --gt --out are required");
    if (!partitions || k < partitions) fail("invalid partitions/k");

    std::vector<size_t> efs;
    {
        std::istringstream stream(ef_list);
        std::string item;
        while (std::getline(stream, item, ','))
            if (!item.empty()) efs.push_back(std::stoull(item));
    }
    if (efs.empty()) fail("empty ef list");
    std::vector<size_t> per_ks;
    {
        std::istringstream stream(per_k_list);
        std::string item;
        while (std::getline(stream, item, ','))
            if (!item.empty()) per_ks.push_back(std::stoull(item));
    }
    if (per_ks.empty()) fail("empty --per-partition-k list");
    for (size_t value : per_ks)
        if (!value || value > k) fail("per-partition k must be within 1..k");

    Vectors base = load_fvecs(base_path, std::numeric_limits<size_t>::max());
    Vectors query_vectors = load_fvecs(query_path, std::numeric_limits<size_t>::max());
    auto gt = load_gt(gt_path, queries);
    const size_t n = base.count, dim = base.dim;
    const size_t nq = std::min({queries, query_vectors.count, gt.size()});
    for (auto& row : gt) row.resize(k);
    std::cout << "base " << n << "x" << dim << " queries " << nq << " gt k=" << gt.front().size()
              << " partitions " << partitions << " k " << k << "\n";

    std::vector<Id> part(n);
    {
        std::vector<Id> order(n);
        std::iota(order.begin(), order.end(), 0U);
        std::mt19937_64 rng(seed);
        std::shuffle(order.begin(), order.end(), rng);
        for (size_t i = 0; i < n; ++i) part[order[i]] = static_cast<Id>(i % partitions);
    }
    std::vector<size_t> sizes(partitions, 0);
    for (Id b : part) ++sizes[b];

    CountedSpace space(dim);
    const size_t M = 16, ef_construction = 200;

    const auto build_start = Clock::now();
    hnswlib::HierarchicalNSW<float> single(&space, n, M, ef_construction);
    for (size_t i = 0; i < n; ++i)
        single.addPoint(base.data.data() + i * dim, static_cast<hnswlib::labeltype>(i));
    const double single_build_s = micros(build_start) / 1e6;

    const auto partition_build_start = Clock::now();
    std::vector<std::unique_ptr<hnswlib::HierarchicalNSW<float>>> graphs(partitions);
    {
        std::vector<std::vector<size_t>> members(partitions);
        for (size_t i = 0; i < n; ++i) members[part[i]].push_back(i);
        for (size_t b = 0; b < partitions; ++b) {
            graphs[b] = std::make_unique<hnswlib::HierarchicalNSW<float>>(
                &space, members[b].size(), M, ef_construction);
            for (size_t member : members[b])
                graphs[b]->addPoint(base.data.data() + member * dim,
                                    static_cast<hnswlib::labeltype>(member));
        }
    }
    const double partition_build_s = micros(partition_build_start) / 1e6;

    auto set_single_ef = [&](size_t ef) { single.setEf(ef); };
    auto set_partition_ef = [&](size_t ef, size_t per_k) {
        for (auto& graph : graphs) graph->setEf(std::max(ef, per_k));
    };

    auto partition_search = [&](size_t q, size_t per_k, std::vector<std::pair<float, Id>>& out) {
        const float* vec = query_vectors.data.data() + q * dim;
        out.clear();
        for (size_t b = 0; b < partitions; ++b) {
            auto found = graphs[b]->searchKnn(vec, per_k);
            while (!found.empty()) {
                out.emplace_back(found.top().first, static_cast<Id>(found.top().second));
                found.pop();
            }
        }
        std::sort(out.begin(), out.end());
        if (out.size() > k) out.resize(k);
    };

    auto hits_of = [&](size_t q, const std::vector<std::pair<float, Id>>& got) {
        size_t hits = 0;
        for (const auto& item : got)
            if (std::find(gt[q].begin(), gt[q].end(), item.second) != gt[q].end()) ++hits;
        return hits;
    };

    std::ostringstream report;
    report.precision(10);
    report << "{\n  \"kind\": \"single_graph_vs_even_partitions\",\n"
           << "  \"objects\": " << n << ", \"dim\": " << dim << ", \"queries\": " << nq
           << ", \"k\": " << k << ", \"partitions\": " << partitions
           << ", \"per_partition_k_values\": [";
    for (size_t i = 0; i < per_ks.size(); ++i) report << (i ? "," : "") << per_ks[i];
    report << "], \"seed\": " << seed
           << ", \"single_build_s\": " << single_build_s
           << ", \"partition_build_s\": " << partition_build_s
           << ", \"thread_pool\":" << threads << ",\n"
           << "  \"build_scope\": \"single-threaded builds, M=16, efConstruction=200; one global "
              "graph plus one 10-graph set; query timing excludes builds\",\n"
           << "  \"partition_sizes\": [";
    for (size_t b = 0; b < partitions; ++b) report << (b ? "," : "") << sizes[b];
    report << "],\n";

    // ---- single graph, one thread ----
    report << "  \"single\": {";
    for (size_t e = 0; e < efs.size(); ++e) {
        set_single_ef(efs[e]);
        Stats stats;
        for (size_t q = 0; q < nq; ++q) {
            const auto start = Clock::now();
            auto got = take(single.searchKnn(query_vectors.data.data() + q * dim, k), k);
            stats.add(micros(start), hits_of(q, got), gt[q].size(), got.size());
        }
        space.calls = 0;
        space.counting = true;
        for (size_t q = 0; q < nq; ++q) single.searchKnn(query_vectors.data.data() + q * dim, k);
        space.counting = false;
        report << (e ? "," : "") << "\n    \"ef" << efs[e] << "\": ";
        write_stats(report, stats, space.calls.load());
    }
    report << "\n  },\n";

    // ---- 10 graphs serial, one thread ----
    report << "  \"partition_serial\": {";
    for (size_t p = 0; p < per_ks.size(); ++p) {
        report << (p ? "," : "") << "\n    \"k" << per_ks[p] << "\": {";
        for (size_t e = 0; e < efs.size(); ++e) {
            set_partition_ef(efs[e], per_ks[p]);
            Stats stats;
            std::vector<std::pair<float, Id>> got;
            for (size_t q = 0; q < nq; ++q) {
                const auto start = Clock::now();
                partition_search(q, per_ks[p], got);
                stats.add(micros(start), hits_of(q, got), gt[q].size(), got.size());
            }
            space.calls = 0;
            space.counting = true;
            for (size_t q = 0; q < nq; ++q) partition_search(q, per_ks[p], got);
            space.counting = false;
            report << (e ? "," : "") << "\n      \"ef" << efs[e] << "\": ";
            write_stats(report, stats, space.calls.load());
        }
        report << "}";
    }
    report << "\n  },\n";

    // ---- 10 graphs on 10 threads, per-query wall latency ----
    {
        std::atomic<uint64_t> generation{0};
        std::atomic<int> pending{0};
        std::atomic<bool> stop{false};
        const float* query_vector = nullptr;
        size_t query_per_k = per_ks.front();
        std::vector<std::vector<std::pair<float, Id>>> per_thread(partitions);
        std::vector<std::thread> pool;
        for (size_t b = 0; b < partitions; ++b) {
            pool.emplace_back([&, b]() {
                uint64_t seen = 0;
                while (!stop.load(std::memory_order_acquire)) {
                    const uint64_t current = generation.load(std::memory_order_acquire);
                    if (current == seen) {
                        std::this_thread::yield();
                        continue;
                    }
                    seen = current;
                    auto found = graphs[b]->searchKnn(query_vector, query_per_k);
                    auto& sink = per_thread[b];
                    sink.clear();
                    while (!found.empty()) {
                        sink.emplace_back(found.top().first, static_cast<Id>(found.top().second));
                        found.pop();
                    }
                    pending.fetch_sub(1, std::memory_order_acq_rel);
                }
            });
        }
        report << "  \"partition_parallel\": {";
        for (size_t p = 0; p < per_ks.size(); ++p) {
            report << (p ? "," : "") << "\n    \"k" << per_ks[p] << "\": {";
            for (size_t e = 0; e < efs.size(); ++e) {
                set_partition_ef(efs[e], per_ks[p]);
                query_per_k = per_ks[p];
                Stats stats;
                std::vector<std::pair<float, Id>> merged;
                for (size_t q = 0; q < nq; ++q) {
                    query_vector = query_vectors.data.data() + q * dim;
                    pending.store(static_cast<int>(partitions), std::memory_order_release);
                    const auto start = Clock::now();
                    generation.fetch_add(1, std::memory_order_release);
                    while (pending.load(std::memory_order_acquire) > 0) std::this_thread::yield();
                    const double us = micros(start);
                    merged.clear();
                    for (auto& sink : per_thread)
                        merged.insert(merged.end(), sink.begin(), sink.end());
                    std::sort(merged.begin(), merged.end());
                    if (merged.size() > k) merged.resize(k);
                    stats.add(us, hits_of(q, merged), gt[q].size(), merged.size());
                }
                report << (e ? "," : "") << "\n      \"ef" << efs[e] << "\": ";
                write_stats(report, stats, 0);
            }
            report << "}";
        }
        report << "\n  },\n";
        stop = true;
        for (auto& thread : pool) thread.join();
    }

    // ---- throughput with a shared core pool ----
    auto batch = [&](bool partitioned, size_t per_k, Stats& stats) {
        std::atomic<size_t> next{0};
        std::mutex mutex;
        Stats total;
        std::vector<std::thread> pool;
        for (size_t t = 0; t < threads; ++t) {
            pool.emplace_back([&]() {
                Stats mine;
                std::vector<std::pair<float, Id>> got;
                while (true) {
                    const size_t q = next.fetch_add(1);
                    if (q >= nq) break;
                    const auto start = Clock::now();
                    if (partitioned) {
                        partition_search(q, per_k, got);
                    } else {
                        got = take(single.searchKnn(query_vectors.data.data() + q * dim, k), k);
                    }
                    const double us = micros(start);
                    mine.add(us, hits_of(q, got), gt[q].size(), got.size());
                }
                std::lock_guard<std::mutex> guard(mutex);
                total.absorb(mine);
            });
        }
        for (auto& thread : pool) thread.join();
        stats = total;
    };

    report << "  \"throughput_" << threads << "threads\": {\"single\": {";
    for (size_t e = 0; e < efs.size(); ++e) {
        set_single_ef(efs[e]);
        Stats stats;
        const auto start = Clock::now();
        batch(false, per_ks.front(), stats);
        const double wall = micros(start);
        report << (e ? "," : "") << "\n    \"ef" << efs[e] << "\": {\"qps\":"
               << (wall > 0 ? double(stats.latency.size()) / (wall / 1e6) : 0.0)
               << ",\"recall_at_100\":" << recall(stats) << "}";
    }
    report << "}, \"partition\": {";
    for (size_t p = 0; p < per_ks.size(); ++p) {
        report << (p ? "," : "") << "\n    \"k" << per_ks[p] << "\": {";
        for (size_t e = 0; e < efs.size(); ++e) {
            set_partition_ef(efs[e], per_ks[p]);
            Stats stats;
            const auto start = Clock::now();
            batch(true, per_ks[p], stats);
            const double wall = micros(start);
            report << (e ? "," : "") << "\n      \"ef" << efs[e] << "\": {\"qps\":"
                   << (wall > 0 ? double(stats.latency.size()) / (wall / 1e6) : 0.0)
                   << ",\"recall_at_100\":" << recall(stats) << "}";
        }
        report << "}";
    }
    report << "}},\n";

    report << "  \"notes\": [\"recall@100 against ann-benchmarks SIFT1M exact top-100\", "
              "\"distance calls measured in an untimed second pass\", "
              "\"partitions are random and even, so no partition can be pruned for a global top-k "
              "query; every partition holds true neighbours\", "
              "\"per-partition k is a separate parameter from global k; with k=100 and 10 "
              "partitions, per-partition 50 merges up to 500 candidates\", "
              "\"partition_parallel uses one thread per partition and therefore "
           << partitions << " cores; throughput arms use a shared " << threads << "-core pool\"]\n}\n";

    std::ofstream out(out_path);
    if (!out) fail("cannot open " + out_path);
    out << report.str();
    std::cout << report.str();
    return 0;
}
