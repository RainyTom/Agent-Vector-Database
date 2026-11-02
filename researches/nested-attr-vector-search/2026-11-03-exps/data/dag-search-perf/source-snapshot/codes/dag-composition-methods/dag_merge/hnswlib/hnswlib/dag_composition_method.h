#pragma once
// HNSW graph composition/search adaptation; method-local fork, not shared at runtime.
#include "global_prefilter_hnsw.h"
#include "global_acorn1_hnsw.h"

static constexpr size_t kHnswM = 64;
static constexpr size_t kHnswEfConstruction = 200;
// ---------------------------------------------------------------- composition
struct Knobs {
    const char* name = "baseline";
    size_t portals = 8;        // farthest-point representatives per input group
    size_t connector_k = 1;    // nearest portals joined per other input
    int reverse_cap = 16;      // per-node cap on absorbed reverse bridges, <0 disables
    bool distributed = true;   // direct items also bridge into each input
    size_t prune_m = 0;        // 0 keeps every edge; else diverse out-degree cap
    int jump_mode = 1;         // 0 none, 1 owner->subtree reps, 2 +child->parent, 3 +root hub
    int order = 0;             // 0 bottom-up topo, 1 subtree asc, 2 subtree desc
    int reverse_mode = 0;      // 0 keep nearest reverse sources, 1 first-come (order-sensitive)
    uint32_t local_seed = 100; // per-attribute graph seed base
    bool shuffle_local = false;// build local graphs in shuffled insertion order
};

// Same farthest-point shape as representatives(), with a parameterised count.
static std::vector<Id> representatives_k(std::vector<Id> candidates, const Vectors& base,
                                         RawL2Space& l2, size_t k) {
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    if (candidates.size() <= k || k == 0) return candidates;
    std::vector<Id> selected{candidates.front()};
    std::vector<float> nearest(candidates.size(), std::numeric_limits<float>::infinity());
    std::vector<char> taken(candidates.size());
    taken[0] = 1;
    while (selected.size() < k) {
        size_t best = candidates.size();
        for (size_t i = 0; i < candidates.size(); ++i) {
            nearest[i] = std::min(nearest[i], l2.dist(base.at(candidates[i]), base.at(selected.back())));
            if (taken[i]) continue;
            if (best == candidates.size() || nearest[i] > nearest[best]) best = i;
        }
        require(best != candidates.size(), "representative selection failed");
        taken[best] = 1;
        selected.push_back(candidates[best]);
    }
    return selected;
}

