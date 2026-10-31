#pragma once

#include "eligibility_index.h"
#include "raw_l2_space.h"
#include <chrono>
#include <memory>
#include <queue>
#include <random>
#include <utility>

// Physical placement is independent of logical ownership. Only insert/delete,
// bounded local maintenance, and explicit repack change these vanilla graphs.
namespace packed {
using Neighbor = std::pair<float, npi::Id>;
struct Work {
    uint64_t graph_calls = 0, distance_calls = 0, hops = 0;
    uint64_t merged_candidates = 0, qualified_ids = 0;
    size_t effective_ef_min = 0, effective_ef_max = 0;
};
struct UpdateWork {
    uint64_t distance_calls = 0, graph_insertions = 0, graph_deletions = 0;
    uint64_t compactions = 0, splits = 0;
    double rebuild_us = 0;
};

class Index {
    using Id = npi::Id;
    using Graph = hnswlib::HierarchicalNSW<float>;
    using Clock = std::chrono::steady_clock;
    static constexpr size_t none = std::numeric_limits<size_t>::max();
    class CountedL2 : public RawL2Space {
    public:
        explicit CountedL2(size_t dim) : RawL2Space(dim) {}
        uint64_t calls = 0;
        hnswlib::DISTFUNC<float> get_dist_func() override { return counted; }
        void* get_dist_func_param() override { return this; }
    private:
        static float counted(const void* a, const void* b, const void* param) {
            auto* self = const_cast<CountedL2*>(static_cast<const CountedL2*>(param));
            ++self->calls;
            return self->dist(static_cast<const float*>(a), static_cast<const float*>(b));
        }
    };
    struct Block {
        size_t target = 1;
        std::vector<Id> live;
        std::unique_ptr<Graph> graph;
    };
    struct Filter : hnswlib::BaseFilterFunctor {
        const eligibility::Result& eligible;
        explicit Filter(const eligibility::Result& value) : eligible(value) {}
        bool operator()(hnswlib::labeltype label) override {
            return label <= std::numeric_limits<Id>::max() && eligible.contains(static_cast<Id>(label));
        }
    };
    const std::vector<float>& base_;
    size_t dim_, block_size_, live_count_ = 0;
    bool shuffled_, node_layout_;
    CountedL2 space_;
    std::vector<npi::Item> items_;
    std::vector<int> parent_;
    std::vector<std::vector<Id>> children_;
    std::vector<Id> preorder_;
    std::vector<size_t> rank_, loc_, home_, eligible_counts_;
    std::vector<Block> blocks_;
    uint64_t build_calls_ = 0;

