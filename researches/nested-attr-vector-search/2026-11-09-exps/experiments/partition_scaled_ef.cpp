// Single graph vs P disjoint graphs, with each arm's efSearch set independently.
//
// Motivation under test: the earlier comparison reused one ef for both arms.
// A partition holds 1/P of the points, so its intrinsic search budget scales
// with 1/P as well; giving both arms the same ef charges the partitioned arm
// for a beam it does not need and makes merge cost look better than it is.
//
// Two families of arms are reported:
//   * matched-recall arms: each arm's ef is chosen from a sweep so both land on
//     the same recall; then per-query latency, distance calls and merge cost are
//     compared directly.
//   * fixed-ratio arms: single graph at ef_s, partitioned at ef_p = ef_s / P
//     (and a few neighbours), i.e. the proportional reduction under test.
//
// Distances go through the pinned hnswlib L2Space (RawL2Space).
#include "raw_l2_space.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
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
    std::cerr << "partition_scaled_ef: " << message << '\n';
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

std::vector<std::vector<Id>> load_gt(const std::string& path, size_t queries) {
    std::ifstream in(path, std::ios::binary);
    if (!in) fail("cannot open " + path);
    std::vector<std::vector<Id>> out;
    std::vector<int32_t> row;
    for (size_t q = 0; q < queries; ++q) {
        int32_t dim = 0;
        if (!in.read(reinterpret_cast<char*>(&dim), sizeof(dim))) break;
        if (dim <= 0) fail("bad ivecs header in " + path);
        row.assign(static_cast<size_t>(dim), 0);
        if (!in.read(reinterpret_cast<char*>(row.data()), size_t(dim) * sizeof(int32_t)))
            fail("truncated ivecs in " + path);
        std::vector<Id> ids;
        ids.reserve(row.size());
        for (int32_t value : row) ids.push_back(static_cast<Id>(value));
        out.push_back(std::move(ids));
    }
    if (out.empty()) fail("empty ground truth " + path);
    return out;
}

double micros(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}

struct CountedSpace : RawL2Space {
    explicit CountedSpace(size_t dim) : RawL2Space(dim) {}
    static float counted(const void* a, const void* b, const void* param) {
        auto* self = const_cast<CountedSpace*>(static_cast<const CountedSpace*>(param));
        ++self->calls;
        return self->dist(static_cast<const float*>(a), static_cast<const float*>(b));
    }
    hnswlib::DISTFUNC<float> get_dist_func() override { return counted; }
    void* get_dist_func_param() override { return this; }
    uint64_t calls = 0;
};

struct Stats {
    std::vector<double> latency;
    uint64_t hits = 0, denominator = 0, distance_calls = 0;
    void absorb(const Stats& other) {
        latency.insert(latency.end(), other.latency.begin(), other.latency.end());
        hits += other.hits;
        denominator += other.denominator;
        distance_calls += other.distance_calls;
    }
};

double mean(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    return std::accumulate(values.begin(), values.end(), 0.0) / double(values.size());
}

double pctl(std::vector<double> values, double p) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    size_t index = size_t(p * double(values.size() - 1));
    return values[index];
}

double recall_of(const Stats& stats) {
    return stats.denominator ? double(stats.hits) / double(stats.denominator) : 0.0;
}

std::vector<std::pair<float, Id>> take(
        std::priority_queue<std::pair<float, hnswlib::labeltype>> found, size_t k) {
    std::vector<std::pair<float, Id>> result;
    result.reserve(std::min(k, found.size()));
    while (!found.empty() && result.size() < k) {
        result.emplace_back(found.top().first, static_cast<Id>(found.top().second));
        found.pop();
    }
    return result;
}

size_t hits_of(size_t q, const std::vector<std::pair<float, Id>>& got,
               const std::vector<std::vector<Id>>& gt) {
    size_t hits = 0;
    for (const auto& item : got)
        if (std::find(gt[q].begin(), gt[q].end(), item.second) != gt[q].end()) ++hits;
    return hits;
}