// Retain both entry meanings without changing the shared graph adjacency.
struct ComposedGraph : Merged {
    std::vector<std::vector<Id>> direct_portals;
};
// Portal summaries require children before parents. With nearest-distance
// reverse admission, edge insertion order itself does not change the result.
static ComposedGraph build_variant(const Vectors& base, const Workload& w, RawL2Space& l2, const Knobs& kn) {
    ComposedGraph m;
    m.edges.resize(base.n);
    m.portals.resize(w.members.size());
    m.local.resize(w.members.size());
    std::vector<std::vector<Id>> own(w.members.size());
    for (Id c = 0; c < w.members.size(); ++c) {
        const auto& members = w.members[c];
        if (members.empty()) continue;
        own[c] = representatives_k(members, base, l2, kn.portals);
        auto& g = m.local[c];
        g = std::make_unique<Graph>(&l2, members.size(), kHnswM, kHnswEfConstruction, kn.local_seed + c);
        if (kn.shuffle_local) {
            std::vector<Id> insertion(members);
            std::mt19937 rng(kn.local_seed + 7 * c);
            std::shuffle(insertion.begin(), insertion.end(), rng);
            for (Id item : insertion) g->addPoint(base.at(item), item);
        } else {
            for (Id item : members) g->addPoint(base.at(item), item);
        }
        for (size_t i = 0; i < members.size(); ++i) {
            Id u = g->getExternalLabel(i);
            auto* list = g->get_linklist0(i);
            auto* ids = reinterpret_cast<hnswlib::tableint*>(list + 1);
            for (size_t j = 0; j < g->getListCount(list); ++j) {
                Id v = g->getExternalLabel(ids[j]);
                m.edges[u].push_back(v); m.edges[v].push_back(u);
            }
        }
    }
    auto normalize = [&]() {
        size_t count = 0;
        for (auto& row : m.edges) {
            std::sort(row.begin(), row.end());
            row.erase(std::unique(row.begin(), row.end()), row.end());
            count += row.size();
        }
        return count;
    };
    m.local_edges = normalize();

    // Phase A: portals for every concept, children before parents.
    for (auto it = w.topo.rbegin(); it != w.topo.rend(); ++it) {
        Id c = *it;
        std::vector<Id> pool;
        if (!own[c].empty()) pool.insert(pool.end(), own[c].begin(), own[c].end());
        for (Id child : w.children[c])
            if (!m.portals[child].empty())
                pool.insert(pool.end(), m.portals[child].begin(), m.portals[child].end());
        m.portals[c] = representatives_k(std::move(pool), base, l2, kn.portals);
    }

    auto connect = [&](Id u, Id v) {
        if (u != v) { m.edges[u].push_back(v); m.edges[v].push_back(u); }
    };
    std::vector<std::priority_queue<Pair>> reverse_bridges(base.n);
    std::vector<Id> process(w.topo.rbegin(), w.topo.rend());
    if (kn.order) {
        std::stable_sort(process.begin(), process.end(), [&](Id a, Id b) {
            const size_t sa = w.desc[a].size(), sb = w.desc[b].size();
            return kn.order == 1 ? sa < sb : sa > sb;
        });
    }
    for (Id c : process) {
        std::vector<std::vector<Id>> groups;
        if (!own[c].empty()) groups.push_back(own[c]);
        for (Id child : w.children[c]) if (!m.portals[child].empty()) groups.push_back(m.portals[child]);
        if (groups.empty()) continue;
        // Prim MST over input groups; shared descendants may give zero-distance joins.
        std::vector<char> used(groups.size());
        std::vector<float> best(groups.size(), std::numeric_limits<float>::infinity());
        std::vector<std::pair<Id, Id>> endpoints(groups.size());
        best[0] = 0;
        for (size_t step = 0; step < groups.size(); ++step) {
            size_t g = groups.size();
            for (size_t i = 0; i < groups.size(); ++i)
                if (!used[i] && (g == groups.size() || best[i] < best[g])) g = i;
            require(g != groups.size(), "merge MST failed");
            used[g] = 1;
            if (step) connect(endpoints[g].first, endpoints[g].second);
            for (size_t h = 0; h < groups.size(); ++h) if (!used[h])
                for (Id u : groups[g]) for (Id v : groups[h]) {
                    float d = l2.dist(base.at(u), base.at(v));
                    if (d < best[h]) { best[h] = d; endpoints[h] = {u, v}; }
                }
        }
        // Every portal joins its connector_k nearest portals in each other input.
        for (size_t g = 0; g < groups.size(); ++g)
            for (size_t h = 0; h < groups.size(); ++h) if (g != h)
                for (Id u : groups[g]) {
                    std::vector<Pair> nearest;
                    for (Id v : groups[h]) nearest.emplace_back(l2.dist(base.at(u), base.at(v)), v);
                    std::sort(nearest.begin(), nearest.end());
                    for (size_t j = 0; j < nearest.size() && j < kn.connector_k; ++j)
                        connect(u, nearest[j].second);
                }
        if (kn.distributed) {
            std::vector<Id> sources{c};
            sources.insert(sources.end(), w.children[c].begin(), w.children[c].end());
            for (Id source : sources) for (Id u : w.members[source])
                for (const auto& group : groups) {
                    Pair nearest{std::numeric_limits<float>::infinity(), 0};
                    for (Id v : group) if (w.owner[v] != source)
                        nearest = std::min(nearest, Pair{l2.dist(base.at(u), base.at(v)), v});
                    if (std::isfinite(nearest.first)) {
                        m.edges[u].push_back(nearest.second);
                        if (kn.reverse_cap > 0) {
                            auto& reverse = reverse_bridges[nearest.second];
                            if (kn.reverse_mode == 0) {
                                reverse.emplace(nearest.first, u);
                                while (reverse.size() > size_t(kn.reverse_cap)) reverse.pop();
                            } else if (reverse.size() < size_t(kn.reverse_cap)) {
                                reverse.emplace(nearest.first, u);
                            }
                        }
                    }
                }
        }
    }
    if (kn.reverse_cap > 0) for (Id v = 0; v < base.n; ++v) {
        while (!reverse_bridges[v].empty()) {
            m.edges[v].push_back(reverse_bridges[v].top().second);
            reverse_bridges[v].pop();
        }
    }
    size_t connected = normalize();
    m.connector_edges = connected - m.local_edges;

    // Highway/Skip strategy. Mode 1 is the previous static cross-level shortcut.
    if (kn.jump_mode >= 1) {
        for (Id c : w.topo) for (Id u : own[c]) for (Id v : m.portals[c]) connect(u, v);
    }
    if (kn.jump_mode >= 2) {
        for (Id c : w.topo) for (Id child : w.children[c])
            for (Id u : m.portals[child]) for (Id v : m.portals[c]) connect(u, v);
    }
    if (kn.jump_mode >= 3) {
        std::vector<Id> hub;
        for (Id c = 0; c < w.members.size(); ++c)
            if (!m.portals[c].empty()) {
                bool has_parent = false;
                for (Id p = 0; p < w.children.size() && !has_parent; ++p)
                    has_parent = std::find(w.children[p].begin(), w.children[p].end(), c) != w.children[p].end();
                if (!has_parent) hub.insert(hub.end(), m.portals[c].begin(), m.portals[c].end());
            }
        for (Id c : w.topo) for (Id u : m.portals[c]) for (Id v : hub) connect(u, v);
    }
    m.jump_edges = normalize() - connected;

    // Neighbor adjustment: diverse out-degree cap (hnswlib heuristic shape).
    if (kn.prune_m) {
        std::vector<Pair> candidates;
        for (Id u = 0; u < base.n; ++u) {
            if (m.edges[u].size() <= kn.prune_m) continue;
            candidates.clear();
            for (Id v : m.edges[u]) candidates.emplace_back(l2.dist(base.at(u), base.at(v)), v);
            std::sort(candidates.begin(), candidates.end());
            std::vector<Id> kept;
            for (const auto& [du, v] : candidates) {
                if (kept.size() == kn.prune_m) break;
                bool redundant = false;
                for (Id s : kept) {
                    if (l2.dist(base.at(v), base.at(s)) < du) { redundant = true; break; }
                }
                if (!redundant) kept.push_back(v);
            }
            // Diversity may fill fewer than the cap; top up by distance.
            for (const auto& [du, v] : candidates) {
                if (kept.size() == kn.prune_m) break;
                if (std::find(kept.begin(), kept.end(), v) == kept.end()) kept.push_back(v);
            }
            std::sort(kept.begin(), kept.end());
            m.edges[u].swap(kept);
        }
        m.local_edges = normalize();
        m.connector_edges = 0;
        m.jump_edges = 0;
    }
    m.direct_portals = std::move(own);
    return m;
}

