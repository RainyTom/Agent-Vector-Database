// A finite API/semantics learning probe, NOT a speed/recall benchmark.
// Compile against the repository's pinned hnswlib and common RawL2Space.
#include "raw_l2_space.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct Allowed final : hnswlib::BaseFilterFunctor {
    size_t checks = 0;
    bool operator()(hnswlib::labeltype id) override {
        ++checks;
        return id == 20 || id == 40;
    }
};
using Result = std::pair<float, hnswlib::labeltype>;
static std::vector<Result> sorted_results(hnswlib::HierarchicalNSW<float>& graph,
                                         const float* q, Allowed* allowed) {
    auto heap = graph.searchKnn(q, 2, allowed);
    std::vector<Result> values;
    while (!heap.empty()) { values.push_back(heap.top()); heap.pop(); }
    std::sort(values.begin(), values.end());
    return values;
}
int main() {
    try {
        RawL2Space space(4); // Outlives graph; never copied with its parameter pointer.
        hnswlib::HierarchicalNSW<float> graph(&space, 8, 4, 20, 100);
        graph.setEf(16);
        const std::array<std::array<float, 4>, 4> points = {{{0, 0, 0, 0}, {1, 0, 0, 0},
                                                          {2, 0, 0, 0}, {3, 0, 0, 0}}};
        for (size_t i = 0; i < points.size(); ++i) graph.addPoint(points[i].data(), (i + 1) * 10);
        const std::array<float, 4> query = {.25f, 0, 0, 0};
        Allowed allowed;
        const auto before = sorted_results(graph, query.data(), &allowed);
        require(before.size() == 2 && before[0].second == 20 && before[1].second == 40,
                "filter did not return the exact two allowed IDs on this finite graph");
        require(before[0].first == space.dist(query.data(), points[1].data()) &&
                before[0].first == .5625f && before[1].first == 7.5625f,
                "L2 units differ from shared squared-L2 kernel");
        graph.markDelete(20);
        const size_t stored_after_delete = graph.getCurrentElementCount();
        const size_t tombstones = graph.getDeletedCount();
        const auto after_delete = sorted_results(graph, query.data(), &allowed);
        require(stored_after_delete == 4 && tombstones == 1,
                "markDelete unexpectedly reclaimed the stored element slot");
        require(after_delete.size() == 1 && after_delete[0].second == 40,
                "deleted or disallowed ID returned, or insufficient eligible result mishandled");
        graph.unmarkDelete(20);
        const auto restored = sorted_results(graph, query.data(), &allowed);
        require(restored == before && graph.getCurrentElementCount() == 4 && graph.getDeletedCount() == 0,
                "unmarkDelete did not restore this immutable vector and label");
        const auto payload = graph.getDataByLabel<float>(20);
        require(payload.size() == 4 && payload[0] == 1, "revived vector payload mismatch");
        std::cout << "{\"complete\":true,\"kind\":\"finite_pinned_hnsw_API_probe_not_performance\","
                  << "\"stored_before_delete\":4,\"stored_after_markDelete\":" << stored_after_delete
                  << ",\"tombstones_after_markDelete\":" << tombstones
                  << ",\"filter_before_ids\":[20,40],\"filter_after_delete_ids\":[40],"
                  << "\"filter_after_restore_ids\":[20,40],\"squared_distances_before\":["
                  << before[0].first << ',' << before[1].first << "],\"filter_callback_calls\":"
                  << allowed.checks << ",\"limitations\":[\"Four constructed points only; no finite-ef recall theorem.\","
                  << "\"No persistence, physical erasure, concurrent safety or database transaction tested.\"]}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
