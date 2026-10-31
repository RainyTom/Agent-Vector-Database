#pragma once

#include <faiss/MetricType.h>
#include <faiss/utils/distances.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
#if defined(__AVX2__) || defined(__F16C__)
#include <immintrin.h>
#endif

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
                  const std::vector<std::pair<int, int>>& bindings,
                  bool transposed = false, bool local_geometry_order = false,
                  bool half_scan = false, const float* rerank_rows = nullptr) {
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
        }
        if (local_geometry_order) {
            auto code = [&](faiss::idx_t id) {
                unsigned bits = 0;
                for (size_t j = 0; j < std::min(size_t{8}, dim_); ++j)
                    bits |= static_cast<unsigned>(base[static_cast<size_t>(id) * dim_ + j] >= 0) << j;
                return bits;
            };
            for (const Run& run : runs_)
                std::sort(ids_.get() + run.begin, ids_.get() + run.local_end,
                          [&](faiss::idx_t a, faiss::idx_t b) {
                              const unsigned ka = code(a), kb = code(b);
                              return ka < kb || (ka == kb && a < b);
                          });
        }
        for (size_t pos = 0; pos < count_; ++pos)
            std::memcpy(packed_.get() + pos * dim_,
                        base.data() + static_cast<size_t>(ids_[pos]) * dim_,
                        dim_ * sizeof(float));
        transposed_ = transposed;
        if (transposed_) {
            std::unique_ptr<float[]> tile(new float[8 * dim_]);
            for (size_t first = 0; first < count_; first += 8) {
                const size_t width = std::min(size_t{8}, count_ - first);
                for (size_t lane = 0; lane < width; ++lane)
                    for (size_t j = 0; j < dim_; ++j)
                        tile[j * width + lane] = packed_[(first + lane) * dim_ + j];
                std::memcpy(packed_.get() + first * dim_, tile.get(),
                            width * dim_ * sizeof(float));
            }
        }
        half_scan_ = half_scan;
        base_rows_ = rerank_rows;
        if (half_scan_) {
            // Shadow copy in the same eight-lane tile layout, but bfloat-free
            // binary16. Halves the bytes streaming through the scope scan while
            // keeping the FP32 payload as the exact reranking source.
            static_assert(sizeof(uint16_t) == 2, "binary16 shadow needs 16-bit words");
            half_.reset(new uint16_t[count_ * dim_]);
            const size_t words = count_ * dim_;
            size_t i = 0;
#if defined(__F16C__)
            for (; i + 8 <= words; i += 8) {
                __m256 v = _mm256_loadu_ps(packed_.get() + i);
                __m128i h = _mm256_cvtps_ph(v, _MM_FROUND_TO_NEAREST_INT |
                                                    _MM_FROUND_NO_EXC);
                _mm_storeu_si128(reinterpret_cast<__m128i*>(half_.get() + i), h);
            }
#endif
            for (; i < words; ++i) half_[i] = float_to_half(packed_[i]);
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
    // Mode 3: transposed eight-vector tiles, SIMD across points; boundary
    // tiles may compute extra distances but only in-scope IDs enter top-k.
    // Mode 4: tile-wide partial-distance cutoff feedback. All eligible lanes
    // must exceed the global heap cutoff before skipping a tile; the float
    // margin is conservative engineering practice, not a new rounding theorem.
    void search(const float* query, std::pair<size_t, size_t> scope, int k,
                float* distances, faiss::idx_t* labels, int mode = 0) const {
        if (mode < 0 || mode > 5 || transposed_ != (mode >= 3))
            throw std::invalid_argument("ScopeRunIndex: incompatible search mode/layout");
        if (scope.first > scope.second || scope.second > count_)
            throw std::out_of_range("ScopeRunIndex: invalid packed range");
        if (k <= 0) return;
        if (mode == 5) {
            if (!half_scan_ || !half_ || !base_rows_)
                throw std::invalid_argument(
                        "ScopeRunIndex: mode 5 requires the binary16 shadow");
            // Coarse pass streams binary16 tiles, then survivors are rescored
            // against the FP32 payload. The extra rerank slots absorb binary16
            // ordering error near the k-th distance; the returned distances are
            // always exact FP32 squared L2.
            const size_t capacity =
                    std::min(scope.second - scope.first, static_cast<size_t>(k) + 8);
            if (capacity == 0) {
                std::fill_n(distances, k, std::numeric_limits<float>::infinity());
                std::fill_n(labels, k, static_cast<faiss::idx_t>(-1));
                return;
            }
            std::vector<float> coarse(capacity, std::numeric_limits<float>::infinity());
            std::vector<faiss::idx_t> coarse_ids(capacity, static_cast<faiss::idx_t>(-1));
            size_t filled = 0;
            for (size_t first = scope.first / 8 * 8; first < scope.second; first += 8) {
                const size_t width = std::min(size_t{8}, count_ - first);
                const uint16_t* tile = half_.get() + first * dim_;
                float sums[8] = {};
#if defined(__AVX2__) && defined(__F16C__)
                if (width == 8) {
                    __m256 s0 = _mm256_setzero_ps(), s1 = s0, s2 = s0, s3 = s0;
                    bool abandoned = false;
                    unsigned outside = 0;
                    for (unsigned lane = 0; lane < 8; ++lane)
                        if (first + lane < scope.first || first + lane >= scope.second)
                            outside |= 1u << lane;
                    size_t j = 0;
                    for (; j + 4 <= dim_; j += 4) {
                        __m256 d0 = _mm256_sub_ps(
                                _mm256_set1_ps(query[j]),
                                _mm256_cvtph_ps(_mm_loadu_si128(
                                        reinterpret_cast<const __m128i*>(tile + j * 8))));
                        __m256 d1 = _mm256_sub_ps(
                                _mm256_set1_ps(query[j + 1]),
                                _mm256_cvtph_ps(_mm_loadu_si128(
                                        reinterpret_cast<const __m128i*>(tile + (j + 1) * 8))));
                        __m256 d2 = _mm256_sub_ps(
                                _mm256_set1_ps(query[j + 2]),
                                _mm256_cvtph_ps(_mm_loadu_si128(
                                        reinterpret_cast<const __m128i*>(tile + (j + 2) * 8))));
                        __m256 d3 = _mm256_sub_ps(
                                _mm256_set1_ps(query[j + 3]),
                                _mm256_cvtph_ps(_mm_loadu_si128(
                                        reinterpret_cast<const __m128i*>(tile + (j + 3) * 8))));
                        s0 = _mm256_add_ps(s0, _mm256_mul_ps(d0, d0));
                        s1 = _mm256_add_ps(s1, _mm256_mul_ps(d1, d1));
                        s2 = _mm256_add_ps(s2, _mm256_mul_ps(d2, d2));
                        s3 = _mm256_add_ps(s3, _mm256_mul_ps(d3, d3));
                        if (filled == capacity && (j + 4) % 64 == 0) {
                            const float margin = 16 * std::numeric_limits<float>::epsilon() *
                                                 (dim_ + 1) * std::max(1.0f, coarse[0]) +
                                                 kHalfProbeMargin;
                            __m256 cutoff = _mm256_set1_ps(coarse[0] + margin);
                            __m256 partial = _mm256_add_ps(_mm256_add_ps(s0, s1),
                                                          _mm256_add_ps(s2, s3));
                            unsigned rejected = static_cast<unsigned>(_mm256_movemask_ps(
                                _mm256_cmp_ps(partial, cutoff, _CMP_GT_OQ)));
                            if ((rejected | outside) == 255u) {
                                abandoned = true;
                                break;
                            }
                        }
                    }
                    if (abandoned) continue;
                    s0 = _mm256_add_ps(_mm256_add_ps(s0, s1), _mm256_add_ps(s2, s3));
                    for (; j < dim_; ++j) {
                        __m256 delta = _mm256_sub_ps(
                                _mm256_set1_ps(query[j]),
                                _mm256_cvtph_ps(_mm_loadu_si128(
                                        reinterpret_cast<const __m128i*>(tile + j * 8))));
                        s0 = _mm256_add_ps(s0, _mm256_mul_ps(delta, delta));
                    }
                    _mm256_storeu_ps(sums, s0);
                } else
#endif
                {
                    for (size_t j = 0; j < dim_; ++j)
                        for (size_t lane = 0; lane < width; ++lane) {
                            const float delta = query[j] - half_to_float(tile[j * width + lane]);
                            sums[lane] += delta * delta;
                        }
                }
                for (size_t lane = 0; lane < width; ++lane)
                    if (first + lane >= scope.first && first + lane < scope.second)
                        offer(sums[lane], ids_[first + lane], capacity, filled,
                              coarse.data(), coarse_ids.data());
            }
            for (size_t i = 0; i < filled; ++i) {
                // packed_ follows the eight-lane tile layout, so exact rescoring
                // must read the caller's row-major base rows by global ID.
                const float* row = base_rows_ +
                        static_cast<size_t>(coarse_ids[i]) * dim_;
                coarse[i] = faiss::fvec_L2sqr(query, row, dim_);
            }
            std::vector<size_t> order(filled);
            for (size_t i = 0; i < filled; ++i) order[i] = i;
            std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                return better(coarse[a], coarse_ids[a], coarse[b], coarse_ids[b]);
            });
            const size_t emit = std::min(filled, static_cast<size_t>(k));
            std::fill_n(distances, k, std::numeric_limits<float>::infinity());
            std::fill_n(labels, k, static_cast<faiss::idx_t>(-1));
            for (size_t i = 0; i < emit; ++i) {
                distances[i] = coarse[order[i]];
                labels[i] = coarse_ids[order[i]];
            }
            return;
        }
        const size_t capacity = static_cast<size_t>(k);
        std::fill_n(distances, capacity, std::numeric_limits<float>::infinity());
        std::fill_n(labels, capacity, static_cast<faiss::idx_t>(-1));
        size_t filled = 0;
        size_t pos = scope.first;
        if (mode >= 3) {
            for (size_t first = scope.first / 8 * 8; first < scope.second; first += 8) {
                const size_t width = std::min(size_t{8}, count_ - first);
                const float* tile = packed_.get() + first * dim_;
                float sums[8] = {};
#if defined(__AVX2__)
                if (width == 8) {
                    __m256 s0 = _mm256_setzero_ps(), s1 = s0, s2 = s0, s3 = s0;
                    bool abandoned = false;
                    unsigned outside = 0;
                    for (unsigned lane = 0; lane < 8; ++lane)
                        if (first + lane < scope.first || first + lane >= scope.second)
                            outside |= 1u << lane;
                    size_t j = 0;
                    for (; j + 4 <= dim_; j += 4) {
                        __m256 d0 = _mm256_sub_ps(_mm256_set1_ps(query[j]), _mm256_loadu_ps(tile + j * 8));
                        __m256 d1 = _mm256_sub_ps(_mm256_set1_ps(query[j + 1]), _mm256_loadu_ps(tile + (j + 1) * 8));
                        __m256 d2 = _mm256_sub_ps(_mm256_set1_ps(query[j + 2]), _mm256_loadu_ps(tile + (j + 2) * 8));
                        __m256 d3 = _mm256_sub_ps(_mm256_set1_ps(query[j + 3]), _mm256_loadu_ps(tile + (j + 3) * 8));
                        s0 = _mm256_add_ps(s0, _mm256_mul_ps(d0, d0));
                        s1 = _mm256_add_ps(s1, _mm256_mul_ps(d1, d1));
                        s2 = _mm256_add_ps(s2, _mm256_mul_ps(d2, d2));
                        s3 = _mm256_add_ps(s3, _mm256_mul_ps(d3, d3));
                        if (mode == 4 && filled == capacity && (j + 4) % 64 == 0) {
                            const float margin = 16 * std::numeric_limits<float>::epsilon() *
                                                 (dim_ + 1) * std::max(1.0f, distances[0]);
                            __m256 cutoff = _mm256_set1_ps(distances[0] + margin);
                            __m256 partial = _mm256_add_ps(_mm256_add_ps(s0, s1),
                                                          _mm256_add_ps(s2, s3));
                            unsigned rejected = static_cast<unsigned>(_mm256_movemask_ps(
                                _mm256_cmp_ps(partial, cutoff, _CMP_GT_OQ)));
                            if ((rejected | outside) == 255u) {
                                abandoned = true;
                                break;
                            }
                        }
                    }
                    if (abandoned) continue;
                    s0 = _mm256_add_ps(_mm256_add_ps(s0, s1), _mm256_add_ps(s2, s3));
                    for (; j < dim_; ++j) {
                        __m256 delta = _mm256_sub_ps(_mm256_set1_ps(query[j]), _mm256_loadu_ps(tile + j * 8));
                        s0 = _mm256_add_ps(s0, _mm256_mul_ps(delta, delta));
                    }
                    _mm256_storeu_ps(sums, s0);
                } else
#endif
                {
                    for (size_t j = 0; j < dim_; ++j)
                        for (size_t lane = 0; lane < width; ++lane) {
                            float delta = query[j] - tile[j * width + lane];
                            sums[lane] += delta * delta;
                        }
                }
                for (size_t lane = 0; lane < width; ++lane)
                    if (first + lane >= scope.first && first + lane < scope.second)
                        offer(sums[lane], ids_[first + lane], capacity, filled, distances, labels);
            }
            pos = scope.second;
        }
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
               count_ * sizeof(faiss::idx_t) + runs_.capacity() * sizeof(Run) +
               (half_ ? count_ * dim_ * sizeof(uint16_t) : 0);
    }

