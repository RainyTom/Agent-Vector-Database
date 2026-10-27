#pragma once

#include <faiss/MetricType.h>
#include <faiss/utils/distances.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

// Static, single-binding squared-L2 index. Bindings are (global entry ID, node),
// and base is row-major with the global entry ID as its row number. A forest is
// allowed; node numbering need not be topological. Local vectors precede child
// subtrees in each DFS run. Every binding has exactly one physical vector copy;
// unbound base rows have none. The caller's base and children remain unchanged.
// Duplicate global IDs (including repeated identical bindings) are rejected.
//
// Search assumes finite vector components and representable squared distances.
// Modes 0 and 1 exhaustively scan the requested run; their float accumulation
// orders can differ. Mode 2 is classical partial-distance early abandonment
// with a deliberately conservative float tolerance, not a new theoretical
// guarantee or a promise of bitwise equivalence to an exhaustive FAISS scan.
// All search state lives on the stack or in caller-owned result buffers, so
// simultaneous const searches with separate output buffers are supported.
class ScopeRunIndex {
public:
    ScopeRunIndex(const std::vector<float>& base,
                  int dim,
                  const std::vector<std::vector<int>>& children,
                  const std::vector<std::pair<int, int>>& bindings) {
        if (dim <= 0 || base.size() % static_cast<size_t>(dim) != 0)
            throw std::invalid_argument("ScopeRunIndex: invalid base dimensions");
        dim_ = static_cast<size_t>(dim);
        count_ = bindings.size();
        if (count_ > std::numeric_limits<size_t>::max() / dim_ /
                             sizeof(float))
            throw std::length_error("ScopeRunIndex: packed payload too large");

        const size_t nodes = children.size();
        const size_t base_rows = base.size() / dim_;
        runs_.resize(nodes);
        std::vector<unsigned char> bound(base_rows, 0);
        for (const auto& binding : bindings) {
            const int id = binding.first;
            const int node = binding.second;
            if (id < 0 || static_cast<size_t>(id) >= base_rows || node < 0 ||
                static_cast<size_t>(node) >= nodes)
                throw std::invalid_argument("ScopeRunIndex: invalid binding");
            if (bound[static_cast<size_t>(id)])
                throw std::invalid_argument(
                        "ScopeRunIndex: duplicate global ID; single binding required");
            bound[static_cast<size_t>(id)] = 1;
            ++runs_[static_cast<size_t>(node)].local_end;
        }

        std::vector<unsigned char> has_parent(nodes, 0);
        for (const auto& list : children) {
            for (int child : list) {
                if (child < 0 || static_cast<size_t>(child) >= nodes)
                    throw std::invalid_argument("ScopeRunIndex: invalid child");
                if (has_parent[static_cast<size_t>(child)])
                    throw std::invalid_argument(
                            "ScopeRunIndex: child has multiple incoming edges");
                has_parent[static_cast<size_t>(child)] = 1;
            }
        }

        struct Frame {
            size_t node;
            size_t next_child;
        };
        std::vector<Frame> stack;
        stack.reserve(nodes);
        size_t cursor = 0;
        size_t visited = 0;
        for (size_t root = 0; root < nodes; ++root) {
            if (has_parent[root]) continue;
            runs_[root].begin = cursor;
            cursor += runs_[root].local_end;
            runs_[root].local_end = cursor;
            ++visited;
            stack.push_back({root, 0});
            while (!stack.empty()) {
                Frame& frame = stack.back();
                if (frame.next_child == children[frame.node].size()) {
                    runs_[frame.node].subtree_end = cursor;
                    stack.pop_back();
                    continue;
                }
                const size_t child = static_cast<size_t>(
                        children[frame.node][frame.next_child++]);
                runs_[child].begin = cursor;
                cursor += runs_[child].local_end;
                runs_[child].local_end = cursor;
                ++visited;
                stack.push_back({child, 0});
            }
        }
        if (visited != nodes)
            throw std::invalid_argument("ScopeRunIndex: children contain a cycle");

        // Default-initialized arrays avoid zero-filling the multi-GB payload
        // immediately before overwriting every element from the original base.
        packed_.reset(new float[count_ * dim_]);
        ids_.reset(new faiss::idx_t[count_]);
        std::vector<size_t> next(nodes);
        for (size_t node = 0; node < nodes; ++node)
            next[node] = runs_[node].begin;
        for (const auto& binding : bindings) {
            const size_t id = static_cast<size_t>(binding.first);
            const size_t pos = next[static_cast<size_t>(binding.second)]++;
            ids_[pos] = static_cast<faiss::idx_t>(id);
            std::memcpy(packed_.get() + pos * dim_, base.data() + id * dim_,
                        dim_ * sizeof(float));
        }
    }

