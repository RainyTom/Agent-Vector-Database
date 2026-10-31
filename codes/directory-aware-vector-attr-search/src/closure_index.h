#pragma once

#include "closure_order.h"
#include "raw_l2_space.h"
#include "eligibility_index.h"
#include <array>
#include <chrono>
#include <cstring>
#include <functional>
#include <memory>
#include <queue>

namespace closure {
using Id = npi::Id;
using Neighbor = std::pair<float, Id>;
enum class Mode { Unpruned, Geometric, Interval, Hierarchy };
struct Work {
    uint64_t distance_calls = 0, expanded = 0, visited_checks = 0, pool_candidates = 0;
    uint64_t scope_rejected = 0, pruned = 0, retained = 0, witness_tests = 0;
    uint64_t hierarchy_steps = 0, backbone_ids = 0, qualification_tests = 0;
    double compile_us = 0;
    double eligibility_us = 0, eligible_scan_us = 0;
    size_t effective_ef = 0;
    const char* execution_path = "legacy";
    uint64_t qualification_directory_visits = 0, qualification_posting_words = 0, qualification_attribute_words = 0;
};
struct UpdateWork {
    uint64_t geometry_calls = 0, pools_rebuilt = 0, graph_insertions = 0, graph_deletions = 0;
    uint64_t compactions = 0, metadata_writes = 0, affected_items = 0;
    double rebuild_us = 0;
    double eligibility_update_us = 0;
};
struct BuildStats { double total_s = 0, graph_s = 0, pools_s = 0, eligibility_build_us = 0; uint64_t geometry_calls = 0; };

// Single-threaded dynamic experiment index. Dataset vectors must remain immutable
// and the Dataset must outlive this object. Parent/items are updated in place;
// legacy Dataset tin/tout deliberately are neither consulted nor maintained.
class Index {
    using Clock = std::chrono::steady_clock;
    using Graph = hnswlib::HierarchicalNSW<float>;
    struct Candidate { float distance = 0; Id id = 0; uint64_t witnesses = 0; };
    struct Pool { std::array<Candidate, 64> entries{}; size_t count = 0; };

    // HNSW retains this exact parameter address. Delegating through RawL2 keeps
    // graph, pool, witness and query geometry on the same pinned distance kernel.
    class CountedSpace : public hnswlib::SpaceInterface<float> {
    public:
        explicit CountedSpace(size_t dim) : raw(dim) {}
        size_t get_data_size() override { return raw.get_data_size(); }
        hnswlib::DISTFUNC<float> get_dist_func() override { return &callback; }
        void* get_dist_func_param() override { return this; }
        float distance(const float* a, const float* b) { ++calls; return raw.dist(a, b); }
        RawL2Space raw;
        uint64_t calls = 0;
    private:
        static float callback(const void* a, const void* b, const void* parameter) {
            auto& self = *const_cast<CountedSpace*>(static_cast<const CountedSpace*>(parameter));
            return self.distance(static_cast<const float*>(a), static_cast<const float*>(b));
        }
    };

public:
    explicit Index(npi::Dataset& data, size_t pool_size = 64,
                   std::string policy = "off", size_t scan_threshold = 512)
        : started_(Clock::now()), data_(data), space_(checked_dimension(data)),
          order_(data), pool_size_(pool_size), pools_(data.n), seen_(data.n, 0),
          eligibility_policy_(std::move(policy)), scan_threshold_(scan_threshold) {
        if (!pool_size || pool_size > 64) fail("pool_size must be in [1,64]");
        if (eligibility_policy_ != "off" && eligibility_policy_ != "cap" &&
            eligibility_policy_ != "hybrid" && eligibility_policy_ != "exact") fail("invalid eligibility policy");
        if (eligibility_policy_ != "off") {
            const auto start = Clock::now();
            eligibility_ = std::make_unique<eligibility::Index>(data.items, data.parent);
            stats_.eligibility_build_us = microseconds(start);
        }
        const auto graph_start = Clock::now();
        rebuild_graph();
        stats_.graph_s = seconds(graph_start);
        const auto pool_start = Clock::now();
        for (size_t i = 0; i < data_.n; ++i)
            if (data_.items[i].alive) rebuild_pool(static_cast<Id>(i));
        stats_.pools_s = seconds(pool_start);
        stats_.geometry_calls = space_.calls;
        stats_.total_s = seconds(started_);
    }
    Index(const Index&) = delete;
    Index& operator=(const Index&) = delete;
    Index(Index&&) = delete;
    Index& operator=(Index&&) = delete;

