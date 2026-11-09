#include "asi_index.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <numeric>
#include <queue>
#include <random>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "hnswlib/hnswlib.h"
#include "native_graph_audit.h"
#include "raw_l2_space.h"

namespace {

// One attribute's HNSW graph, kept only while building.
struct AttributeGraph {
    std::vector<uint32_t> members;                       // local id -> object id
    std::vector<std::vector<uint32_t>> local_adjacency;  // layer-0 out-neighbours, local ids
    std::vector<uint32_t> representative_nodes;          // local ids
    uint32_t entry_node = 0;                             // local id used by the representative entry mode
    uint32_t top_layer_entry_node = 0;                   // local id used by the HNSW entry mode
    int top_level = 0;
    std::vector<uint32_t> upper_row_begin;               // local node -> first row of upper layers
    std::vector<uint32_t> upper_row_offset;              // row -> first target index
    std::vector<uint32_t> upper_targets;                 // concatenated upper-layer neighbours
};

// Builds one attribute's HNSW, extracts layer 0, upper layers and representatives.
void build_attribute_graph(const float* base, int dim, const std::vector<uint32_t>& members,
                           const BuildOptions& options, int random_seed, int attribute_id,
                           FILE* audit_file, AttributeGraph& graph) {
    const int member_count = static_cast<int>(members.size());
    graph.members = members;
    graph.local_adjacency.assign(size_t(member_count), {});
    if (member_count == 0) return;
    if (member_count == 1) {   // hnswlib needs no graph for a single object
        graph.representative_nodes = {0};
        graph.entry_node = 0;
        graph.top_layer_entry_node = 0;
        graph.top_level = 0;
        graph.upper_row_begin.assign(2, 0);
        graph.upper_row_offset.assign(1, 0);
        return;
    }

    RawL2Space space{size_t(dim)};
    hnswlib::HierarchicalNSW<float> hnsw(&space, size_t(member_count),
                                         size_t(options.max_degree), options.construction_width,
                                         random_seed);
    for (int i = 0; i < member_count; ++i)
        hnsw.addPoint(base + size_t(members[size_t(i)]) * size_t(dim), hnswlib::labeltype(i));

    if (audit_file) write_native_graph_audit(audit_file, attribute_id, hnsw);

    graph.top_layer_entry_node = hnsw.enterpoint_node_;
    graph.top_level = hnsw.maxlevel_;
    graph.upper_row_begin.resize(size_t(member_count) + 1);
    graph.upper_row_offset.assign(1, 0);
    for (int i = 0; i < member_count; ++i) {
        graph.upper_row_begin[size_t(i)] = uint32_t(graph.upper_row_offset.size() - 1);
        for (int level = 1; level <= hnsw.element_levels_[size_t(i)]; ++level) {
            auto* links = hnsw.get_linklist(hnswlib::tableint(i), level);
            const size_t neighbour_count = hnsw.getListCount(links);
            const auto* neighbours = reinterpret_cast<const hnswlib::tableint*>(links + 1);
            graph.upper_targets.insert(graph.upper_targets.end(), neighbours,
                                       neighbours + neighbour_count);
            graph.upper_row_offset.push_back(uint32_t(graph.upper_targets.size()));
        }
    }
    graph.upper_row_begin[size_t(member_count)] = uint32_t(graph.upper_row_offset.size() - 1);

    // layer 0 as a directed adjacency list
    for (int i = 0; i < member_count; ++i) {
        auto* links = hnsw.get_linklist0(hnswlib::tableint(i));
        const size_t neighbour_count = hnsw.getListCount(links);
        auto& out = graph.local_adjacency[size_t(i)];
        out.reserve(neighbour_count);
        const auto* neighbours = reinterpret_cast<const hnswlib::tableint*>(links + 1);
        for (size_t j = 0; j < neighbour_count; ++j) {
            const int neighbour = int(neighbours[j]);
            if (neighbour >= 0 && neighbour < member_count && neighbour != i)
                out.push_back(uint32_t(neighbour));
        }
    }

    // representatives: farthest-first from local id 0.
    // rule 0: one per p objects, capped by Kcap.  rule 1: round(sqrt(n)), uncapped.
    const int representative_target = [&]() {
        if (options.representative_rule == 1) {
            const int root = int(std::lround(std::sqrt(double(member_count))));
            return std::max(1, std::min(member_count, root));
        }
        return std::max(1, std::min({options.representative_cap, member_count}));
    }();
    graph.representative_nodes.clear();
    graph.representative_nodes.push_back(0);
    std::vector<float> nearest_representative_distance(size_t(member_count), INFINITY);
    for (int r = 1; r < representative_target; ++r) {
        const uint32_t last = graph.representative_nodes.back();
        const float* last_vector = base + size_t(members[size_t(last)]) * size_t(dim);
        float farthest = -1;
        uint32_t farthest_id = 0;
        for (int i = 0; i < member_count; ++i) {
            const float d = space.dist(last_vector, base + size_t(members[size_t(i)]) * size_t(dim));
            if (d < nearest_representative_distance[size_t(i)])
                nearest_representative_distance[size_t(i)] = d;
            if (nearest_representative_distance[size_t(i)] > farthest) {
                farthest = nearest_representative_distance[size_t(i)];
                farthest_id = uint32_t(i);
            }
        }
        graph.representative_nodes.push_back(farthest_id);
    }

    // minimum spanning tree over representatives, added to layer 0 both ways
    const int representative_count = int(graph.representative_nodes.size());
    if (representative_count >= 2) {
        std::vector<char> in_tree(size_t(representative_count), 0);
        std::vector<float> best_key(size_t(representative_count), INFINITY);
        std::vector<int> tree_parent(size_t(representative_count), -1);
        best_key[0] = 0;
        for (int step = 0; step < representative_count; ++step) {
            int next = -1;
            for (int c = 0; c < representative_count; ++c)
                if (!in_tree[size_t(c)] && (next < 0 || best_key[size_t(c)] < best_key[size_t(next)]))
                    next = c;
            in_tree[size_t(next)] = 1;
            for (int c = 0; c < representative_count; ++c) {
                if (in_tree[size_t(c)]) continue;
                const float d = space.dist(
                    base + size_t(members[size_t(graph.representative_nodes[size_t(next)])]) * size_t(dim),
                    base + size_t(members[size_t(graph.representative_nodes[size_t(c)])]) * size_t(dim));
                if (d < best_key[size_t(c)]) { best_key[size_t(c)] = d; tree_parent[size_t(c)] = next; }
            }
        }
        for (int c = 1; c < representative_count; ++c) {
            if (tree_parent[size_t(c)] < 0) continue;
            const uint32_t a = graph.representative_nodes[size_t(c)];
            const uint32_t b = graph.representative_nodes[size_t(tree_parent[size_t(c)])];
            graph.local_adjacency[a].push_back(b);
            graph.local_adjacency[b].push_back(a);
        }
    }
}

// Nearest member of `graph` to `from_vector`, as a local id.
uint32_t nearest_member(const float* base, int dim, const AttributeGraph& graph,
                        const float* from_vector, const RawL2Space& space) {
    float best = INFINITY;
    uint32_t best_local = 0;
    for (size_t j = 0; j < graph.members.size(); ++j) {
        const float d = space.dist(from_vector, base + size_t(graph.members[j]) * size_t(dim));
        if (d < best) { best = d; best_local = uint32_t(j); }
    }
    return best_local;
}

// `k` nearest members of `graph` to `from_vector`, as local ids.
std::vector<uint32_t> nearest_members(const float* base, int dim, const AttributeGraph& graph,
                                      const float* from_vector, const RawL2Space& space, int k) {
    std::priority_queue<std::pair<float, uint32_t>> best;   // max-heap
    for (size_t j = 0; j < graph.members.size(); ++j) {
        const float d = space.dist(from_vector, base + size_t(graph.members[j]) * size_t(dim));
        if (int(best.size()) < k) best.push({d, uint32_t(j)});
        else if (d < best.top().first) { best.pop(); best.push({d, uint32_t(j)}); }
    }
    std::vector<uint32_t> out;
    out.reserve(best.size());
    while (!best.empty()) { out.push_back(best.top().second); best.pop(); }
    return out;
}

void write_raw(FILE* out, const void* data, size_t bytes) {
    if (bytes) std::fwrite(data, 1, bytes, out);
}

template <typename T>
void write_vector(FILE* out, const std::vector<T>& values) {
    const uint64_t count = values.size();
    write_raw(out, &count, sizeof(count));
    write_raw(out, values.data(), values.size() * sizeof(T));
}

template <typename T>
bool read_vector(FILE* in, std::vector<T>& values) {
    uint64_t count = 0;
    if (std::fread(&count, sizeof(count), 1, in) != 1) return false;
    values.resize(size_t(count));
    return count == 0 || std::fread(values.data(), sizeof(T), size_t(count), in) == size_t(count);
}

bool read_raw(FILE* in, void* data, size_t bytes) {
    return bytes == 0 || std::fread(data, 1, bytes, in) == bytes;
}

}  // namespace


