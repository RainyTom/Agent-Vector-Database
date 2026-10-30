// WQR: Witness-Qualified Routing — production C++ implementation for dynamic
// hierarchical multi-attribute filtered vector search. Frozen spec: researches/
// nested-attr-vector-search/2026-10-30-exps/wqr-index-spec.md. No modification
// to existing NodeANN/dynamic_triad behavior; no new search space exploration.
#pragma once

#include "raw_l2_space.h"
#include "hnswlib/hnswlib.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <queue>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wqr {

using Id = uint32_t;
using NodeId = uint32_t;
using BlockId = uint32_t;
using ProxyId = uint32_t;

// ---------------------------------------------------------------------------
// Immutable raw vector container (canonical copy). HNSW copies payload again;
// we do not pretend there is only one copy in memory.
// ---------------------------------------------------------------------------
struct VectorStore {
    size_t dim = 0;
    std::vector<float> data;
    void init(size_t d) { dim = d; }
    const float* at(Id id) const { return data.data() + size_t(id) * dim; }
    void set(Id id, const float* v) {
        if (id * dim >= data.size()) data.resize((id + 1) * dim);
        std::copy(v, v + dim, data.begin() + id * dim);
    }
    float l2(const float* a, const float* b) const {
        float s = 0;
        for (size_t i = 0; i < dim; ++i) { const float d = a[i] - b[i]; s += d * d; }
        return s;
    }
};

// ---------------------------------------------------------------------------
// Deterministic geometric partitioning (spec §4.1). No attribute input.
// ---------------------------------------------------------------------------
struct Partitioner {
    static constexpr size_t B0 = 1024;
    const VectorStore& vs;
    size_t dim;
    explicit Partitioner(const VectorStore& v) : vs(v), dim(v.dim) {}

    // Split items into two halves by median of (rawL2(x,b) - rawL2(x,c)).
    // Deterministic: ties broken by object ID.
    std::pair<std::vector<Id>, std::vector<Id>> split(const std::vector<Id>& items) const {
        if (items.size() <= B0) return {items, {}};
        const Id a = *std::min_element(items.begin(), items.end());
        auto farthest = [&](Id pivot) -> Id {
            Id best = pivot;
            float bestD = -1;
            for (Id x : items) {
                if (x == pivot) continue;
                const float d = vs.l2(vs.at(x), vs.at(pivot));
                if (d > bestD || (d == bestD && x < best)) { bestD = d; best = x; }
            }
            return best;
        };
        const Id b = farthest(a), c = farthest(b);
        std::vector<std::pair<float, Id>> keyed;
        keyed.reserve(items.size());
        for (Id x : items) {
            const float db = vs.l2(vs.at(x), vs.at(b));
            const float dc = vs.l2(vs.at(x), vs.at(c));
            keyed.emplace_back(db - dc, x);
        }
        std::sort(keyed.begin(), keyed.end());
        const size_t mid = keyed.size() / 2;
        std::vector<Id> left, right;
        for (size_t i = 0; i < mid; ++i) left.push_back(keyed[i].second);
        for (size_t i = mid; i < keyed.size(); ++i) right.push_back(keyed[i].second);
        return {left, right};
    }

    // Recursive balanced partition until every block <= B0.
    std::vector<std::vector<Id>> blocks(std::vector<Id> items) const {
        std::vector<std::vector<Id>> result;
        std::vector<std::vector<Id>> stack{std::move(items)};
        while (!stack.empty()) {
            auto current = std::move(stack.back()); stack.pop_back();
            if (current.size() <= B0) { result.push_back(std::move(current)); continue; }
            auto [l, r] = split(current);
            stack.push_back(std::move(l)); stack.push_back(std::move(r));
        }
        return result;
    }
};

// ---------------------------------------------------------------------------
// Euler tour implicit treap (spec §3.1). Order-statistics, cut/link, rank.
// Each directory node contributes enter(u) and exit(u) tokens.
// ---------------------------------------------------------------------------
struct EulerTour {
    struct Token {
        NodeId node = 0;
        bool is_enter = false;
        uint64_t priority = 0;
    };
    struct TreapNode {
        Token token;
        TreapNode* left = nullptr;
        TreapNode* right = nullptr;
        TreapNode* parent = nullptr;
        size_t size = 1;
    };
    // Deterministic random priorities seeded from spec seed 100.
    std::mt19937_64 rng{100};

    std::unordered_map<NodeId, TreapNode*> enter_token;
    std::unordered_map<NodeId, TreapNode*> exit_token;
    TreapNode* root = nullptr;
    size_t total_tokens = 0;

    static size_t sz(TreapNode* t) { return t ? t->size : 0; }
    static void pull(TreapNode* t) {
        if (!t) return;
        t->size = 1 + sz(t->left) + sz(t->right);
        if (t->left) t->left->parent = t;
        if (t->right) t->right->parent = t;
    }
    static void setParent(TreapNode* t, TreapNode* p) { if (t) t->parent = p; }

