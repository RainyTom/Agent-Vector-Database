// NodeANN CLI: `build` writes one HNSW graph per attribute, `search` queries
// them and merges. Build and search are separate runs over a saved index.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "dataset.h"
#include "nodeann_index.h"
#include "nodeann_search.h"

namespace {

constexpr int default_ground_truth_depth = 10;  // layout file is gt_<k>.txt

void print_usage() {
    std::fprintf(stderr,
                 "usage:\n"
                 "  nodeann build  --data DIR --index-dir DIR [--M 16] [--ef-construction 200]\n"
                 "  nodeann search --data DIR --index-dir DIR [--per-n N[,N...]] [--ef E] [--k K] [--repeat R]"
                 " [--queries Q]\n");
}

std::vector<int> parse_per_n(const std::string& value) {
    std::vector<int> values;
    size_t start = 0;
    while (start <= value.size()) {
        const size_t comma = value.find(',', start);
        const std::string piece = value.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!piece.empty()) values.push_back(std::atoi(piece.c_str()));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return values;
}

int run_build(const std::string& data_directory, const std::string& index_directory, int M,
              int ef_construction) {
    Dataset dataset;
    std::string error;
    if (!dataset.load(data_directory, default_ground_truth_depth, &error)) {
        std::fprintf(stderr, "load failed: %s\n", error.c_str());
        return 2;
    }
    NodeANNIndex index(dataset.dim, M, ef_construction);
    const double build_ms = index.build(dataset, &error);
    if (build_ms < 0.0) {
        std::fprintf(stderr, "build failed: %s\n", error.c_str());
        return 2;
    }
    if (!index.save(index_directory, &error)) {
        std::fprintf(stderr, "save failed: %s\n", error.c_str());
        return 2;
    }
    std::printf("{\"build_ms\": %.1f, \"attributes\": %zu, \"nodes\": %zu, \"M\": %d,"
                " \"ef_construction\": %d, \"index_dir\": \"%s\"}\n",
                build_ms, index.attribute_count(), index.total_nodes(), M, ef_construction,
                index_directory.c_str());
    return 0;
}

int run_search(const std::string& data_directory, const std::string& index_directory,
               const std::vector<int>& per_n_values, const std::vector<int>& ef_values,
               int repeat, int query_limit, int ground_truth_depth) {
    Dataset dataset;
    std::string error;
    if (!dataset.load(data_directory, ground_truth_depth, &error)) {
        std::fprintf(stderr, "load failed: %s\n", error.c_str());
        return 2;
    }
    NodeANNIndex index(dataset.dim, NodeANNIndex::default_M, NodeANNIndex::default_ef_construction);
    if (!index.load(index_directory, dataset, &error)) {
        std::fprintf(stderr, "load failed: %s\n", error.c_str());
        return 2;
    }
    for (int per_n : per_n_values) {
        if (per_n <= 0) {
            std::fprintf(stderr, "per-n must be positive\n");
            return 2;
        }
        for (int ef : ef_values) {
        const SearchStats stats = run_search(dataset, index, per_n, ef, repeat, query_limit < 0
                                                                                  ? int(dataset.num_queries())
                                                                                  : query_limit);
        std::printf("{\"per_n\": %d, \"ef\": %d, \"recall\": %.6f, \"qps\": %.1f,"
                    " \"dists_per_q\": %.1f, \"graph_searches_per_q\": %.1f,"
                    " \"eligible_per_q\": %.1f}\n",
                    stats.per_n, stats.ef, stats.recall, stats.qps, stats.dists_per_query,
                    stats.graph_searches_per_query, stats.eligible_per_query);
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 2;
    }
    const std::string command = argv[1];
    std::string data_directory;
    std::string index_directory;
    std::vector<int> per_n_values;
    int M = NodeANNIndex::default_M;
    int ef_construction = NodeANNIndex::default_ef_construction;
    std::vector<int> ef_values;
    int ground_truth_depth = default_ground_truth_depth;
    int repeat = 1;
    int query_limit = -1;

    for (int i = 2; i < argc; ++i) {
        const std::string flag = argv[i];
        auto next_value = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", flag.c_str());
                std::exit(2);
            }
            return std::string(argv[++i]);
        };
        if (flag == "--data") data_directory = next_value();
        else if (flag == "--index-dir") index_directory = next_value();
        else if (flag == "--M") M = std::atoi(next_value().c_str());
        else if (flag == "--ef-construction") ef_construction = std::atoi(next_value().c_str());
        else if (flag == "--k") { ground_truth_depth = std::atoi(next_value().c_str()); }
        else if (flag == "--ef") {
            const std::string list = next_value();
            std::size_t start = 0;
            while (start <= list.size()) {
                const std::size_t comma = list.find(',', start);
                const std::string item = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                if (!item.empty()) ef_values.push_back(std::atoi(item.c_str()));
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        }
        else if (flag == "--repeat") repeat = std::atoi(next_value().c_str());
        else if (flag == "--queries") query_limit = std::atoi(next_value().c_str());
        else if (flag == "--per-n") {
            const std::vector<int> parsed = parse_per_n(next_value());
            per_n_values.insert(per_n_values.end(), parsed.begin(), parsed.end());
        } else {
            std::fprintf(stderr, "unknown flag: %s\n", flag.c_str());
            print_usage();
            return 2;
        }
    }
    if (data_directory.empty() || index_directory.empty()) {
        print_usage();
        return 2;
    }
    if (command == "build") return run_build(data_directory, index_directory, M, ef_construction);
    if (per_n_values.empty()) per_n_values.push_back(ground_truth_depth);
    if (ef_values.empty()) ef_values.push_back(ground_truth_depth);
    if (command == "search")
        return run_search(data_directory, index_directory, per_n_values, ef_values, repeat,
                          query_limit, ground_truth_depth);
    std::fprintf(stderr, "unknown command: %s\n", command.c_str());
    print_usage();
    return 2;
}