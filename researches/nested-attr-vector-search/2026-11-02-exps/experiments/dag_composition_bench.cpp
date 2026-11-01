// DAG composition research: composition order, portal/connector degree,
// neighbor pruning, highway strategy, and qualification-kernel cost.
// Fixed SIFT x CCS workload; no quantization; hnswlib and RawL2Space untouched.
#define main merge_benchmark_main
#include "../../2026-11-01-exps/experiments/merge_arms.cpp"
#undef main

#include <array>
#include <unordered_map>

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

// Portal sets are a pure function of the subtree, so they can be computed
// bottom-up before any edge is inserted. Composition order then only decides
// which parent wins scarce reverse-edge budget.
static Merged build_variant(const Vectors& base, const Workload& w, RawL2Space& l2, const Knobs& kn) {
    Merged m;
    m.edges.resize(base.n);
    m.portals.resize(w.members.size());
    m.local.resize(w.members.size());
    std::vector<std::vector<Id>> own(w.members.size());
    for (Id c = 0; c < w.members.size(); ++c) {
        const auto& members = w.members[c];
        if (members.empty()) continue;
        own[c] = representatives_k(members, base, l2, kn.portals);
        auto& g = m.local[c];
        g = std::make_unique<Graph>(&l2, members.size(), 16, 200, 100 + c);
        for (Id item : members) g->addPoint(base.at(item), item);
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
        for (Id root = 0; root < w.members.size(); ++root)
            if (!w.children[root].empty() && m.portals[root].size()) {}
        for (Id r : w.topo) if (std::find(w.topo.begin(), w.topo.end(), r) != w.topo.end()) break;
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
    return m;
}

// ------------------------------------------------------------- instrumentation
struct SearchStats {
    size_t distances = 0, expansions = 0, rejected_edges = 0, duplicate_edges = 0;
    bool trace_enabled = false;
    std::vector<Id> trace;   // item ids whose qualification was tested, in order
};

template<class Qualification>
static std::vector<Id> search_counted(const Merged& m, const Vectors& base, RawL2Space& l2,
    const float* query, const std::vector<Id>& roots, size_t ef, Scratch& scratch,
    Qualification qualifies, SearchStats& stats) {
    ++scratch.epoch;
    std::priority_queue<Pair, std::vector<Pair>, std::greater<Pair>> frontier;
    std::priority_queue<Pair> best;
    auto offer = [&](Id u) {
        if (scratch.seen[u] == scratch.epoch) { ++stats.duplicate_edges; return; }
        if (stats.trace_enabled) stats.trace.push_back(u);
        if (!qualifies(u)) { ++stats.rejected_edges; return; }
        scratch.seen[u] = scratch.epoch;
        Pair p{l2.dist(query, base.at(u)), u}; ++stats.distances;
        if (best.size() < ef || p < best.top()) {
            frontier.push(p); best.push(p);
            if (best.size() > ef) best.pop();
        }
    };
    for (Id c : roots) for (Id u : m.portals[c]) offer(u);
    while (!frontier.empty()) {
        Pair p = frontier.top(); frontier.pop();
        if (best.size() == ef && p > best.top()) break;
        ++stats.expansions;
        for (Id v : m.edges[p.second]) offer(v);
    }
    while (best.size() > 10) best.pop();
    std::vector<Id> result;
    while (!best.empty()) { result.push_back(best.top().second); best.pop(); }
    return result;
}

// Native-graph distance counter for the NodeANN reference arm.
struct NativeCounter {
    Graph* graph = nullptr;
    hnswlib::DISTFUNC<float> saved = nullptr;
    void* saved_param = nullptr;
    size_t distances = 0;
    static float call(const void* q, const void* v, const void* ctx) {
        auto& self = *static_cast<NativeCounter*>(const_cast<void*>(ctx));
        ++self.distances;
        return self.saved(q, v, self.saved_param);
    }
    explicit NativeCounter(Graph& g) : graph(&g), saved(g.fstdistfunc_), saved_param(g.dist_func_param_) {
        require(g.cur_element_count > 0, "empty local graph");
        g.fstdistfunc_ = call; g.dist_func_param_ = this;
    }
    ~NativeCounter() { graph->fstdistfunc_ = saved; graph->dist_func_param_ = saved_param; }
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

template<bool INLINE_OWNER, class Qualification>
static std::vector<Id> search_csr(const Csr& csr, const Merged& m, const Workload& w,
    const Vectors& base, RawL2Space& l2, const float* query, const std::vector<Id>& roots,
    size_t ef, Scratch& scratch, Qualification qualifies, SearchStats& stats) {
    ++scratch.epoch;
    std::priority_queue<Pair, std::vector<Pair>, std::greater<Pair>> frontier;
    std::priority_queue<Pair> best;
    auto offer = [&](Id u, Id owner) {
        if (scratch.seen[u] == scratch.epoch) { ++stats.duplicate_edges; return; }
        if (stats.trace_enabled) stats.trace.push_back(u);
        if (!qualifies(u, owner)) { ++stats.rejected_edges; return; }
        scratch.seen[u] = scratch.epoch;
        Pair p{l2.dist(query, base.at(u)), u}; ++stats.distances;
        if (best.size() < ef || p < best.top()) {
            frontier.push(p); best.push(p);
            if (best.size() > ef) best.pop();
        }
    };
    for (Id c : roots) for (Id u : m.portals[c]) offer(u, w.owner[u]);
    while (!frontier.empty()) {
        Pair p = frontier.top(); frontier.pop();
        if (best.size() == ef && p > best.top()) break;
        ++stats.expansions;
        const uint64_t begin = csr.offsets[p.second], end = csr.offsets[p.second + 1];
        for (uint64_t k = begin; k < end; ++k)
            offer(csr.ids[k], INLINE_OWNER ? csr.owners[k] : w.owner[csr.ids[k]]);
    }
    while (best.size() > 10) best.pop();
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
    explicit DenseScratch(size_t n) : bits((n + 63) / 64, 0) {}
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

template<class Edges, class Qualification>
static std::vector<Id> search_dense(const Edges& edges, const Merged& m, const Vectors& base,
    RawL2Space& l2, const float* query, const std::vector<Id>& roots, size_t ef,
    DenseScratch& visited, Qualification qualifies, SearchStats& stats) {
    visited.begin();
    std::priority_queue<Pair, std::vector<Pair>, std::greater<Pair>> frontier;
    std::priority_queue<Pair> best;
    auto offer = [&](Id u, Id owner) {
        if (visited.test(u)) { ++stats.duplicate_edges; return; }
        if (stats.trace_enabled) stats.trace.push_back(u);
        if (!qualifies(u, owner)) { ++stats.rejected_edges; return; }
        visited.set(u);
        Pair p{l2.dist(query, base.at(u)), u}; ++stats.distances;
        if (best.size() < ef || p < best.top()) {
            frontier.push(p); best.push(p);
            if (best.size() > ef) best.pop();
        }
    };
    for (Id c : roots) for (Id u : m.portals[c]) offer(u, edges.owner_of(u));
    while (!frontier.empty()) {
        Pair p = frontier.top(); frontier.pop();
        if (best.size() == ef && p > best.top()) break;
        ++stats.expansions;
        edges.for_each(p.second, [&](Id v, Id owner) { offer(v, owner); });
    }
    while (best.size() > 10) best.pop();
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

static uint64_t now_ns() {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now().time_since_epoch()).count());
}
static double median_us(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// -------------------------------------------------------------------- main
static int bench_main(int argc, char** argv) try {
    require(argc == 5, "usage: dag_composition_bench BASE.fvecs QUERY.fvecs DAG_DIR OUTDIR");
    const std::string out = argv[4];
    Workload w = load_workload(argv[3]);
    Vectors base = load_vectors(argv[1], w.owner.size());
    Vectors queries = load_vectors(argv[2], 24);
    require(base.dim == 128 && queries.dim == base.dim, "SIFT128 required");
    RawL2Space l2(base.dim);

    // Query scopes, roots and exact GT are computed once, outside every timer.
    std::vector<Query> workload(24);
    for (size_t i = 0; i < 24; ++i) {
        auto& q = workload[i];
        q.allowed.resize(w.members.size());
        for (Id c : w.scopes[i * (w.scopes.size() - 1) / 23]) {
            for (Id d : w.desc[c]) q.allowed[d] = 1;
            q.roots.push_back(c);
        }
        std::priority_queue<Pair> truth;
        for (Id u = 0; u < base.n; ++u) if (q.allowed[w.owner[u]]) {
            ++q.count;
            truth.emplace(l2.dist(queries.at(i), base.at(u)), u);
            if (truth.size() > 10) truth.pop();
        }
        while (!truth.empty()) { q.truth.push_back(truth.top().second); truth.pop(); }
    }

    const Knobs variants[] = {
        {.name = "baseline"},
        {.name = "portal4", .portals = 4},
        {.name = "portal16", .portals = 16},
        {.name = "portal32", .portals = 32},
        {.name = "connector2", .connector_k = 2},
        {.name = "connector3", .connector_k = 3},
        {.name = "no_reverse", .reverse_cap = 0},
        {.name = "reverse64", .reverse_cap = 64},
        {.name = "no_distributed", .distributed = false},
        {.name = "prune16", .prune_m = 16},
        {.name = "prune24", .prune_m = 24},
        {.name = "prune32", .prune_m = 32},
        {.name = "jump0", .jump_mode = 0},
        {.name = "jump2", .jump_mode = 2},
        {.name = "jump3", .jump_mode = 3},
        {.name = "order_size_asc", .order = 1},
        {.name = "order_size_desc", .order = 2},
        {.name = "order_fcfs_dag", .reverse_mode = 1},
        {.name = "order_fcfs_asc", .order = 1, .reverse_mode = 1},
        {.name = "order_fcfs_desc", .order = 2, .reverse_mode = 1},
    };
    const std::vector<size_t> efs{32, 128, 512, 2048, base.n};
    std::ofstream csv(out + "/variants.csv");
    require(bool(csv), "cannot write variants.csv");
    csv << "variant,ef,query,eligible,truth,hits,latency_us,distances,expansions,"
           "rejected_edges,duplicate_edges,degree_mean,local_edges,connector_edges,jump_edges\n"
        << std::setprecision(12);
    Scratch scratch(base.n);
    std::vector<double> timings;

    for (const auto& kn : variants) {
        auto begin = Clock::now();
        Merged built = build_variant(base, w, l2, kn);
        double build_s = us(begin) / 1e6;
        size_t edges = 0;
        for (const auto& row : built.edges) edges += row.size();
        double degree = double(edges) / base.n;
        double best_us = std::numeric_limits<double>::infinity(), best_recall = 0, max_recall = 0;
        size_t max_ef_misses = 0;
        for (size_t ef : efs) {
            double total_us = 0, recall_sum = 0;
            size_t nonempty = 0, distances = 0, expansions = 0, rejected = 0, duplicated = 0;
            for (size_t i = 0; i < 24; ++i) {
                const auto& q = workload[i];
                if (q.truth.empty()) continue;
                ++nonempty;
                SearchStats stats;
                std::vector<Id> result;
                timings.clear();
                for (int repeat = 0; repeat < 11; ++repeat) {
                    stats = {};
                    auto t0 = Clock::now();
                    result = search_counted(built, base, l2, queries.at(i), q.roots,
                        std::min(ef, q.count), scratch,
                        [&](Id u) { return bool(q.allowed[w.owner[u]]); }, stats);
                    double elapsed = us(t0);
                    if (repeat) timings.push_back(elapsed);
                }
                size_t hits = 0;
                for (Id u : result) {
                    require(q.allowed[w.owner[u]], "query returned disqualified item");
                    if (l2.dist(queries.at(i), base.at(u)) <= l2.dist(queries.at(i), base.at(q.truth.front()))) ++hits;
                }
                if (ef == base.n && hits != q.truth.size()) ++max_ef_misses;
                recall_sum += double(hits) / q.truth.size();
                total_us += median_us(timings);
                distances += stats.distances; expansions += stats.expansions;
                rejected += stats.rejected_edges; duplicated += stats.duplicate_edges;
                csv << kn.name << ',' << ef << ',' << i << ',' << q.count << ',' << q.truth.size()
                    << ',' << hits << ',' << median_us(timings) << ',' << stats.distances << ','
                    << stats.expansions << ',' << stats.rejected_edges << ',' << stats.duplicate_edges
                    << ',' << degree << ',' << built.local_edges << ','
                    << built.connector_edges << ',' << built.jump_edges << '\n';
            }
            double recall = recall_sum / nonempty, latency = total_us / nonempty;
            max_recall = std::max(max_recall, recall);
            if (recall >= 0.95 && latency < best_us) { best_us = latency; best_recall = recall; }
            std::cout << "RESULT variant=" << kn.name << " ef=" << ef << " recall=" << recall
                      << " latency_us=" << latency << " distances=" << distances / nonempty
                      << " expansions=" << expansions / nonempty
                      << " rejected_edges=" << rejected / nonempty
                      << " duplicate_edges=" << duplicated / nonempty << '\n';
        }
        std::cout << std::setprecision(12)
            << "SUMMARY variant=" << kn.name
            << " gate_us=" << (std::isfinite(best_us) ? best_us : -1.0)
            << " gate_recall=" << best_recall
            << " recall_max=" << max_recall
            << " max_ef_miss_queries=" << max_ef_misses
            << " build_s=" << build_s
            << " degree_mean=" << degree
            << " local_edges=" << built.local_edges
            << " connector_edges=" << built.connector_edges
            << " jump_edges=" << built.jump_edges << '\n';
    }

    // ------------------------------------ unified-ef arm comparison
    // ef is ONE number for every arm: the per-query search budget. NodeANN
    // splits it across the eligible local graphs (floor(ef/G), never below k);
    // merged/ACORN/prefilter consume it directly as their search width.
    // The legacy per-graph reading is kept as its own arm so the difference is
    // visible instead of being silently normalised away.
    std::cout << "building global graph for global arms\n";
    Graph global(&l2, base.n, 16, 200, 100);
    {
        std::vector<Id> order(base.n);
        std::iota(order.begin(), order.end(), 0);
        std::mt19937 rng(20261101);
        std::shuffle(order.begin(), order.end(), rng);
        for (Id u : order) global.addPoint(base.at(u), u);
    }
    QualifiedOwnerEntries entries(global, w);
    Merged baseline = build_variant(base, w, l2, Knobs{.name = "baseline"});
    AcornScratch acorn_scratch(base.n);

    std::ofstream arms(out + "/arms.csv");
    require(bool(arms), "cannot write arms.csv");
    arms << "arm,ef,ef_local,query,eligible,truth,hits,latency_us,distances,expansions,graphs,"
            "bitmap_build_us\n" << std::setprecision(12);
    Merged pruned = build_variant(base, w, l2, Knobs{.name = "prune16", .prune_m = 16});
    const char* arm_names[]{"merged", "merged_flat", "merged_flat_owner", "merged_bitmap",
                            "merged_dense", "merged_flat_owner_dense", "merged_prune16_dense",
                            "nodeann_shared", "nodeann_pergraph", "global_acorn1",
                            "global_prefilter"};
    const int arms_count = 11;
    const int nodeann_shared = 7, nodeann_pergraph = 8, acorn = 9, prefilter = 10;
    Csr csr = flatten(baseline, w);
    DenseScratch dense(base.n);
    for (size_t ef : efs) {
        double recall_sum[11]{}, total_us[11]{}, distance_sum[11]{}, expansion_sum[11]{}, graph_sum[11]{};
        double bitmap_build[11]{};
        size_t nonempty = 0, misses[11]{};
        for (size_t i = 0; i < 24; ++i) {
            const auto& q = workload[i];
            if (q.truth.empty()) continue;
            ++nonempty;
            size_t graphs = 0;
            for (Id c = 0; c < q.allowed.size(); ++c) if (q.allowed[c] && baseline.local[c]) ++graphs;
            const size_t split = graphs ? std::max<size_t>(10, ef / graphs) : ef;
            // Item-level bitmap (21 KB for this fixture) built before the timer.
            ItemBitmapKernel items;
            items.words.assign((base.n + 63) / 64, 0);
            double build_us = 0;
            {
                auto t0 = Clock::now();
                for (Id u = 0; u < base.n; ++u)
                    if (q.allowed[w.owner[u]]) items.words[u >> 6] |= UINT64_C(1) << (u & 63);
                build_us = us(t0);
            }
            for (int arm = 0; arm < arms_count; ++arm) {
                timings.clear();
                std::vector<Id> result;
                size_t distances = 0, expansions = 0, used_graphs = 0;
                size_t local_ef = (arm == nodeann_pergraph) ? ef : split;
                for (int repeat = 0; repeat < 11; ++repeat) {
                    distances = 0; expansions = 0; used_graphs = 0;
                    result.clear();
                    auto t0 = Clock::now();
                    if (arm == 0 || arm == 3) {
                        SearchStats stats;
                        if (arm == 0)
                            result = search_counted(baseline, base, l2, queries.at(i), q.roots,
                                std::min(ef, q.count), scratch,
                                [&](Id u) { return bool(q.allowed[w.owner[u]]); }, stats);
                        else
                            result = search_counted(baseline, base, l2, queries.at(i), q.roots,
                                std::min(ef, q.count), scratch,
                                [&](Id u) { return items(u); }, stats);
                        distances = stats.distances; expansions = stats.expansions;
                    } else if (arm == 1 || arm == 2) {
                        SearchStats stats;
                        if (arm == 1)
                            result = search_csr<false>(csr, baseline, w, base, l2, queries.at(i),
                                q.roots, std::min(ef, q.count), scratch,
                                [&](Id, Id owner) { return bool(q.allowed[owner]); }, stats);
                        else
                            result = search_csr<true>(csr, baseline, w, base, l2, queries.at(i),
                                q.roots, std::min(ef, q.count), scratch,
                                [&](Id, Id owner) { return bool(q.allowed[owner]); }, stats);
                        distances = stats.distances; expansions = stats.expansions;
                    } else if (arm == 4) {
                        SearchStats stats;
                        VecEdges edges{&baseline.edges, &w};
                        result = search_dense(edges, baseline, base, l2, queries.at(i), q.roots,
                            std::min(ef, q.count), dense,
                            [&](Id, Id owner) { return bool(q.allowed[owner]); }, stats);
                        distances = stats.distances; expansions = stats.expansions;
                    } else if (arm == 5) {
                        SearchStats stats;
                        CsrEdges edges{&csr, &w};
                        result = search_dense(edges, baseline, base, l2, queries.at(i), q.roots,
                            std::min(ef, q.count), dense,
                            [&](Id, Id owner) { return bool(q.allowed[owner]); }, stats);
                        distances = stats.distances; expansions = stats.expansions;
                    } else if (arm == 6) {
                        SearchStats stats;
                        VecEdges edges{&pruned.edges, &w};
                        result = search_dense(edges, pruned, base, l2, queries.at(i), q.roots,
                            std::min(ef, q.count), dense,
                            [&](Id, Id owner) { return bool(q.allowed[owner]); }, stats);
                        distances = stats.distances; expansions = stats.expansions;
                    } else if (arm == nodeann_shared || arm == nodeann_pergraph) {
                        std::priority_queue<Pair> top;
                        for (Id c = 0; c < q.allowed.size(); ++c) if (q.allowed[c] && baseline.local[c]) {
                            auto& graph = *baseline.local[c];
                            graph.setEf(std::min(local_ef, std::max<size_t>(10, w.members[c].size())));
                            NativeCounter counter(graph);
                            auto local = graph.searchKnn(queries.at(i), 10);
                            distances += counter.distances;
                            ++used_graphs;
                            while (!local.empty()) {
                                top.emplace(local.top().first, Id(local.top().second)); local.pop();
                                if (top.size() > 10) top.pop();
                            }
                        }
                        while (!top.empty()) { result.push_back(top.top().second); top.pop(); }
                    } else {
                        NativeCounter counter(global);
                        NoopAcornObserver observer;
                        if (arm == acorn)
                            result = search_global_acorn1(global, entries, queries.at(i), 10, ef,
                                acorn_scratch,
                                [&](Id u) { return bool(q.allowed[w.owner[u]]); },
                                [&](Id c) { return bool(q.allowed[c]); }, observer);
                        else
                            result = search_global_prefilter(global, entries, queries.at(i), 10, ef,
                                scratch,
                                [&](Id u) { return bool(q.allowed[w.owner[u]]); },
                                [&](Id c) { return bool(q.allowed[c]); });
                        distances = counter.distances;
                    }
                    double elapsed = us(t0);
                    if (repeat) timings.push_back(elapsed);
                }
                size_t hits = 0;
                for (Id u : result) {
                    require(q.allowed[w.owner[u]], "arm returned disqualified item");
                    if (l2.dist(queries.at(i), base.at(u)) <=
                        l2.dist(queries.at(i), base.at(q.truth.front()))) ++hits;
                }
                if (ef == base.n && hits != q.truth.size()) ++misses[arm];
                recall_sum[arm] += double(hits) / q.truth.size();
                total_us[arm] += median_us(timings);
                distance_sum[arm] += distances;
                expansion_sum[arm] += expansions;
                graph_sum[arm] += used_graphs;
                bitmap_build[arm] += arm == 3 ? build_us : 0.0;
                arms << arm_names[arm] << ',' << ef << ',' << local_ef << ',' << i << ','
                     << q.count << ',' << q.truth.size() << ',' << hits << ','
                     << median_us(timings) << ',' << distances << ',' << expansions << ','
                     << used_graphs << ',' << (arm == 3 ? build_us : 0.0) << '\n';
            }
        }
        for (int arm = 0; arm < arms_count; ++arm)
            std::cout << "ARM arm=" << arm_names[arm] << " ef=" << ef
                      << " recall=" << recall_sum[arm] / nonempty
                      << " latency_us=" << total_us[arm] / nonempty
                      << " distances=" << distance_sum[arm] / nonempty
                      << " expansions=" << expansion_sum[arm] / nonempty
                      << " graphs=" << graph_sum[arm] / nonempty
                      << " bitmap_build_us=" << bitmap_build[arm] / nonempty
                      << " max_ef_miss=" << misses[arm] << '\n';
    }

    // ------------------------------------------- qualification kernel comparison
    std::ofstream kern(out + "/kernels.csv");
    require(bool(kern), "cannot write kernels.csv");
    kern << "kernel,pattern,checks,build_us,ns_per_check,matched\n" << std::setprecision(12);
    {
        // Real access pattern: every qualification test issued by one baseline query.
        volatile size_t sink = 0;
        Merged built = build_variant(base, w, l2, Knobs{.name = "baseline"});
        for (size_t qi = 0; qi < 24; ++qi) {
            const auto& q = workload[qi];
            if (q.truth.empty()) continue;
            Scratch local_scratch(base.n);
            SearchStats stats;
            stats.trace_enabled = true;
            {
                // Baseline trace for this query (ef=512); build is outside the loop.
                search_counted(built, base, l2, queries.at(qi), q.roots, std::min<size_t>(512, q.count),
                    local_scratch, [&](Id u) { return bool(q.allowed[w.owner[u]]); }, stats);
            }
            CharKernel bytes{q.allowed};
            OwnerBitmapKernel owners;
            owners.words.assign((q.allowed.size() + 63) / 64, 0);
            for (Id d = 0; d < q.allowed.size(); ++d) if (q.allowed[d]) owners.words[d >> 6] |= UINT64_C(1) << (d & 63);
            ItemBitmapKernel items;
            items.words.assign((base.n + 63) / 64, 0);

            auto run_char = [&]() { size_t n = 0; for (Id u : stats.trace) n += bytes(w.owner[u]); sink += n; return n; };
            auto run_owner_bits = [&]() { size_t n = 0; for (Id u : stats.trace) n += owners(w.owner[u]); sink += n; return n; };
            auto build_items = [&]() {
                auto t0 = now_ns();
                std::fill(items.words.begin(), items.words.end(), 0);
                for (Id u = 0; u < base.n; ++u)
                    if (bytes(w.owner[u])) items.words[u >> 6] |= UINT64_C(1) << (u & 63);
                return double(now_ns() - t0) / 1000.0;
            };
            double build_us = build_items();
            auto run_item_bits = [&]() { size_t n = 0; for (Id u : stats.trace) n += items(u); sink += n; return n; };

            std::vector<double> tc, tb, ti;
            for (int rep = 0; rep < 9; ++rep) {
                auto t0 = now_ns(); size_t c = run_char(); tc.push_back(double(now_ns() - t0) / stats.trace.size());
                t0 = now_ns(); size_t b = run_owner_bits(); tb.push_back(double(now_ns() - t0) / stats.trace.size());
                t0 = now_ns(); size_t d2 = run_item_bits(); ti.push_back(double(now_ns() - t0) / stats.trace.size());
                require(c == b && b == d2, "qualification kernels disagree");
            }
            require(build_us >= 0, "item bitmap build");
            if (qi % 6 == 0) {
                kern << "char_owner," << "trace" << ',' << stats.trace.size() << ",0,"
                     << median_us(tc) << ',' << 0 << '\n'
                     << "owner_bitmap,trace," << stats.trace.size() << ",0,"
                     << median_us(tb) << ',' << 0 << '\n'
                     << "item_bitmap,trace," << stats.trace.size() << ',' << build_us << ','
                     << median_us(ti) << ',' << 0 << '\n';
            }
        }
        // Multi-label predicate: bitmap vs per-item label scan. Synthetic, itemised in report.
        std::mt19937 rng(20261102);
        const size_t n = 200000, labels_per_item = 8, n_labels = 4096;
        std::vector<std::array<uint16_t, 8>> tags(n);
        for (auto& t : tags) for (auto& x : t) x = uint16_t(rng() % n_labels);
        std::vector<uint64_t> query_bits(n_labels / 64, 0);
        query_bits[3] = 0xF0F0; query_bits[17] = 0x0F0F;      // two required labels
        std::vector<uint64_t> item_bits((n + 63) / 64, 0);
        auto t0 = now_ns();
        for (size_t i = 0; i < n; ++i) {
            bool ok = false;
            for (auto x : tags[i]) if ((query_bits[x >> 6] >> (x & 63)) & 1) { ok = true; break; }
            if (ok) item_bits[i >> 6] |= UINT64_C(1) << (i & 63);
        }
        double build2 = double(now_ns() - t0) / 1e3;
        std::vector<double> scan, bits;
        for (int rep = 0; rep < 9; ++rep) {
            auto s0 = now_ns(); size_t hit = 0;
            for (size_t i = 0; i < n; ++i) {
                for (auto x : tags[i]) if ((query_bits[x >> 6] >> (x & 63)) & 1) { ++hit; break; }
            }
            sink += hit;
            scan.push_back(double(now_ns() - s0) / n);
            s0 = now_ns(); size_t hit2 = 0;
            for (size_t i = 0; i < n; ++i) hit2 += (item_bits[i >> 6] >> (i & 63)) & 1;
            sink += hit2;
            bits.push_back(double(now_ns() - s0) / n);
            require(hit == hit2, "multi-label kernels disagree");
        }
        kern << "label_scan,multilabel," << n << ',' << build2 << ',' << median_us(scan) << ',' << 1 << '\n'
             << "item_bitmap,multilabel," << n << ",0," << median_us(bits) << ',' << 1 << '\n';
        std::cout << "KERNEL sink=" << sink << '\n';
    }

    // Scope resolution: subtree enumeration vs per-concept bitmap OR.
    std::ofstream scope(out + "/scope.csv");
    require(bool(scope), "cannot write scope.csv");
    scope << "query,dirs,enumerated_writes,word_ors,enumerate_us,bitmap_us,mismatch\n" << std::setprecision(12);
    {
        const size_t words = (w.members.size() + 63) / 64;
        std::vector<std::vector<uint64_t>> dir_bits(w.members.size(), std::vector<uint64_t>(words, 0));
        for (Id c = 0; c < w.members.size(); ++c)
            for (Id d : w.desc[c]) dir_bits[c][d >> 6] |= UINT64_C(1) << (d & 63);
        std::vector<uint64_t> acc(words);
        volatile uint64_t sink2 = 0;
        for (size_t qi = 0; qi < 24; ++qi) {
            std::vector<Id> dirs;
            for (Id c : w.scopes[qi * (w.scopes.size() - 1) / 23]) dirs.push_back(c);
            size_t writes = 0;
            for (Id c : dirs) writes += w.desc[c].size();
            std::vector<double> te, tb;
            size_t mismatch = 0;
            for (int rep = 0; rep < 9; ++rep) {
                auto t0 = now_ns();
                std::vector<char> mask(w.members.size(), 0);
                for (Id c : dirs) for (Id d : w.desc[c]) mask[d] = 1;
                te.push_back(double(now_ns() - t0) / 1e3);
                t0 = now_ns();
                std::fill(acc.begin(), acc.end(), 0);
                for (Id c : dirs) for (size_t k = 0; k < words; ++k) acc[k] |= dir_bits[c][k];
                tb.push_back(double(now_ns() - t0) / 1e3);
                for (Id d = 0; d < w.members.size(); ++d) {
                    bool bit = (acc[d >> 6] >> (d & 63)) & 1;
                    if (bit != bool(mask[d])) ++mismatch;
                }
                sink2 += acc[0];
            }
            scope << qi << ',' << dirs.size() << ',' << writes << ',' << dirs.size() * words << ','
                  << median_us(te) << ',' << median_us(tb) << ',' << mismatch << '\n';
        }
        std::cout << "SCOPE sink=" << sink2 << '\n';
    }
    return 0;
} catch (const std::exception& e) {
    std::cerr << "dag_composition_bench: " << e.what() << '\n';
    return 1;
}

#ifndef DAG_COMPOSITION_LIBRARY
int main(int argc, char** argv) { return bench_main(argc, argv); }
#endif