    // Split root into left (first pos elements) and right (rest).
    static std::pair<TreapNode*, TreapNode*> split(TreapNode* t, size_t pos) {
        if (!t) return {nullptr, nullptr};
        const size_t leftSize = sz(t->left);
        if (pos <= leftSize) {
            auto [l, r] = split(t->left, pos);
            setParent(l, nullptr);
            t->left = r; pull(t); t->parent = nullptr;
            return {l, t};
        }
        auto [l, r] = split(t->right, pos - leftSize - 1);
        t->right = l; pull(t); t->parent = nullptr;
        setParent(r, nullptr);
        return {t, r};
    }

    static TreapNode* merge(TreapNode* l, TreapNode* r) {
        if (!l) return r;
        if (!r) return l;
        if (l->token.priority > r->token.priority) {
            l->right = merge(l->right, r); pull(l); return l;
        }
        r->left = merge(l, r->left); pull(r); return r;
    }

    // Rank of token in current sequence (0-based).
    static size_t rank(TreapNode* node) {
        size_t result = sz(node->left);
        while (node->parent) {
            if (node == node->parent->right) result += 1 + sz(node->parent->left);
            node = node->parent;
        }
        return result;
    }

    TreapNode* newToken(NodeId node, bool enter) {
        auto* t = new TreapNode{{node, enter, rng()}, nullptr, nullptr, nullptr, 1};
        return t;
    }

    // Build initial Euler sequence from parent array (iterative, no recursion).
    void build(const std::vector<NodeId>& parent) {
        std::vector<std::vector<NodeId>> children(parent.size());
        for (NodeId v = 0; v < parent.size(); ++v)
            if (parent[v] != v && parent[v] < parent.size()) children[parent[v]].push_back(v);
        std::vector<NodeId> order;
        {
            std::vector<std::pair<NodeId, bool>> stack;
            for (NodeId v = 0; v < parent.size(); ++v)
                if (parent[v] == v) stack.emplace_back(v, true);
            while (!stack.empty()) {
                auto [v, entering] = stack.back(); stack.pop_back();
                if (entering) {
                    order.push_back(v);
                    stack.emplace_back(v, false);
                    for (auto it = children[v].rbegin(); it != children[v].rend(); ++it)
                        stack.emplace_back(*it, true);
                } else {
                    // exit token appended implicitly by loop
                }
            }
        }
        // Build treap from the enter/exit sequence.
        for (NodeId v : order) {
            auto* e = newToken(v, true);
            enter_token[v] = e;
            auto [l, r] = split(root, total_tokens);
            root = merge(merge(l, e), r);
            ++total_tokens;
        }
        // Append exit tokens in reverse order of enter (to close subtrees).
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            auto* x = newToken(*it, false);
            exit_token[*it] = x;
            auto [l, r] = split(root, total_tokens);
            root = merge(merge(l, x), r);
            ++total_tokens;
        }
    }

    // Cut subtree [enter(src), exit(src)] from sequence.
    std::pair<TreapNode*, TreapNode*> cut(NodeId src) {
        const size_t a = rank(enter_token.at(src));
        const size_t b = rank(exit_token.at(src)) + 1;
        auto [l, rest] = split(root, a);
        auto [mid, r] = split(rest, b - a);
        root = merge(l, r);
        return {mid, nullptr};
    }

    // Insert cut sequence before exit(dst).
    void link(TreapNode* seq, NodeId dst) {
        const size_t pos = rank(exit_token.at(dst));
        auto [l, r] = split(root, pos);
        root = merge(merge(l, seq), r);
    }

    // R: reparent src under dst. src must not be root; dst not inside src subtree.
    void reparent(NodeId src, NodeId dst) {
        if (src == dst) throw std::invalid_argument("R: src == dst");
        // Check dst not inside src subtree.
        const size_t es = rank(enter_token.at(src));
        const size_t xs = rank(exit_token.at(src));
        const size_t ed = rank(enter_token.at(dst));
        const size_t xd = rank(exit_token.at(dst));
        if (ed >= es && ed <= xs && xd >= es && xd <= xs)
            throw std::invalid_argument("R: dst inside src subtree");
        auto [seq, _] = cut(src);
        // After cut, recompute dst exit rank (it may have shifted).
        link(seq, dst);
    }

    // Recursive containment check: is `owner` inside subtree rooted at `anchor`?
    bool contains(NodeId anchor, NodeId owner) const {
        const size_t ea = rank(enter_token.at(anchor));
        const size_t xa = rank(exit_token.at(anchor));
        const size_t eo = rank(enter_token.at(owner));
        return ea <= eo && eo < xa;
    }

    ~EulerTour() {
        std::function<void(TreapNode*)> free = [&](TreapNode* t) {
            if (!t) return;
            free(t->left); free(t->right); delete t;
        };
        free(root);
    }
};

// ---------------------------------------------------------------------------
// Block: slots, attributes, owner postings, zone masks, local HNSW.
// ---------------------------------------------------------------------------
struct Block {
    static constexpr size_t B0 = 1024;
    static constexpr size_t MAX_CAPACITY = 2 * B0;
    static constexpr size_t MIN_CAPACITY = 16;

