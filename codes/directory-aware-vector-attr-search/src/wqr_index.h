// WQR: Witness-Qualified Routing — production C++ implementation.
// Frozen spec: researches/nested-attr-vector-search/2026-10-30-exps/wqr-index-spec.md
// All raw FP32 squared-L2 distances go through RawL2Space (pinned hnswlib kernel).
// No hand-written scalar distance implementations anywhere.
#pragma once

#include "raw_l2_space.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <numeric>
#include <queue>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wqr {

using Id = size_t;
using NodeId = size_t;
constexpr size_t NONE = std::numeric_limits<size_t>::max();
constexpr size_t B0 = 1024;                 // target block size
constexpr size_t BLOCK_MAX_LIVE = 2 * B0;   // split threshold
constexpr size_t BLOCK_MIN_CAP = 64;        // capacity stays word-aligned
constexpr size_t PROXIES_PER_BLOCK = 4;
constexpr size_t HNSW_M = 16;
constexpr size_t HNSW_EFC = 200;
constexpr uint64_t HNSW_SEED = 100;
constexpr double DEAD_RATIO = 0.2;

[[noreturn]] inline void fail(const std::string& m) { throw std::runtime_error(m); }

class CountedL2 : public RawL2Space {
public:
    explicit CountedL2(size_t dim) : RawL2Space(dim) {}
    mutable uint64_t calls = 0;
    // Ad-hoc counted call for build/oracle paths (RawL2Space::dist bypasses the counter).
    float cdist(const float* a, const float* b) const {
        ++calls;
        return dist(a, b);
    }
    hnswlib::DISTFUNC<float> get_dist_func() override { return counted; }
    void* get_dist_func_param() override { return this; }
private:
    static float counted(const void* a, const void* b, const void* param) {
        const auto* self = static_cast<const CountedL2*>(param);
        ++self->calls;
        return self->dist(static_cast<const float*>(a), static_cast<const float*>(b));
    }
};

// ---------------------------------------------------------------------------
// Euler tour over an implicit treap. Build follows real DFS event order:
// enter(u) emitted on first visit, exit(u) after all children finish.
// ---------------------------------------------------------------------------
class EulerTour {
    struct TNode {
        NodeId node;
        bool is_enter;
        uint64_t prio;
        TNode *l = nullptr, *r = nullptr, *p = nullptr;
        size_t size = 1;
    };
    std::mt19937_64 rng{100};
    std::vector<TNode*> enter_tok, exit_tok;
    TNode* root_ = nullptr;
    size_t nodes_ = 0;
    NodeId root_node_ = NONE;

    static size_t sz(TNode* t) { return t ? t->size : 0; }
    static void pull(TNode* t) {
        t->size = 1 + sz(t->l) + sz(t->r);
        if (t->l) t->l->p = t;
        if (t->r) t->r->p = t;
    }
    static std::pair<TNode*, TNode*> split(TNode* t, size_t pos) {
        if (!t) return {nullptr, nullptr};
        if (pos <= sz(t->l)) {
            auto [a, b] = split(t->l, pos);
            t->l = b; pull(t); t->p = nullptr;
            if (a) a->p = nullptr;
            return {a, t};
        }
        auto [a, b] = split(t->r, pos - sz(t->l) - 1);
        t->r = a; pull(t); t->p = nullptr;
        if (b) b->p = nullptr;
        return {t, b};
    }
    static TNode* merge(TNode* a, TNode* b) {
        if (!a) { if (b) b->p = nullptr; return b; }
        if (!b) { a->p = nullptr; return a; }
        if (a->prio > b->prio) { a->r = merge(a->r, b); pull(a); a->p = nullptr; return a; }
        b->l = merge(a, b->l); pull(b); b->p = nullptr; return b;
    }
    static size_t rank(TNode* t) {
        size_t r = sz(t->l);
        while (t->p) {
            if (t == t->p->r) r += 1 + sz(t->p->l);
            t = t->p;
        }
        return r;
    }

public:
    void build(const std::vector<int>& parent) {
        nodes_ = parent.size();
        enter_tok.assign(nodes_, nullptr);
        exit_tok.assign(nodes_, nullptr);
        // Real DFS event order, iterative, children in ascending id.
        std::vector<std::vector<NodeId>> children(nodes_);
        for (NodeId v = 0; v < nodes_; ++v) {
            if (parent[v] < 0) {
                if (root_node_ != NONE) fail("multiple roots");
                root_node_ = v;
            } else children[parent[v]].push_back(v);
        }
        if (root_node_ == NONE) fail("missing root");
        std::vector<TNode*> seq;
        seq.reserve(2 * nodes_);
        std::vector<std::pair<NodeId, size_t>> stack{{root_node_, 0}};
        std::vector<bool> entered(nodes_, false);
        while (!stack.empty()) {
            NodeId v = stack.back().first;
            size_t& ci = stack.back().second;
            if (!entered[v]) {
                entered[v] = true;
                auto* e = new TNode{v, true, rng()};
                enter_tok[v] = e; seq.push_back(e);
            }
            if (ci < children[v].size()) {
                const NodeId child = children[v][ci];
                ++ci;  // increment before emplace (reference may dangle after)
                stack.emplace_back(child, 0);
            } else {
                auto* x = new TNode{v, false, rng()};
                exit_tok[v] = x; seq.push_back(x);
                stack.pop_back();
            }
        }
        if (seq.size() != 2 * nodes_) fail("DFS token count mismatch");
        for (TNode* t : seq) root_ = merge(root_, t);
    }
    size_t enterRank(NodeId v) const { return rank(enter_tok.at(v)); }
    size_t exitRank(NodeId v) const { return rank(exit_tok.at(v)); }
    bool contains(NodeId anchor, NodeId owner) const {
        return enterRank(anchor) <= enterRank(owner) && enterRank(owner) < exitRank(anchor);
    }
    NodeId root() const { return root_node_; }
    void reparent(NodeId src, NodeId dst) {
        if (src == root_node_) fail("R: cannot move root");
        if (contains(src, dst)) fail("R: destination inside source subtree");
        const size_t a = rank(enter_tok[src]);
        const size_t b = rank(exit_tok[src]) + 1;
        auto [l, rest] = split(root_, a);
        auto [mid, rr] = split(rest, b - a);
        root_ = merge(l, rr);
        // Recompute destination exit rank after the cut.
        const size_t pos = rank(exit_tok[dst]);
        auto [l2, r2] = split(root_, pos);
        root_ = merge(merge(l2, mid), r2);
    }
    ~EulerTour() {
        for (TNode* t : enter_tok) delete t;
        for (TNode* t : exit_tok) delete t;
    }
};

