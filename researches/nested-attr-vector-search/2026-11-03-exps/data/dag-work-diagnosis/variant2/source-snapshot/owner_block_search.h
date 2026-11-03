#pragma once
// Research candidate: one immutable graph, with each row segmented by target
// owner. No query-specific copied graph and no per-owner top-k/frontier.
struct OwnerBlocks {
    struct Block { size_t begin; uint32_t count; Id owner; };
    std::vector<size_t> row;
    std::vector<Block> blocks;
    std::vector<Id> ids;
};
static OwnerBlocks make_owner_blocks(const Merged& graph, const Workload& w) {
    OwnerBlocks out;
    out.row.reserve(graph.edges.size() + 1);
    size_t total = 0;
    for (const auto& row : graph.edges) total += row.size();
    out.ids.reserve(total);
    std::vector<Id> sorted;
    for (const auto& row : graph.edges) {
        require(std::is_sorted(row.begin(), row.end()), "candidate requires original ID-sorted adjacency");
        out.row.push_back(out.blocks.size());
        sorted.assign(row.begin(), row.end());
        std::sort(sorted.begin(), sorted.end(), [&](Id a, Id b) {
            return w.owner[a] != w.owner[b] ? w.owner[a] < w.owner[b] : a < b;
        });
        size_t first = 0;
        while (first < sorted.size()) {
            size_t end = first + 1;
            const Id owner = w.owner[sorted[first]];
            while (end < sorted.size() && w.owner[sorted[end]] == owner) ++end;
            const size_t begin = out.ids.size();
            out.ids.insert(out.ids.end(), sorted.begin() + first, sorted.begin() + end);
            require(end - first <= std::numeric_limits<uint32_t>::max(), "owner block too large");
            out.blocks.push_back({begin, uint32_t(end - first), owner});
            first = end;
        }
    }
    out.row.push_back(out.blocks.size());
    require(out.ids.size() == total, "owner grouping lost edges");
    return out;
}
struct BlockWork { size_t tested = 0, skipped_entries = 0; };
template<bool Count>
static std::vector<Id> search_owner_blocks(size_t k, const OwnerBlocks& graph,
    const std::vector<std::vector<Id>>& entries, const Vectors& base, const Workload& w,
    RawL2Space& l2, const float* query, const Query& q, size_t ef, Scratch& scratch,
    SearchStats& stats, BlockWork& blocks) {
    ef = std::max(k, ef);
    if (++scratch.epoch == 0) {
        std::fill(scratch.seen.begin(), scratch.seen.end(), 0);
        scratch.epoch = 1;
    }
    auto& frontier = scratch.frontier;
    auto& best = scratch.best;
    auto& pending = scratch.pending;
    frontier.clear(); best.clear(); pending.clear();
    if (frontier.capacity() < ef) frontier.reserve(ef);
    if (best.capacity() < ef) best.reserve(ef);
    const std::greater<Pair> nearer;
    auto admit_qualified = [&](Id u) {
        if (scratch.seen[u] == scratch.epoch) {
            if constexpr (Count) ++stats.duplicate_edges;
            return;
        }
        scratch.seen[u] = scratch.epoch;
        pending.push_back(u);
#if defined(__GNUC__) || defined(__clang__)
        __builtin_prefetch(base.at(u), 0, 1);
        if (base.dim > 16) __builtin_prefetch(base.at(u) + 16, 0, 1);
#endif
    };
    auto score_pending = [&]() {
        for (Id u : pending) {
            Pair p{l2.dist(query, base.at(u)), u};
            if constexpr (Count) {
                ++stats.distances;
                stats.rejected_distances += !q.allowed[w.owner[u]];
            }
            if (best.size() == ef && !(p < best.front())) continue;
            frontier.push_back(p);
            std::push_heap(frontier.begin(), frontier.end(), nearer);
            if (best.size() < ef) {
                best.push_back(p); std::push_heap(best.begin(), best.end());
            } else {
                size_t parent = 0;
                for (size_t child = 1; child < best.size(); child = 2 * parent + 1) {
                    if (child + 1 < best.size() && best[child] < best[child + 1]) ++child;
                    if (!(p < best[child])) break;
                    best[parent] = best[child]; parent = child;
                }
                best[parent] = p;
            }
        }
        pending.clear();
    };
    // Entry ordering is preserved, including cross-root duplicate entries.
    for (Id c : q.roots) for (Id u : entries[c]) {
        if constexpr (Count) ++stats.item_checks;
        if (q.allowed[w.owner[u]]) admit_qualified(u);
    }
    score_pending();
    while (!frontier.empty()) {
        const Pair p = frontier.front();
        if (best.size() == ef && p > best.front()) break;
        std::pop_heap(frontier.begin(), frontier.end(), nearer); frontier.pop_back();
        if constexpr (Count) ++stats.expansions;
        size_t selected_blocks = 0;
        for (size_t b = graph.row[p.second]; b < graph.row[p.second + 1]; ++b) {
            const auto& block = graph.blocks[b];
            if constexpr (Count) { ++stats.owner_checks; ++blocks.tested; }
            if (!q.allowed[block.owner]) {
                if constexpr (Count) blocks.skipped_entries += block.count;
                continue;
            }
            ++selected_blocks;
            if constexpr (Count) stats.adjacency_entries += block.count;
            for (size_t j = block.begin; j < block.begin + block.count; ++j) admit_qualified(graph.ids[j]);
        }
        // Each block is already ID-sorted. Multiple selected blocks need this
        // timed sort to restore the exact scoring order of the original row.
        if (selected_blocks > 1) std::sort(pending.begin(), pending.end());
        score_pending();
    }
    while (best.size() > k) { std::pop_heap(best.begin(), best.end()); best.pop_back(); }
    std::vector<Id> result;
    result.reserve(best.size());
    while (!best.empty()) {
        std::pop_heap(best.begin(), best.end()); result.push_back(best.back().second); best.pop_back();
    }
    return result;
}