// ------------------------------------------------------------- instrumentation
struct SearchStats {
    size_t distances = 0, expansions = 0, rejected_edges = 0, duplicate_edges = 0;
    size_t item_checks = 0, owner_checks = 0, adjacency_entries = 0, rejected_distances = 0;
    bool trace_enabled = false;
    std::vector<Id> trace;   // item ids whose qualification was tested, in order
};

template<bool Count = true, class Qualification>
static std::vector<Id> search_counted(size_t k, const Merged& m, const Vectors& base, RawL2Space& l2,
    const float* query, const std::vector<Id>& roots, size_t ef, Scratch& scratch,
    Qualification qualifies, SearchStats& stats,
    const std::vector<std::vector<Id>>* entries = nullptr) {
    ef = std::max(ef, k);
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
    // Qualification is immutable during one b=0 query. Cache failures too;
    // revisiting a rejected item can neither score nor traverse it.
    auto admit = [&](Id u) {
        if (scratch.seen[u] == scratch.epoch) {
            if constexpr (Count) ++stats.duplicate_edges;
            return;
        }
        scratch.seen[u] = scratch.epoch;
        if constexpr (Count) {
            ++stats.item_checks;
            if (stats.trace_enabled) stats.trace.push_back(u);
        }
        if (!qualifies(u)) { if constexpr (Count) ++stats.rejected_edges; return; }
        pending.push_back(u);
#if defined(__GNUC__) || defined(__clang__)
        // Only qualified vectors are prefetched; distance order stays unchanged.
        __builtin_prefetch(base.at(u), 0, 1);
        if (base.dim > 16) __builtin_prefetch(base.at(u) + 16, 0, 1);
#endif
    };
    auto score_pending = [&]() {
        for (Id u : pending) {
            Pair p{l2.dist(query, base.at(u)), u};
            if constexpr (Count) {
                ++stats.distances;
                stats.rejected_distances += !qualifies(u);
            }
            if (best.size() == ef && !(p < best.front())) continue;
            frontier.push_back(p);
            std::push_heap(frontier.begin(), frontier.end(), nearer);
            if (best.size() < ef) {
                best.push_back(p);
                std::push_heap(best.begin(), best.end());
            } else {
                // Replace the worst in one sift-down, rather than push + pop.
                size_t parent = 0;
                for (size_t child = 1; child < best.size(); child = 2 * parent + 1) {
                    if (child + 1 < best.size() && best[child] < best[child + 1]) ++child;
                    if (!(p < best[child])) break;
                    best[parent] = best[child];
                    parent = child;
                }
                best[parent] = p;
            }
        }
        pending.clear();
    };
    for (Id c : roots) for (Id u : (entries ? *entries : m.portals)[c]) admit(u);
    score_pending();
    while (!frontier.empty()) {
        Pair p = frontier.front();
        if (best.size() == ef && p > best.front()) break;
        std::pop_heap(frontier.begin(), frontier.end(), nearer);
        frontier.pop_back();
        if constexpr (Count) {
            ++stats.expansions;
            stats.adjacency_entries += m.edges[p.second].size();
        }
        for (Id v : m.edges[p.second]) admit(v);
        score_pending();
    }
    while (best.size() > k) {
        std::pop_heap(best.begin(), best.end());
        best.pop_back();
    }
    std::vector<Id> result;
    result.reserve(best.size());
    while (!best.empty()) {
        std::pop_heap(best.begin(), best.end());
        result.push_back(best.back().second);
        best.pop_back();
    }
    return result;
}

