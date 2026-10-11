#include "vector_join/simjoin_paper.hpp"
#include "vector_join/runtime.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>

#ifdef NDEBUG
#error "Correctness tests must retain assertions in Release builds"
#endif

namespace {

vjoin::Matrix matrix(std::initializer_list<std::initializer_list<float>> rows) {
  vjoin::Matrix result;
  result.rows = rows.size();
  result.dim = rows.begin()->size();
  for (const auto& row : rows) result.values.insert(result.values.end(), row.begin(), row.end());
  return result;
}

vjoin::simjoin::Graph complete_graph(std::size_t size) {
  vjoin::simjoin::Graph graph;
  graph.edges.resize(size);
  graph.backend = "test-complete-graph";
  graph.provenance = "unit-test";
  for (std::uint32_t left = 0; left < size; ++left) {
    for (std::uint32_t right = 0; right < size; ++right) {
      if (left != right) graph.edges[left].push_back(right);
    }
  }
  return graph;
}

}  // namespace

int main() {
  const auto base = matrix({{0.0F, 0.0F}, {0.1F, 0.0F}, {5.0F, 5.0F},
                            {5.1F, 5.0F}});
  const auto queries = matrix({{0.05F, 0.0F}, {5.05F, 5.0F}});
  const auto base_graph = complete_graph(base.rows);
  const auto query_graph = complete_graph(queries.rows);

  for (const auto order : {vjoin::simjoin::OrderMethod::kBaseMST,
                           vjoin::simjoin::OrderMethod::kWindowOrderSort,
                           vjoin::simjoin::OrderMethod::kWindowOrderEpsilon}) {
    vjoin::simjoin::Config config;
    config.epsilon = 0.15F;
    config.window_size = 4;
    config.order = order;
    const auto output = vjoin::simjoin::run(
        queries, base, query_graph, base_graph, config, queries.rows);
    assert(output.metrics.average_recall == 1.0);
    assert(output.metrics.average_precision == 1.0);
    assert(output.metrics.distance_metric == "l2");
    assert(output.metrics.result_pairs == 4);
    assert(output.metrics.distance_computations > 0);
  }

  vjoin::simjoin::Config k_config;
  k_config.k = 2;
  k_config.window_size = 4;
  const auto k_output = vjoin::simjoin::run(
      queries, base, query_graph, base_graph, k_config, queries.rows);
  assert(k_output.metrics.average_recall == 1.0);
  assert(k_output.metrics.average_precision == 1.0);
  assert(k_output.metrics.method == "kJoinSlide-paper-derived");

  vjoin::Matrix oversized;
  oversized.rows = vjoin::memory_limit_bytes / 512 + 1;
  oversized.dim = 2;
  bool rejected = false;
  try {
    static_cast<void>(vjoin::simjoin::run_hnsw(
        oversized, oversized, vjoin::simjoin::Config{}, 0));
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  assert(rejected);

  // A bounded window can prune the only productive path on a sparse graph.
  const auto sparse_base = matrix({{5.0F}, {3.0F}, {4.0F}, {0.0F}});
  const auto single_query = matrix({{0.0F}});
  auto sparse = complete_graph(4);
  sparse.edges = {{1, 2}, {}, {3}, {}};
  vjoin::simjoin::Config sparse_config;
  sparse_config.epsilon = 0.2F;
  sparse_config.window_size = 1;
  auto miss = vjoin::simjoin::run(single_query, sparse_base, complete_graph(1), sparse,
                                 sparse_config, 1);
  assert(miss.queries[0].matches.empty());
  assert(miss.metrics.average_recall == 0.0);
  assert(miss.metrics.paper_formula_nselectivity == 0.25);
  sparse_config.window_size = 2;
  auto hit = vjoin::simjoin::run(single_query, sparse_base, complete_graph(1), sparse,
                                sparse_config, 1);
  assert(hit.queries[0].matches == std::vector<std::uint32_t>{3});
  assert(hit.metrics.average_recall == 1.0);
  assert(hit.metrics.total_online_ms >= hit.metrics.join_ms);

  sparse_config.k = 1;
  sparse_config.window_size = 1;
  auto k_pruned = vjoin::simjoin::run(single_query, sparse_base, complete_graph(1), sparse,
                                     sparse_config, 1);
  assert(k_pruned.queries[0].matches == std::vector<std::uint32_t>{1});
  sparse_config.window_size = 2;
  auto k_recovered = vjoin::simjoin::run(single_query, sparse_base, complete_graph(1), sparse,
                                        sparse_config, 1);
  assert(k_recovered.queries[0].matches == std::vector<std::uint32_t>{3});
  sparse_config.k = 0;

  auto unevaluated = vjoin::simjoin::run(single_query, sparse_base, complete_graph(1), sparse,
                                        sparse_config, 0);
  assert(std::isnan(unevaluated.metrics.average_recall));
  assert(std::isnan(unevaluated.metrics.paper_formula_nselectivity));

  // Approximate order search must not scan every X vector. maxT=0 on this
  // connected identical-vector chain; width 1 top-1 fallback explores two nodes.
  vjoin::Matrix repeated;
  repeated.rows = 128;
  repeated.dim = 1;
  repeated.values.assign(128, 10.0F);
  vjoin::simjoin::Graph chain;
  chain.edges.resize(128);
  for (std::uint32_t i = 1; i < 128; ++i) {
    chain.edges[i-1].push_back(i);
    chain.edges[i].push_back(i-1);
  }
  sparse_config.order = vjoin::simjoin::OrderMethod::kWindowOrderEpsilon;
  sparse_config.order_search_window = 1;
  sparse_config.order_repeats = 3;
  auto ordered = vjoin::simjoin::run(repeated, single_query, chain, complete_graph(1),
                                    sparse_config, 0);
  assert(ordered.metrics.online_order_ndc < repeated.rows);
  assert(ordered.metrics.order_repeats == 3);

  // Self-join includes the diagonal; disconnected in-range regions reduce
  // returned cardinality but must never change true selectivity.
  const auto self_data = matrix({{0.0F}, {10.0F}, {0.1F}});
  auto self_graph = complete_graph(3);
  self_graph.edges = {{1}, {2}, {0}};
  sparse_config.self_join = true;
  auto self = vjoin::simjoin::run(self_data, self_data, self_graph, self_graph, sparse_config, 3);
  assert(self.metrics.evaluated_true_pairs == 5);
  assert(self.metrics.result_pairs == 4);
  assert(std::abs(self.metrics.paper_formula_nselectivity - 5.0 / 3.0) < 1e-12);
  assert(self.metrics.online_order_ms == 0.0);

  std::cout << "test_simjoin_paper: OK\n";
}