// ---------------------------------------------------------------------------
// Block state. All bitmaps indexed by slot; word count = ceil(capacity/64).
// Planes hold the LSB-indexed bit of the (encoded) value: plane[b][w] bit w%64
// is bit b of the slot's value.
// ---------------------------------------------------------------------------
struct Block {
    size_t dim = 0, capacity = 0, generation = 0, live = 0;
    bool retired = false;
    std::vector<Id> slot_to_id;              // NONE = free slot
    std::vector<size_t> free_slots;
    size_t next_slot = 0;
    std::vector<uint64_t> alive_bits;
    std::vector<std::vector<uint64_t>> category_planes;  // [bit 0..63][word]
    std::vector<std::vector<uint64_t>> numeric_planes;   // encoded int64
    std::vector<std::vector<uint64_t>> tag_planes;
    std::unordered_map<NodeId, std::vector<size_t>> owner_posting;
    std::unordered_map<Id, size_t> owner_pos;            // id → position in owner posting
    std::vector<uint8_t> zone_of;                        // 255 = none
    std::vector<std::vector<uint64_t>> zone_bits;
    std::vector<std::vector<float>> proxies;             // independent copies
    std::vector<size_t> proxy_labels;                    // global proxy label per zone
    std::unique_ptr<CountedL2> space;
    std::unique_ptr<hnswlib::HierarchicalNSW<float>> graph;

    explicit Block(size_t d) : dim(d) {
        category_planes.resize(64);
        numeric_planes.resize(64);
        tag_planes.resize(64);
        space = std::make_unique<CountedL2>(d);
    }
    size_t words() const { return (capacity + 63) / 64; }
    void setCapacity(size_t cap) {
        capacity = cap;
        const size_t w = words();
        slot_to_id.resize(cap, NONE);
        alive_bits.assign(w, 0);
        for (auto& p : category_planes) p.assign(w, 0);
        for (auto& p : numeric_planes) p.assign(w, 0);
        for (auto& p : tag_planes) p.assign(w, 0);
        zone_of.assign(cap, 255);
    }
    void growCapacity(size_t cap) {
        const size_t oldWords = words();
        capacity = cap;
        const size_t w = words();
        slot_to_id.resize(cap, NONE);
        alive_bits.resize(w, 0);
        for (auto& p : category_planes) p.resize(w, 0);
        for (auto& p : numeric_planes) p.resize(w, 0);
        for (auto& p : tag_planes) p.resize(w, 0);
        zone_of.resize(cap, 255);
        for (auto& z : zone_bits) z.resize(w, 0);
        (void)oldWords;
    }
    static uint64_t encodeNumeric(int64_t v) { return static_cast<uint64_t>(v) ^ (1ULL << 63); }
    void setBit(std::vector<uint64_t>& plane, size_t slot, bool value) {
        const size_t w = slot / 64, b = slot % 64;
        if (value) plane[w] |= (1ULL << b); else plane[w] &= ~(1ULL << b);
    }
    void writeAttrs(size_t slot, int64_t cat, int64_t num, uint64_t tags) {
        const uint64_t enc = encodeNumeric(num);
        for (size_t b = 0; b < 64; ++b) {
            setBit(category_planes[b], slot, (static_cast<uint64_t>(cat) >> b) & 1);
            setBit(numeric_planes[b], slot, (enc >> b) & 1);
            setBit(tag_planes[b], slot, (tags >> b) & 1);
        }
    }
    void clearAttrs(size_t slot) {
        for (size_t b = 0; b < 64; ++b) {
            setBit(category_planes[b], slot, false);
            setBit(numeric_planes[b], slot, false);
            setBit(tag_planes[b], slot, false);
        }
    }
    void setAlive(size_t slot, bool v) { setBit(alive_bits, slot, v); }
    bool isAlive(size_t slot) const { return (alive_bits[slot / 64] >> (slot % 64)) & 1; }
    void setZone(size_t slot, uint8_t zone) {
        const uint8_t old = zone_of[slot];
        if (old != 255) {
            auto& zb = zone_bits[old];
            zb[slot / 64] &= ~(1ULL << (slot % 64));
        }
        zone_of[slot] = zone;
        if (zone != 255) {
            if (zone_bits.size() <= zone) zone_bits.resize(zone + 1, std::vector<uint64_t>(words(), 0));
            auto& zb = zone_bits[zone];
            if (zb.size() < words()) zb.resize(words(), 0);
            zb[slot / 64] |= (1ULL << (slot % 64));
        }
    }
    void ownerAdd(NodeId owner, size_t slot, Id id) {
        auto& list = owner_posting[owner];
        owner_pos[id] = list.size();
        list.push_back(slot);
    }
    void ownerRemove(NodeId owner, Id id) {
        auto it = owner_posting.find(owner);
        if (it == owner_posting.end()) fail("ownerRemove: missing owner posting");
        auto& list = it->second;
        auto pit = owner_pos.find(id);
        if (pit == owner_pos.end()) fail("ownerRemove: missing position");
        const size_t pos = pit->second, last = list.size() - 1;
        if (pos != last) {
            const size_t lastSlot = list[last];
            const Id lastId = slot_to_id[lastSlot];
            list[pos] = lastSlot;
            owner_pos[lastId] = pos;
        }
        list.pop_back();
        owner_pos.erase(pit);
        if (list.empty()) owner_posting.erase(it);
    }
};

// ---------------------------------------------------------------------------
// Query predicate (matches dynamic_triad semantics exactly).
// Numeric field enabled iff NOT(lo == -1 && hi == -1); endpoints are literal.
// ---------------------------------------------------------------------------
struct Query {
    size_t qidx = 0;
    int recursive = 0;
    NodeId anchor = 0;
    int64_t category = -1, lo = -1, hi = -1;
    int tagmode = 0;
    uint64_t tags = 0;
    int combine = 0;  // 0 = AND, 1 = OR
};

struct MaskWork {
    uint64_t bitset_word_ops = 0;
    uint64_t owner_rank_queries = 0;
    uint64_t owner_cache_hits = 0;
    uint64_t posting_slots_read = 0;
};

