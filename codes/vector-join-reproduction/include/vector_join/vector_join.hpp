#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vjoin {

struct Matrix {
  std::size_t rows{};
  std::size_t dim{};
  std::vector<float> values;

  [[nodiscard]] const float* row(std::size_t i) const;
  [[nodiscard]] float* row(std::size_t i);

  static Matrix load_raw(const std::string& path, std::size_t dim,
                         std::size_t max_rows = 0);
};

enum class Method {
  kIndependent,
  kHardSharing,
  kSoftSharing,
  kMergedIndex,
  kMergedAdaptive,
};

[[nodiscard]] Method parse_method(const std::string& value);
[[nodiscard]] const char* method_name(Method method);

struct Config {
  Method method{Method::kIndependent};
  float threshold{300.0F};  // squared L2
  std::size_t graph_m{16};
  std::size_t ef_construction{200};
  std::size_t greedy_beam{256};
  std::size_t hybrid_beam{256};
  std::size_t convergence_threshold{10};
  float ood_ratio_threshold{1.5F};
  std::uint32_t seed{100};
};

struct QueryResult {
  std::vector<std::uint32_t> matches;
  std::uint64_t distance_computations{};
  std::uint64_t greedy_distance_computations{};
  std::uint64_t expansion_distance_computations{};
  bool hybrid_used{};
};

struct Metrics {
  std::string method;
  std::size_t base_rows{};
  std::size_t query_rows{};
  std::size_t dimensions{};
  float threshold{};
  double build_ms{};
  double order_ms{};
  double join_ms{};
  std::uint64_t result_pairs{};
  std::uint64_t distance_computations{};
  std::uint64_t greedy_distance_computations{};
  std::uint64_t expansion_distance_computations{};
  std::uint64_t hybrid_queries{};
  std::size_t evaluated_ground_truth_queries{};
  double macro_recall{};
  double pair_recall{};
  double precision{};
};

struct RunOutput {
  Metrics metrics;
  std::vector<QueryResult> queries;
};

RunOutput run(const Matrix& queries, const Matrix& base, const Config& config,
              std::size_t ground_truth_limit = 0);

[[nodiscard]] std::string metrics_json(const Metrics& metrics);

}  // namespace vjoin