    size_t dim = 0;
    size_t capacity = B0;
    size_t live_count = 0;
    size_t generation = 0;

    // slot → object ID; SIZE_MAX = empty slot
    std::vector<Id> slot_to_id;
    // object ID → slot
    std::unordered_map<Id, size_t> id_to_slot;
    // alive bitmap (1 = alive)
    std::vector<uint64_t> alive_bits;
    // zone assignment per slot
    std::vector<uint8_t> zone_of;
    // zone masks: zone → bitmap of slots
    std::vector<std::vector<uint64_t>> zone_bits;

    // Attribute bitplanes (64 bits each, MSB first).
    std::vector<std::vector<uint64_t>> category_planes;  // category[bit][word]
    std::vector<std::vector<uint64_t>> numeric_bits;
    std::vector<std::vector<uint64_t>> tag_bits;

    // owner → list of slots (sparse posting)
    std::unordered_map<NodeId, std::vector<size_t>> owner_slots;
    // object ID → position in owner's posting list
    std::unordered_map<Id, size_t> owner_pos;

    // Local HNSW
    std::unique_ptr<hnswlib::HierarchicalNSW<float>> graph;
    std::unique_ptr<RawL2Space> space;

    // Proxy vectors (independent copies, not source object IDs)
    std::vector<std::vector<float>> proxies;
    // Proxy → zone index within this block
    std::vector<uint8_t> proxy_zone;
    // zone → proxy index within block (SIZE_MAX = no proxy)
    std::vector<size_t> zone_proxy;

    explicit Block(size_t d) : dim(d) {
        slot_to_id.resize(capacity, SIZE_MAX);
        alive_bits.resize(capacity / 64);
        zone_of.resize(capacity, 255);
        category_planes.resize(64, std::vector<uint64_t>(capacity / 64));
        numeric_bits.resize(64, std::vector<uint64_t>(capacity / 64));
        tag_bits.resize(64, std::vector<uint64_t>(capacity / 64));
    }

    static uint64_t encodeSigned(int64_t v) {
        return static_cast<uint64_t>(v) ^ (1ULL << 63);
    }

    void setBit(std::vector<uint64_t>& plane, size_t slot, bool value) {
        const size_t w = slot / 64, b = slot % 64;
        if (value) plane[w] |= uint64_t(1) << b;
        else plane[w] &= ~(uint64_t(1) << b);
    }
    bool getBit(const std::vector<uint64_t>& plane, size_t slot) const {
        return (plane[slot / 64] >> (slot % 64)) & 1;
    }

    void setAlive(size_t slot, bool alive) { setBit(alive_bits, slot, alive); }
    bool isAlive(size_t slot) const { return getBit(alive_bits, slot); }

    void setCategory(size_t slot, int64_t value) {
        for (size_t bit = 0; bit < 64; ++bit)
            setBit(category_planes[bit], slot, (value >> (63 - bit)) & 1);
    }
    void setNumeric(size_t slot, int64_t value) {
        const uint64_t enc = encodeSigned(value);
        for (size_t bit = 0; bit < 64; ++bit)
            setBit(numeric_planes(bit), slot, (enc >> (63 - bit)) & 1);
    }
    // MSB-first bitplane storage for numeric (bit 63 = MSB).
    std::vector<uint64_t>& numeric_planes(size_t bit) { return numeric_bits[63 - bit]; }
    const std::vector<uint64_t>& numeric_planes(size_t bit) const { return numeric_bits[63 - bit]; }

    void setTags(size_t slot, uint64_t tags) {
        for (size_t bit = 0; bit < 64; ++bit)
            setBit(tag_bits[bit], slot, (tags >> bit) & 1);
    }

    // owner posting maintenance
    void addOwnerSlot(NodeId owner, size_t slot, Id id) {
        auto& list = owner_slots[owner];
        owner_pos[id] = list.size();
        list.push_back(slot);
    }
    void removeOwnerSlot(NodeId owner, size_t slot, Id id) {
        auto& list = owner_slots.at(owner);
        const size_t pos = owner_pos.at(id);
        const size_t last = list.size() - 1;
        if (pos != last) {
            const size_t lastSlot = list[last];
            const Id lastId = slot_to_id[lastSlot];
            list[pos] = lastSlot;
            owner_pos[lastId] = pos;
        }
        list.pop_back();
        owner_pos.erase(id);
        if (list.empty()) owner_slots.erase(owner);
    }

    // zone maintenance
    void setZone(size_t slot, uint8_t zone) {
        const uint8_t old = zone_of[slot];
        if (old != 255) setBit(zone_bits[old], slot, false);
        zone_of[slot] = zone;
        if (zone != 255) {
            if (zone_bits.size() <= zone) zone_bits.resize(zone + 1, std::vector<uint64_t>(capacity / 64));
            setBit(zone_bits[zone], slot, true);
        }
    }

