#include "vector_join/simjoin_paper.hpp"
#include "vector_join/runtime.hpp"
#include "vector_join/build_info.hpp"
#include "detail/simjoin_search.hpp"

#include <hnswlib/hnswlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace vjoin::simjoin {
namespace {

using Clock = std::chrono::steady_clock;
using Node = std::uint32_t;
constexpr Node kRoot = std::numeric_limits<Node>::max();
constexpr std::uint64_t kServerLimit = memory_limit_bytes;

double elapsed_ms(Clock::time_point begin, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - begin).count();
}

using detail::squared_l2;
using detail::join_slide;
using detail::k_join_slide;

float l2(const float* left, const float* right, std::size_t dim,
         std::uint64_t* ndc = nullptr) {
  return std::sqrt(squared_l2(left, right, dim, ndc));
}

std::uint64_t checked_mul(std::uint64_t left, std::uint64_t right) {
  if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return left * right;
}

std::uint64_t estimate_peak(const Matrix& queries, const Matrix& base,
                            const Config& config) {
  const std::uint64_t vector_bytes =
      checked_mul(queries.values.size() + base.values.size(), sizeof(float));
  const std::uint64_t graph_bytes = checked_mul(
      queries.rows + base.rows,
      512ULL + static_cast<std::uint64_t>(config.graph_m) * 4ULL * sizeof(Node));
  // epsilon output is data-dependent, not a dense Cartesian product allocation.
  // Do not reject sparse large joins solely because |X|*|Y| is large. The OS
  // limit bounds real allocations, including result growth and Python copies.
  const std::uint64_t result_bytes = saturated_add(
      checked_mul(queries.rows, sizeof(QueryResult)),
      config.k == 0 ? 0 : checked_mul(checked_mul(queries.rows, std::min(config.k, base.rows)), 2 * sizeof(Node)));
  const std::uint64_t work_bytes = checked_mul(
      queries.rows + base.rows,
      8ULL * sizeof(Node) + 4ULL * sizeof(float));
  const std::uint64_t subtotal = saturated_add(saturated_add(vector_bytes, graph_bytes),
                                               saturated_add(result_bytes, work_bytes));
  return subtotal > std::numeric_limits<std::uint64_t>::max() / 5ULL
             ? std::numeric_limits<std::uint64_t>::max()
             : subtotal * 5ULL / 4ULL;
}

void enforce_server_limit(const Matrix& queries, const Matrix& base,
                          const Config& config) {
  enforce_process_limit();
  if (estimate_peak(queries, base, config) >= kServerLimit) {
    throw std::runtime_error("run rejected: estimated allocations reach configured process limit");
  }
}

struct Edge {
  Node left;
  Node right;
  float weight;
};

class DisjointSet {
 public:
  explicit DisjointSet(std::size_t size) : parent_(size), rank_(size) {
    std::iota(parent_.begin(), parent_.end(), 0);
  }
  Node find(Node node) {
    if (parent_[node] != node) parent_[node] = find(parent_[node]);
    return parent_[node];
  }
  bool unite(Node left, Node right) {
    left = find(left);
    right = find(right);
    if (left == right) return false;
    if (rank_[left] < rank_[right]) std::swap(left, right);
    parent_[right] = left;
    if (rank_[left] == rank_[right]) ++rank_[left];
    return true;
  }

 private:
  std::vector<Node> parent_;
  std::vector<std::uint8_t> rank_;
};

std::vector<Edge> undirected_edges(const Matrix& vectors, const Graph& graph,
                                  std::uint64_t* ndc = nullptr) {
  std::vector<Edge> result;
  std::unordered_set<std::uint64_t> seen;
  for (Node left = 0; left < graph.edges.size(); ++left) {
    for (Node right : graph.edges[left]) {
      if (right >= graph.edges.size() || left == right) continue;
      const Node a = std::min(left, right);
      const Node b = std::max(left, right);
      const auto key = (static_cast<std::uint64_t>(a) << 32U) | b;
      if (!seen.insert(key).second) continue;
      result.push_back({a, b, l2(vectors.row(a), vectors.row(b), vectors.dim, ndc)});
    }
  }
  return result;
}

