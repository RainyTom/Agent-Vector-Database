#pragma once

#include "vector_join/vector_join.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace vjoin::simjoin {

enum class OrderMethod {
  kBaseMST,
  kWindowOrderSort,
  kWindowOrderEpsilon,
};

struct Graph {
  std::vector<std::vector<std::uint32_t>> edges;
  std::uint32_t entry{};
  std::string backend;
  std::string provenance;
};

struct Config {
  float epsilon{0.4F};                 // Euclidean L2, not squared L2.
  std::size_t window_size{100};        // Paper parameter w.
  std::size_t k{};                     // 0: epsilon join; >0: k join.
  OrderMethod order{OrderMethod::kWindowOrderSort};
  bool self_join{};
  std::size_t graph_m{20};             // Locally chosen unless evidenced otherwise.
  std::size_t ef_construction{200};    // Locally chosen unless evidenced otherwise.
  std::uint32_t seed{100};             // Paper does not report a seed.
  std::size_t order_search_window{32}; // Local choice for approximate maxT search.
  std::size_t order_repeats{1};
};

struct QueryResult {
  std::vector<std::uint32_t> matches;
  std::uint64_t distance_computations{};
};

struct Metrics {
  std::string method;
  std::string implementation_status;
  std::string implementation_language{"C++20"};
  std::string compiler;
  std::string paper_reference_environment{"C++11;g++-11.4.1;-O3"};
  std::string graph_backend;
  std::string graph_provenance;
  std::string distance_metric{"l2"};
  std::string empty_set_metric_policy{
      "empty_truth_recall_one;empty_actual_precision_one"};
  std::size_t query_rows{};
  std::size_t base_rows{};
  std::size_t dimensions{};
  float epsilon{};
  std::size_t window_size{};
  std::size_t k{};
  std::string order_method;
  std::uint32_t seed{};
  std::uint64_t estimated_peak_bytes{};
  double graph_build_ms{};
  double offline_order_ms{};
  double online_order_ms{};
  double join_ms{};
  double total_online_ms{};
  bool self_join{};
  std::size_t order_search_window{};
  std::size_t order_repeats{};
  std::uint64_t online_order_ndc{};
  std::uint64_t offline_order_ndc{};
  std::uint64_t process_peak_rss_bytes{};
  std::uint64_t evaluated_true_pairs{};
  std::uint64_t distance_computations{};
  std::uint64_t result_pairs{};
  std::size_t evaluated_ground_truth_queries{};
  double average_recall{std::numeric_limits<double>::quiet_NaN()};
  double average_precision{std::numeric_limits<double>::quiet_NaN()};
  double mean_results_per_query{};
  double paper_formula_nselectivity{std::numeric_limits<double>::quiet_NaN()};
  double true_mean_results_per_query{std::numeric_limits<double>::quiet_NaN()};
};

struct RunOutput {
  Metrics metrics;
  std::vector<QueryResult> queries;
};

Graph build_hnsw_graph(const Matrix& vectors, const Config& config);
Graph load_graph(const std::string& path, std::size_t expected_nodes,
                 std::uint32_t entry, std::string backend,
                 std::string provenance);

RunOutput run(const Matrix& queries, const Matrix& base, const Graph& query_graph,
              const Graph& base_graph, const Config& config,
              std::size_t ground_truth_limit = 0);

RunOutput run_hnsw(const Matrix& queries, const Matrix& base,
                   const Config& config,
                   std::size_t ground_truth_limit = 0);

[[nodiscard]] std::string metrics_json(const Metrics& metrics);

}  // namespace vjoin::simjoin
