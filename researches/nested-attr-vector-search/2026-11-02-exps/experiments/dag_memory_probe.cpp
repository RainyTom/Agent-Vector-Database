// Finite native allocation probe, not a search benchmark.
#include "dag_composition_fixture.h"
int main() try {
    RawL2Space raw(128);
    Graph graph(&raw, 2, 64, 200, 100);
    std::vector<float> first(128, 0), second(128, 1);
    graph.addPoint(first.data(), 0); graph.addPoint(second.data(), 1);
    auto* links = graph.get_linklist0(0);
    const auto before_count = graph.getListCount(links);
    require(before_count > 0, "probe has no edge to remove");
    const auto* before_base = graph.data_level0_memory_;
    const auto before_stride = graph.size_data_per_element_;
    const auto before_links = graph.size_links_level0_;
    graph.setListCount(links, before_count - 1);
    std::cout << "{\"M\":" << graph.M_
              << ",\"efConstruction\":" << graph.ef_construction_
              << ",\"maxM0\":" << graph.maxM0_
              << ",\"before_effective_neighbors\":" << before_count
              << ",\"after_effective_neighbors\":" << graph.getListCount(links)
              << ",\"before_level0_neighbor_bytes_per_capacity_item\":" << before_links
              << ",\"after_level0_neighbor_bytes_per_capacity_item\":" << graph.size_links_level0_
              << ",\"before_record_stride\":" << before_stride
              << ",\"after_record_stride\":" << graph.size_data_per_element_
              << ",\"upper_level_neighbor_bytes_per_occurrence\":" << graph.size_links_per_element_
              << ",\"allocation_base_unchanged\":" << (before_base == graph.data_level0_memory_ ? "true" : "false")
              << ",\"label_lock_payload_bytes\":" << graph.label_op_locks_.size() * sizeof(std::mutex)
              << "}\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n'; return 1;
}
