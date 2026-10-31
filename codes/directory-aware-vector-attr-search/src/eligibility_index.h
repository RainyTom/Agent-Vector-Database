#pragma once

#include "neighborhood_filter.h"
#include <algorithm>

// Exact metadata-only qualification. Direct-owner postings are sparse words;
// descendants are enumerated at query time, never materialized per directory.
namespace eligibility {
using npi::Id;
struct Result {
    std::vector<uint64_t> scope, eligible;
    size_t scope_count = 0, count = 0;
    uint64_t directory_visits = 0, posting_words = 0, attribute_words = 0;
    bool contains(Id id) const { return id / 64 < eligible.size() && (eligible[id / 64] & (UINT64_C(1) << (id % 64))); }
    bool in_scope(Id id) const { return id / 64 < scope.size() && (scope[id / 64] & (UINT64_C(1) << (id % 64))); }
};
class Index {
    // Sorted (word, bits) pairs: the scope union streams a directory's words
    // sequentially, so a contiguous vector avoids one pointer chase per word.
    using Posting = std::vector<std::pair<uint32_t, uint64_t>>;
    std::vector<npi::Item> items_;
    std::vector<int> parent_;
    std::vector<std::vector<Id>> children_;
    std::vector<Posting> owners_;
    std::vector<uint64_t> live_;
    std::vector<Id> stack_;
    Result result_;
    static uint64_t ordered(int64_t n) { return static_cast<uint64_t>(n) ^ (UINT64_C(1) << 63); }
    void posting(Id owner, size_t word, uint64_t bit, bool add) {
        auto& list = owners_[owner];
        const uint32_t key = static_cast<uint32_t>(word);
        auto it = std::lower_bound(list.begin(), list.end(), key,
                                   [](const Posting::value_type& e, uint32_t k) { return e.first < k; });
        if (add) {
            if (it != list.end() && it->first == key) it->second |= bit;
            else list.insert(it, Posting::value_type(key, bit));
        } else if (it != list.end() && it->first == key) {
            if (!(it->second &= ~bit)) list.erase(it);
        }
    }
public:
    Index(const std::vector<npi::Item>& items, const std::vector<int>& parent)
        : items_(items.size()), parent_(parent), children_(parent.size()), owners_(parent.size()),
          live_((items.size() + 63) / 64, 0) {
        if (items.size() > std::numeric_limits<Id>::max() || parent.empty())
            npi::filter_detail::fail("invalid qualification index dimensions");
        for (size_t i = 0; i < parent.size(); ++i) {
            if (parent[i] >= 0) {
                if (static_cast<size_t>(parent[i]) >= parent.size()) npi::filter_detail::fail("invalid qualification parent");
                children_[parent[i]].push_back(static_cast<Id>(i));
            }
        }
        result_.scope.resize(live_.size()); result_.eligible.resize(live_.size());
        stack_.reserve(parent.size());
        for (size_t i = 0; i < items.size(); ++i) update(static_cast<Id>(i), items[i]);
    }
    void update(Id id, const npi::Item& next) {
        if (id >= items_.size() || next.owner >= owners_.size()) npi::filter_detail::fail("invalid qualification update");
        const auto old = items_[id];
        const size_t word = id / 64;
        const uint64_t bit = UINT64_C(1) << (id % 64);
        if (old.alive && (!next.alive || old.owner != next.owner)) posting(old.owner, word, bit, false);
        if (next.alive && (!old.alive || old.owner != next.owner)) posting(next.owner, word, bit, true);
        if (next.alive) live_[word] |= bit; else live_[word] &= ~bit;
        // Stored old metadata remains authoritative even when the caller has
        // already overwritten its Dataset copy. Attribute values live only here:
        // no bit-sliced planes to repair on update.
        items_[id] = next;
    }
    Id owner(Id id) const { return items_.at(id).owner; }
    void reparent(Id node, Id parent) {
        if (node >= parent_.size() || parent >= parent_.size() || parent_[node] < 0)
            npi::filter_detail::fail("invalid qualification reparent");
        for (int p = static_cast<int>(parent); p >= 0; p = parent_[p])
            if (p == static_cast<int>(node)) npi::filter_detail::fail("qualification reparent cycle");
        if (parent_[node] == static_cast<int>(parent)) return;
        auto& old = children_[parent_[node]];
        old.erase(std::find(old.begin(), old.end(), node));
        children_[parent].push_back(node);
        parent_[node] = static_cast<int>(parent);
    }
    // Predicate evaluation walks the scope bitmap word by word and tests the
    // dense per-object record. The previous bit-sliced form read 64 scattered
    // planes per word, which is cache-miss bound once the scope spans most
    // words; this form touches each candidate's fields sequentially instead.
    const Result& prepare(const npi::Query& q) {
        if (q.anchor >= parent_.size()) npi::filter_detail::fail("invalid qualification anchor");
        std::fill(result_.scope.begin(), result_.scope.end(), 0);
        std::fill(result_.eligible.begin(), result_.eligible.end(), 0);
        result_.scope_count = result_.count = 0;
        result_.directory_visits = result_.posting_words = result_.attribute_words = 0;
        stack_.clear(); stack_.push_back(q.anchor);
        while (!stack_.empty()) {
            const Id dir = stack_.back(); stack_.pop_back(); ++result_.directory_visits;
            for (const auto& entry : owners_[dir]) {
                result_.scope[entry.first] |= entry.second;
                ++result_.posting_words;
            }
            if (q.recursive) for (Id child : children_[dir]) stack_.push_back(child);
        }
        const bool has_category = q.category != -1;
        const bool has_range = q.lo != -1 || q.hi != -1;
        const bool no_upper = q.hi == std::numeric_limits<int64_t>::max();
        const bool has_tags = q.tagmode != 0;
        for (size_t w = 0; w < live_.size(); ++w) {
            uint64_t bits = result_.scope[w] & live_[w];
            result_.scope[w] = bits;
            result_.scope_count += static_cast<size_t>(__builtin_popcountll(bits));
            if (!bits) continue;
            uint64_t keep = 0;
            for (uint64_t rest = bits; rest; rest &= rest - 1) {
                const Id id = static_cast<Id>(w) * 64 + static_cast<Id>(__builtin_ctzll(rest));
                const npi::Item& item = items_[id];
                bool any = false;
                bool accept = q.combine ? false : true;
                const auto add = [&](bool value) {
                    any = true;
                    accept = q.combine ? (accept || value) : (accept && value);
                };
                if (has_category) { ++result_.attribute_words; add(item.category == q.category); }
                if (has_range) {
                    ++result_.attribute_words;
                    add(item.numeric >= q.lo && (no_upper || item.numeric <= q.hi));
                }
                if (has_tags) {
                    ++result_.attribute_words;
                    bool hit = false;
                    switch (q.tagmode) {
                        case 1: hit = (item.tags & q.tags) == q.tags; break;
                        case 2: hit = (item.tags & q.tags) != 0; break;
                        case 3: hit = item.tags == q.tags; break;
                        default: hit = (item.tags & ~q.tags) == 0; break;
                    }
                    add(hit);
                }
                if (any ? accept : true) { keep |= UINT64_C(1) << (id % 64); ++result_.count; }
            }
            result_.eligible[w] = keep;
        }
        return result_;
    }
    size_t bytes() const {
        // Capacity-based estimate; ordered-map nodes include three links and
        // color/alignment allowance, not allocator metadata or process RSS.
        size_t n = sizeof(*this) + items_.capacity() * sizeof(npi::Item) + parent_.capacity() * sizeof(int)
            + children_.capacity() * sizeof(std::vector<Id>) + owners_.capacity() * sizeof(Posting)
            + stack_.capacity() * sizeof(Id)
            + (live_.capacity() + result_.scope.capacity() + result_.eligible.capacity()) * sizeof(uint64_t);
        for (const auto& child : children_) n += child.capacity() * sizeof(Id);
        for (const auto& owner : owners_) n += owner.capacity() * sizeof(Posting::value_type);
        return n;
    }
};
} // namespace eligibility
