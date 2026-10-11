// Compile the private search implementation into this test translation unit
// so sparse adversarial graphs can exercise it without HNSW masking the bug.
#include "../src/vector_join.cpp"
#include <cassert>

#ifdef NDEBUG
#error "Correctness tests must retain assertions"
#endif

int main() {
  vjoin::enforce_process_limit();
  vjoin::Matrix queries{1, 1, {0.0F}};
  vjoin::Matrix base{4, 1, {0.1F, 2.0F, 3.0F, 0.2F}};
  auto merged = vjoin::concatenate(queries, base);
  vjoin::Graph graph;
  graph.edges = {{1}, {2}, {3}, {4}, {}};
  vjoin::Config config;
  config.threshold = 1.0F;
  config.hybrid_beam = 1;
  vjoin::SearchContext context{queries, base, &merged, graph, config, true};
  auto state = vjoin::greedy_phase(0, {0}, context);
  vjoin::expansion_phase(0, true, context, state);
  // The queue never exceeds one bridge, but two bridges must be expanded.
  assert((state.in_range == std::vector<vjoin::Node>{1, 4}));
  assert(state.expansion_computations == 3);

  auto bfs_state = vjoin::greedy_phase(0, {0}, context);
  vjoin::expansion_phase(0, false, context, bfs_state);
  assert((bfs_state.in_range == std::vector<vjoin::Node>{1}));

  // OOD measurements are part of online distance work.
  std::uint64_t ndc = 0;
  static_cast<void>(vjoin::predict_ood(0, context, ndc));
  assert(ndc == 2);
  return 0;
}