// Computes Live / Scope / Extra separately, then Live & Scope & Extra.
class MaskComputer {
    const Block& blk;
    const EulerTour& euler;
    const Query& q;
    MaskWork& work;
    std::unordered_map<NodeId, bool> scope_cache;

    size_t words() const { return blk.words(); }

    std::vector<uint64_t> categoryEq(int64_t cat) {
        const uint64_t cv = static_cast<uint64_t>(cat);
        std::vector<uint64_t> acc(words(), ~0ULL);
        for (size_t b = 0; b < 64; ++b) {
            const bool want = (cv >> b) & 1;
            for (size_t w = 0; w < words(); ++w) {
                acc[w] &= want ? blk.category_planes[b][w] : ~blk.category_planes[b][w];
                ++work.bitset_word_ops;
            }
        }
        return acc;
    }
    // MSB→LSB less-than circuit on encoded numeric planes.
    void numericLT(uint64_t bound, std::vector<uint64_t>& lt, std::vector<uint64_t>& eq) {
        lt.assign(words(), 0);
        eq.assign(words(), ~0ULL);
        for (int b = 63; b >= 0; --b) {
            const bool bit = (bound >> b) & 1;
            const auto& p = blk.numeric_planes[b];
            for (size_t w = 0; w < words(); ++w) {
                if (bit) {
                    lt[w] |= eq[w] & ~p[w];
                    eq[w] &= p[w];
                } else {
                    eq[w] &= ~p[w];
                }
                ++work.bitset_word_ops;
            }
        }
    }
    std::vector<uint64_t> numericRange(int64_t lo, int64_t hi) {
        std::vector<uint64_t> ltLo, eqLo, ltHi, eqHi;
        numericLT(Block::encodeNumeric(lo), ltLo, eqLo);
        numericLT(Block::encodeNumeric(hi), ltHi, eqHi);
        std::vector<uint64_t> acc(words());
        for (size_t w = 0; w < words(); ++w) {
            acc[w] = ~ltLo[w] & (ltHi[w] | eqHi[w]);
            ++work.bitset_word_ops;
        }
        return acc;
    }
    std::vector<uint64_t> tagMask() {
        std::vector<uint64_t> acc;
        switch (q.tagmode) {
            case 1: {  // Q ⊆ ITEM
                acc.assign(words(), ~0ULL);
                for (size_t t = 0; t < 64; ++t) {
                    if (!((q.tags >> t) & 1)) continue;
                    for (size_t w = 0; w < words(); ++w) { acc[w] &= blk.tag_planes[t][w]; ++work.bitset_word_ops; }
                }
                break;
            }
            case 2: {  // Q ∩ ITEM ≠ ∅
                acc.assign(words(), 0);
                for (size_t t = 0; t < 64; ++t) {
                    if (!((q.tags >> t) & 1)) continue;
                    for (size_t w = 0; w < words(); ++w) { acc[w] |= blk.tag_planes[t][w]; ++work.bitset_word_ops; }
                }
                break;
            }
            case 4: {  // ITEM ⊆ Q
                acc.assign(words(), ~0ULL);
                for (size_t t = 0; t < 64; ++t) {
                    if ((q.tags >> t) & 1) continue;
                    for (size_t w = 0; w < words(); ++w) { acc[w] &= ~blk.tag_planes[t][w]; ++work.bitset_word_ops; }
                }
                break;
            }
            case 3: {  // ITEM == Q  (mode1 AND mode4)
                acc.assign(words(), ~0ULL);
                for (size_t t = 0; t < 64; ++t) {
                    const bool in = (q.tags >> t) & 1;
                    for (size_t w = 0; w < words(); ++w) {
                        acc[w] &= in ? blk.tag_planes[t][w] : ~blk.tag_planes[t][w];
                        ++work.bitset_word_ops;
                    }
                }
                break;
            }
            default: fail("unknown tagmode");
        }
        return acc;
    }
    std::vector<uint64_t> scopeMask() {
        std::vector<uint64_t> acc(words(), 0);
        for (const auto& [owner, slots] : blk.owner_posting) {
            auto it = scope_cache.find(owner);
            bool pass;
            if (it != scope_cache.end()) { pass = it->second; ++work.owner_cache_hits; }
            else {
                pass = q.recursive ? euler.contains(q.anchor, owner) : (owner == q.anchor);
                scope_cache[owner] = pass;
                ++work.owner_rank_queries;
            }
            if (!pass) continue;
            for (size_t s : slots) {
                acc[s / 64] |= (1ULL << (s % 64));
                ++work.posting_slots_read;
            }
        }
        return acc;
    }

public:
    MaskComputer(const Block& b, const EulerTour& e, const Query& query, MaskWork& w)
        : blk(b), euler(e), q(query), work(w) {}

    // Extra mask over enabled fields only. AND: start all-ones; OR: start zero.
    // No enabled field → all-ones (extra = true in canonical semantics).
    std::vector<uint64_t> extraMask() {
        const bool useOr = q.combine != 0;
        std::vector<uint64_t> acc(words(), useOr ? 0 : ~0ULL);
        bool any = false;
        auto add = [&](const std::vector<uint64_t>& m) {
            any = true;
            for (size_t w = 0; w < words(); ++w) {
                acc[w] = useOr ? (acc[w] | m[w]) : (acc[w] & m[w]);
                ++work.bitset_word_ops;
            }
        };
        if (q.category != -1) add(categoryEq(q.category));
        if (!(q.lo == -1 && q.hi == -1)) add(numericRange(q.lo, q.hi));
        if (q.tagmode != 0) add(tagMask());
        if (!any) acc.assign(words(), ~0ULL);
        return acc;
    }

    // Final: Live & Scope & Extra. Components constructed independently.
    std::vector<uint64_t> compute() {
        auto scope = scopeMask();
        auto extra = extraMask();
        std::vector<uint64_t> acc(words());
        for (size_t w = 0; w < words(); ++w) {
            acc[w] = blk.alive_bits[w] & scope[w] & extra[w];
            ++work.bitset_word_ops;
        }
        return acc;
    }
};

// ---------------------------------------------------------------------------
// WQR index.
// ---------------------------------------------------------------------------
enum class Arm { GeometryRoute, BlockGate, WQR, EagerWQR };