    // Offsets count packed vectors, not float components. Direct bindings are
    // retained even for internal nodes; a recursive run includes those bindings
    // and all descendants, while a local run includes only the direct bindings.
    std::pair<size_t, size_t> range(int node, bool recursive) const {
        if (node < 0 || static_cast<size_t>(node) >= runs_.size())
            throw std::out_of_range("ScopeRunIndex: invalid scope node");
        const Run& run = runs_[static_cast<size_t>(node)];
        return {run.begin, recursive ? run.subtree_end : run.local_end};
    }

    // Sorted by (squared distance, global ID), with missing slots (inf, -1).
    // k <= 0 writes nothing. For k > 0 the caller supplies k writable elements
    // in each output array and a query containing dim float components.
    //
    // Mode 0: FAISS single-point SIMD L2 and one bounded global top-k heap.
    // Mode 1: four-vector interleaved L2, sharing each query-component load.
    // The bundled FAISS has no fvec_L2sqr_batch_4, so this is a real portable
    // four-way kernel, not four single-point calls masquerading as batching.
    // Mode 2: chunks of 64 components screen candidates against the current
    // heap threshold, then surviving candidates receive full FAISS L2.
    void search(const float* query, std::pair<size_t, size_t> scope, int k,
                float* distances, faiss::idx_t* labels, int mode = 0) const {
        if (mode < 0 || mode > 2)
            throw std::invalid_argument("ScopeRunIndex: invalid search mode");
        if (scope.first > scope.second || scope.second > count_)
            throw std::out_of_range("ScopeRunIndex: invalid packed range");
        if (k <= 0) return;
        const size_t capacity = static_cast<size_t>(k);
        std::fill_n(distances, capacity, std::numeric_limits<float>::infinity());
        std::fill_n(labels, capacity, static_cast<faiss::idx_t>(-1));
        size_t filled = 0;
        size_t pos = scope.first;
        if (mode == 1) {
            for (; scope.second - pos >= 4; pos += 4) {
                const float* p0 = packed_.get() + pos * dim_;
                const float* p1 = p0 + dim_;
                const float* p2 = p1 + dim_;
                const float* p3 = p2 + dim_;
                float d0 = 0, d1 = 0, d2 = 0, d3 = 0;
                for (size_t j = 0; j < dim_; ++j) {
                    const float q = query[j];
                    const float a = q - p0[j];
                    const float b = q - p1[j];
                    const float c = q - p2[j];
                    const float d = q - p3[j];
                    d0 += a * a;
                    d1 += b * b;
                    d2 += c * c;
                    d3 += d * d;
                }
                offer(d0, ids_[pos], capacity, filled, distances, labels);
                offer(d1, ids_[pos + 1], capacity, filled, distances, labels);
                offer(d2, ids_[pos + 2], capacity, filled, distances, labels);
                offer(d3, ids_[pos + 3], capacity, filled, distances, labels);
            }
        }
        for (; pos < scope.second; ++pos) {
            const float* point = packed_.get() + pos * dim_;
            if (mode == 2 && filled == capacity &&
                std::isfinite(distances[0])) {
                double partial = 0;
                bool reject = false;
                const double threshold = static_cast<double>(distances[0]);
                // Engineering margin for differences between chunked and full
                // float reduction orders. This is intentionally permissive,
                // dimension-scaled, and not a formal rounding-error theorem.
                const double relative_margin =
                        16.0 * std::numeric_limits<float>::epsilon() *
                        (static_cast<double>(dim_) + 1.0);
                for (size_t offset = 0; offset < dim_; offset += 64) {
                    const size_t chunk = std::min(size_t{64}, dim_ - offset);
                    partial += static_cast<double>(faiss::fvec_L2sqr(
                            query + offset, point + offset, chunk));
                    const double scale = std::max(
                            std::max(partial, threshold),
                            static_cast<double>(std::numeric_limits<float>::min()));
                    if (partial > threshold + relative_margin * scale) {
                        reject = true;
                        break;
                    }
                }
                if (reject) continue;
            }
            const float distance = faiss::fvec_L2sqr(query, point, dim_);
            offer(distance, ids_[pos], capacity, filled, distances, labels);
        }
        // In-place heapsort leaves the untouched missing slots at the end.
        for (size_t remaining = filled; remaining > 1;) {
            --remaining;
            std::swap(distances[0], distances[remaining]);
            std::swap(labels[0], labels[remaining]);
            sift_down(remaining, distances, labels);
        }
    }