    std::vector<Neighbor> search(const npi::Query& query, Mode mode, size_t ef, size_t k,
                                 Work& work, bool attribute_witness = true) {
        npi::filter_detail::validate(query, data_);
        if (!k || (!eligibility_ && ef < k)) fail("search requires k >= 1 (off requires ef >= k)");
        work.effective_ef = ef;
        if (eligibility_) {
            const auto start = Clock::now();
            prepared_ = &eligibility_->prepare(query);
            work.eligibility_us = microseconds(start);
            work.qualification_directory_visits = prepared_->directory_visits;
            work.qualification_posting_words = prepared_->posting_words;
            work.qualification_attribute_words = prepared_->attribute_words;
            const size_t requested_ef = ef;
            k = std::min(k, prepared_->count);
            ef = std::min(prepared_->count, std::max(k, ef));
            work.effective_ef = ef;
            if (!prepared_->count) { work.execution_path = "empty"; return {}; }
            if (eligibility_policy_ == "exact" || (eligibility_policy_ == "hybrid" &&
                prepared_->count <= std::max({k, requested_ef, scan_threshold_}))) {
                work.execution_path = "exact";
                const auto scan_start = Clock::now();
                std::priority_queue<Neighbor> best;
                const float* qvec = data_.tests.data() + query.qidx * data_.dim;
                for (size_t word = 0; word < prepared_->eligible.size(); ++word) {
                    uint64_t bits = prepared_->eligible[word];
                    while (bits) {
                        const Id id = static_cast<Id>(word * 64 + __builtin_ctzll(bits));
                        bits &= bits - 1;
                        ++work.distance_calls;
                        const Neighbor key{space_.raw.dist(qvec, vector(id)), id};
                        if (best.size() < k) best.push(key);
                        else if (key < best.top()) { best.pop(); best.push(key); }
                    }
                }
                std::vector<Neighbor> answer(best.size());
                for (size_t i = answer.size(); i-- > 0;) { answer[i] = best.top(); best.pop(); }
                work.eligible_scan_us = microseconds(scan_start);
                return answer;
            }
            work.execution_path = "graph";
        }
        const auto bounds = order_.bounds(query.anchor, query.recursive != 0);
        if (!bounds.live_count) return {};
        if (++epoch_ == 0) { std::fill(seen_.begin(), seen_.end(), 0); epoch_ = 1; }
        std::priority_queue<Neighbor, std::vector<Neighbor>, std::greater<Neighbor>> frontier;
        std::priority_queue<Neighbor> results;
        const float* qvec = data_.tests.data() + query.qidx * data_.dim;
        auto offer = [&](Id id) {
            ++work.visited_checks;
            if (seen_[id] == epoch_) return;
            seen_[id] = epoch_;
            ++work.distance_calls;
            const Neighbor key{space_.raw.dist(qvec, vector(id)), id};
            if (results.size() < ef || key < results.top()) {
                frontier.push(key);
                ++work.qualification_tests;
                if (npi::filter_detail::extra(data_.items[id], query)) {
                    results.push(key);
                    if (results.size() > ef) results.pop();
                }
            }
        };
        offer(order_.live_at(bounds.first_live));
        while (!frontier.empty()) {
            if (results.size() == ef && frontier.top() > results.top()) break;
            const Id source = frontier.top().second;
            frontier.pop();
            ++work.expanded;
            // Consecutive live objects form the same bidirectional, scope-bounded
            // backbone in every mode, including ordinary-attribute-invalid nodes.
            const size_t ordinal = order_.prefix_live(order_.rank(order_.object(source)));
            if (ordinal > bounds.first_live) {
                ++work.backbone_ids;
                offer(order_.live_at(ordinal - 1));
            }
            if (ordinal + 1 < bounds.first_live + bounds.live_count) {
                ++work.backbone_ids;
                offer(order_.live_at(ordinal + 1));
            }
            const uint64_t selected = compile(source, query, mode, attribute_witness, bounds, work);
            const auto& pool = pools_[source];
            for (size_t i = 0; i < pool.count; ++i)
                if (selected & bit(i)) offer(pool.entries[i].id);
        }
        while (results.size() > k) results.pop();
        std::vector<Neighbor> answer(results.size());
        for (size_t i = answer.size(); i-- > 0;) { answer[i] = results.top(); results.pop(); }
        return answer;
    }

