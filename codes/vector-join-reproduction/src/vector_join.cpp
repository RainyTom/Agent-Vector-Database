#include "vector_join/vector_join.hpp"

#include <hnswlib/hnswlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace vjoin {
namespace {

using Clock = std::chrono::steady_clock;
using Node = std::uint32_t;
constexpr Node kNoNode = std::numeric_limits<Node>::max();

double elapsed_ms(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

float squared_l2(const float* a, const float* b, std::size_t dim) {
  float sum = 0.0F;
  for (std::size_t i = 0; i < dim; ++i) {
    const float delta = a[i] - b[i];
    sum += delta * delta;
  }
  return sum;
}

struct Graph {
  std::vector<std::vector<Node>> edges;
  Node entry{0};
};

Graph build_graph(const Matrix& matrix, const Config& config) {
  if (matrix.rows == 0) return {};
  hnswlib::L2Space space(static_cast<int>(matrix.dim));
  hnswlib::HierarchicalNSW<float> index(
      &space, matrix.rows, config.graph_m, config.ef_construction, config.seed);
  for (std::size_t i = 0; i < matrix.rows; ++i) {
    index.addPoint(matrix.row(i), i);
  }

  Graph graph;
  graph.edges.resize(matrix.rows);
  graph.entry = static_cast<Node>(index.getExternalLabel(index.enterpoint_node_));
  for (std::size_t internal = 0; internal < matrix.rows; ++internal) {
    const Node source = static_cast<Node>(index.getExternalLabel(internal));
    auto* links = index.get_linklist0(static_cast<hnswlib::tableint>(internal));
    const int count = index.getListCount(links);
    const auto* neighbors = reinterpret_cast<const hnswlib::tableint*>(links + 1);
    auto& out = graph.edges[source];
    out.reserve(static_cast<std::size_t>(count));
    for (int j = 0; j < count; ++j) {
      out.push_back(static_cast<Node>(index.getExternalLabel(neighbors[j])));
    }
  }
  return graph;
}

Matrix concatenate(const Matrix& queries, const Matrix& base) {
  Matrix merged;
  merged.rows = queries.rows + base.rows;
  merged.dim = base.dim;
  merged.values.reserve(merged.rows * merged.dim);
  merged.values.insert(merged.values.end(), queries.values.begin(), queries.values.end());
  merged.values.insert(merged.values.end(), base.values.begin(), base.values.end());
  return merged;
}

struct Edge {
  Node left;
  Node right;
  float weight;
};

struct DisjointSet {
  explicit DisjointSet(std::size_t n) : parent(n), rank(n, 0) {
    std::iota(parent.begin(), parent.end(), 0);
  }
  Node find(Node x) {
    if (parent[x] != x) parent[x] = find(parent[x]);
    return parent[x];
  }
  bool unite(Node a, Node b) {
    a = find(a);
    b = find(b);
    if (a == b) return false;
    if (rank[a] < rank[b]) std::swap(a, b);
    parent[b] = a;
    if (rank[a] == rank[b]) ++rank[a];
    return true;
  }
  std::vector<Node> parent;
  std::vector<std::uint8_t> rank;
};

struct QueryOrder {
  std::vector<Node> order;
  std::vector<Node> parent;
};

QueryOrder mst_order(const Matrix& queries, const Graph& graph) {
  QueryOrder result;
  result.parent.assign(queries.rows, kNoNode);
  if (queries.rows == 0) return result;

  std::vector<Edge> candidates;
  std::unordered_set<std::uint64_t> seen;
  for (Node u = 0; u < graph.edges.size(); ++u) {
    for (Node v : graph.edges[u]) {
      if (u == v || v >= queries.rows) continue;
      const Node a = std::min(u, v);
      const Node b = std::max(u, v);
      const std::uint64_t key = (static_cast<std::uint64_t>(a) << 32U) | b;
      if (!seen.insert(key).second) continue;
      candidates.push_back({a, b, squared_l2(queries.row(a), queries.row(b), queries.dim)});
    }
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const Edge& a, const Edge& b) { return a.weight < b.weight; });

  std::vector<std::vector<Node>> tree(queries.rows);
  DisjointSet dsu(queries.rows);
  for (const Edge& edge : candidates) {
    if (!dsu.unite(edge.left, edge.right)) continue;
    tree[edge.left].push_back(edge.right);
    tree[edge.right].push_back(edge.left);
  }

  std::vector<bool> visited(queries.rows, false);
  std::vector<Node> stack;
  result.order.reserve(queries.rows);
  for (Node root = 0; root < queries.rows; ++root) {
    if (visited[root]) continue;
    visited[root] = true;
    stack.push_back(root);
    while (!stack.empty()) {
      const Node current = stack.back();
      stack.pop_back();
      result.order.push_back(current);
      for (auto it = tree[current].rbegin(); it != tree[current].rend(); ++it) {
        if (visited[*it]) continue;
        visited[*it] = true;
        result.parent[*it] = current;
        stack.push_back(*it);
      }
    }
  }
  return result;
}

void trim_min_heap(std::priority_queue<std::pair<float, Node>,
                                       std::vector<std::pair<float, Node>>,
                                       std::greater<>>& heap,
                   std::size_t limit) {
  if (heap.size() <= limit) return;
  std::vector<std::pair<float, Node>> items;
  items.reserve(heap.size());
  while (!heap.empty()) {
    items.push_back(heap.top());
    heap.pop();
  }
  if (items.size() > limit) items.resize(limit);
  for (const auto& item : items) heap.push(item);
}

struct SearchState {
  std::vector<Node> in_range;
  std::vector<Node> visited;
  Node closest_data{kNoNode};
  float closest_distance{std::numeric_limits<float>::infinity()};
  std::uint64_t greedy_computations{};
  std::uint64_t expansion_computations{};
};

struct SearchContext {
  const Matrix& query_matrix;
  const Matrix& data_matrix;
  const Matrix* merged_matrix;
  const Graph& graph;
  const Config& config;
  bool merged;