    size_t words() const { return capacity / 64; }
};

// ---------------------------------------------------------------------------
// Query-local ExactMask computation (spec §5). Bitplane-based, no per-object
// interpretation of values.
// ---------------------------------------------------------------------------
struct ExactMaskComputer {
    const Block& block;
    const EulerTour& euler;
    const std::vector<NodeId>& parent;  // directory parent for non-recursive scope

    // Result: bitmap of live slots satisfying all enabled filters.
    struct Result {
        std::vector<uint64_t> mask;
        size_t words_used = 0;
        size_t owner_lookups = 0;
        size_t owner_cache_hits = 0;
    };

    struct Query {
        size_t qidx = 0;
        NodeId anchor = 0;
        bool recursive = false;
        int64_t category = -1;
        int64_t lo = -1, hi = -1;
        int tagmode = 0;      // 0=disabled,1=Q⊆ITEM,2=Q∩ITEM,3=ITEM=Q,4=ITEM⊆Q
        uint64_t tags = 0;
        bool combine_or = false;  // false=AND, true=OR
    };

    Result compute(const Query& q, size_t capacity) const {
        Result result;
        const size_t words = capacity / 64;
        result.mask.assign(words, 0);
        result.words_used = words;

        // Start with live mask.
        for (size_t w = 0; w < words; ++w)
            result.mask[w] = block.alive_bits[w];

        // Category equality (64-bit bitplanes, MSB first).
        if (q.category != -1) {
            for (size_t w = 0; w < words; ++w) {
                uint64_t match = UINT64_MAX;
                for (size_t bit = 0; bit < 64; ++bit) {
                    const bool want = (q.category >> (63 - bit)) & 1;
                    match &= want ? block.category_planes[bit][w] : ~block.category_planes[bit][w];
                }
                result.mask[w] &= match;
            }
        }

        // Numeric closed range [lo, hi] using less/equal circuit.
        if (q.lo != -1 || q.hi != -1) {
            const int64_t lo = q.lo == -1 ? std::numeric_limits<int64_t>::min() : q.lo;
            const int64_t hi = q.hi == -1 ? std::numeric_limits<int64_t>::max() : q.hi;
            const uint64_t loEnc = Block::encodeSigned(lo);
            const uint64_t hiEnc = Block::encodeSigned(hi);
            for (size_t w = 0; w < words; ++w) {
                uint64_t lt_lo = 0, eq_lo = UINT64_MAX;
                uint64_t lt_hi = 0, eq_hi = UINT64_MAX;
                for (int bit = 63; bit >= 0; --bit) {
                    const uint64_t p = block.numeric_bits[63 - bit][w];
                    const bool blo = (loEnc >> bit) & 1;
                    const bool bhi = (hiEnc >> bit) & 1;
                    if (blo) { lt_lo |= eq_lo & ~p; eq_lo &= p; }
                    else eq_lo &= ~p;
                    if (bhi) { lt_hi |= eq_hi & ~p; eq_hi &= p; }
                    else eq_hi &= ~p;
                }
                const uint64_t ge_lo = ~(lt_lo & ~eq_lo) & ~(lt_lo);  // NOT less-than-lo
                const uint64_t in_range = (lt_hi | eq_hi) & ~lt_lo;    // ≤ hi AND ≥ lo
                result.mask[w] &= in_range;
            }
        }

        // Tags (four modes).
        if (q.tagmode != 0) {
            std::vector<uint64_t> tag_mask(words, 0);
            switch (q.tagmode) {
                case 1: { // Q ⊆ ITEM
                    for (size_t w = 0; w < words; ++w) tag_mask[w] = UINT64_MAX;
                    for (size_t t = 0; t < 64; ++t) {
                        if (!((q.tags >> t) & 1)) continue;
                        for (size_t w = 0; w < words; ++w)
                            tag_mask[w] &= block.tag_bits[t][w];
                    }
                    break;
                }
                case 2: { // Q ∩ ITEM ≠ ∅
                    for (size_t t = 0; t < 64; ++t) {
                        if (!((q.tags >> t) & 1)) continue;
                        for (size_t w = 0; w < words; ++w)
                            tag_mask[w] |= block.tag_bits[t][w];
                    }
                    break;
                }
                case 4: { // ITEM ⊆ Q
                    for (size_t w = 0; w < words; ++w) tag_mask[w] = UINT64_MAX;
                    for (size_t t = 0; t < 64; ++t) {
                        if ((q.tags >> t) & 1) continue;
                        for (size_t w = 0; w < words; ++w)
                            tag_mask[w] &= ~block.tag_bits[t][w];
                    }
                    break;
                }
                case 3: { // ITEM = Q
                    for (size_t w = 0; w < words; ++w) {
                        uint64_t sub = UINT64_MAX, sup = UINT64_MAX;
                        for (size_t t = 0; t < 64; ++t) {
                            if ((q.tags >> t) & 1) sub &= block.tag_bits[t][w];
                            else sup &= ~block.tag_bits[t][w];
                        }
                        tag_mask[w] = sub & sup;
                    }
                    break;
                }
            }
            for (size_t w = 0; w < words; ++w) result.mask[w] &= tag_mask[w];
        }

        // Scope: recursive containment or direct owner match.
        {
            std::unordered_map<NodeId, bool> cache;
            for (const auto& [owner, slots] : block.owner_slots) {
                bool pass;
                auto it = cache.find(owner);
                if (it != cache.end()) { pass = it->second; ++result.owner_cache_hits; }
                else {
                    pass = q.recursive ? euler.contains(q.anchor, owner) : (owner == q.anchor);
                    cache[owner] = pass;
                    ++result.owner_lookups;
                }
                if (pass) for (size_t s : cache.size() ? std::vector<size_t>{} : std::vector<size_t>{}) {}
                if (pass) for (size_t s : block.owner_slots.at(owner)) {}
                if (pass) {
                    for (size_t s : block.owner_slots.at(owner)) {
                        const size_t w = s / 64, b = s % 64;
                        result.mask[w] |= uint64_t(1) << b;
                    }
                } else {
                    for (size_t s : block.owner_slots.at(owner)) {
                        const size_t w = s / 64, b = s % 64;
                        result.mask[w] &= ~(uint64_t(1) << b);
                    }
                }
            }
        }

        // AND/OR combine: if combine_or, at least one enabled field must pass.
        // The above computed AND; for OR we need to recompute with OR semantics.
        // For simplicity, if combine_or and any field is enabled, recompute.
        if (q.combine_or && (q.category != -1 || q.lo != -1 || q.hi != -1 || q.tagmode != 0)) {
            // Recompute with OR: start from 0, OR each enabled field's mask.
            std::vector<uint64_t> or_mask(words, 0);
            if (q.category != -1) {
                for (size_t w = 0; w < words; ++w) {
                    uint64_t match = UINT64_MAX;
                    for (size_t bit = 0; bit < 64; ++bit) {
                        const bool want = (q.category >> (63 - bit)) & 1;
                        match &= want ? block.category_planes[bit][w] : ~block.category_planes[bit][w];
                    }
                    or_mask[w] |= match;
                }
            }
            if (q.lo != -1 || q.hi != -1) {
                const int64_t lo = q.lo == -1 ? std::numeric_limits<int64_t>::min() : q.lo;
                const int64_t hi = q.hi == -1 ? std::numeric_limits<int64_t>::max() : q.hi;
                const uint64_t loEnc = Block::encodeSigned(lo);
                const uint64_t hiEnc = Block::encodeSigned(hi);
                for (size_t w = 0; w < words; ++w) {
                    uint64_t lt_lo = 0, eq_lo = UINT64_MAX;
                    uint64_t lt_hi = 0, eq_hi = UINT64_MAX;
                    for (int bit = 63; bit >= 0; --bit) {
                        const uint64_t p = block.numeric_bits[63 - bit][w];
                        const bool blo = (loEnc >> bit) & 1;
                        const bool bhi = (hiEnc >> bit) & 1;
                        if (blo) { lt_lo |= eq_lo & ~p; eq_lo &= p; }
                        else eq_lo &= ~p;
                        if (bhi) { lt_hi |= eq_hi & ~p; eq_hi &= p; }
                        else eq_hi &= ~p;
                    }
                    or_mask[w] |= (lt_hi | eq_hi) & ~lt_lo;
                }
            }
            if (q.tagmode != 0) {
                std::vector<uint64_t> tag_mask(words, 0);
                switch (q.tagmode) {
                    case 1: {
                        for (size_t w = 0; w < words; ++w) tag_mask[w] = UINT64_MAX;
                        for (size_t t = 0; t < 64; ++t) {
                            if (!((q.tags >> t) & 1)) continue;
                            for (size_t w = 0; w < words; ++w)
                                tag_mask[w] &= block.tag_bits[t][w];
                        }
                        break;
                    }
                    case 2: {
                        for (size_t t = 0; t < 64; ++t) {
                            if (!((q.tags >> t) & 1)) continue;
                            for (size_t w = 0; w < words; ++w)
                                tag_mask[w] |= block.tag_bits[t][w];
                        }
                        break;
                    }
                    case 4: {
                        for (size_t w = 0; w < words; ++w) tag_mask[w] = UINT64_MAX;
                        for (size_t t = 0; t < 64; ++t) {
                            if ((q.tags >> t) & 1) continue;
                            for (size_t w = 0; w < words; ++w)
                                tag_mask[w] &= ~block.tag_bits[t][w];
                        }
                        break;
                    }
                    case 3: {
                        for (size_t w = 0; w < words; ++w) {
                            uint64_t sub = UINT64_MAX, sup = UINT64_MAX;
                            for (size_t t = 0; t < 64; ++t) {
                                if ((q.tags >> t) & 1) sub &= block.tag_bits[t][w];
                                else sup &= ~block.tag_bits[t][w];
                            }
                            tag_mask[w] = sub & sup;
                        }
                        break;
                    }
                }
                for (size_t w = 0; w < words; ++w) or_mask[w] |= tag_mask[w];
            }
            // Combine OR with scope and live.
            for (size_t w = 0; w < words; ++w)
                result.mask[w] = result.mask[w] & or_mask[w];
        }

        return result;
    }
};