    std::vector<Id> neighbors_for_test(Id source, const npi::Query& query, Mode mode,
                                      bool attribute_witness = true) {
        npi::filter_detail::validate(query, data_);
        require_item(source, true);
        const auto bounds = order_.bounds(query.anchor, query.recursive != 0);
        if (!within(order_.rank(order_.object(source)), bounds)) return {};
        Work work;
        const uint64_t selected = compile(source, query, mode, attribute_witness, bounds, work);
        std::vector<Id> result;
        result.reserve(pools_[source].count);
        for (size_t i = 0; i < pools_[source].count; ++i)
            if (selected & bit(i)) result.push_back(pools_[source].entries[i].id);
        return result;
    }

    void apply_value(Id id, int64_t category, int64_t numeric, UpdateWork& work) {
        require_item(id, true);
        auto& item = data_.items[id];
        const uint64_t writes = (item.category != category) + (item.numeric != numeric);
        item.category = category; item.numeric = numeric;
        work.metadata_writes += writes;
        work.affected_items += writes != 0;
        update_eligibility(id, work);
    }
    void apply_tags(Id id, uint64_t tags, UpdateWork& work) {
        require_item(id, true);
        if (data_.items[id].tags != tags) {
            data_.items[id].tags = tags; ++work.metadata_writes; ++work.affected_items;
        }
        update_eligibility(id, work);
    }
    void apply_move(Id id, Id destination, UpdateWork& work) {
        require_item(id, true); require_dir(destination);
        if (data_.items[id].owner != destination) {
            order_.move_object(id, destination);
            data_.items[id].owner = destination;
            ++work.metadata_writes; ++work.affected_items;
        }
        update_eligibility(id, work);
    }
    void apply_batch(Id source, Id destination, UpdateWork& work) {
        require_dir(source); require_dir(destination);
        if (order_.beneath(destination, source)) fail("B destination is in source subtree");
        const auto bounds = order_.bounds(source, true);
        std::vector<Id> affected;
        affected.reserve(bounds.live_count);
        for (size_t i = 0; i < bounds.live_count; ++i)
            affected.push_back(order_.live_at(bounds.first_live + i));
        for (Id id : affected) apply_move(id, destination, work);
    }
    void apply_reparent(Id source, Id destination, UpdateWork& work) {
        require_dir(source); require_dir(destination);
        if (data_.parent[source] < 0) fail("cannot reparent root");
        if (order_.beneath(destination, source)) fail("R destination is in source subtree");
        if (data_.parent[source] == static_cast<int>(destination)) return;
        const size_t affected = order_.bounds(source, true).live_count;
        order_.reparent(source, destination);
        data_.parent[source] = static_cast<int>(destination);
        if (eligibility_) {
            const auto start = Clock::now();
            eligibility_->reparent(source, destination); prepared_ = nullptr;
            work.eligibility_update_us += microseconds(start);
        }
        ++work.metadata_writes;
        work.affected_items += affected;
    }
    void apply_delete(Id id, UpdateWork& work) {
        require_item(id, true);
        graph_->markDelete(id);
        ++work.graph_deletions;
        order_.remove_object(id);
        data_.items[id].alive = false;
        ++work.metadata_writes; ++work.affected_items;
        update_eligibility(id, work);
        const size_t stored = graph_->cur_element_count.load();
        if (stored && graph_->num_deleted_.load() >= (stored + 2) / 3) compact(work);
    }
    void apply_insert(Id id, Id owner, int64_t category, int64_t numeric, uint64_t tags,
                      UpdateWork& work) {
        require_item(id, false); require_dir(owner);
        const auto start = Clock::now();
        const uint64_t before = space_.calls;
        const auto found = graph_->label_lookup_.find(id);
        if (found == graph_->label_lookup_.end()) graph_->addPoint(vector(id), id);
        else graph_->unmarkDelete(id);
        ++work.graph_insertions;
        data_.items[id] = npi::Item{owner, category, numeric, tags, true};
        update_eligibility(id, work);
        order_.insert_object(id, owner);
        work.metadata_writes += 5; ++work.affected_items;
        rebuild_pool(id); ++work.pools_rebuilt;
        // A fresh insertion modifies outgoing level-0 lists only at its selected
        // neighbors. External labels are NOT internal HNSW node indices. Revival
        // also refreshes this local set; other pools remain approximate/stale.
        const auto internal = graph_->label_lookup_.at(id);
        auto* links = graph_->get_linklist0(internal);
        const auto* neighbors = reinterpret_cast<const hnswlib::tableint*>(links + 1);
        const size_t count = graph_->getListCount(links);
        for (size_t i = 0; i < count; ++i) {
            const auto label = graph_->getExternalLabel(neighbors[i]);
            if (label >= data_.n) fail("HNSW returned invalid external label");
            const Id other = static_cast<Id>(label);
            if (other != id && data_.items[other].alive) {
                rebuild_pool(other); ++work.pools_rebuilt;
            }
        }
        work.geometry_calls += space_.calls - before;
        work.rebuild_us += microseconds(start);
    }