  [[nodiscard]] bool is_data(Node node) const {
    return !merged || node >= query_matrix.rows;
  }
  [[nodiscard]] Node data_id(Node node) const {
    return merged ? node - static_cast<Node>(query_matrix.rows) : node;
  }
  [[nodiscard]] const float* vector(Node node) const {
    return merged ? merged_matrix->row(node) : data_matrix.row(node);
  }
};

void consider_data(Node node, float distance, const SearchContext& context,
                   SearchState& state) {
  if (!context.is_data(node)) return;
  if (distance < state.closest_distance) {
    state.closest_distance = distance;
    state.closest_data = node;
  }
  if (distance <= context.config.threshold) state.in_range.push_back(node);
}

SearchState greedy_phase(Node query_id, const std::vector<Node>& seeds,
                         const SearchContext& context) {
  SearchState state;
  const float* query = context.query_matrix.row(query_id);
  std::vector<bool> seen(context.graph.edges.size(), false);
  using Heap = std::priority_queue<std::pair<float, Node>,
                                   std::vector<std::pair<float, Node>>,
                                   std::greater<>>;
  Heap queue;
  for (Node seed : seeds) {
    if (seed >= seen.size() || seen[seed]) continue;
    seen[seed] = true;
    const float distance = squared_l2(query, context.vector(seed), context.query_matrix.dim);
    ++state.greedy_computations;
    queue.emplace(distance, seed);
    state.visited.push_back(seed);
    consider_data(seed, distance, context, state);
  }

  float best = std::numeric_limits<float>::infinity();
  std::size_t stale = 0;
  while (!queue.empty()) {
    const auto [distance, current] = queue.top();
    if (context.is_data(current) && distance <= context.config.threshold) break;
    if (distance + 1e-6F < best) {
      best = distance;
      stale = 0;
    } else if (++stale >= context.config.convergence_threshold) {
      break;
    }
    queue.pop();
    for (Node neighbor : context.graph.edges[current]) {
      if (seen[neighbor]) continue;
      seen[neighbor] = true;
      const float candidate_distance =
          squared_l2(query, context.vector(neighbor), context.query_matrix.dim);
      ++state.greedy_computations;
      queue.emplace(candidate_distance, neighbor);
      state.visited.push_back(neighbor);
      consider_data(neighbor, candidate_distance, context, state);
    }
    trim_min_heap(queue, context.config.greedy_beam);
    // In a merged index, probing the query node's neighborhood is the offline
    // lookup described by StopGreedy in the paper.
    if (context.merged && current == query_id) break;
  }
  return state;
}

void expansion_phase(Node query_id, bool hybrid, const SearchContext& context,
                     SearchState& state) {
  const float* query = context.query_matrix.row(query_id);
  std::vector<bool> seen(context.graph.edges.size(), false);
  for (Node node : state.visited) seen[node] = true;

  std::deque<Node> in_range_queue;
  for (Node node : state.in_range) in_range_queue.push_back(node);
  using Heap = std::priority_queue<std::pair<float, Node>,
                                   std::vector<std::pair<float, Node>>,
                                   std::greater<>>;
  Heap bridge_queue;
  std::size_t expanded_bridges = 0;

  while (!in_range_queue.empty() || (hybrid && !bridge_queue.empty() &&
                                     expanded_bridges < context.config.hybrid_beam)) {
    Node current;
    if (!in_range_queue.empty()) {
      current = in_range_queue.front();
      in_range_queue.pop_front();
    } else {
      current = bridge_queue.top().second;
      bridge_queue.pop();
      ++expanded_bridges;
    }
    for (Node neighbor : context.graph.edges[current]) {
      if (seen[neighbor]) continue;
      seen[neighbor] = true;
      // Expansion in the merged index does not traverse query nodes.
      if (context.merged && !context.is_data(neighbor)) continue;
      const float distance =
          squared_l2(query, context.vector(neighbor), context.query_matrix.dim);
      ++state.expansion_computations;
      state.visited.push_back(neighbor);
      if (distance < state.closest_distance) {
        state.closest_distance = distance;
        state.closest_data = neighbor;
      }
      if (distance <= context.config.threshold) {
        state.in_range.push_back(neighbor);
        in_range_queue.push_back(neighbor);
      } else if (hybrid) {
        bridge_queue.emplace(distance, neighbor);
        trim_min_heap(bridge_queue, context.config.hybrid_beam);
      }
    }
  }
  std::sort(state.in_range.begin(), state.in_range.end());
  state.in_range.erase(std::unique(state.in_range.begin(), state.in_range.end()),
                       state.in_range.end());
}

bool predict_ood(Node query_id, const SearchContext& context) {
  if (!context.merged || query_id >= context.graph.edges.size()) return false;
  const float* query = context.query_matrix.row(query_id);
  double d1 = 0.0;
  double d2 = 0.0;
  std::size_t n1 = 0;
  std::size_t n2 = 0;
  for (Node neighbor : context.graph.edges[query_id]) {
    if (!context.is_data(neighbor)) continue;
    d1 += std::sqrt(squared_l2(query, context.vector(neighbor), context.query_matrix.dim));
    ++n1;
    const float* data = context.vector(neighbor);
    for (Node two_hop : context.graph.edges[neighbor]) {
      if (!context.is_data(two_hop)) continue;
      d2 += std::sqrt(squared_l2(data, context.vector(two_hop), context.query_matrix.dim));
      ++n2;
    }
  }
  if (n1 == 0 || n2 == 0) return true;
  return (d1 / static_cast<double>(n1)) / (d2 / static_cast<double>(n2)) >
         context.config.ood_ratio_threshold;
}

std::vector<Node> ground_truth(const Matrix& queries, const Matrix& base,
                               Node query_id, float threshold) {
  std::vector<Node> result;
  for (Node data_id = 0; data_id < base.rows; ++data_id) {
    if (squared_l2(queries.row(query_id), base.row(data_id), base.dim) <= threshold) {
      result.push_back(data_id);
    }
  }
  return result;
}

}  // namespace

