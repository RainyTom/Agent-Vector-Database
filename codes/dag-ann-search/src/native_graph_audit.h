#pragma once

#include <cstdio>
#include "hnswlib/hnswlib.h"

// Diagnostic-only native topology, before ASI adds MST or cross-attribute edges.
// Both consumers insert each owner's objects in increasing object-ID order.
static inline void write_native_graph_audit(
        FILE* output, int concept, const hnswlib::HierarchicalNSW<float>& graph) {
    const size_t count = graph.cur_element_count.load();
    std::fprintf(output, "{\"concept\":%d,\"entry\":%u,\"maxlevel\":%d,\"nodes\":[",
                 concept, graph.enterpoint_node_, graph.maxlevel_);
    for (size_t i = 0; i < count; ++i) {
        const auto node = hnswlib::tableint(i);
        std::fprintf(output, "%s{\"label\":%llu,\"levels\":[", i ? "," : "",
                     static_cast<unsigned long long>(graph.getExternalLabel(node)));
        for (int level = 0; level <= graph.element_levels_[i]; ++level) {
            auto* links = graph.get_linklist_at_level(node, level);
            const size_t degree = graph.getListCount(links);
            const auto* neighbors = reinterpret_cast<const hnswlib::tableint*>(links + 1);
            std::fprintf(output, "%s[", level ? "," : "");
            for (size_t j = 0; j < degree; ++j)
                std::fprintf(output, "%s%u", j ? "," : "", neighbors[j]);
            std::fprintf(output, "]");
        }
        std::fprintf(output, "]}");
    }
    std::fprintf(output, "]}\n");
}
