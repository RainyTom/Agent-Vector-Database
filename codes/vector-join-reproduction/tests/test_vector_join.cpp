#include "vector_join/vector_join.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

vjoin::Matrix matrix(std::initializer_list<std::initializer_list<float>> rows) {
  vjoin::Matrix result;
  result.rows = rows.size();
  result.dim = rows.begin()->size();
  for (const auto& row : rows) result.values.insert(result.values.end(), row.begin(), row.end());
  return result;
}

}  // namespace

int main() {
  const auto base = matrix({{0.0F, 0.0F}, {0.1F, 0.0F}, {5.0F, 5.0F},
                            {5.1F, 5.0F}, {10.0F, 10.0F}});
  const auto queries = matrix({{0.05F, 0.0F}, {5.05F, 5.0F}});

  for (const auto method : {vjoin::Method::kIndependent, vjoin::Method::kHardSharing,
                            vjoin::Method::kSoftSharing, vjoin::Method::kMergedIndex,
                            vjoin::Method::kMergedAdaptive}) {
    vjoin::Config config;
    config.method = method;
    config.threshold = 0.02F;
    config.graph_m = 4;
    config.ef_construction = 20;
    config.greedy_beam = 16;
    config.hybrid_beam = 16;
    config.convergence_threshold = 20;
    const auto output = vjoin::run(queries, base, config, queries.rows);
    assert(output.metrics.graph_backend == "hnswlib");
    assert(output.metrics.implementation_language == "C++20");
    assert(output.metrics.distance_metric == "squared_l2");
    assert(output.metrics.graph_m == config.graph_m);
    assert(output.metrics.ef_construction == config.ef_construction);
    assert(output.metrics.seed == config.seed);
    assert(output.metrics.precision == 1.0);
    assert(output.metrics.macro_recall >= 0.5);
    assert(output.queries.size() == queries.rows);
  }

  assert(vjoin::parse_method("hard") == vjoin::Method::kHardSharing);
  assert(std::string(vjoin::method_name(vjoin::Method::kMergedAdaptive)) == "mi-adapt");

  // A query far from a tight data cluster should activate the paper's
  // d1/d2 OOD rule and exercise the hybrid-expansion branch.
  const auto tight_base = matrix({{0.0F, 0.0F}, {0.01F, 0.0F}, {0.0F, 0.01F},
                                  {0.01F, 0.01F}, {0.02F, 0.0F}, {0.0F, 0.02F}});
  const auto far_query = matrix({{100.0F, 100.0F}});
  vjoin::Config ood_config;
  ood_config.method = vjoin::Method::kMergedAdaptive;
  ood_config.threshold = 25000.0F;
  ood_config.graph_m = 4;
  ood_config.ef_construction = 20;
  const auto ood_output = vjoin::run(far_query, tight_base, ood_config, 1);
  assert(ood_output.metrics.hybrid_queries == 1);
  assert(ood_output.metrics.precision == 1.0);

  std::cout << "vector_join_tests: OK\n";
}