const float* Matrix::row(std::size_t i) const { return values.data() + i * dim; }
float* Matrix::row(std::size_t i) { return values.data() + i * dim; }

Matrix Matrix::load_raw(const std::string& path, std::size_t dimensions,
                        std::size_t max_rows) {
  if (dimensions == 0) throw std::invalid_argument("dimension must be positive");
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) throw std::runtime_error("cannot open vector file: " + path);
  const auto bytes = static_cast<std::size_t>(input.tellg());
  const std::size_t row_bytes = dimensions * sizeof(float);
  if (bytes % row_bytes != 0) {
    throw std::runtime_error("file size is not divisible by dim*sizeof(float): " + path);
  }
  Matrix matrix;
  matrix.dim = dimensions;
  matrix.rows = bytes / row_bytes;
  if (max_rows != 0) matrix.rows = std::min(matrix.rows, max_rows);
  matrix.values.resize(matrix.rows * dimensions);
  input.seekg(0);
  input.read(reinterpret_cast<char*>(matrix.values.data()),
             static_cast<std::streamsize>(matrix.values.size() * sizeof(float)));
  if (!input) throw std::runtime_error("failed to read vector file: " + path);
  return matrix;
}

Method parse_method(const std::string& value) {
  if (value == "independent") return Method::kIndependent;
  if (value == "hard") return Method::kHardSharing;
  if (value == "soft") return Method::kSoftSharing;
  if (value == "mi") return Method::kMergedIndex;
  if (value == "mi-adapt") return Method::kMergedAdaptive;
  throw std::invalid_argument("unknown method: " + value);
}

