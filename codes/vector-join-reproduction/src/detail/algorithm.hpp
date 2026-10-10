#pragma once

#include "vector_join/vector_join.hpp"

namespace vjoin::detail {

enum class CachePolicy { kNone, kJoinWindow, kClosestPoint };

struct AlgorithmPlan {
  Method method;
  bool build_query_order{};
  bool merged_index{};
  bool adaptive_expansion{};
  CachePolicy cache_policy{CachePolicy::kNone};
};

RunOutput run_plan(const Matrix& queries, const Matrix& base,
                   const Config& config, std::size_t ground_truth_limit,
                   const AlgorithmPlan& plan);

}  // namespace vjoin::detail