// Counted RawL2Space wrapper for instrumentation.
struct CountedL2 : RawL2Space {
    mutable size_t calls = 0;
    explicit CountedL2(size_t d) : RawL2Space(d) {}
    static float count(const void* a, const void* b, const void* p) {
        auto* self = static_cast<const CountedL2*>(p);
        ++self->calls;
        return self->dist(static_cast<const float*>(a), static_cast<const float*>(b));
    }
    hnswlib::DISTFUNC<float> get_dist_func() override { return count; }
    void* get_dist_func_param() override { return this; }
};

// ---------------------------------------------------------------------------
// WQR index: geometry blocks + proxy graph + Euler directory + attributes.
// ---------------------------------------------------------------------------
struct WQRIndex {
    static constexpr size_t B0 = 1024;
    static constexpr size_t PROXIES_PER_BLOCK = 4;
    static constexpr size_t M = 16;
    static constexpr size_t EF_CONSTRUCTION = 200;
    static constexpr uint64_t SEED = 100;

    VectorStore vectors;
    EulerTour euler;
    std::vector<NodeId> parent;  // directory parent array
    std::vector<Block> blocks;
    std::unique_ptr<hnswlib::HierarchicalNSW<float>> proxy_graph;
    std::unique_ptr<CountedL2> proxy_space;
    std::vector<std::unique_ptr<CountedL2>> local_spaces;