struct ProxyMeta {
    size_t block = NONE;     // block index (registry index; NONE = retired)
    size_t generation = 0;
    uint8_t zone = 0;
    bool live = false;
};

struct Location { size_t block = NONE, slot = NONE; };

struct Item { NodeId node = 0; int64_t category = 0, numeric = 0; uint64_t tags = 0; bool alive = false; };

struct QueryCounters {
    uint64_t portal_distances = 0, local_distances = 0;
    uint64_t masks_prepared = 0;
    MaskWork mask_work;
    size_t portal_returned = 0, blocks_selected = 0;
    size_t local_searches = 0;
    std::vector<size_t> selected_blocks;
    std::vector<size_t> reference_blocks;  // exact portal order, offline attribution
};

struct MaintenanceCounters {
    uint64_t distance_calls = 0;    // graph build/rebuild/insert distances this event
    uint64_t ann_insertions = 0, ann_deletions = 0;
    uint64_t rebuilds = 0, splits = 0, compactions = 0, registry_compactions = 0;
    uint64_t posting_writes = 0;
};

class WQRIndex {
public:
    // Canonical state.
    std::vector<Item> items;
    std::vector<int> parent;
    size_t dim = 0;
    std::vector<float> vectors;   // canonical vectors (n * dim)

    EulerTour euler;
    std::vector<std::vector<Id>> direct;      // directory node → direct live ids
    std::unordered_map<Id, size_t> direct_pos;

    std::vector<Block> blocks;                // registry, retired entries kept until compaction
    size_t retired_blocks = 0;
    std::vector<Location> loc;                // id → (block, slot)
    std::vector<ProxyMeta> proxies;
    size_t live_proxies = 0, dead_proxies = 0;
    std::unique_ptr<CountedL2> pspace;
    std::unique_ptr<hnswlib::HierarchicalNSW<float>> pgraph;
    std::unique_ptr<CountedL2> build_space;                 // partitioning / proxy / zone distances
    size_t registry_generation = 0;

    const float* vec(Id id) const { return vectors.data() + id * dim; }

    void init(size_t n_items, size_t dimension) {
        dim = dimension;
        items.assign(n_items, Item{});
        loc.assign(n_items, Location{});
        vectors.assign(n_items * dim, 0);
        pspace = std::make_unique<CountedL2>(dimension);
        build_space = std::make_unique<CountedL2>(dimension);
    }

    // ---- geometry helpers: ALL distances via CountedL2 (RawL2Space kernel) ----
    float dist(const float* a, const float* b) const { return build_space->cdist(a, b); }

    // Deterministic balanced partition (spec §4.1). Returns blocks of ids.
    std::vector<std::vector<Id>> partition(std::vector<Id> ids) const {
        std::vector<std::vector<Id>> out;
        std::vector<std::vector<Id>> stack{std::move(ids)};
        while (!stack.empty()) {
            auto cur = std::move(stack.back()); stack.pop_back();
            if (cur.size() <= B0) { out.push_back(std::move(cur)); continue; }
            const Id a = *std::min_element(cur.begin(), cur.end());
            auto farthest = [&](Id pivot) {
                Id best = pivot; float bd = -1;
                for (Id x : cur) {
                    if (x == pivot) continue;
                    const float d = build_space->cdist(vec(x), vec(pivot));
                    if (d > bd || (d == bd && x < best)) { bd = d; best = x; }
                }
                return best;
            };
            const Id b = farthest(a), c = farthest(b);
            std::vector<std::pair<float, Id>> keyed;
            keyed.reserve(cur.size());
            for (Id x : cur)
                keyed.emplace_back(build_space->cdist(vec(x), vec(b)) - build_space->cdist(vec(x), vec(c)), x);
            std::sort(keyed.begin(), keyed.end());
            const size_t mid = keyed.size() / 2;
            std::vector<Id> left, right;
            for (size_t i = 0; i < mid; ++i) left.push_back(keyed[i].second);
            for (size_t i = mid; i < keyed.size(); ++i) right.push_back(keyed[i].second);
            stack.push_back(std::move(left));
            stack.push_back(std::move(right));
        }
        return out;
    }

    // Farthest-first proxy selection (spec §3.3): first = smallest id; then
    // max min-distance, tie by id. Copies vectors into independent proxies.
    void selectProxies(Block& blk, const std::vector<Id>& ids) {
        const size_t r = std::min(PROXIES_PER_BLOCK, ids.size());
        std::vector<Id> chosen{*std::min_element(ids.begin(), ids.end())};
        std::vector<float> nearest(ids.size(), std::numeric_limits<float>::infinity());
        for (size_t i = 0; i < ids.size(); ++i)
            nearest[i] = build_space->cdist(vec(ids[i]), vec(chosen[0]));
        while (chosen.size() < r) {
            Id best = NONE; float bd = -1;
            for (size_t i = 0; i < ids.size(); ++i) {
                const Id x = ids[i];
                if (std::find(chosen.begin(), chosen.end(), x) != chosen.end()) continue;
                if (nearest[i] > bd || (nearest[i] == bd && x < best)) { bd = nearest[i]; best = x; }
            }
            chosen.push_back(best);
            for (size_t i = 0; i < ids.size(); ++i)
                nearest[i] = std::min(nearest[i], build_space->cdist(vec(ids[i]), vec(best)));
        }
        blk.proxies.clear();
        blk.proxy_labels.clear();
        blk.zone_bits.clear();
        for (Id pid : chosen)
            blk.proxies.emplace_back(vec(pid), vec(pid) + dim);
        blk.zone_bits.assign(chosen.size(), std::vector<uint64_t>(blk.words(), 0));
        // Assign every live object to its nearest proxy (tie → lower zone).
        for (Id id : ids) {
            const size_t slot = loc[id].slot;
            uint8_t winner = 0; float bd = std::numeric_limits<float>::infinity();
            for (size_t z = 0; z < blk.proxies.size(); ++z) {
                const float d = build_space->cdist(vec(id), blk.proxies[z].data());
                if (d < bd) { bd = d; winner = static_cast<uint8_t>(z); }
            }
            blk.setZone(slot, winner);
        }
        // Register proxies globally and insert into proxy graph.
        for (size_t z = 0; z < blk.proxies.size(); ++z) {
            proxies.push_back({registryIndexOf(blk), blk.generation, static_cast<uint8_t>(z), true});
            const size_t label = proxies.size() - 1;
            blk.proxy_labels.push_back(label);
            ++live_proxies;
            pgraphAdd(label, blk.proxies[z].data());
        }
    }