std::vector<Edge> kruskal(std::size_t nodes, std::vector<Edge> edges) {
  std::sort(edges.begin(), edges.end(), [](const Edge& left, const Edge& right) {
    return std::tie(left.weight, left.left, left.right) <
           std::tie(right.weight, right.left, right.right);
  });
  DisjointSet sets(nodes);
  std::vector<Edge> tree;
  tree.reserve(nodes == 0 ? 0 : nodes - 1);
  for (const Edge& edge : edges) {
    if (!sets.unite(edge.left, edge.right)) continue;
    tree.push_back(edge);
    if (tree.size() + 1 == nodes) break;
  }
  if (nodes != 0 && tree.size() + 1 != nodes) {
    throw std::runtime_error("order graph is disconnected; paper requires a connected graph");
  }
  return tree;
}

// Algorithm 4: component-wise minimum outgoing edges over sorted neighbor lists.
std::vector<Edge> paper_mst(std::size_t nodes, std::vector<Edge> edges) {
  std::vector<std::vector<std::pair<float, Node>>> adjacency(nodes);
  for (const Edge& edge : edges) {
    adjacency[edge.left].emplace_back(edge.weight, edge.right);
    adjacency[edge.right].emplace_back(edge.weight, edge.left);
  }
  for (auto& neighbors : adjacency) std::sort(neighbors.begin(), neighbors.end());

  DisjointSet sets(nodes);
  std::vector<std::size_t> cursor(nodes, 0);
  std::vector<Edge> tree;
  tree.reserve(nodes == 0 ? 0 : nodes - 1);
  while (tree.size() + 1 < nodes) {
    std::vector<Edge> minimum(nodes, {kRoot, kRoot, 0.0F});
    for (Node node = 0; node < nodes; ++node) {
      auto& position = cursor[node];
      while (position < adjacency[node].size() &&
             sets.find(node) == sets.find(adjacency[node][position].second)) {
        ++position;
      }
      if (position == adjacency[node].size()) continue;
      const auto [weight, neighbor] = adjacency[node][position];
      const Node component = sets.find(node);
      if (minimum[component].left == kRoot || weight < minimum[component].weight) {
        minimum[component] = {node, neighbor, weight};
      }
    }
    bool progressed = false;
    for (const Edge& edge : minimum) {
      if (edge.left == kRoot || !sets.unite(edge.left, edge.right)) continue;
      tree.push_back(edge);
      progressed = true;
    }
    if (!progressed) {
      throw std::runtime_error("order graph is disconnected; Algorithm 4 cannot form an MST");
    }
  }
  std::sort(tree.begin(), tree.end(), [](const Edge& left, const Edge& right) {
    return std::tie(left.weight, left.left, left.right) <
           std::tie(right.weight, right.left, right.right);
  });
  return tree;
}

// Algorithm 5: merge the already sorted graph-MST and root-edge streams.
std::vector<Edge> window_order_tree(std::size_t query_nodes,
                                    const std::vector<Edge>& graph_tree,
                                    std::vector<Edge> root_edges) {
  std::sort(root_edges.begin(), root_edges.end(),
            [](const Edge& left, const Edge& right) {
              return std::tie(left.weight, left.left) <
                     std::tie(right.weight, right.left);
            });
  DisjointSet sets(query_nodes + 1);
  std::vector<Edge> result;
  result.reserve(query_nodes);
  std::size_t graph_position = 0;
  std::size_t root_position = 0;
  while (result.size() < query_nodes &&
         (graph_position < graph_tree.size() || root_position < root_edges.size())) {
    const bool take_root = root_position < root_edges.size() &&
                           (graph_position == graph_tree.size() ||
                            root_edges[root_position].weight <=
                                graph_tree[graph_position].weight);
    const Edge& edge = take_root ? root_edges[root_position++]
                                 : graph_tree[graph_position++];
    if (sets.unite(edge.left, edge.right)) result.push_back(edge);
  }
  if (result.size() != query_nodes) {
    throw std::runtime_error("Algorithm 5 could not connect the root to the query graph");
  }
  return result;
}

