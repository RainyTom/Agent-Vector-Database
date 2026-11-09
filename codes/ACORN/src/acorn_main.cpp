// ACORN-1 baseline: one global HNSW over all objects, predicate-aware
// level-0 traversal. `build` writes the saved index, `search` reloads it.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_set>
#include <vector>

#include "acorn_index.h"
#include "acorn_search.h"
#include "dataset.h"

namespace {

void print_usage() {
    std::fprintf(stderr,
        "usage:\n"
        "  acorn_main build  --data DIR --index PATH [--M 16] [--efC 200]\n"
        "  acorn_main search --data DIR --index PATH --efs 10,40,200 [--k 10]\n"
        "                    [--queries 200] [--repeat 1] [--two-hop 0|1]\n"
        "                    [--trace-file PATH]\n");
}

std::vector<size_t> parse_ef_list(const std::string& text) {
    std::vector<size_t> values;
    size_t begin = 0;
    while (begin <= text.size()) {
        const size_t comma = text.find(',', begin);
        const std::string token = text.substr(begin, comma == std::string::npos ? std::string::npos : comma - begin);
        if (!token.empty()) {
            const long value = std::strtol(token.c_str(), nullptr, 10);
            if (value > 0) values.push_back(static_cast<size_t>(value));
        }
        if (comma == std::string::npos) break;
        begin = comma + 1;
    }
    return values;
}

struct Flags {
    std::string data_directory;
    std::string index_path;
    std::string ef_list = "10,40,200";
    std::string trace_file;
    int k = 10;
    int query_count = 200;
    int repeat = 1;
    bool two_hop = true;
    size_t M = 16;
    size_t ef_construction = 200;
    size_t M_beta = 8;
    size_t gamma = 2;
    size_t build_threads = 1;
    size_t hnsw_M = 0;
    double prefilter_threshold = 0.5;   // 1/gamma for gamma = 2
};

bool parse_flags(int argc, char** argv, Flags* flags) {
    for (int i = 2; i < argc; ++i) {
        const std::string name = argv[i];
        auto next = [&](std::string* value) -> bool {
            if (i + 1 >= argc) return false;
            *value = argv[++i];
            return true;
        };
        std::string value;
        if (name == "--data") { if (!next(&flags->data_directory)) return false; }
        else if (name == "--index") { if (!next(&flags->index_path)) return false; }
        else if (name == "--efs") { if (!next(&flags->ef_list)) return false; }
        else if (name == "--trace-file") { if (!next(&flags->trace_file)) return false; }
        else if (name == "--k") { if (!next(&value)) return false; flags->k = std::atoi(value.c_str()); }
        else if (name == "--queries") { if (!next(&value)) return false; flags->query_count = std::atoi(value.c_str()); }
        else if (name == "--repeat") { if (!next(&value)) return false; flags->repeat = std::atoi(value.c_str()); }
        else if (name == "--two-hop") { if (!next(&value)) return false; flags->two_hop = std::atoi(value.c_str()) != 0; }
        else if (name == "--prefilter-threshold") { if (!next(&value)) return false; flags->prefilter_threshold = std::atof(value.c_str()); }
        else if (name == "--M-beta") { if (!next(&value)) return false; flags->M_beta = static_cast<size_t>(std::strtoul(value.c_str(), nullptr, 10)); }
        else if (name == "--hnsw-M") { if (!next(&value)) return false; flags->hnsw_M = static_cast<size_t>(std::strtoul(value.c_str(), nullptr, 10)); }
        else if (name == "--build-threads") { if (!next(&value)) return false; flags->build_threads = static_cast<size_t>(std::strtoul(value.c_str(), nullptr, 10)); }
        else if (name == "--gamma") { if (!next(&value)) return false; flags->gamma = static_cast<size_t>(std::strtoul(value.c_str(), nullptr, 10)); }
        else if (name == "--M") { if (!next(&value)) return false; flags->M = static_cast<size_t>(std::strtoul(value.c_str(), nullptr, 10)); }
        else if (name == "--efC") { if (!next(&value)) return false; flags->ef_construction = static_cast<size_t>(std::strtoul(value.c_str(), nullptr, 10)); }
        else {
            std::fprintf(stderr, "unknown flag: %s\n", name.c_str());
            return false;
        }
    }
    return true;
}

int run_build(const Flags& flags) {
    AcornBuildConfig config;
    config.M = flags.M;
    config.ef_construction = flags.ef_construction;
    config.seed = 100;
    config.M_beta = flags.M_beta;
    config.gamma = flags.gamma;
    config.build_threads = flags.build_threads;
    config.hnsw_M = flags.hnsw_M;

    AcornBuildStats stats;
    std::string error;
    if (!acorn_build_index(flags.data_directory, flags.index_path, config, &stats, &error)) {
        std::fprintf(stderr, "build failed: %s\n", error.c_str());
        return 2;
    }
    std::printf("{\"method\":\"acorn\",\"action\":\"build\",\"dataset\":\"%s\",\"index\":\"%s\","
                "\"objects\":%zu,\"dim\":%d,\"M\":%zu,\"efC\":%zu,\"seed\":%u,\"build_ms\":%.1f}\n",
                flags.data_directory.c_str(), flags.index_path.c_str(), stats.object_count,
                stats.dimension, config.M, config.ef_construction, config.seed, stats.build_ms);
    return 0;
}

int run_search(const Flags& flags) {
    // Ground truth depth follows the requested k; datasets carry gt_10.txt and,
    // for the top-20 runs, gt_20.txt. Both are exact filtered top-k files.
    Dataset dataset;
    std::string error;
    if (!dataset.load(flags.data_directory, std::max(flags.k, 10), &error)) {
        std::fprintf(stderr, "dataset load failed: %s\n", error.c_str());
        return 2;
    }

    AcornIndex index;
    if (!index.load(flags.index_path, dataset.dim, &error)) {
        std::fprintf(stderr, "index load failed: %s\n", error.c_str());
        return 2;
    }
    if (index.object_count() != dataset.num_objects()) {
        std::fprintf(stderr, "index holds %zu nodes but dataset has %zu objects\n",
                     index.object_count(), dataset.num_objects());
        return 2;
    }

    AcornPredicateSpace predicate_space;
    predicate_space.build(dataset);

    const size_t query_count = std::min<size_t>(
        std::max(flags.query_count, 0), std::min(dataset.num_queries(), dataset.query_vectors.size() / size_t(dataset.dim)));
    if (query_count == 0) {
        std::fprintf(stderr, "no usable queries\n");
        return 2;
    }

    std::vector<size_t> ef_values = parse_ef_list(flags.ef_list);
    if (ef_values.empty()) {
        std::fprintf(stderr, "no positive ef values in --efs %s\n", flags.ef_list.c_str());
        return 2;
    }

    // One predicate bitmap per query; also the eligible-set size |E(U)|.
    std::vector<std::vector<uint64_t>> predicate_bits(query_count);
    std::vector<size_t> eligible_per_query(query_count, 0);
    std::vector<int> closed_attributes;
    double predicate_ms_total = 0.0;
    for (size_t qi = 0; qi < query_count; ++qi) {
        const auto start = std::chrono::steady_clock::now();
        predicate_space.closure_bitmap(dataset, dataset.query_attributes[qi], &predicate_bits[qi], &closed_attributes);
        eligible_per_query[qi] = predicate_space.eligible_count(closed_attributes);
        predicate_ms_total += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
    }
    // Counting vs scanning must agree; a mismatch would make eligible_per_q a lie.
    for (size_t qi = 0; qi < query_count; ++qi) {
        const size_t scanned = predicate_space.exact_eligible_count(dataset, predicate_bits[qi]);
        if (scanned != eligible_per_query[qi]) {
            std::fprintf(stderr, "eligible mismatch on query %zu: count %zu scan %zu\n",
                         qi, eligible_per_query[qi], scanned);
            return 2;
        }
    }
    double eligible_per_q = 0.0;
    for (size_t qi = 0; qi < query_count; ++qi) eligible_per_q += double(eligible_per_query[qi]);
    eligible_per_q /= double(query_count);

    FILE* trace = nullptr;
    if (!flags.trace_file.empty()) {
        trace = std::fopen(flags.trace_file.c_str(), "w");
        if (!trace) {
            std::perror(flags.trace_file.c_str());
            return 2;
        }
    }

    const size_t k = static_cast<size_t>(std::max(flags.k, 0));
    const size_t repeat = static_cast<size_t>(std::max(flags.repeat, 1));
    const size_t gt_depth = static_cast<size_t>(dataset.k);
    AcornSearchScratch scratch;

    for (size_t ef : ef_values) {
        unsigned long long hit = 0, denominator = 0, distance_total = 0, visited_total = 0, returned_total = 0;
        size_t prefilter_queries = 0;
        double best_pass_ms = 1e18;
        double sum_pass_ms = 0.0;
        for (size_t rep = 0; rep < repeat; ++rep) {
            double pass_ms = 0.0;
            for (size_t qi = 0; qi < query_count; ++qi) {
                const double selectivity = double(eligible_per_query[qi]) /
                                           double(index.object_count());
                const bool use_prefilter = selectivity < flags.prefilter_threshold;
                const AcornQueryResult result = use_prefilter
                    ? acorn_prefilter_query(index, predicate_space, predicate_bits[qi],
                                            dataset.query_vector(qi), flags.k)
                    : acorn_search_query(index, predicate_space, predicate_bits[qi],
                                         dataset.query_vector(qi), ef, flags.k,
                                         flags.M_beta, flags.M, &scratch);
                if (use_prefilter) ++prefilter_queries;
                pass_ms += result.query_ms;
                if (rep != 0) continue;
                distance_total += result.distance_calls;
                visited_total += result.visited_nodes;
                returned_total += result.object_ids.size();

                const size_t compare_depth = std::min(k, gt_depth);
                size_t valid = 0;
                for (size_t j = 0; j < gt_depth; ++j)
                    if (dataset.ground_truth[qi * gt_depth + j] >= 0) ++valid;
                std::unordered_set<int> found(result.object_ids.begin(), result.object_ids.end());
                size_t query_hit = 0;
                for (size_t j = 0; j < compare_depth; ++j) {
                    const int truth = dataset.ground_truth[qi * gt_depth + j];
                    if (truth >= 0 && found.count(truth)) ++query_hit;
                }
                hit += query_hit;
                denominator += std::min(k, valid);

                if (trace) {
                    std::fprintf(trace, "{\"qi\":%zu,\"ef\":%zu,\"two_hop\":%d,\"visited\":%llu,"
                                        "\"distance_calls\":%llu,\"returned\":[",
                                 qi, ef, int(flags.two_hop), result.visited_nodes, result.distance_calls);
                    for (size_t i = 0; i < result.object_ids.size(); ++i)
                        std::fprintf(trace, "%s%d", i ? "," : "", result.object_ids[i]);
                    std::fprintf(trace, "]}\n");
                }
            }
            best_pass_ms = std::min(best_pass_ms, pass_ms);
            sum_pass_ms += pass_ms;
        }
        const double recall = denominator ? double(hit) / double(denominator) : 0.0;
        const double qps = best_pass_ms > 0.0 ? double(query_count) * 1000.0 / best_pass_ms : 0.0;
        std::printf("{\"method\":\"acorn\",\"ef\":%zu,\"two_hop\":%d,\"k\":%zu,\"queries\":%zu,"
                    "\"recall\":%.6f,\"qps\":%.1f,\"per_query_ms\":%.3f,"
                    "\"dists_per_q\":%.1f,\"visited_per_q\":%.1f,\"eligible_per_q\":%.1f,"
                    "\"avg_returned\":%.1f,\"predicate_build_ms_per_q\":%.4f,\"prefilter_queries\":%zu,\"repeat\":%zu}\n",
                    ef, int(flags.two_hop), k, query_count, recall, qps,
                    sum_pass_ms / double(repeat) / double(query_count),
                    double(distance_total) / double(query_count),
                    double(visited_total) / double(query_count), eligible_per_q,
                    double(returned_total) / double(query_count),
                    predicate_ms_total / double(query_count), prefilter_queries, repeat);
    }

    if (trace) std::fclose(trace);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 2;
    }
    const std::string command = argv[1];
    Flags flags;
    if (!parse_flags(argc, argv, &flags)) {
        print_usage();
        return 2;
    }
    if (flags.data_directory.empty() || flags.index_path.empty()) {
        std::fprintf(stderr, "--data and --index are required\n");
        print_usage();
        return 2;
    }
    if (command == "build") return run_build(flags);
    if (command == "search") return run_search(flags);
    std::fprintf(stderr, "unknown command: %s\n", command.c_str());
    print_usage();
    return 2;
}