    // Global reverse mapping: object ID → (block, slot)
    struct Location { BlockId block; size_t slot; };
    std::vector<Location> location;
    std::vector<bool> alive;
    std::vector<NodeId> owner;
    std::vector<int64_t> category;
    std::vector<int64_t> numeric;
    std::vector<uint64_t> tags;

    // Proxy metadata: proxy label → (block, zone)
    struct ProxyMeta { BlockId block; uint8_t zone; };
    std::vector<ProxyMeta> proxy_meta;

    size_t dim = 0;
    size_t n_items = 0;

    void init(size_t dimension, size_t max_items) {
        dim = dimension;
        n_items = max_items;
        vectors.init(dimension);
        vectors.data.resize(max_items * dimension);
        location.resize(max_items, {BlockId(-1), size_t(-1)});
        alive.assign(max_items, false);
        owner.resize(max_items, 0);
        category.resize(max_items, 0);
        numeric.resize(max_items, 0);
        tags.resize(max_items, 0);
        proxy_space = std::make_unique<CountedL2>(dimension);
    }

    // Build from canonical items + parent + vectors.
    void build(const std::vector<Id>& item_ids,
               const std::vector<NodeId>& parent_arr,
               const VectorStore& canonical) {
        parent = parent_arr;
        vectors = canonical;
        euler.build(parent);
        Partitioner partitioner(vectors);
        auto block_ids = partitioner.blocks(item_ids);
        for (const auto& ids : block_ids) {
            blocks.emplace_back(dim);
            Block& b = blocks.back();
            local_spaces.emplace_back(std::make_unique<CountedL2>(dim));
            b.space = std::make_unique<RawL2Space>(dim);
            b.graph = std::make_unique<hnswlib::HierarchicalNSW<float>>(
                b.space.get(), b.capacity, M, EF_CONSTRUCTION, SEED);
            for (Id id : ids) {
                const size_t slot = b.live_count++;
                b.slot_to_id[slot] = id;
                b.id_to_slot[id] = slot;
                b.setAlive(slot, true);
                b.setCategory(slot, category[id]);
                b.setNumeric(slot, numeric[id]);
                b.setTags(slot, tags[id]);
                b.addOwnerSlot(owner[id], slot, id);
                location[id] = {BlockId(blocks.size() - 1), slot};
                alive[id] = true;
                b.graph->addPoint(vectors.at(id), id);
            }
            // Farthest-first proxy selection (spec §3.3).
            std::vector<size_t> selected;
            selected.push_back(0);  // smallest object ID is slot 0 by construction
            while (selected.size() < std::min(PROXIES_PER_BLOCK, ids.size())) {
                size_t best = SIZE_MAX;
                float bestD = -1;
                for (size_t i = 0; i < ids.size(); ++i) {
                    if (std::find(selected.begin(), selected.end(), i) != selected.end()) continue;
                    float nearest = std::numeric_limits<float>::infinity();
                    for (size_t j : selected)
                        nearest = std::min(nearest, vectors.l2(vectors.at(ids[i]), vectors.at(ids[j])));
                    if (nearest > bestD || (nearest == bestD && ids[i] < ids[best])) {
                        bestD = nearest; best = i;
                    }
                }
                selected.push_back(best);
            }
            // Copy proxy vectors.
            for (size_t s : selected) {
                b.proxies.emplace_back(vectors.at(ids[s]), vectors.at(ids[s]) + dim);
                proxy_meta.push_back({BlockId(blocks.size() - 1), uint8_t(b.proxies.size() - 1)});
            }
            // Assign zones: each object to nearest proxy.
            b.zone_bits.resize(selected.size(), std::vector<uint64_t>(b.capacity / 64));
            for (size_t i = 0; i < ids.size(); ++i) {
                size_t winner = 0;
                float nearest = std::numeric_limits<float>::infinity();
                for (size_t j = 0; j < selected.size(); ++j) {
                    const float d = vectors.l2(vectors.at(ids[i]), b.proxies[j].data());
                    if (d < nearest || (d == nearest && j < winner)) { nearest = d; winner = j; }
                }
                b.setZone(i, uint8_t(winner));
            }
            b.proxy_zone.resize(selected.size());
            for (size_t j = 0; j < selected.size(); ++j) b.proxy_zone[j] = uint8_t(j);
        }
        // Build proxy graph.
        proxy_graph = std::make_unique<hnswlib::HierarchicalNSW<float>>(
            proxy_space.get(), proxy_meta.size() + 1, M, EF_CONSTRUCTION, SEED);
        for (size_t i = 0; i < proxy_meta.size(); ++i) {
            const auto& pm = proxy_meta[i];
            proxy_graph->addPoint(blocks[pm.block].proxies[pm.zone].data(), i);
        }
    }