struct OrderResult {
  std::vector<Node> order;
  std::vector<Node> parent;
  double offline_ms{};
  double online_ms{};
  std::uint64_t offline_ndc{};
  std::uint64_t online_ndc{};
};

OrderResult make_order(const Matrix& queries, const Matrix& base,
                       const Graph& query_graph, const Graph& base_graph,
                       OrderMethod method, std::size_t search_window,
                       std::size_t repeats) {
  OrderResult result;
  std::vector<Edge> query_edges;
  std::vector<Edge> offline_tree;
  double offline_ms = 0.0;
  if (method != OrderMethod::kBaseMST) {
    const auto offline_begin = Clock::now();
    query_edges = undirected_edges(queries, query_graph, &result.offline_ndc);
    offline_tree = paper_mst(queries.rows, query_edges);
    offline_ms = elapsed_ms(offline_begin, Clock::now());
  }

  double online_total = 0.0;
  for (std::size_t repetition = 0; repetition < repeats; ++repetition) {
  const auto online_begin = Clock::now();
  result.order.clear();
  if (method == OrderMethod::kBaseMST) {
    query_edges = undirected_edges(queries, query_graph, &result.online_ndc);
  }
  std::vector<Edge> candidates = method == OrderMethod::kBaseMST
                                     ? query_edges
                                     : offline_tree;
  float max_tree_edge = 0.0F;
  for (const Edge& edge : candidates) max_tree_edge = std::max(max_tree_edge, edge.weight);

  std::vector<Edge> root_edges;
  std::vector<Node> root_candidates;
  if (method == OrderMethod::kWindowOrderEpsilon) {
    // Local explicit policy: Algorithm 3 at radius maxT on X, then Algorithm 6
    // top-1 fallback if no point is found. The paper does not state search w.
    root_candidates = join_slide(queries, query_graph, base.row(base_graph.entry),
                                  {query_graph.entry}, max_tree_edge, search_window,
                                  result.online_ndc);
    if (root_candidates.empty()) {
      root_candidates = k_join_slide(queries, query_graph, base.row(base_graph.entry),
                                      {query_graph.entry}, 1, search_window,
                                      result.online_ndc);
    }
  } else {
    root_candidates.resize(queries.rows);
    std::iota(root_candidates.begin(), root_candidates.end(), 0);
  }
  root_edges.reserve(root_candidates.size());
  for (Node query : root_candidates) {
    const float distance = l2(queries.row(query), base.row(base_graph.entry), queries.dim,
                              &result.online_ndc);
    root_edges.push_back({query, static_cast<Node>(queries.rows), distance});
  }
  std::vector<Edge> tree;
  if (method == OrderMethod::kBaseMST) {
    candidates.insert(candidates.end(), root_edges.begin(), root_edges.end());
    tree = kruskal(queries.rows + 1, std::move(candidates));
  } else {
    tree = window_order_tree(queries.rows, candidates, std::move(root_edges));
  }
  std::vector<std::vector<Node>> adjacency(queries.rows + 1);
  for (const Edge& edge : tree) {
    adjacency[edge.left].push_back(edge.right);
    adjacency[edge.right].push_back(edge.left);
  }

  result.parent.assign(queries.rows, kRoot);
  const Node root = static_cast<Node>(queries.rows);
  std::vector<bool> visited(queries.rows + 1, false);
  std::vector<Node> stack{root};
  visited[root] = true;
  while (!stack.empty()) {
    const Node current = stack.back();
    stack.pop_back();
    if (current != root) result.order.push_back(current);
    for (auto it = adjacency[current].rbegin(); it != adjacency[current].rend(); ++it) {
      if (visited[*it]) continue;
      visited[*it] = true;
      if (*it != root) result.parent[*it] = current == root ? kRoot : current;
      stack.push_back(*it);
    }
  }
  const auto online_end = Clock::now();
  online_total += elapsed_ms(online_begin, online_end);
  }
  result.offline_ms = offline_ms;
  result.online_ms = online_total / repeats;
  result.online_ndc /= repeats;
  return result;
}