    size_t scope_size(const npi::Query& query) const {
        npi::filter_detail::validate(query, data_);
        return order_.bounds(query.anchor, query.recursive != 0).live_count;
    }
    bool in_scope(Id id, const npi::Query& query) const {
        npi::filter_detail::validate(query, data_);
        if (id >= data_.n || !data_.items[id].alive) return false;
        return within(order_.rank(order_.object(id)), order_.bounds(query.anchor, query.recursive != 0));
    }
    uint64_t pool_fingerprint() const {
        // FNV-style state identity only, explicitly NOT a cryptographic digest.
        uint64_t hash = UINT64_C(14695981039346656037);
        auto mix = [&](uint64_t value) { hash ^= value; hash *= UINT64_C(1099511628211); };
        mix(pool_size_); mix(pools_.size());
        for (size_t source = 0; source < pools_.size(); ++source) {
            mix(source); mix(pools_[source].count);
            for (size_t i = 0; i < pools_[source].count; ++i) {
                const auto& entry = pools_[source].entries[i];
                uint32_t raw;
                std::memcpy(&raw, &entry.distance, sizeof(raw));
                mix(entry.id); mix(raw); mix(entry.witnesses);
            }
        }
        return hash;
    }
    size_t index_bytes() const {
        // Capacity-based resident structure estimate, not RSS/allocator accounting.
        // Excludes borrowed Dataset and transient query/build/compaction workspaces.
        size_t bytes = sizeof(*this) + order_.allocation_bytes() + pools_.capacity() * sizeof(Pool)
                     + seen_.capacity() * sizeof(uint32_t) + sizeof(Graph);
        bytes += graph_->max_elements_ * (graph_->size_data_per_element_ + sizeof(char*));
        bytes += graph_->element_levels_.capacity() * sizeof(int);
        bytes += (graph_->link_list_locks_.capacity() + graph_->label_op_locks_.capacity()) * sizeof(std::mutex);
        bytes += graph_->label_lookup_.bucket_count() * sizeof(void*) +
                 graph_->label_lookup_.size() * (sizeof(std::pair<const hnswlib::labeltype, hnswlib::tableint>) + 2 * sizeof(void*));
        // Serial HNSW uses one visited array; pool/container bookkeeping estimated.
        bytes += graph_->max_elements_ * sizeof(hnswlib::vl_type) + sizeof(hnswlib::VisitedListPool) + sizeof(hnswlib::VisitedList);
        for (size_t i = 0; i < graph_->cur_element_count.load(); ++i)
            if (graph_->element_levels_[i] > 0)
                bytes += graph_->size_links_per_element_ * graph_->element_levels_[i] + 1;
        return bytes + eligibility_bytes();
    }
    const BuildStats& build_stats() const { return stats_; }
    size_t eligibility_bytes() const { return eligibility_ ? eligibility_->bytes() : 0; }
    const eligibility::Result* prepared_eligibility() const { return prepared_; }

private:
    Clock::time_point started_;
    npi::Dataset& data_;
    CountedSpace space_;
    DirectoryOrder order_;
    size_t pool_size_;
    std::vector<Pool> pools_;
    std::vector<uint32_t> seen_;
    uint32_t epoch_ = 0;
    std::unique_ptr<Graph> graph_;
    BuildStats stats_;
    std::string eligibility_policy_;
    size_t scan_threshold_;
    std::unique_ptr<eligibility::Index> eligibility_;
    const eligibility::Result* prepared_ = nullptr;

