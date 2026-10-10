#include "detail/algorithm.hpp"

namespace vjoin {

RunOutput run_fast_avj_merged(const Matrix& queries, const Matrix& base,
                             const Config& config,
                             std::size_t ground_truth_limit) {
  return detail::run_plan(queries, base, config, ground_truth_limit,
                          {Method::kMergedIndex, false, true, false,
                           detail::CachePolicy::kNone});
}

}  // namespace vjoin
