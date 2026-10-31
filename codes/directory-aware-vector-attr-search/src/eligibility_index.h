#pragma once

#include "neighborhood_filter.h"
#include <map>

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
    using Posting = std::map<size_t, uint64_t>;
    std::vector<npi::Item> items_;
    std::vector<int> parent_;
    std::vector<std::vector<Id>> children_;
    std::vector<Posting> owners_;
    std::vector<uint64_t> live_;
    std::array<std::vector<uint64_t>, 64> category_, numeric_, tags_;
    std::vector<Id> stack_;
    Result result_;
    static uint64_t ordered(int64_t n) { return static_cast<uint64_t>(n) ^ (UINT64_C(1) << 63); }
    void posting(Id owner, size_t word, uint64_t bit, bool add) {
        if (add) owners_[owner][word] |= bit;
        else {
            auto it = owners_[owner].find(word);
            if (it != owners_[owner].end() && !(it->second &= ~bit)) owners_[owner].erase(it);
        }
    }
    static void planes(std::array<std::vector<uint64_t>, 64>& p, size_t word,
                       uint64_t bit, uint64_t old_value, uint64_t value) {
        uint64_t changed = old_value ^ value;
        while (changed) {
            const unsigned b = static_cast<unsigned>(__builtin_ctzll(changed));
            p[b][word] ^= bit;
            changed &= changed - 1;
        }
    }
    uint64_t less(size_t word, uint64_t value, uint64_t universe) {
        uint64_t equal = universe, lower = 0;
        for (int b = 63; b >= 0; --b) {
            const uint64_t plane = numeric_[b][word];
            ++result_.attribute_words;
            if (value & (UINT64_C(1) << b)) { lower |= equal & ~plane; equal &= plane; }
            else equal &= ~plane;
        }
        return lower;
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
        for (auto* planes : {&category_, &numeric_, &tags_})
            for (auto& p : *planes) p.resize(live_.size());
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
        // Dead objects have zero planes. Stored old metadata remains authoritative
        // even when the caller has already overwritten its Dataset copy.
        planes(category_, word, bit, old.alive ? static_cast<uint64_t>(old.category) : 0,
               next.alive ? static_cast<uint64_t>(next.category) : 0);
        planes(numeric_, word, bit, old.alive ? ordered(old.numeric) : 0, next.alive ? ordered(next.numeric) : 0);
        planes(tags_, word, bit, old.alive ? old.tags : 0, next.alive ? next.tags : 0);
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
    const Result& prepare(const npi::Query& q) {
        if (q.anchor >= parent_.size()) npi::filter_detail::fail("invalid qualification anchor");
        std::fill(result_.scope.begin(), result_.scope.end(), 0);
        std::fill(result_.eligible.begin(), result_.eligible.end(), 0);
        result_.scope_count = result_.count = 0;
        result_.directory_visits = result_.posting_words = result_.attribute_words = 0;
        stack_.clear(); stack_.push_back(q.anchor);
        while (!stack_.empty()) {
            const Id dir = stack_.back(); stack_.pop_back(); ++result_.directory_visits;
            for (const auto& posting : owners_[dir]) {
                result_.scope[posting.first] |= posting.second;
                ++result_.posting_words;
            }
            if (q.recursive) for (Id child : children_[dir]) stack_.push_back(child);
        }
        for (size_t w = 0; w < live_.size(); ++w) {
            const uint64_t scope = result_.scope[w] &= live_[w];
            result_.scope_count += static_cast<size_t>(__builtin_popcountll(scope));
            if (!scope) continue;
            bool any = false;
            uint64_t extra = q.combine ? 0 : scope;
            auto add = [&](uint64_t mask) { any = true; if (q.combine) extra |= mask; else extra &= mask; };
            if (q.category != -1) {
                uint64_t match = scope, value = static_cast<uint64_t>(q.category);
                for (unsigned b = 0; b < 64; ++b) {
                    const uint64_t plane = category_[b][w]; ++result_.attribute_words;
                    match &= value & (UINT64_C(1) << b) ? plane : ~plane;
                }
                add(match);
            }
            if (q.lo != -1 || q.hi != -1) {
                const uint64_t lower = less(w, ordered(q.lo), scope);
                const uint64_t upper = q.hi == std::numeric_limits<int64_t>::max() ? scope : less(w, ordered(q.hi) + 1, scope);
                add(upper & ~lower);
            }
            if (q.tagmode) {
                uint64_t match = q.tagmode == 2 ? 0 : scope;
                for (unsigned b = 0; b < 64; ++b) {
                    const bool set = (q.tags & (UINT64_C(1) << b)) != 0;
                    if ((q.tagmode <= 2 && !set) || (q.tagmode == 4 && set)) continue;
                    const uint64_t plane = tags_[b][w]; ++result_.attribute_words;
                    if (q.tagmode == 2) match |= plane & scope;
                    else match &= (q.tagmode == 1 || (q.tagmode == 3 && set)) ? plane : ~plane;
                }
                add(match);
            }
            result_.eligible[w] = any ? extra & scope : scope;
            result_.count += static_cast<size_t>(__builtin_popcountll(result_.eligible[w]));
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
        for (const auto& owner : owners_) n += owner.size() * (sizeof(Posting::value_type) + 4 * sizeof(void*));
        for (const auto* planes : {&category_, &numeric_, &tags_})
            for (const auto& p : *planes) n += p.capacity() * sizeof(uint64_t);
        return n;
    }
};
} // namespace eligibility
