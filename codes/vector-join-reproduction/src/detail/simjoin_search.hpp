#pragma once
#include "vector_join/simjoin_paper.hpp"

namespace vjoin::simjoin::detail {
using Node = std::uint32_t;
inline float squared_l2(const float* left, const float* right, std::size_t dim,
                         std::uint64_t* ndc = nullptr) {
  if (ndc != nullptr) ++*ndc;
  float result = 0.0F;
  for (std::size_t i = 0; i < dim; ++i) {
    const float delta = left[i] - right[i];
    result += delta * delta;
  }
  return result;
}
std::vector<Node> join_slide(const Matrix&, const Graph&, const float*,
                             const std::vector<Node>&, float, std::size_t,
                             std::uint64_t&);
std::vector<Node> k_join_slide(const Matrix&, const Graph&, const float*,
                               const std::vector<Node>&, std::size_t, std::size_t,
                               std::uint64_t&);
}