    // Query: WQR portal routing + local search.
    struct QueryResult {
        std::vector<std::pair<float, Id>> results;  // (distance, object ID)
        size_t qidx = 0;
        double us = 0;
        size_t portal_distances = 0;
        size_t local_distances = 0;
        size_t masks_prepared = 0;
        size_t blocks_selected = 0;
        size_t owner_lookups = 0;
        size_t owner_cache_hits = 0;
    };

    QueryResult query(const float* q, size_t k, size_t probe_blocks,
                      size_t portal_ef, size_t local_ef,
                      const ExactMaskComputer::Query& filter) const {
        QueryResult result;
        if (blocks.empty() || proxy_meta.empty()) return result;

        const size_t P = std::min(4 * probe_blocks, proxy_meta.size());
        if (P == 0) return result;

        // Query-local mask cache per block.
        std::vector<std::vector<uint64_t>> masks(blocks.size());
        std::vector<bool> ready(blocks.size(), false);
        std::vector<std::vector<uint64_t>> active_zones(blocks.size());

        auto getMask = [&](BlockId b) -> const std::vector<uint64_t>& {
            if (!ready[b]) {
                ready[b] = true;
                ++result.masks_prepared;
                ExactMaskComputer computer{blocks[b], euler, parent};
                auto r = computer.compute(filter, blocks[b].capacity);
                masks[b] = std::move(r.mask);
                result.owner_lookups += r.owner_lookups;
                result.owner_cache_hits += r.owner_cache_hits;
                // Compute active zones.
                const auto& blk = blocks[b];
                active_zones[b].assign(blk.zone_bits.size(), 0);
                for (size_t z = 0; z < blk.zone_bits.size(); ++z) {
                    bool any = false;
                    for (size_t w = 0; w < blk.words(); ++w) {
                        if (masks[b][w] & blk.zone_bits[z][w]) { any = true; break; }
                    }
                    active_zones[b][z] = any;
                }
            }
            return masks[b];
        };

        // Portal filter: WQR zone gate.
        struct PortalFilter : hnswlib::BaseFilterFunctor {
            const WQRIndex& idx;
            std::function<const std::vector<uint64_t>&(BlockId)> getMask;
            std::vector<bool>& ready;
            std::vector<std::vector<uint64_t>>& active_zones;
            PortalFilter(const WQRIndex& i,
                         std::function<const std::vector<uint64_t>&(BlockId)> gm,
                         std::vector<bool>& r,
                         std::vector<std::vector<uint64_t>>& az)
                : idx(i), getMask(std::move(gm)), ready(r), active_zones(az) {}
            bool operator()(hnswlib::labeltype label) override {
                const auto& pm = idx.proxy_meta[label];
                if (!ready[pm.block]) getMask(pm.block);
                return active_zones[pm.block][pm.zone];
            }
        };
        PortalFilter portal_filter(*this, getMask, ready, active_zones);
        proxy_graph->setEf(std::max(portal_ef, P));
        auto portal_hits = proxy_graph->searchKnn(q, P, &portal_filter);
        result.portal_distances = proxy_space->calls;

        // Sort by (distance, proxy_label), deduplicate blocks.
        std::vector<std::pair<float, ProxyId>> ordered;
        ordered.reserve(portal_hits.size());
        while (!portal_hits.empty()) {
            ordered.emplace_back(portal_hits.top().first, portal_hits.top().second);
            portal_hits.pop();
        }
        std::sort(ordered.begin(), ordered.end());
        std::vector<BlockId> selected_blocks;
        std::unordered_set<BlockId> seen;
        for (const auto& [dist, pid] : ordered) {
            const BlockId b = proxy_meta[pid].block;
            if (seen.insert(b).second) {
                seen.insert(b);
                selected_blocks.push_back(b);
                if (selected_blocks.size() >= probe_blocks) break;
            }
        }
        result.blocks_selected = selected_blocks.size();

        // Local search per selected block.
        struct LocalFilter : hnswlib::BaseFilterFunctor {
            const Block& block;
            const std::vector<uint64_t>& mask;
            LocalFilter(const Block& b, const std::vector<uint64_t>& m) : block(b), mask(m) {}
            bool operator()(hnswlib::labeltype label) override {
                const size_t slot = label;  // HNSW label = slot index
                return (mask[slot / 64] >> (slot % 64)) & 1;
            }
        };

        std::priority_queue<std::pair<float, Id>> heap;
        for (BlockId b : selected_blocks) {
            const auto& blk = blocks[b];
            if (!blk.graph) continue;
            const auto& mask = getMask(b);
            size_t popcount = 0;
            for (size_t w = 0; w < blk.words(); ++w)
                popcount += __builtin_popcountll(mask[w]);
            if (popcount == 0) continue;
            const size_t kb = std::min(k, popcount);
            LocalFilter local_filter(blk, mask);
            blk.graph->setEf(std::max(local_ef, kb));
            auto local_hits = blk.graph->searchKnn(q, kb, &local_filter);
            result.local_distances += local_spaces[b]->calls;
            while (!local_hits.empty()) {
                heap.emplace(local_hits.top().first, local_hits.top().second);
                local_hits.pop();
                if (heap.size() > k) heap.pop();
            }
        }
        while (!heap.empty()) {
            result.results.emplace_back(heap.top().first, heap.top().second);
            heap.pop();
        }
        std::reverse(result.results.begin(), result.results.end());
        return result;
    }