    size_t registryIndexOf(const Block& blk) const {
        return static_cast<size_t>(&blk - blocks.data());
    }

    void pgraphAdd(size_t label, const float* data) {
        if (!pgraph) {
            pgraph = std::make_unique<hnswlib::HierarchicalNSW<float>>(
                pspace.get(), std::max<size_t>(64, proxies.size() * 2), HNSW_M, HNSW_EFC, HNSW_SEED);
        }
        if (pgraph->cur_element_count == pgraph->max_elements_)
            pgraph->resizeIndex(std::max(pgraph->max_elements_ * 2, pgraph->cur_element_count + 1));
        pgraph->addPoint(data, label);
    }

    void pgraphRemove(size_t label) {
        if (!pgraph) return;
        pgraph->markDelete(label);
        proxies[label].live = false;
        --live_proxies; ++dead_proxies;
    }

    // Rebuild proxy graph from live proxies; labels kept stable via new build.
    void rebuildProxyGraph(MaintenanceCounters& mc) {
        auto fresh = std::make_unique<hnswlib::HierarchicalNSW<float>>(
            pspace.get(), std::max<size_t>(64, live_proxies * 2), HNSW_M, HNSW_EFC, HNSW_SEED);
        for (size_t label = 0; label < proxies.size(); ++label) {
            if (!proxies[label].live) continue;
            const auto& pm = proxies[label];
            fresh->addPoint(blocks[pm.block].proxies[pm.zone].data(), label);
            ++mc.ann_insertions;
        }
        pgraph = std::move(fresh);
        dead_proxies = 0;
        ++mc.rebuilds;
    }

    // Build a block from the given ids (all live). Slots assigned in id order.
    Block& buildBlockFromIds(const std::vector<Id>& ids, size_t capacity) {
        blocks.emplace_back(dim);
        Block& blk = blocks.back();
        blk.setCapacity(capacity);
        blk.generation = registry_generation;
        blk.graph = std::make_unique<hnswlib::HierarchicalNSW<float>>(
            blk.space.get(), std::max<size_t>(BLOCK_MIN_CAP, 2 * capacity), HNSW_M, HNSW_EFC, HNSW_SEED);
        for (Id id : ids) {
            const size_t slot = blk.live++;
            blk.slot_to_id[slot] = id;
            blk.next_slot = blk.live;
            blk.setAlive(slot, true);
            blk.writeAttrs(slot, items[id].category, items[id].numeric, items[id].tags);
            blk.ownerAdd(items[id].node, slot, id);
            loc[id] = {blocks.size() - 1, slot};
            blk.graph->addPoint(vec(id), id);
        }
        selectProxies(blk, ids);
        return blk;
    }

    // ---- lifecycle -------------------------------------------------------

    void retireBlock(size_t bi, MaintenanceCounters& mc) {
        Block& blk = blocks[bi];
        for (size_t label : blk.proxy_labels) pgraphRemove(label);
        blk.retired = true;
        blk.graph.reset();
        ++retired_blocks;
        ++mc.registry_compactions;  // retire event recorded; physical compaction below
        if (pgraph && live_proxies + dead_proxies > 0 &&
            double(dead_proxies) / double(live_proxies + dead_proxies) > DEAD_RATIO)
            rebuildProxyGraph(mc);
        if (blocks.size() > 0 && retired_blocks * 5 > blocks.size())
            compactRegistry(mc);
    }

    void compactRegistry(MaintenanceCounters& mc) {
        // Move live blocks to front; remap proxy block references.
        std::vector<size_t> remap(blocks.size(), NONE);
        size_t out = 0;
        for (size_t i = 0; i < blocks.size(); ++i) {
            if (blocks[i].retired) continue;
            remap[i] = out;
            if (out != i) {
                blocks[out] = std::move(blocks[i]);
                for (Id id = 0; id < items.size(); ++id)
                    if (loc[id].block == i) loc[id].block = out;
            }
            ++out;
        }
        blocks.erase(blocks.begin() + out, blocks.end());
        for (auto& pm : proxies) if (pm.live) pm.block = remap[pm.block];
        retired_blocks = 0;
        ++mc.registry_compactions;
    }

    // Rebuild local graph live-only, keeping slots.
    void rebuildLocalGraph(Block& blk, MaintenanceCounters& mc) {
        auto fresh = std::make_unique<hnswlib::HierarchicalNSW<float>>(
            blk.space.get(), std::max<size_t>(BLOCK_MIN_CAP, blk.capacity), HNSW_M, HNSW_EFC, HNSW_SEED);
        for (size_t s = 0; s < blk.capacity; ++s) {
            if (blk.slot_to_id[s] == NONE || !blk.isAlive(s)) continue;
            fresh->addPoint(vec(blk.slot_to_id[s]), blk.slot_to_id[s]);
            ++mc.ann_insertions;
        }
        blk.graph = std::move(fresh);
        ++mc.rebuilds;
    }

    // Compact slots (tombstone compaction): live objects to front, rebuild
    // bitplanes/postings/zone bits; keep proxy vectors and per-object zones.
    void compactBlock(Block& blk, MaintenanceCounters& mc) {
        std::vector<Id> liveIds;
        std::vector<uint8_t> zones;
        for (size_t s = 0; s < blk.capacity; ++s)
            if (blk.slot_to_id[s] != NONE && blk.isAlive(s)) {
                liveIds.push_back(blk.slot_to_id[s]);
                zones.push_back(blk.zone_of[s]);
            }
        size_t cap = blk.capacity;
        if (blk.live < cap / 4) cap = std::max(BLOCK_MIN_CAP, 2 * blk.live);
        auto savedProxies = blk.proxies;
        auto savedLabels = blk.proxy_labels;
        blk.setCapacity(cap);
        blk.free_slots.clear();
        blk.owner_posting.clear();
        blk.owner_pos.clear();
        blk.live = 0;
        blk.proxies = std::move(savedProxies);
        blk.proxy_labels = std::move(savedLabels);
        blk.zone_bits.assign(blk.proxies.size(), std::vector<uint64_t>(blk.words(), 0));
        for (size_t i = 0; i < liveIds.size(); ++i) {
            const Id id = liveIds[i];
            const size_t slot = blk.live++;
            blk.slot_to_id[slot] = id;
            blk.setAlive(slot, true);
            blk.writeAttrs(slot, items[id].category, items[id].numeric, items[id].tags);
            blk.ownerAdd(items[id].node, slot, id);
            loc[id] = {registryIndexOf(blk), slot};
            blk.setZone(slot, zones[i]);
        }
        blk.next_slot = blk.live;
        rebuildLocalGraph(blk, mc);
        ++mc.compactions;
    }

