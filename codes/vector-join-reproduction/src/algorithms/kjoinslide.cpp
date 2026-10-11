#include "detail/simjoin_search.hpp"

#include <algorithm>
#include <iterator>
#include <queue>
#include <set>
#include <stdexcept>

namespace vjoin::simjoin::detail {

std::vector<Node> k_join_slide(const Matrix& base, const Graph& graph,
                               const float* query, const std::vector<Node>& seeds,
                               std::size_t k, std::size_t window_size,
                               std::uint64_t& ndc) {
  if (k == 0 || window_size < k) throw std::invalid_argument("k requires w >= k > 0");
  std::set<std::pair<float, Node>> candidates;
  std::vector<bool> visited(base.rows, false);
  std::vector<bool> explored(base.rows, false);
  for (Node seed : seeds) {
    if (seed >= base.rows || visited[seed]) continue;
    visited[seed] = true;
    candidates.emplace(squared_l2(base.row(seed), query, base.dim, &ndc), seed);
  }
  while (true) {
    auto current = std::find_if(candidates.begin(), candidates.end(),
                                [&](const auto& item) { return !explored[item.second]; });
    if (current == candidates.end()) break;
    const Node node = current->second;
    explored[node] = true;
    for (Node neighbor : graph.edges[node]) {
      if (neighbor >= base.rows || visited[neighbor]) continue;
      visited[neighbor] = true;
      candidates.emplace(squared_l2(base.row(neighbor), query, base.dim, &ndc), neighbor);
      while (candidates.size() > window_size) candidates.erase(std::prev(candidates.end()));
    }
  }
  std::vector<Node> result;
  for (const auto& [distance, node] : candidates) {
    static_cast<void>(distance);
    result.push_back(node);
    if (result.size() == k) break;
  }
  std::sort(result.begin(), result.end());
  return result;
}

}  // namespace vjoin::simjoin::detail