// k-means over a set of global node ids. Returns one global node id per centroid:
// the original vector nearest to that centroid, so representatives are real nodes.
// Centroids are trained on a bounded sample to keep the build linear in k*|sample|.
std::vector<uint32_t> kmeans_representatives(const float* base, int dim,
                                             const std::vector<uint32_t>& nodes,
                                             int k, uint64_t seed, const RawL2Space& space) {
    std::vector<uint32_t> reps;
    if (nodes.empty() || k <= 0) return reps;
    if (int(nodes.size()) <= k) {
        reps = nodes;
        std::sort(reps.begin(), reps.end());
        reps.erase(std::unique(reps.begin(), reps.end()), reps.end());
        return reps;
    }

    std::mt19937_64 rng(seed);
    const size_t sample_target = std::min(nodes.size(), std::max<size_t>(2048, size_t(k) * 16));
    std::vector<uint32_t> sample;
    if (sample_target >= nodes.size()) {
        sample = nodes;
    } else {
        std::vector<uint32_t> index(nodes.size());
        std::iota(index.begin(), index.end(), 0u);
        for (size_t i = 0; i < sample_target; ++i) {
            std::uniform_int_distribution<size_t> pick(i, index.size() - 1);
            std::swap(index[i], index[pick(rng)]);
            sample.push_back(nodes[index[i]]);
        }
    }
    const size_t s_count = sample.size();

    std::vector<std::vector<float>> centroid(size_t(k), std::vector<float>(size_t(dim), 0.0f));
    {
        std::vector<size_t> pick_idx(s_count);
        std::iota(pick_idx.begin(), pick_idx.end(), 0u);
        for (int c = 0; c < k; ++c) {
            std::uniform_int_distribution<size_t> pick(size_t(c), s_count - 1);
            const size_t chosen = pick(rng);
            std::swap(pick_idx[size_t(c)], pick_idx[chosen]);
            const float* v = base + size_t(sample[pick_idx[size_t(c)]]) * size_t(dim);
            std::copy(v, v + dim, centroid[size_t(c)].begin());
        }
    }

    std::vector<int> assignment(s_count, 0);
    const int iterations = 4;
    for (int it = 0; it < iterations; ++it) {
        for (size_t i = 0; i < s_count; ++i) {
            const float* v = base + size_t(sample[i]) * size_t(dim);
            float best = std::numeric_limits<float>::infinity();
            int best_c = 0;
            for (int c = 0; c < k; ++c) {
                const float d = space.dist(v, centroid[size_t(c)].data());
                if (d < best) { best = d; best_c = c; }
            }
            assignment[i] = best_c;
        }
        std::vector<std::vector<double>> sum(size_t(k), std::vector<double>(size_t(dim), 0.0));
        std::vector<size_t> count(size_t(k), 0);
        for (size_t i = 0; i < s_count; ++i) {
            const float* v = base + size_t(sample[i]) * size_t(dim);
            const int c = assignment[i];
            for (int d = 0; d < dim; ++d) sum[size_t(c)][size_t(d)] += double(v[d]);
            ++count[size_t(c)];
        }
        for (int c = 0; c < k; ++c) {
            if (!count[size_t(c)]) continue;
            for (int d = 0; d < dim; ++d)
                centroid[size_t(c)][size_t(d)] = float(sum[size_t(c)][size_t(d)] / double(count[size_t(c)]));
        }
    }

    reps.reserve(size_t(k));
    for (int c = 0; c < k; ++c) {
        float best = std::numeric_limits<float>::infinity();
        uint32_t best_node = sample[0];
        for (size_t i = 0; i < s_count; ++i) {
            const float d = space.dist(centroid[size_t(c)].data(),
                                       base + size_t(sample[i]) * size_t(dim));
            if (d < best) { best = d; best_node = sample[i]; }
        }
        reps.push_back(best_node);
    }
    std::sort(reps.begin(), reps.end());
    reps.erase(std::unique(reps.begin(), reps.end()), reps.end());
    return reps;
}