    // Split a block whose live count hit 2*B0 when inserting newId.
    void splitBlock(size_t bi, Id newId, MaintenanceCounters& mc) {
        std::vector<Id> ids;
        for (size_t s = 0; s < blocks[bi].capacity; ++s)
            if (blocks[bi].slot_to_id[s] != NONE && blocks[bi].isAlive(s))
                ids.push_back(blocks[bi].slot_to_id[s]);
        if (newId != NONE) ids.push_back(newId);
        // Balanced 2-way split by median key (spec §7: same §4 method).
        const Id a = *std::min_element(ids.begin(), ids.end());
        auto farthest = [&](Id pivot) {
            Id best = pivot; float bd = -1;
            for (Id x : ids) {
                if (x == pivot) continue;
                const float d = build_space->cdist(vec(x), vec(pivot));
                if (d > bd || (d == bd && x < best)) { bd = d; best = x; }
            }
            return best;
        };
        const Id b = farthest(a), c = farthest(b);
        std::vector<std::pair<float, Id>> keyed;
        for (Id x : ids)
            keyed.emplace_back(build_space->cdist(vec(x), vec(b)) - build_space->cdist(vec(x), vec(c)), x);
        std::sort(keyed.begin(), keyed.end());
        const size_t mid = keyed.size() / 2;
        std::vector<Id> left, right;
        for (size_t i = 0; i < mid; ++i) left.push_back(keyed[i].second);
        for (size_t i = mid; i < keyed.size(); ++i) right.push_back(keyed[i].second);
        retireBlock(bi, mc);
        ++registry_generation;
        ++mc.splits;
        buildBlockFromIds(left, B0);
        buildBlockFromIds(right, B0);
        mc.ann_insertions += ids.size();  // local inserts counted inside buildBlock addPoint? counted separately
    }

    // Choose zone for an object within a block: nearest proxy.
    uint8_t nearestZone(const Block& blk, Id id) {
        uint8_t winner = 0; float bd = std::numeric_limits<float>::infinity();
        for (size_t z = 0; z < blk.proxies.size(); ++z) {
            const float d = build_space->cdist(vec(id), blk.proxies[z].data());
            if (d < bd) { bd = d; winner = static_cast<uint8_t>(z); }
        }
        return winner;
    }

    // Route an unqualified insert: nearest proxy's block (spec §7).
    size_t routeInsertBlock(const float* qvec) {
        if (!pgraph || live_proxies == 0) return NONE;
        const size_t k = std::min<size_t>(8, live_proxies);
        auto hits = pgraph->searchKnn(qvec, k);
        float bd = std::numeric_limits<float>::infinity();
        size_t best = NONE;
        while (!hits.empty()) {
            if (hits.top().first < bd) { bd = hits.top().first; best = hits.top().second; }
            hits.pop();
        }
        if (best == NONE) return NONE;
        return proxies[best].block;
    }

    void insertPhysical(Id id, MaintenanceCounters& mc) {
        if (blocks.empty()) {
            buildBlockFromIds({id}, B0);
            ++mc.ann_insertions;
            return;
        }
        size_t bi = routeInsertBlock(vec(id));
        if (bi == NONE || blocks[bi].retired) { buildBlockFromIds({id}, B0); ++mc.ann_insertions; return; }
        if (blocks[bi].live == BLOCK_MAX_LIVE) {
            splitBlock(bi, id, mc);  // new object is included in the split halves
            return;
        }
        insertIntoBlock(bi, id, mc);
    }

    void insertIntoBlock(size_t bi, Id id, MaintenanceCounters& mc) {
        Block& blk = blocks[bi];
        // Slot selection: reuse free slot, else extend.
        size_t slot;
        if (!blk.free_slots.empty()) { slot = blk.free_slots.back(); blk.free_slots.pop_back(); }
        else if (blk.next_slot < blk.capacity) slot = blk.next_slot++;
        else {
            const size_t newCap = std::min(BLOCK_MAX_LIVE, blk.capacity * 2);
            if (newCap <= blk.capacity) fail("insert: block at max capacity without split");
            blk.growCapacity(newCap);
            slot = blk.next_slot++;
        }
        blk.slot_to_id[slot] = id;
        blk.setAlive(slot, true);
        blk.writeAttrs(slot, items[id].category, items[id].numeric, items[id].tags);
        blk.ownerAdd(items[id].node, slot, id);
        blk.setZone(slot, blk.proxies.empty() ? 255 : nearestZone(blk, id));
        loc[id] = {bi, slot};
        ++blk.live;
        // Graph insert: same label tombstone with same vector → unmark; else add.
        auto it = blk.graph->label_lookup_.find(id);
        if (it != blk.graph->label_lookup_.end() && blk.graph->isMarkedDeleted(it->second)) {
            blk.graph->unmarkDelete(id);
        } else {
            if (blk.graph->cur_element_count == blk.graph->max_elements_) {
                if (blk.live <= blk.capacity) rebuildLocalGraph(blk, mc);  // reclaim tombstones first
                else blk.graph->resizeIndex(blk.graph->max_elements_ * 2);
            }
            blk.graph->addPoint(vec(id), id);
        }
        ++mc.ann_insertions;
    }

    // ---- public event API --------------------------------------------------

    void build(const std::vector<int>& parentArr,
               const std::vector<Item>& initialItems,
               const std::vector<float>& base) {
        parent = parentArr;
        items = initialItems;
        vectors = base;
        euler.build(parent);
        direct.assign(parent.size(), {});
        std::vector<Id> liveIds;
        for (Id id = 0; id < items.size(); ++id) {
            if (!items[id].alive) continue;
            liveIds.push_back(id);
            direct_pos[id] = direct[items[id].node].size();
            direct[items[id].node].push_back(id);
        }
        if (liveIds.empty()) return;  // empty index: no blocks, no proxy graph
        for (const auto& part : partition(liveIds))
            buildBlockFromIds(part, B0);
    }