// Count only in the untimed pass; preserve the original native traversal.
struct NativeCounter {
    Graph& graph;
    const Workload& workload;
    const std::vector<char>& allowed;
    hnswlib::DISTFUNC<float> saved;
    void* saved_param;
    const char* origin;
    ptrdiff_t stride;
    size_t distances = 0, rejected = 0;
    static float call(const void* q, const void* v, const void* ctx) {
        auto& self = *static_cast<NativeCounter*>(const_cast<void*>(ctx));
        const ptrdiff_t delta = static_cast<const char*>(v) - self.origin;
        require(delta >= 0 && delta % self.stride == 0, "invalid native vector address");
        const size_t internal = size_t(delta / self.stride);
        require(internal < self.graph.cur_element_count, "invalid native internal ID");
        ++self.distances;
        self.rejected += !self.allowed[self.workload.owner[self.graph.getExternalLabel(internal)]];
        return self.saved(q, v, self.saved_param);
    }
    NativeCounter(Graph& g, const Workload& w, const std::vector<char>& mask)
        : graph(g), workload(w), allowed(mask), saved(g.fstdistfunc_),
          saved_param(g.dist_func_param_), origin(g.getDataByInternalId(0)),
          stride(g.cur_element_count > 1 ? g.getDataByInternalId(1) - g.getDataByInternalId(0) : 1) {
        require(g.cur_element_count > 0, "empty native graph");
        g.fstdistfunc_ = call; g.dist_func_param_ = this;
    }
    ~NativeCounter() { graph.fstdistfunc_ = saved; graph.dist_func_param_ = saved_param; }
    NativeCounter(const NativeCounter&) = delete;
    NativeCounter& operator=(const NativeCounter&) = delete;
};