std::vector<Node> exact_epsilon(const Matrix& base, const float* query,
                                float epsilon) {
  std::vector<Node> result;
  const float threshold = epsilon * epsilon;
  for (Node node = 0; node < base.rows; ++node) {
    if (squared_l2(base.row(node), query, base.dim) <= threshold) result.push_back(node);
  }
  return result;
}

std::vector<Node> exact_k(const Matrix& base, const float* query, std::size_t k) {
  std::vector<std::pair<float, Node>> distances;
  distances.reserve(base.rows);
  for (Node node = 0; node < base.rows; ++node) {
    distances.emplace_back(squared_l2(base.row(node), query, base.dim), node);
  }
  const std::size_t count = std::min(k, distances.size());
  std::partial_sort(distances.begin(), distances.begin() + count, distances.end());
  std::vector<Node> result;
  result.reserve(count);
  for (std::size_t i = 0; i < count; ++i) result.push_back(distances[i].second);
  std::sort(result.begin(), result.end());
  return result;
}

const char* order_name(OrderMethod method) {
  switch (method) {
    case OrderMethod::kBaseMST: return "base-mst";
    case OrderMethod::kWindowOrderSort: return "mst-window-order-sort";
    case OrderMethod::kWindowOrderEpsilon: return "window-order-epsilon-joinslide";
  }
  return "unknown";
}

}  // namespace

Graph build_hnsw_graph(const Matrix& vectors, const Config& config) {
  enforce_process_limit();
  validate_matrix(vectors);
  if (config.graph_m < 2 || config.ef_construction == 0) {
    throw std::invalid_argument("hnswlib requires M >= 2 and ef_construction > 0");
  }
  if (vectors.rows == 0 || vectors.dim == 0) throw std::invalid_argument("empty vectors");
  hnswlib::L2Space space(static_cast<int>(vectors.dim));
  hnswlib::HierarchicalNSW<float> index(&space, vectors.rows, config.graph_m,
                                        config.ef_construction, config.seed);
  for (std::size_t i = 0; i < vectors.rows; ++i) index.addPoint(vectors.row(i), i);
  Graph graph;
  graph.edges.resize(vectors.rows);
  graph.entry = static_cast<Node>(index.getExternalLabel(index.enterpoint_node_));
  graph.backend = "hnswlib";
  std::ostringstream provenance;
  provenance << "locally-built;M=" << config.graph_m
             << ";ef_construction=" << config.ef_construction
             << ";seed=" << config.seed << ";paper-default-is-NSG";
  graph.provenance = provenance.str();
  for (std::size_t internal = 0; internal < vectors.rows; ++internal) {
    const Node source = static_cast<Node>(index.getExternalLabel(internal));
    auto* links = index.get_linklist0(static_cast<hnswlib::tableint>(internal));
    const int count = index.getListCount(links);
    const auto* neighbors = reinterpret_cast<const hnswlib::tableint*>(links + 1);
    for (int j = 0; j < count; ++j) {
      graph.edges[source].push_back(
          static_cast<Node>(index.getExternalLabel(neighbors[j])));
    }
  }
  return graph;
}