private:
    struct Run {
        size_t begin = 0;
        size_t local_end = 0;
        size_t subtree_end = 0;
    };

    // binary16 <-> binary32 without requiring F16C at compile time. The F16C
    // paths are what actually run on this host; the fallbacks exist so the
    // header still compiles on machines without the extension.
    static float half_to_float(uint16_t h) {
#if defined(__F16C__)
        return _cvtsh_ss(h);
#else
        const uint32_t sign = static_cast<uint32_t>(h >> 15) << 31;
        const uint32_t exp = (h >> 10) & 0x1fu;
        const uint32_t mant = h & 0x3ffu;
        uint32_t bits;
        if (exp == 0) {
            bits = sign;                                    // subnormals flush to zero
        } else if (exp == 0x1fu) {
            bits = sign | 0x7f800000u | (mant << 13);
        } else {
            bits = sign | ((exp - 15u + 127u) << 23) | (mant << 13);
        }
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
#endif
    }

    static uint16_t float_to_half(float value) {
#if defined(__F16C__)
        return static_cast<uint16_t>(
                _cvtss_sh(value, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC));
#else
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        const uint32_t sign = (bits >> 16) & 0x8000u;
        int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xffu) - 127;
        uint32_t mantissa = bits & 0x7fffffu;
        if (exponent > 15) return static_cast<uint16_t>(sign | 0x7c00u);
        if (exponent < -14) return static_cast<uint16_t>(sign);
        mantissa += 0x1000u;                               // round the 13 dropped bits
        if (mantissa & 0x800000u) {
            mantissa = 0;
            if (++exponent > 15) return static_cast<uint16_t>(sign | 0x7c00u);
        }
        return static_cast<uint16_t>(sign |
                (static_cast<uint32_t>(exponent + 15) << 10) | (mantissa >> 13));
#endif
    }

    // Conservative extra slack for the binary16 coarse cutoff. Binary16 keeps
    // 11 significand bits, so per-component error is bounded by 2^-11 relative;
    // summed over unit-norm vectors this stays far below this constant. It is
    // engineering conservatism, not a rounding-error theorem.
    static constexpr float kHalfProbeMargin = 0.05f;

    std::unique_ptr<uint16_t[]> half_;      // binary16 shadow, tile layout
    const float* base_rows_ = nullptr;      // caller-owned row-major base
    bool half_scan_ = false;

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
    bool transposed_ = false;
    std::vector<Run> runs_;
    std::unique_ptr<float[]> packed_;
    std::unique_ptr<faiss::idx_t[]> ids_;
};
