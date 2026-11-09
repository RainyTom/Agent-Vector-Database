// Command line front end: `asi build` and `asi search` are separate runs so the
// index can be built once and reused across query grids and datasets.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "asi_index.h"
#include "asi_search.h"
#include "dataset.h"

namespace {

std::vector<int> parse_int_list(const std::string& text) {
    std::vector<int> values;
    std::stringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ','))
        if (!token.empty()) values.push_back(std::atoi(token.c_str()));
    return values;
}

void print_usage() {
    std::fprintf(stderr,
        "usage:\n"
        "  asi build  --data DIR --index PATH [--M 16] [--efC 200] [--K 8] [--K-rule linear|sqrt|cluster] [--bridge-receivers 1] [--merge-file PATH]\n"
        "  asi search --data DIR --index PATH --efs 10,40,200 [--entry-mode representative|hnsw]\n"
        "             [--entry-mode representative|hnsw] [--k 10] [--queries N]\n"
        "             [--repeat 1] [--trace-file PATH] [--rerank-fp32] [--rerank-pool N]\n");
}

int run_build(int argc, char** argv) {
    std::string data_directory, index_path;
    BuildOptions options;
    for (int i = 2; i < argc; ++i) {
        const std::string flag = argv[i];
        auto next = [&]() { return std::string(argv[++i]); };
        if (flag == "--data") data_directory = next();
        else if (flag == "--index") index_path = next();
        else if (flag == "--M") options.max_degree = std::atoi(next().c_str());
        else if (flag == "--efC") options.construction_width = std::atoi(next().c_str());
        else if (flag == "--K") options.representative_cap = std::atoi(next().c_str());
        else if (flag == "--bridge-receivers") options.bridge_receivers = std::atoi(next().c_str());
        else if (flag == "--merge-file") options.merge_structure_path = next();
        else if (flag == "--K-rule") {
            const std::string rule = next();
            if (rule == "sqrt") options.representative_rule = 1;
            else if (rule == "linear") options.representative_rule = 0;
            else if (rule == "cluster") options.representative_rule = 2;
            else { std::fprintf(stderr, "unknown K rule %s\n", rule.c_str()); return 2; }
        }
        else if (flag == "--no-merge") options.merge_attributes = false;
        else { std::fprintf(stderr, "unknown flag %s\n", flag.c_str()); return 2; }
    }
    if (data_directory.empty() || index_path.empty()) { print_usage(); return 2; }

    Dataset dataset;
    std::string error;
    if (!dataset.load(data_directory, 10, &error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 2; }
    const AsiIndex index = build_index(dataset, options);
    if (!save_index(index, index_path, &error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 2; }
    std::printf("{\"stage\":\"build\",\"data\":\"%s\",\"index\":\"%s\",\"objects\":%zu,"
                "\"attributes\":%d,\"nodes\":%zu,\"local_edges\":%zu,\"bridge_edges\":%zu,"
                "\"representatives\":%zu,\"upper_bytes\":%zu}\n",
                data_directory.c_str(), index_path.c_str(), dataset.num_objects(),
                index.attribute_count, index.node_object.size(),
                index.local_edge_targets.size(), index.bridge_edge_targets.size(),
                index.representative_nodes.size(), index.upper_layer_bytes);
    return 0;
}

int run_search(int argc, char** argv) {
    std::string data_directory, index_path;
    std::vector<int> ef_values{10};
    QueryOptions options;
    int ground_truth_k = 10;
    for (int i = 2; i < argc; ++i) {
        const std::string flag = argv[i];
        auto next = [&]() { return std::string(argv[++i]); };
        if (flag == "--data") data_directory = next();
        else if (flag == "--index") index_path = next();
        else if (flag == "--efs") ef_values = parse_int_list(next());
        else if (flag == "--entry-mode") options.entry_mode = next();
        else if (flag == "--k") { ground_truth_k = std::atoi(next().c_str()); }
        else if (flag == "--queries") options.max_queries = std::atoi(next().c_str());
        else if (flag == "--repeat") options.repeats = std::max(1, std::atoi(next().c_str()));
        else if (flag == "--trace-file") options.trace_path = next();
        else if (flag == "--rerank-fp32") options.rerank_fp32 = true;
        else if (flag == "--rerank-pool") options.rerank_pool = std::atoi(next().c_str());
        else { std::fprintf(stderr, "unknown flag %s\n", flag.c_str()); return 2; }
    }
    if (data_directory.empty() || index_path.empty()) { print_usage(); return 2; }

    Dataset dataset;
    std::string error;
    if (!dataset.load(data_directory, ground_truth_k, &error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 2; }
    AsiIndex index;
    if (!load_index(index, index_path, &error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 2; }

    for (int ef : ef_values) {
        QueryOptions per_point = options;
        per_point.ef = ef;
        const QueryStats stats = run_queries(dataset, index, per_point);
        std::printf("{\"stage\":\"search\",\"ef\":%d,\"entry_mode\":\"%s\",\"k\":%d,"
                    "\"queries\":%zu,\"recall\":%.6f,\"qps\":%.1f,\"dists_per_q\":%.1f,"
                    "\"distance_cache_hits_per_q\":%.1f,\"expanded_per_q\":%.1f,"
                    "\"adjacency_slots_per_q\":%.1f,\"reps_scored_per_q\":%.1f,"
                    "\"eligible_per_q\":%.1f,"
                    "\"attrs_in_scope\":%.2f,\"attrs_entered\":%.2f,\"rerank_calls_per_q\":%.1f}\n",
                    stats.ef, per_point.entry_mode.c_str(), ground_truth_k,
                    stats.ground_truth_entries ? size_t(dataset.num_queries()) : size_t(0),
                    stats.recall, stats.qps, stats.distances_per_query,
                    stats.distance_cache_hits_per_query, stats.expanded_per_query,
                    stats.adjacency_slots_per_query, stats.representative_scores_per_query,
                    stats.eligible_per_query,
                    stats.attributes_in_scope_per_query, stats.attributes_entered_per_query,
                    stats.rerank_calls_per_query);
        std::fflush(stdout);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { print_usage(); return 2; }
    const std::string command = argv[1];
    if (command == "build") return run_build(argc, argv);
    if (command == "search") return run_search(argc, argv);
    print_usage();
    return 2;
}