    void update_eligibility(Id id, UpdateWork& work) {
        if (!eligibility_) return;
        const auto start = Clock::now();
        eligibility_->update(id, data_.items[id]); prepared_ = nullptr;
        work.eligibility_update_us += microseconds(start);
    }

    [[noreturn]] static void fail(const char* message) { throw std::runtime_error(std::string("closure: ") + message); }
    static size_t checked_dimension(const npi::Dataset& data) {
        if (!data.dim || data.dim > static_cast<size_t>(std::numeric_limits<int>::max()) ||
            data.n != data.items.size() || data.n > std::numeric_limits<Id>::max() ||
            data.n > std::numeric_limits<size_t>::max() / data.dim || data.base.size() != data.n * data.dim ||
            data.nq > std::numeric_limits<size_t>::max() / data.dim || data.tests.size() != data.nq * data.dim)
            fail("invalid dataset shape");
        return data.dim;
    }
    static double seconds(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }
    static double microseconds(Clock::time_point start) { return seconds(start) * 1e6; }
    static uint64_t bit(size_t index) { return UINT64_C(1) << index; }
    static bool within(size_t rank, const DirectoryOrder::Bounds& bounds) { return rank >= bounds.begin && rank < bounds.end; }
    const float* vector(Id id) const { return data_.base.data() + static_cast<size_t>(id) * data_.dim; }
    void require_dir(Id id) const { if (id >= data_.parent.size()) fail("invalid directory ID"); }
    void require_item(Id id, bool alive) const {
        if (id >= data_.n || data_.items[id].alive != alive) fail("invalid item ID/liveness for event");
    }
    void rebuild_graph() {
        auto replacement = std::make_unique<Graph>(&space_, std::max<size_t>(1, data_.n), 16, 200, 100);
        replacement->setEf(200);
        for (size_t i = 0; i < data_.n; ++i)
            if (data_.items[i].alive) replacement->addPoint(vector(static_cast<Id>(i)), i);
        graph_ = std::move(replacement);
    }
    void rebuild_pool(Id source) {
        auto& pool = pools_[source];
        pool.count = 0;
        const size_t request = std::min(pool_size_ + 1, order_.live_count());
        if (!request) return;
        auto candidates = graph_->searchKnn(vector(source), request);
        // HNSW returns a maxheap by (distance, external label). At most one extra
        // candidate is needed before dropping self; sorting preserves exact ties.
        std::array<Neighbor, 65> ordered{};
        size_t count = 0;
        while (!candidates.empty()) {
            const auto candidate = candidates.top(); candidates.pop();
            if (candidate.second >= data_.n) fail("HNSW returned invalid external label");
            const Id id = static_cast<Id>(candidate.second);
            if (id != source && data_.items[id].alive) ordered[count++] = {candidate.first, id};
        }
        std::sort(ordered.begin(), ordered.begin() + count);
        pool.count = std::min(count, pool_size_);
        for (size_t j = 0; j < pool.count; ++j) {
            auto& target = pool.entries[j];
            target = Candidate{ordered[j].first, ordered[j].second, 0};
            for (size_t i = 0; i < j; ++i) {
                if (ordered[i].first < target.distance &&
                    space_.distance(vector(ordered[i].second), vector(target.id)) < target.distance)
                    target.witnesses |= bit(i);
            }
        }
    }
    void compact(UpdateWork& work) {
        const auto start = Clock::now();
        const uint64_t before = space_.calls;
        rebuild_graph();
        work.graph_insertions += order_.live_count();
        for (size_t i = 0; i < data_.n; ++i) {
            if (data_.items[i].alive) { rebuild_pool(static_cast<Id>(i)); ++work.pools_rebuilt; }
            else pools_[i].count = 0;
        }
        ++work.compactions;
        work.geometry_calls += space_.calls - before;
        work.rebuild_us += microseconds(start);
    }