AsiIndex build_index(const Dataset& dataset, const BuildOptions& options) {
    const size_t object_count = dataset.num_objects();
    const int dim = dataset.dim;

    AsiIndex index;
    index.object_count = object_count;
    index.attribute_ids = collect_attribute_ids(dataset);
    const int attribute_count = int(index.attribute_ids.size());
    index.attribute_count = attribute_count;

    std::unordered_map<int, int> attribute_position;  // external id -> index
    for (int i = 0; i < attribute_count; ++i)
        attribute_position[index.attribute_ids[size_t(i)]] = i;

    // objects grouped by owning attribute
    std::vector<std::vector<uint32_t>> attribute_members(static_cast<size_t>(attribute_count));
    for (size_t object = 0; object < object_count; ++object) {
        const auto found = attribute_position.find(dataset.owner[object]);
        if (found != attribute_position.end())
            attribute_members[size_t(found->second)].push_back(uint32_t(object));
    }

    // 1. per-attribute graphs
    const auto build_started = std::chrono::steady_clock::now();
    std::vector<AttributeGraph> attribute_graphs(static_cast<size_t>(attribute_count));
    for (int a = 0; a < attribute_count; ++a) {
        build_attribute_graph(dataset.base.data(), dim, attribute_members[size_t(a)], options,
                              100 + index.attribute_ids[size_t(a)], index.attribute_ids[size_t(a)],
                              nullptr, attribute_graphs[size_t(a)]);
    }
    std::fprintf(stderr, "[build] %d attribute graphs in %.1fs (M=%d efC=%d Kcap=%d)\n",
                 attribute_count,
                 std::chrono::duration<double>(std::chrono::steady_clock::now() - build_started).count(),
                 options.max_degree, options.construction_width, options.representative_cap);

    // 2. global node numbering
    index.node_begin.assign(size_t(attribute_count) + 1, 0);
    size_t total_nodes = 0;
    for (int a = 0; a < attribute_count; ++a) {
        index.node_begin[size_t(a)] = uint32_t(total_nodes);
        total_nodes += attribute_graphs[size_t(a)].members.size();
    }
    index.node_begin[size_t(attribute_count)] = uint32_t(total_nodes);

    index.node_attribute.assign(total_nodes, 0);
    index.node_object.assign(total_nodes, 0);
    index.object_node.assign(object_count, 0);
    for (int a = 0; a < attribute_count; ++a) {
        for (size_t j = 0; j < attribute_graphs[size_t(a)].members.size(); ++j) {
            const uint32_t node = index.node_begin[size_t(a)] + uint32_t(j);
            index.node_attribute[node] = uint32_t(a);
            index.node_object[node] = attribute_graphs[size_t(a)].members[j];
            index.object_node[attribute_graphs[size_t(a)].members[j]] = node;
        }
    }

    // 3. local edges and representatives
    std::vector<std::vector<uint32_t>> local_edges(total_nodes);
    for (int a = 0; a < attribute_count; ++a) {
        const uint32_t offset = index.node_begin[size_t(a)];
        for (size_t j = 0; j < attribute_graphs[size_t(a)].local_adjacency.size(); ++j)
            for (uint32_t target : attribute_graphs[size_t(a)].local_adjacency[j])
                local_edges[offset + j].push_back(offset + target);
    }

    index.is_representative.assign(total_nodes, 0);
    index.is_gateway.assign(total_nodes, 0);
    index.representative_nodes.resize(size_t(attribute_count));
    index.entry_node.resize(size_t(attribute_count));
    index.hnsw_entry_node.resize(size_t(attribute_count));
    index.top_level.resize(size_t(attribute_count));
    index.upper_row_begin.resize(size_t(attribute_count));
    index.upper_row_offset.resize(size_t(attribute_count));
    index.upper_targets.resize(size_t(attribute_count));
    for (int a = 0; a < attribute_count; ++a) {
        const AttributeGraph& graph = attribute_graphs[size_t(a)];
        index.representative_nodes[size_t(a)] = graph.representative_nodes;
        index.entry_node[size_t(a)] = graph.entry_node;
        index.hnsw_entry_node[size_t(a)] = graph.top_layer_entry_node;
        index.top_level[size_t(a)] = graph.top_level;
        index.upper_row_begin[size_t(a)] = graph.upper_row_begin;
        index.upper_row_offset[size_t(a)] = graph.upper_row_offset;
        index.upper_targets[size_t(a)] = graph.upper_targets;
        for (uint32_t local : graph.representative_nodes)
            index.is_representative[index.node_begin[size_t(a)] + local] = 1;
        index.upper_layer_bytes += sizeof(uint32_t) * (graph.upper_row_begin.size() +
                                                       graph.upper_row_offset.size() +
                                                       graph.upper_targets.size());
    }

    // 4. merge the attribute subgraphs into one object graph.
    //
    //   representative_rule == 2 (clustered merge):
    //     attributes are processed in reverse topological order. A parent
    //     accumulates its own members plus one child subtree at a time; after
    //     each merge the accumulated set is re-clustered into
    //     round(sqrt(N)) cells, and every cell contributes the original vector
    //     nearest to its centroid as a representative. Representatives are
    //     therefore real nodes. Consecutive levels are linked, which forms the
    //     merge skeleton: coarse representatives of a larger accumulated set
    //     point at the finer representatives of the sets they were built from.
    //
    //   representative_rule == 0/1 (gateway merge):
    //     each parent representative connects to the nearest m members of the
    //     child attribute, and a repair pass gives every attribute at least one
    //     cross-attribute in-edge.
    std::vector<std::vector<uint32_t>> bridge_edges(total_nodes);
    long gateway_edge_count = 0;
    long repaired_edge_count = 0;
    {
        RawL2Space space{size_t(dim)};
        if (options.representative_rule == 2) {
            std::vector<std::vector<int>> children{std::vector<std::vector<int>>(size_t(attribute_count))};
            std::vector<int> parent_count(size_t(attribute_count), 0);
            for (int a = 0; a < attribute_count; ++a) {
                const auto found = dataset.parents_of.find(index.attribute_ids[size_t(a)]);
                if (found == dataset.parents_of.end()) continue;
                for (int parent : found->second) {
                    const auto position = attribute_position.find(parent);
                    if (position == attribute_position.end()) continue;
                    children[size_t(position->second)].push_back(a);
                    ++parent_count[size_t(a)];
                }
            }

            // post-order (reverse topological): every child before its parent
            std::vector<int> order;
            order.reserve(size_t(attribute_count));
            {
                std::vector<char> state(size_t(attribute_count), 0);
                std::function<void(int)> visit = [&](int a) {
                    if (state[size_t(a)]) return;
                    state[size_t(a)] = 1;
                    for (int c : children[size_t(a)]) visit(c);
                    order.push_back(a);
                };
                for (int a = 0; a < attribute_count; ++a)
                    if (parent_count[size_t(a)] == 0) visit(a);
                for (int a = 0; a < attribute_count; ++a) visit(a);
            }

            std::vector<std::vector<uint32_t>> subtree_nodes{std::vector<std::vector<uint32_t>>(size_t(attribute_count))};
            std::vector<std::vector<uint32_t>> subtree_reps{std::vector<std::vector<uint32_t>>(size_t(attribute_count))};
            long cluster_steps = 0;
            long linked_edges = 0;

            for (int a : order) {
                std::vector<uint32_t> accumulated;
                for (uint32_t node = index.node_begin[size_t(a)]; node < index.node_begin[size_t(a) + 1]; ++node)
                    accumulated.push_back(node);

                std::vector<uint32_t> previous_level;
                if (children[size_t(a)].empty()) {
                    if (!accumulated.empty()) {
                        const int cells = std::max(1, int(std::lround(std::sqrt(double(accumulated.size())))));
                        previous_level = kmeans_representatives(dataset.base.data(), dim, accumulated,
                                                                cells, 1000003ull * uint64_t(a + 1), space);
                        ++cluster_steps;
                    }
                }
                for (int c : children[size_t(a)]) {
                    accumulated.insert(accumulated.end(), subtree_nodes[size_t(c)].begin(),
                                       subtree_nodes[size_t(c)].end());
                    if (accumulated.empty()) continue;
                    const int cells = std::max(1, int(std::lround(std::sqrt(double(accumulated.size())))));
                    std::vector<uint32_t> level = kmeans_representatives(
                        dataset.base.data(), dim, accumulated, cells,
                        1000003ull * uint64_t(a + 1) * 31ull + uint64_t(c + 7), space);
                    ++cluster_steps;

                    // the pool a coarser representative may descend through:
                    // the previous level plus the representatives of the child
                    // that was just absorbed
                    std::vector<uint32_t> pool = previous_level;
                    pool.insert(pool.end(), subtree_reps[size_t(c)].begin(), subtree_reps[size_t(c)].end());
                    if (!pool.empty()) {
                        for (uint32_t from : level) {
                            const float* from_vector =
                                dataset.base.data() + size_t(index.node_object[from]) * size_t(dim);
                            // nearest m pool nodes
                            const int m = std::max(1, options.bridge_receivers);
                            std::vector<std::pair<float, uint32_t>> best;
                            best.reserve(size_t(m));
                            for (uint32_t to : pool) {
                                const float d = space.dist(
                                    from_vector,
                                    dataset.base.data() + size_t(index.node_object[to]) * size_t(dim));
                                if (int(best.size()) < m) {
                                    best.push_back({d, to});
                                } else {
                                    size_t worst = 0;
                                    for (size_t j = 1; j < best.size(); ++j)
                                        if (best[j].first > best[worst].first) worst = j;
                                    if (d < best[worst].first) best[worst] = {d, to};
                                }
                            }
                            for (const auto& edge : best) {
                                bridge_edges[size_t(from)].push_back(edge.second);
                                bridge_edges[size_t(edge.second)].push_back(from);
                                index.is_gateway[size_t(from)] = 1;
                                index.is_gateway[size_t(edge.second)] = 1;
                                ++linked_edges;
                            }
                        }
                    }
                    previous_level = std::move(level);
                }
                subtree_reps[size_t(a)] = previous_level;
                subtree_nodes[size_t(a)] = std::move(accumulated);
            }
            for (int a = 0; a < attribute_count; ++a) {
                const uint32_t begin = index.node_begin[size_t(a)];
                const uint32_t end = index.node_begin[size_t(a) + 1];
                for (uint32_t node : subtree_reps[size_t(a)])
                    if (node >= begin && node < end) {
                        index.representative_nodes[size_t(a)].push_back(node - begin);
                        index.is_representative[node] = 1;
                    }
            }
            for (int a = 0; a < attribute_count; ++a)
                if (parent_count[size_t(a)] == 0 && index.skeleton_top.empty())
                    index.skeleton_top = subtree_reps[size_t(a)];
            std::fprintf(stderr, "[merge] clustered: %ld clusterings, %ld skeleton edges, %zu top representatives\n",
                         cluster_steps, linked_edges, index.skeleton_top.size());
        } else {
            // gateway merge (rules 0/1), unchanged
            std::vector<int> attribute_order;
            long gateway_edge_count = 0;
            if (options.merge_attributes) {
                std::vector<int> children_processed(size_t(attribute_count), 0);
                for (int a = 0; a < attribute_count; ++a) {
                    const auto found = dataset.children_of.find(index.attribute_ids[size_t(a)]);
                    children_processed[size_t(a)] = found == dataset.children_of.end() ? 0 : int(found->second.size());
                }
                // topological order (parents first), then reversed to process children first
                std::vector<int> order;
                {
                    std::vector<int> remaining_parents(size_t(attribute_count), 0);
                    for (int a = 0; a < attribute_count; ++a) {
                        const auto found = dataset.parents_of.find(index.attribute_ids[size_t(a)]);
                        if (found == dataset.parents_of.end()) continue;
                        for (int parent : found->second)
                            if (attribute_position.count(parent)) ++remaining_parents[size_t(a)];
                    }
                    std::vector<int> ready;
                    for (int a = 0; a < attribute_count; ++a)
                        if (remaining_parents[size_t(a)] == 0) ready.push_back(a);
                    for (size_t head = 0; head < ready.size(); ++head) {
                        const int a = ready[head];
                        order.push_back(a);
                        const auto found = dataset.children_of.find(index.attribute_ids[size_t(a)]);
                        if (found == dataset.children_of.end()) continue;
                        for (int child : found->second) {
                            const auto position = attribute_position.find(child);
                            if (position == attribute_position.end()) continue;
                            if (--remaining_parents[size_t(position->second)] == 0)
                                ready.push_back(position->second);
                        }
                    }
                }
                std::reverse(order.begin(), order.end());

                RawL2Space space{size_t(dim)};
                for (int child : order) {
                    if (attribute_graphs[size_t(child)].members.empty()) continue;
                    // candidate parents: nearest ancestors that actually own objects
                    std::vector<int> candidate_parents;
                    {
                        std::vector<int> frontier;
                        const auto found = dataset.parents_of.find(index.attribute_ids[size_t(child)]);
                        if (found != dataset.parents_of.end())
                            for (int parent : found->second)
                                if (attribute_position.count(parent))
                                    frontier.push_back(attribute_position[parent]);
                        std::unordered_set<int> visited;
                        for (int level = 0; level < 8 && !frontier.empty(); ++level) {
                            std::vector<int> next;
                            for (int a : frontier) {
                                if (!visited.insert(a).second) continue;
                                if (!attribute_graphs[size_t(a)].members.empty()) {
                                    candidate_parents.push_back(a);
                                    continue;
                                }
                                const auto up = dataset.parents_of.find(index.attribute_ids[size_t(a)]);
                                if (up == dataset.parents_of.end()) continue;
                                for (int grand_parent : up->second)
                                    if (attribute_position.count(grand_parent))
                                        next.push_back(attribute_position[grand_parent]);
                            }
                            frontier.swap(next);
                        }
                    }
                    for (int parent : candidate_parents) {
                        for (uint32_t parent_rep : attribute_graphs[size_t(parent)].representative_nodes) {
                            const uint32_t parent_node = index.node_begin[size_t(parent)] + parent_rep;
                            const float* parent_vector =
                                dataset.base.data() + size_t(attribute_graphs[size_t(parent)].members[parent_rep]) * size_t(dim);
                            const auto receivers = nearest_members(dataset.base.data(), dim,
                                attribute_graphs[size_t(child)], parent_vector, space,
                                std::max(1, options.bridge_receivers));
                            for (uint32_t child_local : receivers) {
                                const uint32_t child_node = index.node_begin[size_t(child)] + child_local;
                                bridge_edges[parent_node].push_back(child_node);
                                bridge_edges[child_node].push_back(parent_node);
                                index.is_gateway[child_node] = 1;
                                ++gateway_edge_count;
                            }
                        }
                    }
                }
            }

            // 5. repair: an attribute with no cross-attribute in-edge gets one to a parent representative
            long repaired_edge_count = 0;
            if (options.merge_attributes) {
                std::vector<uint32_t> representative_attribute(total_nodes, UINT32_MAX);
                for (int a = 0; a < attribute_count; ++a)
                    for (uint32_t local : attribute_graphs[size_t(a)].representative_nodes)
                        representative_attribute[index.node_begin[size_t(a)] + local] = uint32_t(a);

                std::vector<long> cross_in_edges(size_t(attribute_count), 0);
                for (size_t node = 0; node < total_nodes; ++node) {
                    if (representative_attribute[node] == UINT32_MAX) continue;
                    for (uint32_t target : bridge_edges[node])
                        if (representative_attribute[target] != UINT32_MAX &&
                            representative_attribute[target] != representative_attribute[node])
                            ++cross_in_edges[representative_attribute[target]];
                }
                for (int a = 0; a < attribute_count; ++a) {
                    if (attribute_graphs[size_t(a)].members.empty() || cross_in_edges[size_t(a)] > 0) continue;
                    int ancestor = -1;
                    {
                        std::vector<int> frontier;
                        const auto found = dataset.parents_of.find(index.attribute_ids[size_t(a)]);
                        if (found != dataset.parents_of.end())
                            for (int parent : found->second)
                                if (attribute_position.count(parent)) frontier.push_back(attribute_position[parent]);
                        std::unordered_set<int> visited;
                        for (int level = 0; level < 8 && !frontier.empty() && ancestor < 0; ++level) {
                            std::vector<int> next;
                            for (int candidate : frontier) {
                                if (!visited.insert(candidate).second) continue;
                                if (!attribute_graphs[size_t(candidate)].members.empty()) { ancestor = candidate; break; }
                                const auto up = dataset.parents_of.find(index.attribute_ids[size_t(candidate)]);
                                if (up == dataset.parents_of.end()) continue;
                                for (int grand_parent : up->second)
                                    if (attribute_position.count(grand_parent))
                                        next.push_back(attribute_position[grand_parent]);
                            }
                            frontier.swap(next);
                        }
                    }
                    if (ancestor < 0) continue;
                    for (uint32_t local : attribute_graphs[size_t(a)].representative_nodes) {
                        for (uint32_t ancestor_rep : attribute_graphs[size_t(ancestor)].representative_nodes) {
                            const uint32_t node = index.node_begin[size_t(a)] + local;
                            const uint32_t ancestor_node = index.node_begin[size_t(ancestor)] + ancestor_rep;
                            bridge_edges[ancestor_node].push_back(node);
                            bridge_edges[node].push_back(ancestor_node);
                            index.is_gateway[node] = 1;
                            ++repaired_edge_count;
                        }
                    }
                }
            }
            std::fprintf(stderr, "[merge] %ld cross-attribute edges (+%ld repaired)\n",
                         gateway_edge_count, repaired_edge_count);

        }
    }

    // 6. flatten adjacency into CSR
    index.local_edge_begin.assign(total_nodes + 1, 0);
    index.bridge_edge_begin.assign(total_nodes + 1, 0);
    {
        uint32_t local_total = 0, bridge_total = 0;
        for (size_t node = 0; node < total_nodes; ++node) {
            index.local_edge_begin[node] = local_total;
            local_total += uint32_t(local_edges[node].size());
            index.bridge_edge_begin[node] = bridge_total;
            bridge_total += uint32_t(bridge_edges[node].size());
        }
        index.local_edge_begin[total_nodes] = local_total;
        index.bridge_edge_begin[total_nodes] = bridge_total;
        index.local_edge_targets.resize(local_total);
        index.bridge_edge_targets.resize(bridge_total);
        uint32_t local_write = 0, bridge_write = 0;
        for (size_t node = 0; node < total_nodes; ++node) {
            for (uint32_t target : local_edges[node]) index.local_edge_targets[local_write++] = target;
            for (uint32_t target : bridge_edges[node]) index.bridge_edge_targets[bridge_write++] = target;
        }
        std::fprintf(stderr, "[graph] %zu nodes, %u local edges (%.1f/node), %u bridge edges\n",
                     total_nodes, local_total, double(local_total) / double(total_nodes), bridge_total);
    }

    // 7. descendant closure bitmap per attribute
    index.closure_words = (attribute_count + 63) / 64;
    index.closure_bits.assign(size_t(attribute_count) * size_t(index.closure_words), 0);
    {
        std::vector<int> state(size_t(attribute_count), 0);
        std::function<void(int)> visit = [&](int a) {
            if (state[size_t(a)] == 2) return;
            state[size_t(a)] = 1;
            const auto found = dataset.children_of.find(index.attribute_ids[size_t(a)]);
            if (found != dataset.children_of.end())
                for (int child : found->second) {
                    const auto position = attribute_position.find(child);
                    if (position == attribute_position.end()) continue;
                    visit(position->second);
                    for (int w = 0; w < index.closure_words; ++w)
                        index.closure_bits[size_t(a) * size_t(index.closure_words) + size_t(w)] |=
                            index.closure_bits[size_t(position->second) * size_t(index.closure_words) + size_t(w)];
                }
            index.closure_bits[size_t(a) * size_t(index.closure_words) + size_t(a) / 64] |=
                (uint64_t(1) << (size_t(a) % 64));
            state[size_t(a)] = 2;
        };
        for (int a = 0; a < attribute_count; ++a) visit(a);
    }

    return index;
}

