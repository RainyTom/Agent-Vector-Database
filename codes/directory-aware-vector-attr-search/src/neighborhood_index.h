// Predicate-addressable two-hop pages. Experiment prototype for2456849.
// B and C share graph, visited/frontier/results, one-hop navigation and stop rule.
#pragma once
#include "neighborhood_filter.h"
#include "raw_l2_space.h"
#include <chrono>
#include <cstdio>
#include <memory>
#include <queue>
#include <sstream>
#include <unistd.h>

namespace npi {
using Internal = hnswlib::tableint;
using Graph = hnswlib::HierarchicalNSW<float>;
using Neighbor = std::pair<float, Id>;
using Clock = std::chrono::steady_clock;
inline double micros(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}
struct CountedL2 : RawL2Space {
    mutable uint64_t calls = 0;
    explicit CountedL2(size_t d) : RawL2Space(d) {}
    static float counted(const void* a, const void* b, const void* p) {
        auto* s = static_cast<const CountedL2*>(p);
        ++s->calls;
        return s->dist(static_cast<const float*>(a), static_cast<const float*>(b));
    }
    hnswlib::DISTFUNC<float> get_dist_func() override { return counted; }
    void* get_dist_func_param() override { return this; }
};
struct Work {
    uint64_t onehop_ids = 0, twohop_ids = 0, page_containers = 0;
    uint64_t sparse_offsets = 0, dense_words = 0, empty_pages = 0;
    uint64_t duplicate_checks = 0, eligible_expansions = 0, expanded = 0;
    uint64_t visited_checks = 0, distance_calls = 0, upper_ids = 0;
    double qualification_us = 0, neighborhood_us = 0;
    QualStats qualification;
};
// No vector payload. Sparse offsets <=16; dense four64bit words.
struct NeighborPage {
    uint32_t page = 0;
    uint16_t count = 0;
    uint16_t dense = 0;
    union { uint8_t offsets[16]; uint64_t bits[4]; };
};
static_assert(sizeof(NeighborPage) == 40, "account page padding explicitly");

class NeighborhoodIndex {
    Dataset& d_;
    CountedL2 space_;
    std::unique_ptr<Graph> graph_;
    std::vector<Id> external_;
    std::vector<Internal> internal_;
    std::vector<std::vector<NeighborPage>> pages_;
    Qualifier qualifier_;
    std::vector<uint32_t> center_seen_, search_seen_;
    uint32_t center_epoch_ = 0, search_epoch_ = 0;
    std::vector<Id> gathered_, eligible_;
    uint32_t next_epoch(std::vector<uint32_t>& stamps, uint32_t& epoch) {
        if (++epoch == 0) { std::fill(stamps.begin(), stamps.end(), 0); epoch = 1; }
        return epoch;
    }
    struct Links { const Internal* ids; size_t count; };
    Links links(Internal v, int level = 0) const {
        auto* p = level ? graph_->get_linklist(v, level) : graph_->get_linklist0(v);
        return {reinterpret_cast<const Internal*>(p + 1), graph_->getListCount(p)};
    }
    void gather(Internal v, Work* work) {
        gathered_.clear();
        const uint32_t stamp = next_epoch(center_seen_, center_epoch_);
        const auto first = links(v);
        if (work) work->twohop_ids += first.count;
        for (size_t i = 0; i < first.count; ++i) {
            const auto second = links(first.ids[i]);
            if (work) work->twohop_ids += second.count;
            for (size_t j = 0; j < second.count; ++j) {
                const Internal candidate = second.ids[j];
                if (work) ++work->duplicate_checks;
                if (candidate == v || center_seen_[candidate] == stamp) continue;
                center_seen_[candidate] = stamp;
                gathered_.push_back(external_[candidate]);
            }
        }
        std::sort(gathered_.begin(), gathered_.end());
    }
    void build_pages() {
        pages_.resize(external_.size());
        for (Internal v = 0; v < external_.size(); ++v) {
            gather(v, nullptr);
            auto& directory = pages_[v];
            size_t start = 0;
            while (start < gathered_.size()) {
                const uint32_t pg = gathered_[start] / 256;
                size_t end = start + 1;
                while (end < gathered_.size() && gathered_[end] / 256 == pg) ++end;
                NeighborPage p{};
                p.page = pg; p.count = static_cast<uint16_t>(end - start);
                p.dense = p.count > 16;
                if (p.dense) std::fill(std::begin(p.bits), std::end(p.bits), uint64_t(0));
                for (size_t i = start; i < end; ++i) {
                    const uint8_t offset = static_cast<uint8_t>(gathered_[i] % 256);
                    if (p.dense) p.bits[offset / 64] |= uint64_t(1) << (offset % 64);
                    else p.offsets[i - start] = offset;
                }
                directory.push_back(p);
                start = end;
            }
            directory.shrink_to_fit();
        }
    }
    const std::vector<Id>& acquire(Internal v, bool paged, Work& work) {
        const auto start = Clock::now();
        eligible_.clear();
        if (!paged) {
            gather(v, &work);
            const auto qualification_start = Clock::now();
            for (Id id : gathered_) if (qualifier_.point(id)) eligible_.push_back(id);
            work.qualification_us += micros(qualification_start);
        } else {
            for (const auto& p : pages_[v]) {
                ++work.page_containers;
                const std::array<uint64_t,4>* mask;
                if (!qualifier_.page_ready(p.page)) {
                    const auto qualification_start = Clock::now();
                    mask = &qualifier_.page(p.page);
                    work.qualification_us += micros(qualification_start);
                } else mask = &qualifier_.page(p.page);
                const size_t before = eligible_.size();
                if (!p.dense) {
                    work.sparse_offsets += p.count;
                    for (size_t j = 0; j < p.count; ++j) {
                        const uint8_t o = p.offsets[j];
                        if (((*mask)[o / 64] >> (o % 64)) & 1)
                            eligible_.push_back(p.page * 256 + o);
                    }
                } else {
                    work.dense_words += 4;
                    for (size_t j = 0; j < 4; ++j) {
                        uint64_t bits = p.bits[j] & (*mask)[j];
                        while (bits) {
                            const unsigned b = __builtin_ctzll(bits);
                            eligible_.push_back(p.page * 256 + static_cast<Id>(j * 64 + b));
                            bits &= bits - 1;
                        }
                    }
                }
                work.empty_pages += eligible_.size() == before;
            }
        }
        work.eligible_expansions += eligible_.size();
        work.neighborhood_us += micros(start);
        return eligible_;
    }
public:
    double graph_build_s = 0, neighborhood_build_s = 0;
    uint64_t graph_build_distances = 0;
    explicit NeighborhoodIndex(Dataset& d, bool materialize)
        : d_(d), space_(d.dim), internal_(d.n, UINT32_MAX), qualifier_(d) {
        const auto start = Clock::now();
        size_t live = 0;
        for (const auto& i : d.items) live += i.alive;
        graph_ = std::make_unique<Graph>(&space_, std::max(size_t(1), live), 16, 200, 100, false);
        for (Id id = 0; id < d.n; ++id) if (d.items[id].alive)
            graph_->addPoint(d.base.data() + size_t(id) * d.dim, id, false);
        external_.resize(live);
        for (Internal i = 0; i < live; ++i) {
            const Id id = static_cast<Id>(graph_->getExternalLabel(i));
            external_[i] = id; internal_[id] = i;
        }
        graph_build_s = micros(start) / 1e6;
        graph_build_distances = space_.calls;
        center_seen_.resize(live); search_seen_.resize(live);
        if (materialize) {
            const auto pstart = Clock::now();
            build_pages();
            neighborhood_build_s = micros(pstart) / 1e6;
        }
        // HNSW now owns the single active object-vector payload. Reserve vectors
        // are not indexed in this static prototype; source files remain immutable.
        std::vector<float>().swap(d.base);
    }
    size_t n_live() const { return external_.size(); }
    Id external_id(Internal id) const { return external_.at(id); }
    const float* vector(Id id) const {
        if (id >= internal_.size() || internal_[id] == UINT32_MAX)
            throw std::runtime_error("vector requested for unindexed object");
        return reinterpret_cast<const float*>(graph_->getDataByInternalId(internal_[id]));
    }
    std::vector<Neighbor> ground_truth(const Query& q, size_t k = 10) const {
        std::vector<Neighbor> result;
        const float* qv = d_.tests.data() + q.qidx * d_.dim;
        for (Id id = 0; id < d_.n; ++id) if (d_.eligible(id, q))
            result.emplace_back(space_.dist(qv, vector(id)), id);
        std::sort(result.begin(), result.end());
        if (result.size() > k) result.resize(k);
        return result;
    }
    size_t graph_bytes() const { return graph_->indexFileSize(); }
    size_t neighborhood_bytes() const {
        size_t total = pages_.capacity() * sizeof(std::vector<NeighborPage>);
        for (const auto& p : pages_) total += p.capacity() * sizeof(NeighborPage);
        return total;
    }
    size_t workspace_bytes() const {
        return qualifier_.bytes() + (center_seen_.capacity() + search_seen_.capacity()) * sizeof(uint32_t)
            + (gathered_.capacity() + eligible_.capacity()) * sizeof(Id)
            + (external_.capacity() + internal_.capacity()) * sizeof(Id);
    }
    std::string graph_sha256() const {
        // Hash canonical payload + all ordered adjacency, not uninitialized
        // serialized padding. Hashing is construction provenance, outside query.
        char path[] = "/tmp/neighborhood-graph-XXXXXX";
        const int fd = mkstemp(path);
        if (fd < 0) throw std::runtime_error("cannot create graph hash file");
        close(fd);
        try {
            std::ofstream out(path, std::ios::binary);
            auto emit = [&](const auto& x) { out.write(reinterpret_cast<const char*>(&x), sizeof(x)); };
            emit(d_.dim);
            const size_t count = n_live(); emit(count);
            for (Internal v = 0; v < n_live(); ++v) {
                emit(external_[v]); emit(graph_->element_levels_[v]);
                out.write(graph_->getDataByInternalId(v), d_.dim * sizeof(float));
                for (int l = 0; l <= graph_->element_levels_[v]; ++l) {
                    const auto a = links(v, l); emit(a.count);
                    out.write(reinterpret_cast<const char*>(a.ids), a.count * sizeof(Internal));
                }
            }
            out.close();
            if (!out) throw std::runtime_error("graph hash write failed");
            const std::string command = std::string("sha256sum ") + path;
            FILE* pipe = popen(command.c_str(), "r");
            if (!pipe) throw std::runtime_error("sha256sum unavailable");
            char value[65]{};
            const size_t size = fread(value, 1, 64, pipe);
            const int status = pclose(pipe);
            if (size != 64 || status != 0) throw std::runtime_error("graph sha256sum failed");
            unlink(path);
            return std::string(value, 64);
        } catch (...) { unlink(path); throw; }
    }
    std::vector<Neighbor> search(const Query& q, bool paged, size_t ef, Work& work,
                                  std::vector<Internal>* trace = nullptr, size_t k = 10) {
        const uint64_t calls_before = space_.calls;
        qualifier_.begin(q, work.qualification);
        if (external_.empty()) return {};
        ef = std::max(ef, k);
        const uint32_t stamp = next_epoch(search_seen_, search_epoch_);
        const float* qv = d_.tests.data() + q.qidx * d_.dim;
        auto distance = [&](Internal v) {
            return space_.get_dist_func()(qv, graph_->getDataByInternalId(v), &space_);
        };
        Internal entry = graph_->enterpoint_node_;
        float best = distance(entry);
        // Unmodified upper-layer greedy entry search; no attribute exclusion.
        for (int level = graph_->maxlevel_; level > 0; --level) {
            bool changed = true;
            while (changed) {
                changed = false;
                const auto a = links(entry, level);
                work.upper_ids += a.count;
                for (size_t i = 0; i < a.count; ++i) {
                    const float d = distance(a.ids[i]);
                    if (d < best) { best = d; entry = a.ids[i]; changed = true; }
                }
            }
        }
        // Same filtered-HNSW result/stop contract: illegal nodes can navigate.
        // Total ordering makes B/C tie behavior identical and reproducible.
        std::priority_queue<std::pair<float, Internal>> frontier, results;
        float lower = std::numeric_limits<float>::max();
        if (qualifier_.point(external_[entry])) {
            lower = distance(entry);
            frontier.emplace(-lower, entry); results.emplace(lower, entry);
        } else frontier.emplace(-lower, entry);
        search_seen_[entry] = stamp;
        auto visit = [&](Internal candidate, bool known_eligible) {
            ++work.visited_checks;
            if (search_seen_[candidate] == stamp) return;
            search_seen_[candidate] = stamp;
            const float d = distance(candidate);
            if (results.size() < ef || d < lower) {
                frontier.emplace(-d, candidate);
                if (known_eligible || qualifier_.point(external_[candidate])) {
                    results.emplace(d, candidate);
                    if (results.size() > ef) results.pop();
                    if (!results.empty()) lower = results.top().first;
                }
            }
        };
        while (!frontier.empty()) {
            const auto current = frontier.top();
            if (-current.first > lower && results.size() == ef) break;
            frontier.pop();
            const Internal v = current.second;
            ++work.expanded;
            if (trace) trace->push_back(v);
            const auto first = links(v);
            work.onehop_ids += first.count;
            for (size_t i = 0; i < first.count; ++i) visit(first.ids[i], false);
            // Same ID-ordered qualified E2 set in both arms, after base edges.
            const auto& extra = acquire(v, paged, work);
            for (Id id : extra) visit(internal_[id], true);
        }
        std::vector<Neighbor> output;
        while (!results.empty()) {
            const auto p = results.top(); results.pop();
            output.emplace_back(p.first, external_[p.second]);
        }
        std::sort(output.begin(), output.end());
        if (output.size() > k) output.resize(k);
        work.distance_calls = space_.calls - calls_before;
        return output;
    }
    // Cold candidate-retrieval replay on the SAME B-selected centers; not e2e.
    void candidate_replay(const Query& q, const std::vector<Internal>& centers,
                            bool paged, Work& work, std::vector<std::vector<Id>>& out) {
        qualifier_.begin(q, work.qualification);
        for (Internal v : centers) out.push_back(acquire(v, paged, work));
    }
};
} // namespace npi