    void apply_V(Id id, int64_t cat, int64_t num) {
        if (!items[id].alive) fail("V: item not live");
        auto& blk = blocks[loc[id].block];
        blk.writeAttrs(loc[id].slot, cat, num, items[id].tags);
        items[id].category = cat; items[id].numeric = num;
    }
    void apply_T(Id id, uint64_t tags) {
        if (!items[id].alive) fail("T: item not live");
        auto& blk = blocks[loc[id].block];
        blk.writeAttrs(loc[id].slot, items[id].category, items[id].numeric, tags);
        items[id].tags = tags;
    }
    void apply_M(Id id, NodeId dst) {
        if (!items[id].alive) fail("M: item not live");
        auto& blk = blocks[loc[id].block];
        blk.ownerRemove(items[id].node, id);
        blk.ownerAdd(dst, loc[id].slot, id);
        // direct directory posting
        auto& from = direct[items[id].node];
        const size_t pos = direct_pos.at(id), last = from.size() - 1;
        if (pos != last) { from[pos] = from[last]; direct_pos[from[last]] = pos; }
        from.pop_back();
        direct_pos[id] = direct[dst].size();
        direct[dst].push_back(id);
        items[id].node = dst;
    }
    void apply_B(NodeId src, NodeId dst, MaintenanceCounters& mc) {
        // Freeze current live ids in subtree(src), then move each (spec: B ≠ R).
        std::vector<NodeId> stack{src};
        std::vector<NodeId> subtreeNodes;
        while (!stack.empty()) {
            const NodeId v = stack.back(); stack.pop_back();
            subtreeNodes.push_back(v);
            for (NodeId c = 0; c < parent.size(); ++c)
                if (parent[c] >= 0 && static_cast<NodeId>(parent[c]) == v) stack.push_back(c);
        }
        std::vector<Id> frozen;
        for (NodeId n : subtreeNodes)
            for (Id id : direct[n]) frozen.push_back(id);
        for (Id id : frozen) { apply_M(id, dst); ++mc.posting_writes; }
    }

    void apply_R(NodeId src, NodeId dst) {
        euler.reparent(src, dst);
        parent[src] = static_cast<int>(dst);
    }
    void apply_D(Id id, MaintenanceCounters& mc) {
        if (!items[id].alive) fail("D: item not live");
        const auto lc = loc[id];
        Block& blk = blocks[lc.block];
        blk.setAlive(lc.slot, false);
        blk.clearAttrs(lc.slot);
        blk.ownerRemove(items[id].node, id);
        blk.setZone(lc.slot, 255);
        blk.graph->markDelete(id);
        ++mc.ann_deletions;
        // Slot retained for potential restore; also available for reuse.
        blk.free_slots.push_back(lc.slot);
        // direct posting removal
        auto& from = direct[items[id].node];
        const size_t pos = direct_pos.at(id), last = from.size() - 1;
        if (pos != last) { from[pos] = from[last]; direct_pos[from[last]] = pos; }
        from.pop_back();
        direct_pos.erase(id);
        items[id].alive = false;
        --blk.live;
        if (blk.live == 0) { retireBlock(lc.block, mc); return; }
        const size_t dead = blk.graph->num_deleted_;
        const size_t total = blk.graph->cur_element_count;
        if (total > 0 && double(dead) / double(total) > DEAD_RATIO) compactBlock(blk, mc);
    }
    void apply_I(Id id, NodeId node, int64_t cat, int64_t num, uint64_t tags, MaintenanceCounters& mc) {
        if (items[id].alive) fail("I: item already live");
        items[id].node = node; items[id].category = cat; items[id].numeric = num;
        items[id].tags = tags; items[id].alive = true;
        // direct posting add
        direct_pos[id] = direct[node].size();
        direct[node].push_back(id);
        // Restore old location when retained and block alive.
        const auto lc = loc[id];
        if (lc.block != NONE && lc.block < blocks.size() && !blocks[lc.block].retired) {
            Block& blk = blocks[lc.block];
            if (lc.slot < blk.capacity && blk.slot_to_id[lc.slot] == id && !blk.isAlive(lc.slot)) {
                auto git = blk.graph->label_lookup_.find(id);
                if (git != blk.graph->label_lookup_.end() && blk.graph->isMarkedDeleted(git->second)) {
                    // Remove slot from free list (it was retained).
                    auto fit = std::find(blk.free_slots.begin(), blk.free_slots.end(), lc.slot);
                    if (fit != blk.free_slots.end()) blk.free_slots.erase(fit);
                    blk.setAlive(lc.slot, true);
                    blk.writeAttrs(lc.slot, cat, num, tags);
                    blk.ownerAdd(node, lc.slot, id);
                    blk.setZone(lc.slot, blk.proxies.empty() ? 255 : nearestZone(blk, id));
                    blk.graph->unmarkDelete(id);
                    ++blk.live;
                    ++mc.ann_insertions;
                    return;
                }
            }
        }
        insertPhysical(id, mc);
    }

    // ---- query --------------------------------------------------------------

    struct PreparedMasks {
        std::vector<std::vector<uint64_t>> exact;
        std::vector<std::vector<uint64_t>> active_zones;
        std::vector<bool> ready;
    };

    PreparedMasks prepareAll(const Query& q, QueryCounters& qc) const {
        PreparedMasks pm;
        pm.exact.resize(blocks.size());
        pm.active_zones.resize(blocks.size());
        pm.ready.assign(blocks.size(), false);
        for (size_t bi = 0; bi < blocks.size(); ++bi) {
            if (blocks[bi].retired) continue;
            prepareBlock(bi, q, qc, pm);
        }
        return pm;
    }

    void prepareBlock(size_t bi, const Query& q, QueryCounters& qc, PreparedMasks& pm) const {
        if (pm.ready[bi] || blocks[bi].retired) return;
        pm.ready[bi] = true;
        ++qc.masks_prepared;
        MaskComputer mc(blocks[bi], euler, q, qc.mask_work);
        pm.exact[bi] = mc.compute();
        const auto& blk = blocks[bi];
        auto& az = pm.active_zones[bi];
        az.assign(blk.proxies.size(), 0);
        for (size_t z = 0; z < blk.proxies.size(); ++z) {
            bool any = false;
            if (z < blk.zone_bits.size()) {
                for (size_t w = 0; w < blk.words(); ++w)
                    if (pm.exact[bi][w] & blk.zone_bits[z][w]) { any = true; break; }
            }
            az[z] = any;
        }
    }