// ------------------------------------------- flattened adjacency ablation
// Same graph, same search order, different memory layout: CSR removes one
// pointer chase per expansion, and the inline copy removes the random
// item->owner load per inspected edge.
struct Csr {
    std::vector<uint64_t> offsets;
    std::vector<Id> ids;
    std::vector<Id> owners;
};
static Csr flatten(const Merged& m, const Workload& w) {
    Csr c;
    c.offsets.resize(m.edges.size() + 1);
    for (size_t u = 0; u < m.edges.size(); ++u) c.offsets[u + 1] = c.offsets[u] + m.edges[u].size();
    c.ids.reserve(c.offsets.back());
    c.owners.reserve(c.offsets.back());
    for (const auto& row : m.edges)
        for (Id v : row) { c.ids.push_back(v); c.owners.push_back(w.owner[v]); }
    return c;
}

template<bool INLINE_OWNER, bool Count = true, class Qualification>
static std::vector<Id> search_csr(size_t k, const Csr& csr, const Merged& m, const Workload& w,
    const Vectors& base, RawL2Space& l2, const float* query, const std::vector<Id>& roots,
    size_t ef, Scratch& scratch, Qualification qualifies, SearchStats& stats,
    const std::vector<std::vector<Id>>* entries = nullptr) {
    ef = std::max(ef, k);
    ++scratch.epoch;
    std::priority_queue<Pair, std::vector<Pair>, std::greater<Pair>> frontier;
    std::priority_queue<Pair> best;
    auto offer = [&](Id u, Id owner) {
        if (scratch.seen[u] == scratch.epoch) {
            if constexpr (Count) ++stats.duplicate_edges;
            return;
        }
        if constexpr (Count) {
            ++stats.item_checks;
            if (stats.trace_enabled) stats.trace.push_back(u);
        }
        if (!qualifies(u, owner)) { if constexpr (Count) ++stats.rejected_edges; return; }
        scratch.seen[u] = scratch.epoch;
        Pair p{l2.dist(query, base.at(u)), u};
        if constexpr (Count) {
            ++stats.distances;
            stats.rejected_distances += !qualifies(u, owner); // diagnostic attribution, not an algorithmic check
        }
        if (best.size() < ef || p < best.top()) {
            frontier.push(p); best.push(p);
            if (best.size() > ef) best.pop();
        }
    };
    for (Id c : roots) for (Id u : (entries ? *entries : m.portals)[c]) offer(u, w.owner[u]);
    while (!frontier.empty()) {
        Pair p = frontier.top(); frontier.pop();
        if (best.size() == ef && p > best.top()) break;
        const uint64_t begin = csr.offsets[p.second], end = csr.offsets[p.second + 1];
        if constexpr (Count) { ++stats.expansions; stats.adjacency_entries += end - begin; }
        for (uint64_t k = begin; k < end; ++k)
            offer(csr.ids[k], INLINE_OWNER ? csr.owners[k] : w.owner[csr.ids[k]]);
    }
    while (best.size() > k) best.pop();
    std::vector<Id> result;
    while (!best.empty()) { result.push_back(best.top().second); best.pop(); }
    return result;
}

