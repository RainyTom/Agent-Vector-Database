#include "detail/simjoin_search.hpp"

#include <algorithm>
#include <iterator>
#include <queue>
#include <set>
#include <stdexcept>

namespace vjoin::simjoin::detail {

std::vector<Node> join_slide(const Matrix& base, const Graph& graph,
                             const float* query, const std::vector<Node>& seeds,
                             float epsilon, std::size_t window_size,
                             std::uint64_t& ndc) {
  if (window_size == 0) throw std::invalid_argument("w must be positive");
  const float epsilon_squared = epsilon * epsilon;
  std::set<std::pair<float, Node>> queue;
  for (Node seed : seeds) {
    if (seed >= base.rows) throw std::invalid_argument("seed outside base graph");
    queue.emplace(squared_l2(base.row(seed), query, base.dim, &ndc), seed);
  }
  if (queue.empty()) return {};

  if (queue.begin()->first > epsilon_squared) {
    std::vector<bool> visit(base.rows, false);
    for (Node seed : seeds) visit[seed] = true;
    while (!queue.empty()) {
      if (queue.begin()->first <= epsilon_squared) break;
      const auto [distance, current] = *queue.begin();
      queue.erase(queue.begin());
      for (Node neighbor : graph.edges[current]) {
        if (neighbor >= base.rows || visit[neighbor]) continue;
        const float candidate = squared_l2(base.row(neighbor), query, base.dim, &ndc);
        if (candidate < distance) {
          queue.emplace(candidate, neighbor);
          visit[neighbor] = true;
        }
      }
      while (queue.size() > window_size) queue.erase(std::prev(queue.end()));
    }
  }
  if (queue.empty()) return {};

  std::vector<Node> result;
  std::vector<bool> in_result(base.rows, false);
  std::queue<Node> expansion;
  for (const auto& [distance, node] : queue) {
    if (distance > epsilon_squared || in_result[node]) continue;
    in_result[node] = true;
    result.push_back(node);
    expansion.push(node);
  }
  while (!expansion.empty()) {
    const Node current = expansion.front();
    expansion.pop();
    for (Node neighbor : graph.edges[current]) {
      if (neighbor >= base.rows || in_result[neighbor]) continue;
      if (squared_l2(base.row(neighbor), query, base.dim, &ndc) <= epsilon_squared) {
        in_result[neighbor] = true;
        result.push_back(neighbor);
        expansion.push(neighbor);  // Algorithm 3 line 19's `v` is read as y_q.
      }
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

}  // namespace vjoin::simjoin::detail