    struct PortalFilter : hnswlib::BaseFilterFunctor {
        const WQRIndex& idx;
        const Query& q;
        Arm arm;
        QueryCounters& qc;
        PreparedMasks& pm;
        PortalFilter(const WQRIndex& i, const Query& query, Arm a, QueryCounters& c, PreparedMasks& p)
            : idx(i), q(query), arm(a), qc(c), pm(p) {}
        bool operator()(hnswlib::labeltype label) override {
            const auto& meta = idx.proxies[label];
            if (!meta.live) return false;
            const size_t bi = meta.block;
            if (bi == NONE || bi >= idx.blocks.size()) return false;
            const auto& blk = idx.blocks[bi];
            if (blk.retired || blk.generation != meta.generation) return false;
            if (arm == Arm::GeometryRoute) return true;
            idx.prepareBlock(bi, q, qc, pm);
            if (arm == Arm::BlockGate) {
                for (size_t w = 0; w < blk.words(); ++w)
                    if (pm.exact[bi][w]) return true;
                return false;
            }
            return meta.zone < pm.active_zones[bi].size() && pm.active_zones[bi][meta.zone];
        }
    };

    struct LocalFilter : hnswlib::BaseFilterFunctor {
        const WQRIndex& idx;
        size_t bi;
        const std::vector<uint64_t>& mask;
        LocalFilter(const WQRIndex& i, size_t b, const std::vector<uint64_t>& m)
            : idx(i), bi(b), mask(m) {}
        bool operator()(hnswlib::labeltype label) override {
            const Id id = static_cast<Id>(label);      // external object ID
            const auto lc = idx.loc[id];               // id → (block, slot)
            if (lc.block != bi) return false;
            const size_t slot = lc.slot;
            if (slot >= mask.size() * 64) return false;
            return (mask[slot / 64] >> (slot % 64)) & 1;
        }
    };

    // Exact portal order for offline attribution (not used online).
    std::vector<size_t> exactPortalBlocks(const float* qvec, const Query& q, Arm arm,
                                          size_t P, size_t L, QueryCounters& qc) const {
        auto pm = prepareAll(q, qc);
        std::vector<std::pair<float, size_t>> order;
        for (size_t label = 0; label < proxies.size(); ++label) {
            const auto& meta = proxies[label];
            if (!meta.live) continue;
            const size_t bi = meta.block;
            const auto& blk = blocks[bi];
            if (blk.retired || blk.generation != meta.generation) continue;
            bool allow = true;
            if (arm == Arm::BlockGate) {
                allow = false;
                for (size_t w = 0; w < blk.words(); ++w) if (pm.exact[bi][w]) { allow = true; break; }
            } else if (arm == Arm::WQR || arm == Arm::EagerWQR) {
                allow = meta.zone < pm.active_zones[bi].size() && pm.active_zones[bi][meta.zone];
            }
            if (!allow) continue;
            const float d = build_space->cdist(qvec, blk.proxies[meta.zone].data());
            order.emplace_back(d, label);
        }
        std::sort(order.begin(), order.end());
        std::vector<size_t> result;
        std::unordered_set<size_t> seen;
        for (size_t i = 0; i < order.size() && i < P; ++i) {
            const size_t bi = proxies[order[i].second].block;
            if (seen.insert(bi).second) {
                result.push_back(bi);
                if (result.size() >= L) break;
            }
        }
        return result;
    }

    std::vector<std::pair<float, Id>> query(const float* qvec, const Query& q, size_t k,
                                            size_t L, size_t portal_ef, size_t local_ef,
                                            Arm arm, QueryCounters& qc) {
        if (!pgraph || live_proxies == 0) return {};
        const size_t P = std::min(4 * L, live_proxies);
        if (P == 0) return {};
        PreparedMasks pm;
        pm.exact.resize(blocks.size());
        pm.active_zones.resize(blocks.size());
        pm.ready.assign(blocks.size(), false);
        if (arm == Arm::EagerWQR) {
            for (size_t bi = 0; bi < blocks.size(); ++bi)
                if (!blocks[bi].retired) prepareBlock(bi, q, qc, pm);
        }
        PortalFilter pf(*this, q, arm, qc, pm);
        pgraph->setEf(std::max(portal_ef, P));
        const uint64_t p0 = pspace->calls;
        auto hits = pgraph->searchKnn(qvec, P, &pf);
        qc.portal_distances += pspace->calls - p0;
        qc.portal_returned = hits.size();
        std::vector<std::pair<float, size_t>> ordered;
        while (!hits.empty()) { ordered.emplace_back(hits.top().first, hits.top().second); hits.pop(); }
        std::sort(ordered.begin(), ordered.end());
        std::unordered_set<size_t> seen;
        for (const auto& [d, label] : ordered) {
            const size_t bi = proxies[label].block;
            if (seen.insert(bi).second) {
                qc.selected_blocks.push_back(bi);
                if (qc.selected_blocks.size() >= L) break;
            }
        }
        qc.blocks_selected = qc.selected_blocks.size();
        std::priority_queue<std::pair<float, Id>> heap;
        for (size_t bi : qc.selected_blocks) {
            prepareBlock(bi, q, qc, pm);
            const auto& blk = blocks[bi];
            size_t pop = 0;
            for (size_t w = 0; w < blk.words(); ++w) pop += __builtin_popcountll(pm.exact[bi][w]);
            if (pop == 0) continue;
            const size_t kb = std::min(k, pop);
            LocalFilter lf(*this, bi, pm.exact[bi]);
            blk.graph->setEf(std::max(local_ef, kb));
            const uint64_t l0 = blk.space->calls;
            auto lh = blk.graph->searchKnn(qvec, kb, &lf);
            qc.local_distances += blk.space->calls - l0;
            ++qc.local_searches;
            while (!lh.empty()) {
                heap.emplace(lh.top().first, static_cast<Id>(lh.top().second));
                lh.pop();
                if (heap.size() > k) heap.pop();
            }
        }
        std::vector<std::pair<float, Id>> out;
        while (!heap.empty()) { out.push_back(heap.top()); heap.pop(); }
        std::sort(out.begin(), out.end());
        return out;
    }
};

} // namespace wqr