const char* method_name(Method method) {
  switch (method) {
    case Method::kIndependent: return "independent";
    case Method::kHardSharing: return "hard";
    case Method::kSoftSharing: return "soft";
    case Method::kMergedIndex: return "mi";
    case Method::kMergedAdaptive: return "mi-adapt";
  }
  return "unknown";
}

RunOutput run(const Matrix& queries, const Matrix& base, const Config& config,
              std::size_t ground_truth_limit) {
  if (queries.dim != base.dim) throw std::invalid_argument("dimension mismatch");
  if (queries.rows == 0 || base.rows == 0) throw std::invalid_argument("empty input");
  const bool merged_mode = config.method == Method::kMergedIndex ||
                           config.method == Method::kMergedAdaptive;
  const bool sharing = config.method == Method::kHardSharing ||
                       config.method == Method::kSoftSharing;

  const auto build_start = Clock::now();
  Graph query_graph;
  if (sharing) query_graph = build_graph(queries, config);
  Matrix merged;
  Graph data_graph;
  Graph merged_graph;
  if (merged_mode) {
    merged = concatenate(queries, base);
    merged_graph = build_graph(merged, config);
  } else {
    data_graph = build_graph(base, config);
  }
  const auto build_end = Clock::now();

  const auto order_start = Clock::now();
  QueryOrder ordering;
  if (sharing) {
    ordering = mst_order(queries, query_graph);
  } else {
    ordering.parent.assign(queries.rows, kNoNode);
    ordering.order.resize(queries.rows);
    std::iota(ordering.order.begin(), ordering.order.end(), 0);
  }
  const auto order_end = Clock::now();

  const Graph& search_graph = merged_mode ? merged_graph : data_graph;
  SearchContext context{queries, base, merged_mode ? &merged : nullptr,
                        search_graph, config, merged_mode};
  std::vector<std::vector<Node>> cache(queries.rows);
  RunOutput output;
  output.queries.resize(queries.rows);

  const auto join_start = Clock::now();
  for (Node query_id : ordering.order) {
    std::vector<Node> seeds;
    if (merged_mode) {
      seeds.push_back(query_id);
    } else {
      seeds.push_back(search_graph.entry);
      const Node parent = ordering.parent[query_id];
      if (parent != kNoNode) {
        seeds.insert(seeds.end(), cache[parent].begin(), cache[parent].end());
      }
    }
    SearchState state = greedy_phase(query_id, seeds, context);
    const bool hybrid = config.method == Method::kMergedAdaptive &&
                        predict_ood(query_id, context);
    expansion_phase(query_id, hybrid, context, state);

    QueryResult& query_result = output.queries[query_id];
    query_result.matches.reserve(state.in_range.size());
    for (Node node : state.in_range) query_result.matches.push_back(context.data_id(node));
    std::sort(query_result.matches.begin(), query_result.matches.end());
    query_result.matches.erase(
        std::unique(query_result.matches.begin(), query_result.matches.end()),
        query_result.matches.end());
    query_result.greedy_distance_computations = state.greedy_computations;
    query_result.expansion_distance_computations = state.expansion_computations;
    query_result.distance_computations = state.greedy_computations + state.expansion_computations;
    query_result.hybrid_used = hybrid;

    if (config.method == Method::kHardSharing) {
      cache[query_id] = state.in_range;
    } else if (config.method == Method::kSoftSharing && state.closest_data != kNoNode) {
      cache[query_id] = {state.closest_data};
    }
  }
  const auto join_end = Clock::now();

  Metrics& metrics = output.metrics;
  metrics.method = method_name(config.method);
  metrics.base_rows = base.rows;
  metrics.query_rows = queries.rows;
  metrics.dimensions = base.dim;
  metrics.threshold = config.threshold;
  metrics.build_ms = elapsed_ms(build_start, build_end);
  metrics.order_ms = elapsed_ms(order_start, order_end);
  metrics.join_ms = elapsed_ms(join_start, join_end);
  for (const QueryResult& result : output.queries) {
    metrics.result_pairs += result.matches.size();
    metrics.distance_computations += result.distance_computations;
    metrics.greedy_distance_computations += result.greedy_distance_computations;
    metrics.expansion_distance_computations += result.expansion_distance_computations;
    metrics.hybrid_queries += result.hybrid_used ? 1U : 0U;
  }

  const std::size_t eval_count = std::min(ground_truth_limit, queries.rows);
  metrics.evaluated_ground_truth_queries = eval_count;
  std::uint64_t all_true = 0;
  std::uint64_t all_found = 0;
  std::uint64_t all_returned = 0;
  double macro = 0.0;
  for (Node query_id = 0; query_id < eval_count; ++query_id) {
    const std::vector<Node> truth = ground_truth(queries, base, query_id, config.threshold);
    const auto& actual = output.queries[query_id].matches;
    std::vector<Node> intersection;
    std::set_intersection(truth.begin(), truth.end(), actual.begin(), actual.end(),
                          std::back_inserter(intersection));
    const double recall = truth.empty() ? (actual.empty() ? 1.0 : 0.0)
                                        : static_cast<double>(intersection.size()) / truth.size();
    macro += recall;
    all_true += truth.size();
    all_found += intersection.size();
    all_returned += actual.size();
  }
  if (eval_count != 0) {
    metrics.macro_recall = macro / eval_count;
    metrics.pair_recall = all_true == 0 ? 1.0 : static_cast<double>(all_found) / all_true;
    metrics.precision = all_returned == 0 ? (all_true == 0 ? 1.0 : 0.0)
                                          : static_cast<double>(all_found) / all_returned;
  }
  return output;
}