    // Update events: V/T/M/B/R/D/I.
    void apply_V(Id id, int64_t cat, int64_t num) {
        const auto& loc = location[id];
        auto& b = blocks[loc.block];
        b.setCategory(loc.slot, cat);
        b.setNumeric(loc.slot, num);
        category[id] = cat;
        numeric[id] = num;
    }
    void apply_T(Id id, uint64_t t) {
        const auto& loc = location[id];
        blocks[loc.block].setTags(loc.slot, t);
        tags[id] = t;
    }
    void apply_M(Id id, NodeId dst) {
        const auto& loc = location[id];
        auto& b = blocks[loc.block];
        b.removeOwnerSlot(owner[id], loc.slot, id);
        b.addOwnerSlot(dst, loc.slot, id);
        owner[id] = dst;
    }
    void apply_B(NodeId src, NodeId dst) {
        // Freeze source subtree, then move each object.
        std::vector<Id> to_move;
        for (Id id = 0; id < n_items; ++id)
            if (alive[id] && euler.contains(src, owner[id])) to_move.push_back(id);
        for (Id id : to_move) apply_M(id, dst);
    }
    void apply_R(NodeId src, NodeId dst) {
        euler.reparent(src, dst);
        parent[src] = dst;
    }
    void apply_D(Id id) {
        const auto& loc = location[id];
        auto& b = blocks[loc.block];
        b.setAlive(loc.slot, false);
        b.removeOwnerSlot(owner[id], loc.slot, id);
        b.setZone(loc.slot, 255);
        b.graph->markDelete(id);
        alive[id] = false;
    }
    void apply_I(Id id, NodeId own, int64_t cat, int64_t num, uint64_t t) {
        // Restore or insert. For now, assume slot is available.
        const auto& loc = location[id];
        auto& b = blocks[loc.block];
        if (loc.slot != SIZE_MAX && b.slot_to_id[loc.slot] == id) {
            b.setAlive(loc.slot, true);
            b.setCategory(loc.slot, cat);
            b.setNumeric(loc.slot, num);
            b.setTags(loc.slot, t);
            b.addOwnerSlot(own, loc.slot, id);
            b.setZone(loc.slot, 0);  // simplified: always zone 0
            b.graph->unmarkDelete(id);
        } else {
            // Physical new insert: find empty slot or split.
            size_t slot = SIZE_MAX;
            for (size_t s = 0; s < b.capacity; ++s)
                if (b.slot_to_id[s] == SIZE_MAX) { slot = s; break; }
            if (slot == SIZE_MAX) throw std::runtime_error("I: no capacity");
            b.slot_to_id[slot] = id;
            b.id_to_slot[id] = slot;
            b.setAlive(slot, true);
            b.setCategory(slot, cat);
            b.setNumeric(slot, num);
            b.setTags(slot, t);
            b.addOwnerSlot(own, slot, id);
            b.setZone(slot, 0);
            b.graph->addPoint(vectors.at(id), id);
            location[id] = {loc.block, slot};
        }
        alive[id] = true;
        owner[id] = own;
        category[id] = cat;
        numeric[id] = num;
        tags[id] = t;
    }
};

} // namespace wqr