Graph load_graph(const std::string& path, std::size_t expected_nodes,
                 std::uint32_t entry, std::string backend,
                 std::string provenance) {
  enforce_process_limit();
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot open graph file: " + path);
  Graph graph;
  graph.edges.resize(expected_nodes);
  if (entry >= expected_nodes) throw std::invalid_argument("graph entry outside graph");
  graph.entry = entry;
  graph.backend = std::move(backend);
  graph.provenance = std::move(provenance);
  std::string line;
  std::vector<bool> present(expected_nodes, false);
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream row(line);
    Node source;
    if (!(row >> source) || source >= expected_nodes) {
      throw std::runtime_error("invalid graph source in: " + path);
    }
    if (present[source]) throw std::runtime_error("duplicate graph source");
    present[source] = true;
    Node neighbor;
    while (row >> neighbor) {
      if (neighbor >= expected_nodes) throw std::runtime_error("invalid graph neighbor");
      if (neighbor != source) graph.edges[source].push_back(neighbor);
    }
    if (!row.eof()) throw std::runtime_error("malformed graph neighbor token");
  }
  if (std::find(present.begin(), present.end(), false) != present.end()) {
    throw std::runtime_error("graph must contain one row for every vector");
  }
  return graph;
}

RunOutput run(const Matrix& queries, const Matrix& base, const Graph& query_graph,
              const Graph& base_graph, const Config& config,
              std::size_t ground_truth_limit) {
  if (queries.dim != base.dim) throw std::invalid_argument("dimension mismatch");
  if (queries.rows == 0 || base.rows == 0) throw std::invalid_argument("empty input");
  if (query_graph.edges.size() != queries.rows || base_graph.edges.size() != base.rows) {
    throw std::invalid_argument("graph/vector cardinality mismatch");
  }
  if ((!std::isfinite(config.epsilon) || !(config.epsilon > 0.0F)) && config.k == 0) {
    throw std::invalid_argument("epsilon must be positive");
  }
  const std::uint64_t estimated_peak = estimate_peak(queries, base, config);
  enforce_server_limit(queries, base, config);
  validate_matrix(queries);
  validate_matrix(base);
  for (const Graph* graph : {&query_graph, &base_graph}) {
    for (const auto& neighbors : graph->edges) {
      for (Node neighbor : neighbors) {
        if (neighbor >= graph->edges.size()) throw std::invalid_argument("invalid graph neighbor");
      }
    }
  }
  if (config.window_size == 0 || config.order_search_window == 0 || config.order_repeats == 0 || config.k > base.rows ||
      (config.k != 0 && config.window_size < config.k)) {
    throw std::invalid_argument("require positive windows, k <= |Y| and join w >= k");
  }
  if (base_graph.entry >= base.rows || query_graph.entry >= queries.rows) {
    throw std::invalid_argument("entry outside graph");
  }
  if (config.self_join && (queries.rows != base.rows || queries.values != base.values)) {
    throw std::invalid_argument("self join requires identical vectors in identical order");
  }

  OrderResult ordering;
  if (!config.self_join) {
    ordering = make_order(queries, base, query_graph, base_graph, config.order,
                           config.order_search_window, config.order_repeats);
  } else {
    ordering.order.resize(queries.rows);
    std::iota(ordering.order.begin(), ordering.order.end(), 0);
    ordering.parent.assign(queries.rows, kRoot);
  }

  RunOutput output;
  output.queries.resize(queries.rows);
  const auto join_begin = Clock::now();
  for (Node query : ordering.order) {
    std::vector<Node> seeds;
    if (config.self_join) {
      if (query >= base.rows) throw std::invalid_argument("self join requires identical cardinality");
      seeds = {query};
    } else if (ordering.parent[query] == kRoot) {
      seeds = {base_graph.entry};
    } else {
      seeds = output.queries[ordering.parent[query]].matches;
      if (seeds.empty()) seeds = {base_graph.entry};
    }
    auto& result = output.queries[query];
    result.matches = config.k == 0
                         ? join_slide(base, base_graph, queries.row(query), seeds,
                                      config.epsilon, config.window_size,
                                      result.distance_computations)
                         : k_join_slide(base, base_graph, queries.row(query), seeds,
                                        config.k, config.window_size,
                                        result.distance_computations);
  }
  const auto join_end = Clock::now();

  Metrics& metrics = output.metrics;
  metrics.method = config.k == 0 ? "SimJoin-paper-derived" : "kJoinSlide-paper-derived";
  metrics.implementation_status = base_graph.backend == "hnswlib"
                                      ? "paper-derived-graph-backend-deviation"
                                      : "paper-derived-external-graph-unverified";
#if defined(__VERSION__)
  metrics.compiler = __VERSION__;
#else
  metrics.compiler = "unknown";
#endif
  metrics.graph_backend = base_graph.backend;
  metrics.graph_provenance = base_graph.provenance;
  metrics.query_rows = queries.rows;
  metrics.base_rows = base.rows;
  metrics.dimensions = base.dim;
  metrics.epsilon = config.epsilon;
  metrics.window_size = config.window_size;
  metrics.k = config.k;
  metrics.order_method = order_name(config.order);
  metrics.seed = config.seed;
  metrics.estimated_peak_bytes = estimated_peak;
  metrics.offline_order_ms = ordering.offline_ms;
  metrics.online_order_ms = ordering.online_ms;
  metrics.join_ms = elapsed_ms(join_begin, join_end);
  metrics.total_online_ms = metrics.join_ms + metrics.online_order_ms;
  metrics.online_order_ndc = ordering.online_ndc;
  metrics.offline_order_ndc = ordering.offline_ndc;
  metrics.self_join = config.self_join;
  metrics.order_search_window = config.order_search_window;
  metrics.order_repeats = config.self_join ? 0 : config.order_repeats;
  for (const auto& result : output.queries) {
    metrics.distance_computations += result.distance_computations;
    metrics.result_pairs += result.matches.size();
  }
  metrics.mean_results_per_query =
      static_cast<double>(metrics.result_pairs) / static_cast<double>(queries.rows);

  const std::size_t eval_count = std::min(ground_truth_limit, queries.rows);
  metrics.evaluated_ground_truth_queries = eval_count;
  if (eval_count != 0) {
    metrics.average_recall = 0.0;
    metrics.average_precision = 0.0;
  }
  for (Node query = 0; query < eval_count; ++query) {
    const auto truth = config.k == 0 ? exact_epsilon(base, queries.row(query), config.epsilon)
                                     : exact_k(base, queries.row(query), config.k);
    const auto& actual = output.queries[query].matches;
    metrics.evaluated_true_pairs += truth.size();
    std::vector<Node> intersection;
    std::set_intersection(truth.begin(), truth.end(), actual.begin(), actual.end(),
                          std::back_inserter(intersection));
    metrics.average_recall += truth.empty()
                                  ? (actual.empty() ? 1.0 : 0.0)
                                  : static_cast<double>(intersection.size()) / truth.size();
    metrics.average_precision += actual.empty()
                                     ? 1.0
                                     : static_cast<double>(intersection.size()) / actual.size();
  }
  if (eval_count != 0) {
    metrics.average_recall /= static_cast<double>(eval_count);
    metrics.average_precision /= static_cast<double>(eval_count);
  }
  if (eval_count == queries.rows) {
    metrics.paper_formula_nselectivity =
        static_cast<double>(metrics.evaluated_true_pairs) / base.rows;
    metrics.true_mean_results_per_query =
        static_cast<double>(metrics.evaluated_true_pairs) / queries.rows;
  }
  metrics.process_peak_rss_bytes = peak_rss_bytes();
  return output;
}

