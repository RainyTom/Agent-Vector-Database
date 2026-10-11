#include "detail/algorithm.hpp"

namespace vjoin {

RunOutput run_hard_window_cache(const Matrix& queries, const Matrix& base,
                                const Config& config,
                                std::size_t ground_truth_limit) {
  return detail::run_plan(queries, base, config, ground_truth_limit,
                          {Method::kHardSharing, true, false, false,
                           detail::CachePolicy::kJoinWindow});
}

}  // namespace vjoin