    [[noreturn]] static void fail(const char* message) {
        throw std::runtime_error(std::string("packed: ") + message);
    }
    static double micros(Clock::time_point start) {
        return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
    }
    const float* vector(Id id) const { return base_.data() + size_t(id) * dim_; }
    size_t target_floor() const { return node_layout_ ? 1 : block_size_; }
    size_t capacity(const Block& block) const {
        if (block.target > std::numeric_limits<hnswlib::tableint>::max() / 2)
            fail("block target exceeds HNSW internal ID capacity");
        return std::max(node_layout_ ? size_t(16) : size_t(0), 2 * block.target);
    }
    void rank_directories() {
        preorder_.clear();
        std::vector<Id> stack;
        stack.reserve(parent_.size());
        for (size_t i = parent_.size(); i-- > 0;)
            if (parent_[i] < 0) stack.push_back(static_cast<Id>(i));
        while (!stack.empty()) {
            const Id dir = stack.back(); stack.pop_back();
            if (preorder_.size() >= parent_.size()) fail("directory cycle");
            rank_[dir] = preorder_.size();
            preorder_.push_back(dir);
            for (auto it = children_[dir].rbegin(); it != children_[dir].rend(); ++it)
                stack.push_back(*it);
        }
        if (preorder_.size() != parent_.size()) fail("directory cycle or missing root");
    }
    // Counting scatter gives directory preorder / ID order in O(N + D),
    // without sorting all vectors or copying any vector payload.
    std::vector<Id> ordered_live() const {
        std::vector<size_t> offsets(parent_.size() + 1, 0);
        for (const auto& item : items_)
            if (item.alive) ++offsets[rank_[item.owner] + 1];
        for (size_t i = 1; i < offsets.size(); ++i) offsets[i] += offsets[i - 1];
        std::vector<Id> ids(live_count_);
        for (size_t id = 0; id < items_.size(); ++id)
            if (items_[id].alive) ids[offsets[rank_[items_[id].owner]]++] = static_cast<Id>(id);
        return ids;
    }
    // Fixed engine plus explicit rejection sampling avoids std::shuffle's
    // library-dependent mapping while retaining an unbiased Fisher-Yates draw.
    static void shuffle(std::vector<Id>& ids) {
        std::mt19937_64 rng(100);
        for (size_t n = ids.size(); n > 1; --n) {
            const uint64_t bound = n, reject = (uint64_t(0) - bound) % bound;
            uint64_t value;
            do { value = rng(); } while (value < reject);
            std::swap(ids[n - 1], ids[static_cast<size_t>(value % bound)]);
        }
    }
    void assign_home() {
        std::fill(home_.begin(), home_.end(), none);
        // IDs are visited ascending: this is the first owned object in DFS
        // even for the shuffled control, whose physical block order differs.
        for (size_t id = 0; id < items_.size(); ++id)
            if (items_[id].alive && home_[items_[id].owner] == none)
                home_[items_[id].owner] = loc_[id];
        for (Id dir : preorder_)
            if (home_[dir] == none)
                home_[dir] = parent_[dir] < 0 ? 0 : home_[parent_[dir]];
    }
    void build_block(size_t block, UpdateWork* work) {
        auto& b = blocks_[block];
        b.graph.reset();
        if (b.live.empty()) return;
        if (b.live.size() > 2 * b.target) fail("block exceeds target bound");
        b.graph = std::make_unique<Graph>(&space_, capacity(b), 16, 200, 100);
        for (Id id : b.live) {
            b.graph->addPoint(vector(id), id);
            if (work) ++work->graph_insertions;
        }
    }
    void compact(size_t block, UpdateWork& work) {
        const auto start = Clock::now();
        build_block(block, &work);
        ++work.compactions;
        work.rebuild_us += micros(start);
    }
    void split(size_t block, UpdateWork& work) {
        const auto start = Clock::now();
        auto& ids = blocks_[block].live;
        std::sort(ids.begin(), ids.end(), [&](Id a, Id b) {
            const size_t ra = rank_[items_[a].owner], rb = rank_[items_[b].owner];
            return ra != rb ? ra < rb : a < b;
        });
        const size_t middle = ids.size() / 2;
        size_t added = 0;
        while (added < blocks_.size() && !blocks_[added].live.empty()) ++added;
        Block right;
        right.live.assign(ids.begin() + middle, ids.end());
        ids.resize(middle);
        blocks_[block].target = std::max(target_floor(), ids.size());
        right.target = std::max(target_floor(), right.live.size());
        for (Id id : right.live) loc_[id] = added;
        if (added == blocks_.size()) blocks_.push_back(std::move(right));
        else blocks_[added] = std::move(right);
        // Never resizeIndex: both graphs are recreated at their target bounds.
        build_block(block, &work);
        build_block(added, &work);
        ++work.splits;
        work.rebuild_us += micros(start);
    }
    void layout(UpdateWork* work) {
        const auto ids = ordered_live();
        std::vector<size_t> offsets(parent_.size() + 1, 0);
        for (const auto& item : items_) if (item.alive) ++offsets[rank_[item.owner] + 1];
        for (size_t i = 1; i < offsets.size(); ++i) offsets[i] += offsets[i - 1];
        std::vector<Id> groups;
        groups.reserve(parent_.size());
        for (Id dir : preorder_)
            if (offsets[rank_[dir]] != offsets[rank_[dir] + 1]) groups.push_back(dir);
        if (shuffled_) shuffle(groups);
        // Reset scratch memberships. Explicit full repack discards old physical
        // slots first; initial construction also arrives with no old slots.
        for (auto& b : blocks_) { b.graph.reset(); b.live.clear(); b.target = target_floor(); }
        if (blocks_.empty()) blocks_.emplace_back();
        blocks_[0].target = target_floor();
        size_t block = 0;
        for (Id dir : groups) {
            const size_t begin = offsets[rank_[dir]], end = offsets[rank_[dir] + 1], count = end - begin;
            // Whole direct-owner groups are indivisible at layout/repack.
            // Oversized groups get dedicated graphs; node never coalesces.
            if (!blocks_[block].live.empty() &&
                (node_layout_ || count >= block_size_ ||
                 blocks_[block].live.size() > block_size_ - count)) {
                ++block;
                if (block == blocks_.size()) blocks_.emplace_back();
            }
            auto& b = blocks_[block];
            b.live.insert(b.live.end(), ids.begin() + begin, ids.begin() + end);
            b.target = std::max(target_floor(), b.live.size());
            for (size_t i = begin; i < end; ++i) loc_[ids[i]] = block;
        }
        assign_home();
        for (size_t b = 0; b <= block; ++b) {
            build_block(b, work);
            if (work && !blocks_[b].live.empty()) ++work->compactions;
        }
    }
    void search_graph(Graph& graph, const float* q, size_t k, size_t ef, size_t merge_k,
                      Filter& filter, std::priority_queue<Neighbor>& merged, Work& work) {
        const long old_hops = graph.metric_hops.load();
        auto current = graph.enterpoint_node_;
        float best = graph.fstdistfunc_(q, graph.getDataByInternalId(current), graph.dist_func_param_);
        // The pinned searchKnn upper-layer descent, with its actual distance
        // callback and the same base-layer algorithm with metrics enabled.
        for (int level = graph.maxlevel_; level > 0; --level) {
            bool changed = true;
            while (changed) {
                changed = false;
                auto* links = graph.get_linklist(current, level);
                const size_t count = graph.getListCount(links);
                ++graph.metric_hops;
                const auto* neighbors = reinterpret_cast<const hnswlib::tableint*>(links + 1);
                for (size_t i = 0; i < count; ++i) {
                    const auto candidate = neighbors[i];
                    if (candidate >= graph.cur_element_count) fail("invalid HNSW edge");
                    const float distance = graph.fstdistfunc_(q, graph.getDataByInternalId(candidate),
                                                             graph.dist_func_param_);
                    if (distance < best) { best = distance; current = candidate; changed = true; }
                }
            }
        }
        auto found = graph.searchBaseLayerST<false, true>(current, q, ef, &filter);
        // Local and global truncation both use external ID as the tie breaker;
        // internal HNSW IDs are not the public distance/ID ordering.
        std::priority_queue<Neighbor> local;
        while (!found.empty()) {
            const Neighbor value(found.top().first, static_cast<Id>(graph.getExternalLabel(found.top().second)));
            found.pop();
            if (local.size() < k) local.push(value);
            else if (value < local.top()) { local.pop(); local.push(value); }
        }
        work.merged_candidates += local.size();
        while (!local.empty()) {
            const auto value = local.top(); local.pop();
            if (merged.size() < merge_k) merged.push(value);
            else if (value < merged.top()) { merged.pop(); merged.push(value); }
        }
        work.hops += static_cast<uint64_t>(graph.metric_hops.load() - old_hops);
    }

public:
    Index(const std::vector<float>& base, size_t dim, const std::vector<npi::Item>& items,
          const std::vector<int>& parent, size_t block_size, const std::string& layout_name)
        : base_(base), dim_(dim), block_size_(block_size), shuffled_(layout_name == "shuffle"),
          node_layout_(layout_name == "node"),
          space_(dim), items_(items), parent_(parent), children_(parent.size()), rank_(parent.size()),
          loc_(items.size(), none), home_(parent.size(), none) {
        if (!dim || dim > static_cast<size_t>(std::numeric_limits<int>::max()) ||
            base.size() / dim != items.size() || base.size() % dim || parent.empty() ||
            items.size() > std::numeric_limits<Id>::max() ||
            parent.size() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
            (block_size != 1024 && block_size != 4096) ||
            (layout_name != "dfs" && layout_name != "shuffle" && layout_name != "node"))
            fail("invalid dimensions or layout");
        for (size_t d = 0; d < parent.size(); ++d) {
            if (parent[d] < -1 || (parent[d] >= 0 && static_cast<size_t>(parent[d]) >= parent.size()))
                fail("invalid directory parent");
            if (parent[d] >= 0) children_[parent[d]].push_back(static_cast<Id>(d));
        }
        for (const auto& item : items_) {
            if (item.owner >= parent.size()) fail("invalid object owner");
            live_count_ += item.alive;
        }
        preorder_.reserve(parent.size());
        rank_directories();
        layout(nullptr);
        build_calls_ = space_.calls;
    }
    Index(const Index&) = delete;
    Index& operator=(const Index&) = delete;
    Index(Index&&) = delete;
    Index& operator=(Index&&) = delete;