bool save_index(const AsiIndex& index, const std::string& path, std::string* error) {
    FILE* out = std::fopen(path.c_str(), "wb");
    if (!out) { if (error) *error = "cannot open " + path; return false; }
    const uint64_t magic = 0x4153493030310001ull;  // "ASI001"
    write_raw(out, &magic, sizeof(magic));
    int header[3] = {index.attribute_count, index.closure_words, int(index.object_count)};
    write_raw(out, header, sizeof(header));
    write_vector(out, index.attribute_ids);
    write_vector(out, index.node_begin);
    write_vector(out, index.node_attribute);
    write_vector(out, index.node_object);
    write_vector(out, index.object_node);
    write_vector(out, index.local_edge_begin);
    write_vector(out, index.local_edge_targets);
    write_vector(out, index.bridge_edge_begin);
    write_vector(out, index.bridge_edge_targets);
    write_vector(out, index.closure_bits);
    write_vector(out, index.is_representative);
    write_vector(out, index.is_gateway);
    write_vector(out, index.skeleton_top);
    write_vector(out, index.entry_node);
    write_vector(out, index.hnsw_entry_node);
    write_vector(out, index.top_level);
    for (int a = 0; a < index.attribute_count; ++a) {
        write_vector(out, index.representative_nodes[size_t(a)]);
        write_vector(out, index.upper_row_begin[size_t(a)]);
        write_vector(out, index.upper_row_offset[size_t(a)]);
        write_vector(out, index.upper_targets[size_t(a)]);
    }
    const bool ok = std::ferror(out) == 0;
    std::fclose(out);
    if (!ok && error) *error = "write error on " + path;
    return ok;
}

