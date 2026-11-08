// Shared raw FP32 squared-L2 distance for the NodeANN hnswlib research runner.
//
// Contract (2026-10-29 plan, section 3.2): every raw FP32 squared-L2 distance
// in this project MUST go through hnswlib's L2Space::get_dist_func() from the
// pinned revision (ca426729609b3221563047ceb269cc1213e0d376). No hand-written
// scalar/SIMD distance kernels are allowed anywhere else.
//
// Usage:
//   RawL2Space l2(dim);
//   float d = l2.dist(a, b);                 // ad-hoc calls (exact scans, GT)
//   hnswlib::SpaceInterface<float>* s = l2;  // feed to HierarchicalNSW ctor
#pragma once

#include "hnswlib/hnswlib.h"

#include <cstddef>

class RawL2Space : public hnswlib::SpaceInterface<float> {
public:
    explicit RawL2Space(size_t dim) : inner_(static_cast<int>(dim)) {
        fn_ = inner_.get_dist_func();
        param_ = inner_.get_dist_func_param();
    }

    // The one and only raw distance implementation: hnswlib's own function.
    float dist(const float* a, const float* b) const {
        return fn_(a, b, param_);
    }

    // SpaceInterface<float> implementation, delegates to the pinned L2Space so
    // hnswlib graph build/query use exactly the same distance function.
    size_t get_data_size() override { return inner_.get_data_size(); }
    hnswlib::DISTFUNC<float> get_dist_func() override { return fn_; }
    void* get_dist_func_param() override { return param_; }

private:
    hnswlib::L2Space inner_;
    hnswlib::DISTFUNC<float> fn_ = nullptr;
    void* param_ = nullptr;
};