    std::vector<Neighbor> search(const float* q, size_t k, size_t ef,
                                 const eligibility::Result& eligible, Work& work) {
        const uint64_t before = space_.calls;
        eligible_counts_.assign(blocks_.size(), 0);
        size_t qualified = 0;
        for (size_t w = 0; w < eligible.eligible.size(); ++w) {
            for (uint64_t bits = eligible.eligible[w]; bits; bits &= bits - 1) {
                const size_t id = w * 64 + static_cast<unsigned>(__builtin_ctzll(bits));
                if (id >= items_.size() || !items_[id].alive || loc_[id] >= blocks_.size())
                    fail("eligibility bitmap disagrees with physical live objects");
                ++eligible_counts_[loc_[id]];
                ++qualified;
            }
        }
        if (qualified != eligible.count) fail("eligibility count disagrees with bitmap");
        work.qualified_ids += qualified;
        if (!k || !qualified) return {};
        const size_t merge_k = std::min(k, qualified);
        std::priority_queue<Neighbor> merged;
        Filter filter(eligible);
        for (size_t b = 0; b < blocks_.size(); ++b) {
            const size_t count = eligible_counts_[b];
            if (!count) continue;
            if (!blocks_[b].graph) fail("eligible block has no graph");
            const size_t local_k = std::min(k, count), local_ef = std::min(count, std::max(ef, local_k));
            ++work.graph_calls;
            if (!work.effective_ef_min || local_ef < work.effective_ef_min) work.effective_ef_min = local_ef;
            work.effective_ef_max = std::max(work.effective_ef_max, local_ef);
            search_graph(*blocks_[b].graph, q, local_k, local_ef, merge_k, filter, merged, work);
        }
        std::vector<Neighbor> result(merged.size());
        for (size_t i = result.size(); i-- > 0;) { result[i] = merged.top(); merged.pop(); }
        work.distance_calls += space_.calls - before;
        return result;
    }
    void update(Id id, const npi::Item& next, UpdateWork& work) {
        if (id >= items_.size() || next.owner >= parent_.size()) fail("invalid update");
        const bool was_alive = items_[id].alive;
        // Metadata-only operations, including owner moves, never touch graphs.
        if (was_alive == next.alive) { items_[id] = next; return; }
        const uint64_t before = space_.calls;
        if (was_alive) {
            const size_t block = loc_[id];
            auto& b = blocks_[block];
            b.graph->markDelete(id);
            ++work.graph_deletions;
            const auto it = std::find(b.live.begin(), b.live.end(), id);
            if (it == b.live.end()) fail("missing live block member");
            *it = b.live.back(); b.live.pop_back();
            items_[id] = next;
            --live_count_;
            if (b.live.empty() || b.graph->num_deleted_.load() * 3 >= b.graph->cur_element_count.load())
                compact(block, work);
        } else {
            const size_t block = loc_[id] == none ? home_[next.owner] : loc_[id];
            auto& b = blocks_[block];
            // Split the prospective membership before graph insertion when
            // live would exceed 2*target; no oversized scratch graph exists.
            if (b.live.size() == 2 * b.target) {
                b.live.push_back(id);
                loc_[id] = block;
                items_[id] = next;
                ++live_count_;
                split(block, work);
            } else {
                if (b.graph && b.graph->label_lookup_.find(id) == b.graph->label_lookup_.end() &&
                    b.graph->cur_element_count.load() == capacity(b)) compact(block, work);
                if (!b.graph) b.graph = std::make_unique<Graph>(&space_, capacity(b), 16, 200, 100);
                const auto existing = b.graph->label_lookup_.find(id);
                if (existing != b.graph->label_lookup_.end()) {
                    if (!b.graph->isMarkedDeleted(existing->second)) fail("duplicate live graph label");
                    // Immutable vectors permit resurrection without addPoint's
                    // vector-update edge repair, matching the existing runner.
                    b.graph->unmarkDelete(id);
                } else {
                    if (b.graph->cur_element_count.load() >= capacity(b)) fail("full block after compaction");
                    b.graph->addPoint(vector(id), id);
                }
                ++work.graph_insertions;
                b.live.push_back(id);
                loc_[id] = block;
                items_[id] = next;
                ++live_count_;
            }
        }
        work.distance_calls += space_.calls - before;
    }
    void reparent(Id node, Id parent) {
        if (node >= parent_.size() || parent >= parent_.size() || parent_[node] < 0)
            fail("invalid reparent");
        for (int p = static_cast<int>(parent); p >= 0; p = parent_[p])
            if (p == static_cast<int>(node)) fail("reparent cycle");
        if (parent_[node] == static_cast<int>(parent)) return;
        auto& old = children_[parent_[node]];
        old.erase(std::lower_bound(old.begin(), old.end(), node));
        auto& children = children_[parent];
        children.insert(std::lower_bound(children.begin(), children.end(), node), node);
        parent_[node] = static_cast<int>(parent);
        rank_directories();
        // Directory homes remain the initial/repack homes, not moving vectors.
    }
    void repack(UpdateWork& work) {
        const auto start = Clock::now();
        const uint64_t before = space_.calls;
        // Repack is the explicit placement cutover: retired slot metadata and
        // dead-object resurrection locations are reclaimed, not accumulated.
        std::vector<Block>().swap(blocks_);
        std::fill(loc_.begin(), loc_.end(), none);
        layout(&work);
        work.distance_calls += space_.calls - before;
        work.rebuild_us += micros(start);
    }
    size_t block_of(Id id) const { return loc_.at(id); }
    size_t live_count() const { return live_count_; }
    size_t block_count() const { return blocks_.size(); }
    size_t max_block_target() const {
        size_t maximum = 0;
        for (const auto& b : blocks_) maximum = std::max(maximum, b.target);
        return maximum;
    }
    size_t max_block_capacity() const {
        size_t maximum = 0;
        for (const auto& b : blocks_) maximum = std::max(maximum, capacity(b));
        return maximum;
    }
    size_t graph_count() const {
        size_t count = 0;
        for (const auto& b : blocks_) count += bool(b.graph);
        return count;
    }
    size_t dead_count() const {
        size_t count = 0;
        for (const auto& b : blocks_) if (b.graph) count += b.graph->num_deleted_.load();
        return count;
    }
    uint64_t build_distance_calls() const { return build_calls_; }
    size_t graph_bytes() const {
        size_t bytes = 0;
        for (const auto& b : blocks_) if (b.graph) bytes += b.graph->indexFileSize();
        return bytes;
    }
    size_t metadata_bytes() const {
        size_t bytes = sizeof(*this) + items_.capacity() * sizeof(npi::Item) + parent_.capacity() * sizeof(int)
            + children_.capacity() * sizeof(std::vector<Id>) + preorder_.capacity() * sizeof(Id)
            + (rank_.capacity() + loc_.capacity() + home_.capacity() + eligible_counts_.capacity()) * sizeof(size_t)
            + blocks_.capacity() * sizeof(Block);
        for (const auto& children : children_) bytes += children.capacity() * sizeof(Id);
        for (const auto& b : blocks_) bytes += b.live.capacity() * sizeof(Id);
        return bytes;
    }
    size_t bytes() const {
        size_t bytes = metadata_bytes();
        // Resident capacity estimate, not serialization or process RSS. Include
        // hnswlib's substantial fixed 65536 label-lock array for EACH graph.
        // Map/deque allocator bookkeeping is estimated, not measured.
        for (const auto& b : blocks_) if (b.graph) {
            const auto& g = *b.graph;
            bytes += sizeof(Graph) + g.max_elements_ * (g.size_data_per_element_ + sizeof(char*))
                + g.label_op_locks_.capacity() * sizeof(std::mutex)
                + g.link_list_locks_.capacity() * sizeof(std::mutex)
                + g.element_levels_.capacity() * sizeof(int)
                + g.label_lookup_.bucket_count() * sizeof(void*)
                + g.label_lookup_.size() * (sizeof(decltype(g.label_lookup_)::value_type) + 2 * sizeof(void*))
                + sizeof(hnswlib::VisitedListPool) + sizeof(hnswlib::VisitedList)
                + g.max_elements_ * sizeof(hnswlib::vl_type);
            for (size_t i = 0; i < g.cur_element_count.load(); ++i)
                if (g.element_levels_[i] > 0) bytes += g.size_links_per_element_ * g.element_levels_[i] + 1;
        }
        return bytes;
    }
    bool matches(const std::vector<npi::Item>& items, const std::vector<int>& parent) const {
        if (parent != parent_ || items.size() != items_.size()) return false;
        for (size_t i = 0; i < items.size(); ++i) {
            const auto& a = items[i]; const auto& b = items_[i];
            if (a.owner != b.owner || a.category != b.category || a.numeric != b.numeric ||
                a.tags != b.tags || a.alive != b.alive) return false;
        }
        return true;
    }
    uint64_t fingerprint() const {
        uint64_t hash = UINT64_C(1469598103934665603);
        auto add = [&](uint64_t value) {
            for (unsigned byte = 0; byte < 8; ++byte) {
                hash ^= (value >> (8 * byte)) & 255;
                hash *= UINT64_C(1099511628211);
            }
        };
        // Physical membership (including tombstones and retained dead loc),
        // not logical metadata, metrics, mutable query scratch, or edge bytes.
        add(block_size_); add(shuffled_); add(node_layout_); add(blocks_.size());
        for (size_t id = 0; id < items_.size(); ++id) { add(loc_[id]); add(items_[id].alive); }
        for (const auto& b : blocks_) {
            add(b.target);
            add(b.graph ? b.graph->cur_element_count.load() : 0);
            if (b.graph) for (size_t i = 0; i < b.graph->cur_element_count.load(); ++i) {
                add(b.graph->getExternalLabel(static_cast<hnswlib::tableint>(i)));
                add(b.graph->isMarkedDeleted(static_cast<hnswlib::tableint>(i)));
            }
        }
        return hash;
    }
    // Expensive independent structural audit; deliberately not called on any
    // timed path. Dynamic blocks need not remain globally DFS-contiguous.
    bool valid() const {
        if (blocks_.empty() || loc_.size() != items_.size() || home_.size() != parent_.size() ||
            rank_.size() != parent_.size() || preorder_.size() != parent_.size()) return false;
        std::vector<unsigned char> directories(parent_.size(), 0), seen(items_.size(), 0), labels(items_.size(), 0);
        std::vector<Id> stack;
        for (size_t d = parent_.size(); d-- > 0;) if (parent_[d] < 0) stack.push_back(static_cast<Id>(d));
        size_t ordinal = 0;
        while (!stack.empty()) {
            const Id d = stack.back(); stack.pop_back();
            if (d >= parent_.size() || directories[d]++ || ordinal >= preorder_.size() ||
                preorder_[ordinal] != d || rank_[d] != ordinal || home_[d] >= blocks_.size()) return false;
            ++ordinal;
            const auto& children = children_[d];
            if (!std::is_sorted(children.begin(), children.end())) return false;
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                if (*it >= parent_.size() || parent_[*it] != static_cast<int>(d)) return false;
                stack.push_back(*it);
            }
        }
        if (ordinal != parent_.size()) return false;
        size_t live = 0;
        for (size_t block = 0; block < blocks_.size(); ++block) {
            const auto& b = blocks_[block];
            if (b.target < target_floor() || b.live.size() > 2 * b.target ||
                bool(b.graph) != !b.live.empty()) return false;
            for (Id id : b.live) {
                if (id >= items_.size() || seen[id]++ || !items_[id].alive || loc_[id] != block) return false;
                ++live;
            }
            if (!b.graph) continue;
            const auto& g = *b.graph;
            const size_t slots = g.cur_element_count.load();
            if (g.max_elements_ != capacity(b) || slots > g.max_elements_ || !slots ||
                g.label_lookup_.size() != slots || g.enterpoint_node_ >= slots) return false;
            size_t deleted = 0, graph_live = 0;
            for (size_t i = 0; i < slots; ++i) {
                const auto internal = static_cast<hnswlib::tableint>(i);
                const auto id = g.getExternalLabel(internal);
                if (id >= items_.size() || labels[id]++ || loc_[id] != block) return false;
                const auto found = g.label_lookup_.find(id);
                if (found == g.label_lookup_.end() || found->second != internal) return false;
                const bool dead = g.isMarkedDeleted(internal);
                if (dead == items_[id].alive) return false;
                if (dead) ++deleted;
                else { if (!seen[id]) return false; ++graph_live; }
            }
            if (deleted != g.num_deleted_.load() || deleted * 3 >= slots || graph_live != b.live.size()) return false;
        }
        for (size_t id = 0; id < items_.size(); ++id) {
            if (items_[id].owner >= parent_.size() || bool(seen[id]) != items_[id].alive ||
                (items_[id].alive && labels[id] != 1) || (loc_[id] != none && loc_[id] >= blocks_.size())) return false;
        }
        return live == live_count_;
    }
};
} // namespace packed