bool load_index(AsiIndex& index, const std::string& path, std::string* error) {
    FILE* in = std::fopen(path.c_str(), "rb");
    if (!in) { if (error) *error = "cannot open " + path; return false; }
    auto fail = [&](const char* message) { if (error) *error = std::string(message) + " in " + path; std::fclose(in); return false; };
    uint64_t magic = 0;
    if (std::fread(&magic, sizeof(magic), 1, in) != 1 || magic != 0x4153493030310001ull)
        return fail("bad magic");
    int header[3] = {0, 0, 0};
    if (std::fread(header, sizeof(header), 1, in) != 1) return fail("bad header");
    index.attribute_count = header[0];
    index.closure_words = header[1];
    index.object_count = size_t(header[2]);
    if (!read_vector(in, index.attribute_ids)) return fail("attribute ids");
    if (!read_vector(in, index.node_begin)) return fail("node begin");
    if (!read_vector(in, index.node_attribute)) return fail("node attribute");
    if (!read_vector(in, index.node_object)) return fail("node object");
    if (!read_vector(in, index.object_node)) return fail("object node");
    if (!read_vector(in, index.local_edge_begin)) return fail("local edges");
    if (!read_vector(in, index.local_edge_targets)) return fail("local targets");
    if (!read_vector(in, index.bridge_edge_begin)) return fail("bridge edges");
    if (!read_vector(in, index.bridge_edge_targets)) return fail("bridge targets");
    if (!read_vector(in, index.closure_bits)) return fail("closure bits");
    if (!read_vector(in, index.is_representative)) return fail("representative flags");
    if (!read_vector(in, index.is_gateway)) return fail("gateway flags");
    if (!read_vector(in, index.skeleton_top)) return fail("skeleton top");
    if (!read_vector(in, index.entry_node)) return fail("entry nodes");
    if (!read_vector(in, index.hnsw_entry_node)) return fail("hnsw entry nodes");
    if (!read_vector(in, index.top_level)) return fail("top levels");
    index.representative_nodes.assign(size_t(index.attribute_count), {});
    index.upper_row_begin.assign(size_t(index.attribute_count), {});
    index.upper_row_offset.assign(size_t(index.attribute_count), {});
    index.upper_targets.assign(size_t(index.attribute_count), {});
    for (int a = 0; a < index.attribute_count; ++a) {
        if (!read_vector(in, index.representative_nodes[size_t(a)])) return fail("representatives");
        if (!read_vector(in, index.upper_row_begin[size_t(a)])) return fail("upper rows");
        if (!read_vector(in, index.upper_row_offset[size_t(a)])) return fail("upper offsets");
        if (!read_vector(in, index.upper_targets[size_t(a)])) return fail("upper targets");
    }
    std::fclose(in);
    index.upper_layer_bytes = 0;
    for (int a = 0; a < index.attribute_count; ++a)
        index.upper_layer_bytes += sizeof(uint32_t) * (index.upper_row_begin[size_t(a)].size() +
                                                       index.upper_row_offset[size_t(a)].size() +
                                                       index.upper_targets[size_t(a)].size());
    return true;
}