    // Resident payload estimate: this object, packed float coordinates (one
    // dim-vector per binding), one faiss::idx_t global ID per binding, and the
    // capacity of the three-size_t range records. Excludes allocator metadata,
    // caller-owned base/tree/bindings/query/results, and constructor-only
    // scratch (base-row binding flags, node flags/cursors, iterative DFS stack).
    // No HNSW, workload, test vectors, ground truth, or query cache is stored.
    size_t size_bytes() const {
        return sizeof(*this) + count_ * dim_ * sizeof(float) +
               count_ * sizeof(faiss::idx_t) + runs_.capacity() * sizeof(Run);
    }

private:
    struct Run {
        size_t begin = 0;
        size_t local_end = 0;
        size_t subtree_end = 0;
    };

    static bool better(float a, faiss::idx_t aid, float b, faiss::idx_t bid) {
        return a < b || (a == b && aid < bid);
    }

    static void sift_down(size_t size, float* distances, faiss::idx_t* labels) {
        const float distance = distances[0];
        const faiss::idx_t id = labels[0];
        size_t slot = 0;
        for (size_t child = 1; child < size; child = slot * 2 + 1) {
            if (child + 1 < size &&
                better(distances[child], labels[child],
                       distances[child + 1], labels[child + 1]))
                ++child;
            if (!better(distance, id, distances[child], labels[child])) break;
            distances[slot] = distances[child];
            labels[slot] = labels[child];
            slot = child;
        }
        distances[slot] = distance;
        labels[slot] = id;
    }

    static void offer(float distance, faiss::idx_t id, size_t capacity,
                      size_t& filled, float* distances, faiss::idx_t* labels) {
        if (filled == capacity) {
            if (!better(distance, id, distances[0], labels[0])) return;
            distances[0] = distance;
            labels[0] = id;
            sift_down(filled, distances, labels);
            return;
        }
        size_t slot = filled++;
        while (slot > 0) {
            const size_t parent = (slot - 1) / 2;
            if (!better(distances[parent], labels[parent], distance, id)) break;
            distances[slot] = distances[parent];
            labels[slot] = labels[parent];
            slot = parent;
        }
        distances[slot] = distance;
        labels[slot] = id;
    }

    size_t dim_ = 0;
    size_t count_ = 0;
    std::vector<Run> runs_;
    std::unique_ptr<float[]> packed_;
    std::unique_ptr<faiss::idx_t[]> ids_;
};
