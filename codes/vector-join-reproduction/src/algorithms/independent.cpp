#include "detail/algorithm.hpp"

namespace vjoin {

RunOutput run_independent(const Matrix& queries, const Matrix& base,
                          const Config& config,
                          std::size_t ground_truth_limit) {
  return detail::run_plan(
      queries, base, config, ground_truth_limit,
      {Method::kIndependent, false, false, false, detail::CachePolicy::kNone});
}

}  // namespace vjoin