void write_stats(std::ostream& out, const Stats& stats) {
    out << "{\"queries\":" << stats.latency.size()
        << ",\"mean_us\":" << mean(stats.latency)
        << ",\"p50_us\":" << pctl(stats.latency, .50)
        << ",\"p95_us\":" << pctl(stats.latency, .95)
        << ",\"recall_at_k\":" << recall_of(stats)
        << ",\"hits\":" << stats.hits << ",\"denominator\":" << stats.denominator
        << ",\"distance_calls_per_query\":"
        << (stats.latency.empty() ? 0.0 : double(stats.distance_calls) / double(stats.latency.size()))
        << "}";
}

std::vector<size_t> parse_list(const std::string& text) {
    std::vector<size_t> values;
    std::istringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ','))
        if (!item.empty()) values.push_back(std::stoull(item));
    return values;
}

}  // namespace

int main(int argc, char** argv) {
    std::string base_path, query_path, gt_path, out_path;
    size_t partitions = 5, k = 100, queries = 1000, seed = 20261101, threads = 1;
    std::string single_ef_list = "40,64,128,256";
    std::string per_partition_ef_list = "10,16,25,40,64";
    std::string per_partition_k_list = "25,100";
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
        else if (arg == "--single-ef") single_ef_list = next();
        else if (arg == "--partition-ef") per_partition_ef_list = next();
        else if (arg == "--partition-k") per_partition_k_list = next();
        else fail("unknown argument " + arg);
    }
    if (base_path.empty() || query_path.empty() || gt_path.empty() || out_path.empty())
        fail("--base --queries --gt --out are required");
    if (!partitions || k < partitions) fail("invalid partitions/k");

    const std::vector<size_t> single_efs = parse_list(single_ef_list);
    const std::vector<size_t> partition_efs = parse_list(per_partition_ef_list);
    const std::vector<size_t> partition_ks = parse_list(per_partition_k_list);
    if (single_efs.empty() || partition_efs.empty() || partition_ks.empty())
        fail("empty ef or k list");
    for (size_t value : partition_ks)
        if (!value || value > k) fail("per-partition k must be within 1..k");

    Vectors base = load_fvecs(base_path, std::numeric_limits<size_t>::max());
    Vectors query_vectors = load_fvecs(query_path, std::numeric_limits<size_t>::max());
    auto gt = load_gt(gt_path, queries);
    const size_t n = base.count, dim = base.dim;
    const size_t nq = std::min({queries, query_vectors.count, gt.size()});
    for (auto& row : gt) row.resize(k);
    std::cout << "base " << n << "x" << dim << " queries " << nq << " k " << k
              << " partitions " << partitions << "\n";

    std::vector<Id> part(n);
    {
        std::vector<Id> order(n);
        std::iota(order.begin(), order.end(), 0U);
        std::mt19937_64 rng(seed);
        std::shuffle(order.begin(), order.end(), rng);
        for (size_t i = 0; i < n; ++i) part[order[i]] = static_cast<Id>(i % partitions);
    }

    CountedSpace space(dim);
    const size_t M = 16, ef_construction = 200;

    const auto single_build_start = Clock::now();
    hnswlib::HierarchicalNSW<float> single(&space, n, M, ef_construction);
    for (size_t i = 0; i < n; ++i)
        single.addPoint(base.data.data() + i * dim, static_cast<hnswlib::labeltype>(i));
    const double single_build_s = micros(single_build_start) / 1e6;

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
    std::cout << "single_build_s " << single_build_s
              << " partition_build_s " << partition_build_s << "\n";

    std::mutex merge_mutex;

    // One arm = a search configuration. ef is set on the arm's own graph(s).
    auto run_arm = [&](bool partitioned, size_t ef, size_t per_k) -> Stats {
        Stats stats;
        if (partitioned) {
            for (auto& graph : graphs) graph->setEf(std::max(ef, per_k));
        } else {
            single.setEf(std::max(ef, k));
        }
        std::atomic<size_t> next_query{0};
        std::vector<std::thread> pool;
        std::vector<Stats> per_thread(threads);
        for (size_t t = 0; t < threads; ++t) {
            pool.emplace_back([&, t]() {
                Stats mine;
                std::vector<std::pair<float, Id>> got;
                for (;;) {
                    const size_t q = next_query.fetch_add(1);
                    if (q >= nq) break;
                    const float* vec = query_vectors.data.data() + q * dim;
                    const auto start = Clock::now();
                    got.clear();
                    if (partitioned) {
                        for (size_t b = 0; b < partitions; ++b) {
                            auto found = graphs[b]->searchKnn(vec, per_k);
                            while (!found.empty()) {
                                got.emplace_back(found.top().first, static_cast<Id>(found.top().second));
                                found.pop();
                            }
                        }
                        std::stable_sort(got.begin(), got.end());
                        if (got.size() > k) got.resize(k);
                    } else {
                        got = take(single.searchKnn(vec, k), k);
                    }
                    const double us = micros(start);
                    mine.latency.push_back(us);
                    mine.hits += hits_of(q, got, gt);
                    mine.denominator += std::min(k, gt[q].size());
                }
                per_thread[t] = std::move(mine);
            });
        }
        for (auto& thread : pool) thread.join();
        for (auto& mine : per_thread) stats.absorb(mine);
        return stats;
    };

    // Untimed pass for distance counts, since the counter is shared state.
    auto count_arm = [&](bool partitioned, size_t ef, size_t per_k) -> uint64_t {
        if (partitioned) {
            for (auto& graph : graphs) graph->setEf(std::max(ef, per_k));
        } else {
            single.setEf(std::max(ef, k));
        }
        uint64_t calls = 0;
        std::vector<std::pair<float, Id>> got;
        for (size_t q = 0; q < nq; ++q) {
            const float* vec = query_vectors.data.data() + q * dim;
            const uint64_t before = space.calls;
            got.clear();
            if (partitioned) {
                for (size_t b = 0; b < partitions; ++b) {
                    auto found = graphs[b]->searchKnn(vec, per_k);
                    while (!found.empty()) {
                        got.emplace_back(found.top().first, static_cast<Id>(found.top().second));
                        found.pop();
                    }
                }
                std::stable_sort(got.begin(), got.end());
                if (got.size() > k) got.resize(k);
            } else {
                got = take(single.searchKnn(vec, k), k);
            }
            calls += space.calls - before;
        }
        return calls;
    };

    std::ostream* out = &std::cout;
    std::ofstream file;
    if (!out_path.empty() && out_path != "-") {
        file.open(out_path);
        if (!file) fail("cannot write " + out_path);
        out = &file;
    }

    *out << "{\n  \"kind\": \"single_graph_vs_partitions_scaled_ef\",\n"
         << "  \"dataset\": {\"base\": \"" << base_path << "\", \"n\": " << n
         << ", \"dim\": " << dim << ", \"queries\": " << nq << ", \"k\": " << k << "},\n"
         << "  \"partitions\": " << partitions << ",\n"
         << "  \"seed\": " << seed << ",\n"
         << "  \"graph\": {\"M\": " << M << ", \"efConstruction\": " << ef_construction << "},\n"
         << "  \"build_seconds\": {\"single\": " << single_build_s
         << ", \"partitioned\": " << partition_build_s << "},\n"
         << "  \"threads\": " << threads << ",\n";

    *out << "  \"single\": {";
    bool first = true;
    for (size_t ef : single_efs) {
        Stats stats = run_arm(false, ef, 0);
        stats.distance_calls = count_arm(false, ef, 0);
        *out << (first ? "\n" : ",\n") << "    \"" << ef << "\": ";
        write_stats(*out, stats);
        first = false;
    }
    *out << "\n  },\n";

    *out << "  \"partitioned\": {";
    first = true;
    for (size_t per_k : partition_ks) {
        for (size_t ef : partition_efs) {
            Stats stats = run_arm(true, ef, per_k);
            stats.distance_calls = count_arm(true, ef, per_k);
            *out << (first ? "\n" : ",\n") << "    \"k" << per_k << "_ef" << ef << "\": ";
            write_stats(*out, stats);
            first = false;
        }
    }
    *out << "\n  }\n}\n";
    if (file.is_open()) file.close();
    std::cout << "wrote " << out_path << "\n";
    return 0;
}