std::string metrics_json(const Metrics& m) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(6)
      << "{\n"
      << "  \"method\": \"" << m.method << "\",\n"
      << "  \"base_rows\": " << m.base_rows << ",\n"
      << "  \"query_rows\": " << m.query_rows << ",\n"
      << "  \"dimensions\": " << m.dimensions << ",\n"
      << "  \"threshold_squared_l2\": " << m.threshold << ",\n"
      << "  \"build_ms\": " << m.build_ms << ",\n"
      << "  \"order_ms\": " << m.order_ms << ",\n"
      << "  \"join_ms\": " << m.join_ms << ",\n"
      << "  \"result_pairs\": " << m.result_pairs << ",\n"
      << "  \"distance_computations\": " << m.distance_computations << ",\n"
      << "  \"greedy_distance_computations\": " << m.greedy_distance_computations << ",\n"
      << "  \"expansion_distance_computations\": " << m.expansion_distance_computations << ",\n"
      << "  \"hybrid_queries\": " << m.hybrid_queries << ",\n"
      << "  \"ground_truth_queries\": " << m.evaluated_ground_truth_queries << ",\n"
      << "  \"macro_recall\": " << m.macro_recall << ",\n"
      << "  \"pair_recall\": " << m.pair_recall << ",\n"
      << "  \"precision\": " << m.precision << "\n"
      << "}";
  return out.str();
}

}  // namespace vjoin
