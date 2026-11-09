#ifdef FAISS_REFERENCE
#include "../sift_distance_counter.h"
#include <faiss/IndexACORN.h>
#include <faiss/impl/IDSelector.h>
#else
#include "acorn_index.h"
#endif

#include <array>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

constexpr int k = 10;

template <typename T>
std::vector<T> read_prefix(const std::string& file, size_t count) {
    std::ifstream input(file, std::ios::binary);
    std::vector<T> values(count);
    if (!input.read(reinterpret_cast<char*>(values.data()), count * sizeof(T))) {
        throw std::runtime_error("Cannot read " + file);
    }
    return values;
}

struct Fixture {
    std::vector<float> data;
    std::vector<int> levels;
    std::vector<std::array<std::vector<unsigned>, 3>> edges;
    int prefix;
};

Fixture layered() {
    Fixture f{{10, 7, 4, 1, 0}, {2, 2, 1, 0, 0}, {}, -1};
    f.edges.resize(5);
    f.edges[0][2] = {1};
    f.edges[1][1] = {2};
    f.edges[2][0] = {3, 4};
    return f;
}

Fixture two_hops() {
    Fixture f{{0, 5, 1, 3, 7}, {0, 0, 0, 0, 0}, {}, 1};
    f.edges.resize(5);
    f.edges[0][0] = {4, 1, 3};
    f.edges[1][0] = {2, 3};
    f.edges[4][0] = {3};
    return f;
}

void run(const Fixture& f, const std::vector<float>& data, int dimension, int gamma,
         const std::vector<uint8_t>& labels, const std::vector<float>& queries,
         const std::vector<uint8_t>& predicates) {
    const auto count = labels.size();
    const int prefix = f.prefix >= 0 ? f.prefix : gamma == 12 ? 64 : 32;
#ifdef FAISS_REFERENCE
    std::vector<int> metadata(labels.begin(), labels.end());
    faiss::IndexACORNFlat index(dimension, 32, gamma, metadata, prefix);
    index.acorn.efConstruction = 40;
    if (f.levels.empty()) {
        index.add(count, data.data());
    } else {
        index.storage->add(count, data.data());
        index.ntotal = count;
        for (auto layer : f.levels) index.acorn.levels.push_back(layer + 1);
        index.acorn.prepare_level_tab(count, true);
        index.acorn.entry_point = 0;
        index.acorn.max_level = f.levels[0];
        for (size_t node = 0; node < count; ++node) {
            for (int layer = 0; layer <= f.levels[node]; ++layer) {
                size_t begin, end;
                index.acorn.neighbor_range(node, layer, &begin, &end);
                for (auto next : f.edges[node][layer]) index.acorn.neighbors[begin++] = next;
            }
        }
    }
#else
    hnswlib::L2Space space(dimension);
    port::Acorn index(space, gamma, prefix);
    if (f.levels.empty()) {
        index.build(data.data(), count);
    } else {
        index.initialize(data.data(), f.levels);
        index.set_entry(0);
        for (size_t node = 0; node < count; ++node) {
            for (int layer = 0; layer <= f.levels[node]; ++layer) {
                index.set_links(node, layer, f.edges[node][layer]);
            }
        }
    }
    index.validate();
#endif

    // Hash every edge in storage order, including the node and layer boundaries.
    uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&](uint64_t item) { hash = (hash ^ item) * 1099511628211ULL; };
#ifdef FAISS_REFERENCE
    mix(index.acorn.entry_point);
#else
    mix(index.enterpoint_node_);
#endif
    for (size_t node = 0; node < count; ++node) {
        mix(node);
#ifdef FAISS_REFERENCE
        int levels = index.acorn.levels[node];
#else
        int levels = index.element_levels_[node] + 1;
#endif
        mix(levels);
        for (int layer = 0; layer < levels; ++layer) {
#ifdef FAISS_REFERENCE
            size_t begin, end;
            index.acorn.neighbor_range(node, layer, &begin, &end);
            for (; begin < end && index.acorn.neighbors[begin] >= 0; ++begin) {
                mix(index.acorn.neighbors[begin]);
            }
#else
            auto* links = index.get_linklist_at_level(node, layer);
            for (size_t i = 0; i < index.getListCount(links); ++i) mix(links[i + 1]);
#endif
            mix(UINT64_MAX);
        }
    }
    std::cout << "graph," << count << ',' << gamma << ',' << prefix << ',' << hash << '\n';

    for (int ef : {10, 60, 110}) {
        for (size_t row = 0; row < predicates.size(); ++row) {
            std::vector<uint8_t> mask(count);
            std::vector<uint8_t> bits((count + 7) / 8);
            for (size_t i = 0; i < count; ++i) {
                mask[i] = labels[i] == predicates[row];
                bits[i / 8] |= uint8_t(mask[i] << (i % 8));
            }
            const auto* query = queries.data() + row * dimension;
            uint64_t calls = 0;
            std::array<int64_t, k> ids{};
            std::array<float, k> distances{};
            std::vector<unsigned> trace;
#ifdef FAISS_REFERENCE
            faiss::IDSelectorBitmap selector(bits.size(), bits.data());
            faiss::SearchParametersACORN params;
            params.efSearch = ef;
            params.sel = &selector;
            index.acorn.efSearch = ef;
            std::vector<faiss::idx_t> wide_trace;
            sift_distance::Counter counter;
            if (!f.levels.empty()) counter.trace = &wide_trace;
            {
                sift_distance::ScopedCounter scope(index.storage, counter);
                index.search(1, query, k, distances.data(), ids.data(), reinterpret_cast<char*>(mask.data()), &params);
            }
            calls = counter.calls;
            trace.assign(wide_trace.begin(), wide_trace.end());
#else
            auto result = index.search(query, mask.data(), ef, &calls, f.levels.empty() ? nullptr : &trace);
            if (!(result == index.search(query, mask.data(), ef))) throw std::runtime_error("Counting altered results");
            ids = result.ids;
            distances = result.distances;
#endif
            std::cout << "query," << count << ',' << gamma << ',' << prefix << ',' << ef
                      << ',' << row << ',' << calls << ',';
            for (auto id : trace) std::cout << id << ':';
            for (size_t i = 0; i < ids.size(); ++i) {
                if (ids[i] == -1) continue;
                if (ids[i] < 0 || size_t(ids[i]) >= count || !mask[ids[i]]) {
                    throw std::runtime_error("Predicate validation failed");
                }
                std::cout << ',' << ids[i] << ':' << distances[i];
            }
            std::cout << '\n';
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: validate WORKLOAD");
        std::cout << std::setprecision(9);
        const auto first = layered();
        const auto second = two_hops();
        const std::string workload = argv[1];
        const auto base = read_prefix<float>(workload + "/train.f32", 12000 * 128);
        const auto labels = read_prefix<uint8_t>(workload + "/train.labels", 12000);
        const auto queries = read_prefix<float>(workload + "/test.f32", 120 * 128);
        const auto predicates = read_prefix<uint8_t>(workload + "/test.labels", 120);
        for (int gamma : {12, 1}) {
            run(first, first.data, 1, gamma, {1, 1, 1, 1, 2}, {0, 0}, {1, 3});
            run(second, second.data, 1, gamma, {2, 2, 1, 1, 2}, {0, 0}, {1, 3});
            run({{}, {}, {}, -1}, base, 128, gamma, labels, queries, predicates);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
