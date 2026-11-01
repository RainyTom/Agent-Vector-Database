// Synthetic diagnostic of native filtered termination, not a SIFT timing run.
#include "raw_l2_space.h"
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>

struct CountingSpace : hnswlib::SpaceInterface<float> {
    RawL2Space raw;
    size_t calls = 0;
    explicit CountingSpace(size_t dimension) : raw(dimension) {}
    static float distance(const void* a, const void* b, const void* context) {
        auto* self = const_cast<CountingSpace*>(static_cast<const CountingSpace*>(context));
        ++self->calls;
        return self->raw.dist(static_cast<const float*>(a), static_cast<const float*>(b));
    }
    size_t get_data_size() override { return raw.get_data_size(); }
    hnswlib::DISTFUNC<float> get_dist_func() override { return distance; }
    void* get_dist_func_param() override { return this; }
};
struct Filter : hnswlib::BaseFilterFunctor {
    size_t limit, calls = 0;
    explicit Filter(size_t count) : limit(count) {}
    bool operator()(hnswlib::labeltype id) override { ++calls; return id < limit; }
};
int main() try {
    constexpr size_t n = 2048, dim = 16;
    CountingSpace space(dim);
    hnswlib::HierarchicalNSW<float> graph(&space, n, 16, 200, 100);
    std::mt19937 rng(20261101);
    std::normal_distribution<float> normal;
    std::vector<float> base(n * dim), query(dim);
    for (float& value : base) value = normal(rng);
    for (float& value : query) value = normal(rng);
    for (size_t i = 0; i < n; ++i) graph.addPoint(base.data() + i * dim, i);
    std::cout << "objects,eligible,ef,k,returned,filter_calls,raw_distance_calls\n";
    for (size_t count : {size_t(7), size_t(31), size_t(32), size_t(128), n}) {
        graph.setEf(32);
        Filter filter(count);
        space.calls = 0;
        auto result = graph.searchKnn(query.data(), 10, &filter);
        std::cout << n << ',' << count << ",32,10," << result.size()
                  << ',' << filter.calls << ',' << space.calls << '\n';
        if (count < 32 && filter.calls != n)
            throw std::runtime_error("small-result filtered search did not exhaust graph as predicted");
    }
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