    uint64_t compile(Id source, const npi::Query& query, Mode mode, bool attribute_witness,
                     const DirectoryOrder::Bounds& bounds, Work& work) const {
        const auto start = Clock::now();
        const auto& pool = pools_[source];
        std::array<size_t, 64> ranks{};
        uint64_t valid = 0, witness_eligible = 0;
        for (size_t i = 0; i < pool.count; ++i) {
            ++work.pool_candidates;
            const Id id = pool.entries[i].id;
            if (!data_.items[id].alive) { ++work.scope_rejected; continue; }
            ranks[i] = order_.rank(order_.object(id));
            if (!within(ranks[i], bounds)) { ++work.scope_rejected; continue; }
            valid |= bit(i);
            if (mode != Mode::Unpruned) {
                if (attribute_witness) ++work.qualification_tests;
                if (!attribute_witness || npi::filter_detail::extra(data_.items[id], query))
                    witness_eligible |= bit(i);
            }
        }
        const size_t source_rank = order_.rank(order_.object(source));
        std::array<Id, 64> lca_ids{};
        std::array<uint64_t, 64> lca_masks{};
        size_t lca_count = 0;
        uint64_t accepted = 0;
        for (size_t j = 0; j < pool.count; ++j) {
            if (!(valid & bit(j))) continue;
            uint64_t blockers = mode == Mode::Unpruned ? 0 :
                pool.entries[j].witnesses & accepted & witness_eligible;
            if (blockers && mode == Mode::Interval) {
                const size_t low = std::min(source_rank, ranks[j]);
                const size_t high = std::max(source_rank, ranks[j]);
                uint64_t interval = 0;
                for (size_t i = 0; i < j; ++i)
                    if ((blockers & bit(i)) && low < ranks[i] && ranks[i] < high) interval |= bit(i);
                blockers &= interval;
            } else if (blockers && mode == Mode::Hierarchy) {
                // LCA(source owner, target owner) is the first current source
                // ancestor whose token span contains the target object. All such
                // LCAs lie on one chain. Cache its <=64 local membership masks.
                Id lca = data_.items[source].owner;
                size_t low, high;
                for (;;) {
                    ++work.hierarchy_steps;
                    low = order_.rank(order_.enter(lca));
                    high = order_.rank(order_.exit(lca));
                    if (low < ranks[j] && ranks[j] < high) break;
                    const int parent = data_.parent[lca];
                    if (parent < 0) fail("invalid LCA topology");
                    lca = static_cast<Id>(parent);
                }
                size_t slot = 0;
                while (slot < lca_count && lca_ids[slot] != lca) ++slot;
                if (slot == lca_count) {
                    lca_ids[slot] = lca;
                    uint64_t mask = 0;
                    for (size_t i = 0; i < pool.count; ++i)
                        if ((valid & bit(i)) && low < ranks[i] && ranks[i] < high) mask |= bit(i);
                    lca_masks[slot] = mask;
                    ++lca_count;
                }
                blockers &= lca_masks[slot];
            }
            if (mode != Mode::Unpruned) ++work.witness_tests;
            if (blockers) ++work.pruned;
            else { accepted |= bit(j); ++work.retained; }
        }
        work.compile_us += microseconds(start);
        return accepted;
    }
};

} // namespace closure