// ------------------------------------------- visited-set locality ablation
// The per-item visited array is 4 B x 168,937 = 675 KB of random accesses.
// A 21 KB bitset with per-query reset of touched words trades shift logic
// for cache residency. Same traversal, same results.
struct DenseScratch {
    std::vector<uint64_t> bits;
    std::vector<uint32_t> touched;
    explicit DenseScratch(size_t n) : bits((n + 63) / 64, 0) { touched.reserve(bits.size()); }
    void begin() {
        for (uint32_t w : touched) bits[w] = 0;
        touched.clear();
    }
    // Mirrors the epoch array exactly: rejected items are never marked visited.
    bool test(Id u) const { return (bits[u >> 6] >> (u & 63)) & 1; }
    void set(Id u) {
        uint64_t& word = bits[u >> 6];
        if (word == 0) touched.push_back(u >> 6);
        word |= UINT64_C(1) << (u & 63);
    }
};

template<bool Count = true, class Edges, class Qualification>
static std::vector<Id> search_dense(size_t k, const Edges& edges, const Merged& m, const Vectors& base,
    RawL2Space& l2, const float* query, const std::vector<Id>& roots, size_t ef,
    DenseScratch& visited, Qualification qualifies, SearchStats& stats,
    const std::vector<std::vector<Id>>* entries = nullptr) {
    ef = std::max(ef, k);
    visited.begin();
    std::priority_queue<Pair, std::vector<Pair>, std::greater<Pair>> frontier;
    std::priority_queue<Pair> best;
    auto offer = [&](Id u, Id owner) {
        if (visited.test(u)) { if constexpr (Count) ++stats.duplicate_edges; return; }
        if constexpr (Count) {
            ++stats.item_checks;
            if (stats.trace_enabled) stats.trace.push_back(u);
        }
        if (!qualifies(u, owner)) { if constexpr (Count) ++stats.rejected_edges; return; }
        visited.set(u);
        Pair p{l2.dist(query, base.at(u)), u};
        if constexpr (Count) {
            ++stats.distances;
            stats.rejected_distances += !qualifies(u, owner); // diagnostic attribution, not an algorithmic check
        }
        if (best.size() < ef || p < best.top()) {
            frontier.push(p); best.push(p);
            if (best.size() > ef) best.pop();
        }
    };
    for (Id c : roots) for (Id u : (entries ? *entries : m.portals)[c]) offer(u, edges.owner_of(u));
    while (!frontier.empty()) {
        Pair p = frontier.top(); frontier.pop();
        if (best.size() == ef && p > best.top()) break;
        if constexpr (Count) ++stats.expansions;
        edges.for_each(p.second, [&](Id v, Id owner) {
            if constexpr (Count) ++stats.adjacency_entries;
            offer(v, owner);
        });
    }
    while (best.size() > k) best.pop();
    std::vector<Id> result;
    while (!best.empty()) { result.push_back(best.top().second); best.pop(); }
    return result;
}

// Edge adapters so one search body covers the vector-of-vectors and CSR layouts.
struct VecEdges {
    const std::vector<std::vector<Id>>* rows;
    const Workload* w;
    Id owner_of(Id u) const { return w->owner[u]; }
    template<class F> void for_each(Id u, F f) const { for (Id v : (*rows)[u]) f(v, w->owner[v]); }
};
struct CsrEdges {
    const Csr* csr;
    const Workload* w;
    Id owner_of(Id u) const { return w->owner[u]; }
    template<class F> void for_each(Id u, F f) const {
        for (uint64_t k = csr->offsets[u]; k < csr->offsets[u + 1]; ++k)
            f(csr->ids[k], csr->owners[k]);
    }
};

// ------------------------------------------------------- qualification kernels
struct CharKernel {                       // per-directory byte mask (current)
    std::vector<char> allowed;
    bool operator()(Id owner) const { return bool(allowed[owner]); }
};
struct OwnerBitmapKernel {                // per-directory bitmask
    std::vector<uint64_t> words;
    bool operator()(Id owner) const { return (words[owner >> 6] >> (owner & 63)) & 1; }
};
struct ItemBitmapKernel {                 // per-item bitmask, built per query
    std::vector<uint64_t> words;
    bool operator()(Id item) const { return (words[item >> 6] >> (item & 63)) & 1; }
};