RunOutput run_hnsw(const Matrix& queries, const Matrix& base,
                   const Config& config, std::size_t ground_truth_limit) {
  enforce_server_limit(queries, base, config);
  const auto build_begin = Clock::now();
  Graph base_graph = build_hnsw_graph(base, config);
  Graph query_graph;
  if (!config.self_join) query_graph = build_hnsw_graph(queries, config);
  const auto build_end = Clock::now();
  RunOutput output = run(queries, base, config.self_join ? base_graph : query_graph,
                         base_graph, config, ground_truth_limit);
  output.metrics.graph_build_ms = elapsed_ms(build_begin, build_end);
  return output;
}

std::string metrics_json(const Metrics& m) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(9) << "{\n"
      << build_metadata_json() << ",\n"
      << "  \"method\": \"" << m.method << "\",\n"
      << "  \"implementation_status\": \"" << m.implementation_status << "\",\n"
      << "  \"implementation_language\": \"" << m.implementation_language << "\",\n"
      << "  \"compiler\": \"" << m.compiler << "\",\n"
      << "  \"paper_reference_environment\": \""
      << m.paper_reference_environment << "\",\n"
      << "  \"graph_backend\": " << json_string(m.graph_backend) << ",\n"
      << "  \"graph_provenance\": " << json_string(m.graph_provenance) << ",\n"
      << "  \"distance_metric\": \"" << m.distance_metric << "\",\n"
      << "  \"empty_set_metric_policy\": \"" << m.empty_set_metric_policy << "\",\n"
      << "  \"query_rows\": " << m.query_rows << ",\n"
      << "  \"base_rows\": " << m.base_rows << ",\n"
      << "  \"dimensions\": " << m.dimensions << ",\n"
      << "  \"epsilon_l2\": " << m.epsilon << ",\n"
      << "  \"w\": " << m.window_size << ",\n"
      << "  \"k\": " << m.k << ",\n"
      << "  \"order_method\": \"" << m.order_method << "\",\n"
      << "  \"seed\": " << m.seed << ",\n"
      << "  \"estimated_peak_bytes\": " << m.estimated_peak_bytes << ",\n"
      << "  \"allocation_estimate_policy\": \"planning-only; epsilon output data-dependent; RLIMIT_AS enforces actual allocation ceiling\",\n"
      << "  \"graph_build_ms_excluded_from_join\": " << m.graph_build_ms << ",\n"
      << "  \"offline_order_ms\": " << m.offline_order_ms << ",\n"
      << "  \"online_order_ms\": " << m.online_order_ms << ",\n"
      << "  \"join_ms\": " << m.join_ms << ",\n"
      << "  \"total_online_ms\": " << m.total_online_ms << ",\n"
      << "  \"self_join\": " << (m.self_join ? "true" : "false") << ",\n"
      << "  \"order_search_w\": " << m.order_search_window << ",\n"
      << "  \"order_repeats\": " << m.order_repeats << ",\n"
      << "  \"online_order_benchmark_ms\": " << m.online_order_ms * m.order_repeats << ",\n"
      << "  \"order_search_policy\": " << json_string(
          !m.self_join && m.order_method == "window-order-epsilon-joinslide"
          ? "JoinSlide-maxT; kJoinSlide-top1-fallback; locally-chosen" : "not-used") << ",\n"
      << "  \"offline_order_ndc\": " << m.offline_order_ndc << ",\n"
      << "  \"online_order_ndc\": " << m.online_order_ndc << ",\n"
      << "  \"total_online_ndc\": " << m.online_order_ndc + m.distance_computations << ",\n"
      << "  \"process_lifetime_peak_rss_bytes\": " << m.process_peak_rss_bytes << ",\n"
      << "  \"distance_computations\": " << m.distance_computations << ",\n"
      << "  \"result_pairs\": " << m.result_pairs << ",\n"
      << "  \"ground_truth_queries\": " << m.evaluated_ground_truth_queries << ",\n"
      << "  \"quality_status\": " << json_string(m.evaluated_ground_truth_queries == 0 ? "not-evaluated" :
            (m.evaluated_ground_truth_queries == m.query_rows ? "full" : "prefix-sample")) << ",\n"
      << "  \"average_recall\": " << json_number(m.average_recall) << ",\n"
      << "  \"average_precision\": " << json_number(m.average_precision) << ",\n"
      << "  \"returned_mean_results_per_query\": " << m.mean_results_per_query << ",\n"
      << "  \"evaluated_true_pairs\": " << m.evaluated_true_pairs << ",\n"
      << "  \"true_mean_results_per_query\": " << json_number(m.true_mean_results_per_query) << ",\n"
      << "  \"paper_formula_nselectivity\": " << json_number(m.paper_formula_nselectivity) << "\n"
      << "}";
  return out.str();
}

}  // namespace vjoin::simjoin
