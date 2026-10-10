#include "vector_join/vector_join.hpp"

#include <stdexcept>

namespace vjoin {

RunOutput run(const Matrix& queries, const Matrix& base, const Config& config,
              std::size_t ground_truth_limit) {
  switch (config.method) {
    case Method::kIndependent:
      return run_independent(queries, base, config, ground_truth_limit);
    case Method::kHardSharing:
      return run_simjoin_hard(queries, base, config, ground_truth_limit);
    case Method::kSoftSharing:
      return run_fast_avj_soft(queries, base, config, ground_truth_limit);
    case Method::kMergedIndex:
      return run_fast_avj_merged(queries, base, config, ground_truth_limit);
    case Method::kMergedAdaptive:
      return run_fast_avj_adaptive(queries, base, config, ground_truth_limit);
  }
  throw std::invalid_argument("unsupported vector-join method");
}

}  // namespace vjoin
