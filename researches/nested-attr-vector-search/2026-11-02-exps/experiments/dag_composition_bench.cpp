// Shared workload, GT and measurement harness only. Graph construction and
// traversal are supplied by this executable's independent hnswlib fork.
#include "dag_composition_fixture.h"
#include "hnswlib/dag_composition_method.h"
#ifndef DAG_VALIDATION_FAMILY
#error "Build a method's main.cpp with its own hnswlib include directory"
#endif

static uint64_t now_ns() {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now().time_since_epoch()).count());
}
static double median_us(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

static Query resolve_scope(const Workload& w, const std::vector<Id>& dirs, bool recursive) {
    Query q;
    q.allowed.assign(w.members.size(), 0);
    for (Id c : dirs) {
        if (recursive) for (Id d : w.desc[c]) q.allowed[d] = 1;
        else q.allowed[c] = 1;
    }
    if (recursive) {
        std::vector<char> ancestor(w.members.size());
        for (Id c : w.topo) if (q.allowed[c]) {
            if (!ancestor[c]) q.roots.push_back(c);
            for (Id d : w.children[c]) ancestor[d] = 1;
        }
    } else {
        q.roots = dirs;
        std::sort(q.roots.begin(), q.roots.end());
        q.roots.erase(std::unique(q.roots.begin(), q.roots.end()), q.roots.end());
    }
    return q;
}
static const std::vector<std::vector<Id>>& query_entries(const ComposedGraph& graph, bool recursive) {
    return recursive ? graph.portals : graph.direct_portals;
}
static void write_roots(std::ostream& out, const std::vector<Id>& roots) {
    for (size_t i = 0; i < roots.size(); ++i) { if (i) out << ';'; out << roots[i]; }
}
static size_t checked_hits(size_t k, const std::vector<Id>& result, const Query& q,
                           const Workload& w, const Vectors& base, RawL2Space& raw, const float* vector) {
    const size_t truth_count = std::min(k, q.count);
    require(result.size() <= truth_count && q.truth.size() >= truth_count, "invalid result/GT capacity");
    std::vector<Id> unique(result);
    std::sort(unique.begin(), unique.end());
    require(std::adjacent_find(unique.begin(), unique.end()) == unique.end(), "duplicate result");
    if (q.truth.empty()) { require(result.empty(), "nonempty result for empty scope"); return 0; }
    size_t hits = 0;
    const float boundary = raw.dist(vector, base.at(q.truth[q.truth.size() - truth_count]));
    for (Id u : result) {
        require(u < base.n && q.allowed[w.owner[u]], "disqualified result");
        hits += raw.dist(vector, base.at(u)) <= boundary;
    }
    return hits;
}
#include "dag_pareto_benchmark.h"
// ------------------------- per-query cross-owner edge pruning ablation
// Entry lists and every qualified adjacency row stay verbatim; only edges whose
// target owner fails this query's qualification are dropped. Traversal rejects
// such a target before computing a distance and never marks it visited, so the
// pruned graph must return identical IDs with identical distance/expansion
// work. A time difference therefore isolates reading, owner-loading and
// rejecting those entries. Per-query copies are built outside all timed regions.
static std::vector<std::vector<Id>> prune_rows(const std::vector<std::vector<Id>>& rows,
                                               const std::vector<Id>& owner,
                                               const std::vector<char>& allowed) {
    std::vector<std::vector<Id>> kept(rows.size());
    for (size_t u = 0; u < rows.size(); ++u) {
        const auto& src = rows[u];
        size_t n = 0;
        for (Id v : src) n += bool(allowed[owner[v]]);
        if (n == src.size()) { kept[u] = src; continue; }
        kept[u].reserve(n);
        for (Id v : src) if (allowed[owner[v]]) kept[u].push_back(v);
    }
    return kept;
}
static Csr flatten_rows(const std::vector<std::vector<Id>>& rows, const std::vector<Id>& owner,
                        const std::vector<char>* allowed) {
    Csr c;
    c.offsets.assign(rows.size() + 1, 0);
    for (size_t u = 0; u < rows.size(); ++u) {
        size_t n = 0;
        for (Id v : rows[u]) n += !allowed || bool((*allowed)[owner[v]]);
        c.offsets[u + 1] = c.offsets[u] + n;
    }
    c.ids.resize(c.offsets.back());
    c.owners.resize(c.offsets.back());
    size_t at = 0;
    for (size_t u = 0; u < rows.size(); ++u)
        for (Id v : rows[u])
            if (!allowed || bool((*allowed)[owner[v]])) { c.ids[at] = v; c.owners[at] = owner[v]; ++at; }
    require(at == c.offsets.back(), "flattened edge count mismatch");
    return c;
}
static constexpr size_t kEdgePasses = 16, kEdgeRounds = 10;
struct EdgePruneRow {
    size_t cell = 0, k = 0, ef = 0, query = 0, eligible = 0, truth = 0, hits = 0, groups = 0;
    size_t edges = 0, edges_kept = 0, expanded = 0, expanded_kept = 0;
    size_t distances = 0, expansions = 0, item_checks = 0;
    double prune_build_us = 0;
};
static void run_edge_prune(const Vectors& base, const Vectors& queries, const Workload& w,
                           RawL2Space& l2, const std::vector<Query>& workload,
                           bool recursive, const std::string& semantics, const std::string& out) {
    require(DAG_VALIDATION_FAMILY == 0, "edge-prune requires the merged-graph build");
    constexpr std::pair<size_t, size_t> cells[] = {{10, 32}, {100, 128}};
    auto graph = build_variant(base, w, l2, Knobs{});
    const auto& entry = query_entries(graph, recursive);
    Scratch scratch(base.n);
    auto consume = [](const std::vector<Id>& ids) {
        uint64_t h = UINT64_C(1469598103934665603);
        for (Id v : ids) h = (h ^ v) * UINT64_C(1099511628211);
        return h;
    };
    // Phase 1 (untimed): build every per-query pruned copy and audit it.
    std::vector<Merged> pruned(workload.size());
    std::vector<EdgePruneRow> table;
    for (size_t i = 0; i < workload.size(); ++i) {
        const auto& q = workload[i];
        if (!q.count) continue;
        const auto begin = Clock::now();
        pruned[i].edges = prune_rows(graph.edges, w.owner, q.allowed);
        pruned[i].portals = graph.portals;
        const double per_query_build = us(begin);
        Csr csr_full = flatten_rows(graph.edges, w.owner, nullptr);
        Csr csr_pruned = flatten_rows(pruned[i].edges, w.owner, nullptr);
        size_t groups = 0, query_edges = 0, query_kept = 0;
        for (size_t u = 0; u < graph.edges.size(); ++u) {
            query_edges += graph.edges[u].size();
            query_kept += pruned[i].edges[u].size();
        }
        for (Id attr = 0; attr < q.allowed.size(); ++attr)
            groups += q.allowed[attr] && !w.members[attr].empty();
        for (size_t ci = 0; ci < 2; ++ci) {
            const size_t k = cells[ci].first;
            const size_t ef = std::min(std::max(k, cells[ci].second), q.count);
            auto qualify_item = [&](Id u) { return bool(q.allowed[w.owner[u]]); };
            auto qualify_owner = [&](Id, Id owner) { return bool(q.allowed[owner]); };
            SearchStats wm, wp, wf, wfp;
            const auto merged = search_counted<true>(k, graph, base, l2, queries.at(i), q.roots, ef,
                scratch, qualify_item, wm, &entry);
            const auto trimmed = search_counted<true>(k, pruned[i], base, l2, queries.at(i), q.roots, ef,
                scratch, qualify_item, wp, &entry);
            require(merged == trimmed, "edge pruning changed result IDs");
            require(wm.distances == wp.distances && wm.expansions == wp.expansions,
                    "edge pruning changed distance or expansion work");
            const auto flat = search_csr<true, true>(k, csr_full, graph, w, base, l2, queries.at(i),
                q.roots, ef, scratch, qualify_owner, wf, &entry);
            const auto flat_pruned = search_csr<true, true>(k, csr_pruned, graph, w, base, l2, queries.at(i),
                q.roots, ef, scratch, qualify_owner, wfp, &entry);
            require(flat == merged && flat_pruned == trimmed, "flattened layout changed result IDs");
            require(wf.distances == wm.distances && wfp.distances == wm.distances &&
                    wf.expansions == wm.expansions && wfp.expansions == wm.expansions,
                    "flattened layout changed distance or expansion work");
            require(wfp.item_checks == wp.item_checks && wfp.duplicate_edges == wp.duplicate_edges,
                    "flattened layout changed inspected offers");
            require(wf.adjacency_entries == wm.adjacency_entries &&
                    wp.adjacency_entries <= wm.adjacency_entries,
                    "flattened adjacency counts disagree");
            const size_t hits = checked_hits(k, merged, q, w, base, l2, queries.at(i));
            require(checked_hits(k, trimmed, q, w, base, l2, queries.at(i)) == hits,
                    "edge pruning changed the audited hits");
            EdgePruneRow row;
            row.cell = ci; row.k = k; row.ef = ef; row.query = i; row.eligible = q.count;
            row.truth = std::min(k, q.count); row.hits = hits; row.groups = groups;
            row.edges = query_edges; row.edges_kept = query_kept;
            row.expanded = wm.adjacency_entries; row.expanded_kept = wp.adjacency_entries;
            row.distances = wm.distances; row.expansions = wm.expansions;
            row.item_checks = wm.item_checks;
            row.prune_build_us = per_query_build;
            table.push_back(row);
        }
    }
    std::ofstream detail(out + "/edge-prune-queries-" + semantics + ".csv");
    require(bool(detail), "cannot write edge-prune query rows");
    detail << "cell,k,ef,query,group,eligible,truth,hits,edges,edges_kept,removed_fraction,"
              "expanded,expanded_kept,expanded_removed_fraction,distances,expansions,item_checks,"
              "rejected_offers,prune_build_us\n" << std::setprecision(12);
    for (const auto& r : table)
        detail << r.cell << ',' << r.k << ',' << r.ef << ',' << r.query << ','
               << (r.groups == 1 ? "one_graph" : "many_graphs") << ',' << r.eligible << ',' << r.truth
               << ',' << r.hits << ',' << r.edges << ',' << r.edges_kept << ','
               << (r.edges ? double(r.edges - r.edges_kept) / r.edges : 0.0) << ','
               << r.expanded << ',' << r.expanded_kept << ','
               << (r.expanded ? double(r.expanded - r.expanded_kept) / r.expanded : 0.0) << ','
               << r.distances << ',' << r.expansions << ',' << r.item_checks << ','
               << (r.item_checks - r.distances) << ',' << r.prune_build_us << '\n';
    // Phase 2: the fan-out batch protocol, so these times are comparable with
    // the archived subset batches. Arms alternate every round.
    std::ofstream samples(out + "/edge-prune-batches-" + semantics + ".csv");
    require(bool(samples), "cannot write edge-prune batches");
    samples << "semantics,cell,k,ef_requested,group,arm,sample,batch_us\n" << std::setprecision(12);
    auto median = [](std::array<double, kEdgeRounds> a) {
        std::sort(a.begin(), a.end());
        return a[kEdgeRounds / 2];
    };
    std::ofstream summary(out + "/edge-prune-summary-" + semantics + ".csv");
    require(bool(summary), "cannot write edge-prune summary");
    summary << "semantics,cell,k,ef_requested,group,queries,us_merged,us_merged_pruned,"
               "prune_delta_us,prune_delta_fraction,qps_merged,qps_merged_pruned,removed_fraction,"
               "expanded_removed_fraction,reject_fraction,distances_mean,expansions_mean,"
               "item_checks_mean,prune_build_us_mean,batch_us_merged,batch_us_pruned\n"
            << std::setprecision(12);
    const char* group_names[] = {"one_graph", "many_graphs", "all"};
    for (size_t ci = 0; ci < 2; ++ci) {
        const size_t k = cells[ci].first, requested = cells[ci].second;
        std::vector<std::vector<size_t>> subsets(3);
        for (const auto& r : table) {
            if (r.cell != ci) continue;
            subsets[2].push_back(r.query);
            subsets[r.groups == 1 ? 0 : 1].push_back(r.query);
        }
        for (int gi = 0; gi < 3; ++gi) {
            const auto& subset = subsets[gi];
            if (subset.empty()) continue;
            auto one_pass = [&](bool use_pruned) {
                uint64_t h = 0;
                for (size_t i : subset) {
                    const auto& q = workload[i];
                    const size_t ef = std::min(std::max(k, requested), q.count);
                    SearchStats unused;
                    auto qualify = [&](Id u) { return bool(q.allowed[w.owner[u]]); };
                    h += consume(search_counted<false>(k, use_pruned ? pruned[i] : graph, base, l2,
                        queries.at(i), q.roots, ef, scratch, qualify, unused, &entry));
                }
                return h;
            };
            const uint64_t expected = one_pass(false);
            require(one_pass(true) == expected, "batch differs from audited results");
            std::array<double, kEdgeRounds> bm{}, bp{};
            for (size_t round = 0; round < kEdgeRounds; ++round) {
                for (int order = 0; order < 2; ++order) {
                    const bool use_pruned = (order == (round % 2)) ? false : true;
                    const auto begin = Clock::now();
                    for (size_t pass = 0; pass < kEdgePasses; ++pass)
                        require(one_pass(use_pruned) == expected, "timed pass changed results");
                    (use_pruned ? bp : bm)[round] = us(begin);
                }
            }
            for (size_t round = 0; round < kEdgeRounds; ++round) {
                samples << semantics << ',' << ci << ',' << k << ',' << requested << ','
                        << group_names[gi] << ",merged," << round << ',' << bm[round] << '\n'
                        << semantics << ',' << ci << ',' << k << ',' << requested << ','
                        << group_names[gi] << ",merged_pruned," << round << ',' << bp[round] << '\n';
            }
            size_t count = 0, edges = 0, kept = 0, expanded = 0, expanded_kept = 0;
            size_t distances = 0, expansions = 0, checks = 0;
            double build_total = 0;
            for (const auto& r : table) {
                if (r.cell != ci || (gi < 2 && (r.groups == 1) != (gi == 0))) continue;
                ++count; edges += r.edges; kept += r.edges_kept;
                expanded += r.expanded; expanded_kept += r.expanded_kept;
                distances += r.distances; expansions += r.expansions; checks += r.item_checks;
                build_total += r.prune_build_us;
            }
            const double bm_med = median(bm), bp_med = median(bp);
            const double um = bm_med / kEdgePasses / count, up = bp_med / kEdgePasses / count;
            summary << semantics << ',' << ci << ',' << k << ',' << requested << ','
                    << group_names[gi] << ',' << count << ',' << um << ',' << up
                    << ',' << um - up << ',' << (um > 0 ? (um - up) / um : 0.0)
                    << ',' << 1e6 / um << ',' << 1e6 / up
                    << ',' << (edges ? double(edges - kept) / edges : 0.0)
                    << ',' << (expanded ? double(expanded - expanded_kept) / expanded : 0.0)
                    << ',' << (checks ? double(checks - distances) / checks : 0.0)
                    << ',' << double(distances) / count << ',' << double(expansions) / count
                    << ',' << double(checks) / count << ',' << build_total / count
                    << ',' << bm_med << ',' << bp_med << '\n';
            std::cout << "EDGE-PRUNE semantics=" << semantics << " k=" << k
                      << " group=" << group_names[gi] << " queries=" << count
                      << " us_merged=" << um << " us_merged_pruned=" << up
                      << " prune_delta_fraction=" << (um > 0 ? (um - up) / um : 0.0)
                      << " expanded_removed_fraction="
                      << (expanded ? double(expanded - expanded_kept) / expanded : 0.0) << '\n';
        }
    }
}
// -------------------------------------------------------------------- main
static int bench_main(int argc, char** argv) try {
    require(argc >= 5, "usage: dag_composition_bench BASE.fvecs QUERY.fvecs DAG_DIR OUTDIR "
                       "[NQ] [all|arms|variants|pareto|edge-prune] [recursive|direct]");
    const size_t NQ = argc > 5 ? size_t(std::stoul(argv[5])) : 24;
    require(NQ >= 2 && NQ <= 10000, "invalid query count");
    const std::string mode = argc > 6 ? argv[6] : "all";
    const std::string semantics = argc > 7 ? argv[7] : "recursive";
    require(semantics == "recursive" || semantics == "direct", "semantics must be recursive|direct");
    const bool recursive = semantics == "recursive";
    const std::string out = argv[4];
    Workload w = load_workload(argv[3]);
    Vectors base = load_vectors(argv[1], w.owner.size());
    Vectors queries = load_vectors(argv[2], NQ);
    require(base.dim == 128 && queries.dim == base.dim, "SIFT128 required");
    RawL2Space l2(base.dim);

    // Query scopes, roots and exact GT are computed once, outside every timer.
    std::vector<Query> workload(NQ);
    std::vector<std::vector<Id>> qdirs(NQ);   // attributes named by each query
    const size_t truth_limit = mode == "pareto" || mode == "edge-prune" ? 100 : 10;
    std::ofstream query_csv(out + "/queries-" + semantics + ".csv");
    require(bool(query_csv), "cannot write query manifest");
    query_csv << "query,source_scope,vector_query_id,eligible,truth,roots,roots_semantics\n";
    for (size_t i = 0; i < NQ; ++i) {
        qdirs[i] = w.scopes[i * (w.scopes.size() - 1) / (NQ - 1)];
        workload[i] = resolve_scope(w, qdirs[i], recursive);
        auto& q = workload[i];
        std::priority_queue<Pair> truth;
        for (Id u = 0; u < base.n; ++u) if (q.allowed[w.owner[u]]) {
            ++q.count;
            truth.emplace(l2.dist(queries.at(i), base.at(u)), u);
            if (truth.size() > truth_limit) truth.pop();
        }
        while (!truth.empty()) { q.truth.push_back(truth.top().second); truth.pop(); }
        query_csv << i << ',' << i * (w.scopes.size() - 1) / (NQ - 1) << ',' << i << ','
                  << q.count << ',' << q.truth.size() << ',';
        write_roots(query_csv, q.roots);
        query_csv << ',' << semantics << '\n';
    }
    if (mode == "pareto") {
        run_pareto(base, queries, w, l2, workload, recursive, out);
        return 0;
    }
    if (mode == "edge-prune") {
        run_edge_prune(base, queries, w, l2, workload, recursive, semantics, out);
        return 0;
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
        {.name = "build_seed2", .local_seed = 200},
        {.name = "build_shuffle", .local_seed = 300, .shuffle_local = true},
        {.name = "order_fcfs_dag", .reverse_mode = 1},
        {.name = "order_fcfs_asc", .order = 1, .reverse_mode = 1},
        {.name = "order_fcfs_desc", .order = 2, .reverse_mode = 1},
    };
    const std::vector<size_t> efs{32, 128, 512, 2048, base.n};
    std::ofstream csv(out + "/variants-" + semantics + ".csv");
    require(bool(csv), "cannot write variants.csv");
    csv << "variant,ef,query,eligible,truth,hits,latency_us,distances,expansions,"
           "rejected_edges,duplicate_edges,degree_mean,local_edges,connector_edges,jump_edges,returned,rejected_distances,normal_verified\n"
        << std::setprecision(12);
    Scratch scratch(base.n);
    std::vector<double> timings;

    if (DAG_VALIDATION_FAMILY == 0 && mode != "arms") for (const auto& kn : variants) {
        auto begin = Clock::now();
        auto built = build_variant(base, w, l2, kn);
        double build_s = us(begin) / 1e6;
        size_t edges = 0;
        for (const auto& row : built.edges) edges += row.size();
        double degree = double(edges) / base.n;
        double best_us = std::numeric_limits<double>::infinity(), best_recall = 0, max_recall = 0;
        size_t max_ef_misses = 0;
        for (size_t ef : efs) {
            double total_us = 0, recall_sum = 0;
            size_t nonempty = 0, distances = 0, expansions = 0, rejected = 0, duplicated = 0;
            for (size_t i = 0; i < NQ; ++i) {
                const auto& q = workload[i];
                if (q.count) ++nonempty;
                SearchStats stats;
                std::vector<Id> result;
                timings.clear();
                const auto& entry = query_entries(built, recursive);
                for (int repeat = 0; repeat < 11; ++repeat) {
                    SearchStats unused;
                    auto t0 = Clock::now();
                    result = q.count ? search_counted<false>(10, built, base, l2, queries.at(i), q.roots,
                        std::min(ef, q.count), scratch,
                        [&](Id u) { return bool(q.allowed[w.owner[u]]); }, unused, &entry) : std::vector<Id>{};
                    double elapsed = us(t0);
                    if (repeat) timings.push_back(elapsed);
                }
                const auto counted = q.count ? search_counted(10, built, base, l2, queries.at(i), q.roots,
                    std::min(ef, q.count), scratch,
                    [&](Id u) { return bool(q.allowed[w.owner[u]]); }, stats, &entry) : std::vector<Id>{};
                require(counted == result, "variant normal/count mismatch");
                const size_t hits = checked_hits(10, result, q, w, base, l2, queries.at(i));
                if (ef == base.n && hits != q.truth.size()) ++max_ef_misses;
                if (q.count) {
                    recall_sum += double(hits) / q.truth.size();
                    total_us += median_us(timings);
                }
                distances += stats.distances; expansions += stats.expansions;
                rejected += stats.rejected_edges; duplicated += stats.duplicate_edges;
                csv << kn.name << ',' << ef << ',' << i << ',' << q.count << ',' << q.truth.size()
                    << ',' << hits << ',' << median_us(timings) << ',' << stats.distances << ','
                    << stats.expansions << ',' << stats.rejected_edges << ',' << stats.duplicate_edges
                    << ',' << degree << ',' << built.local_edges << ','
                    << built.connector_edges << ',' << built.jump_edges << ',' << result.size() << ",0,1\n";
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

    // Native NodeANN and graph methods receive the same ef value. The split-ef
    // NodeANN control is separate; it is NOT an equal-total-work guarantee.
    if (mode != "variants") {
        Graph global(&l2, DAG_VALIDATION_FAMILY >= 2 ? base.n : 1, kHnswM, kHnswEfConstruction, 100);
        if (DAG_VALIDATION_FAMILY >= 2) {
            std::vector<Id> order(base.n); std::iota(order.begin(), order.end(), 0);
            std::mt19937 rng(20261101); std::shuffle(order.begin(), order.end(), rng);
            for (Id u : order) global.addPoint(base.at(u), u);
        }
        QualifiedOwnerEntries entries(global, w);
        auto baseline = DAG_VALIDATION_FAMILY < 2 ? build_variant(base, w, l2, Knobs{}) : ComposedGraph{};
        baseline.local.resize(w.members.size());
        write_index_parameters(out, global, baseline);
        auto pruned = DAG_VALIDATION_FAMILY == 0 ?
            build_variant(base, w, l2, Knobs{.name = "prune16", .prune_m = 16}) : ComposedGraph{};
        auto pruned24 = DAG_VALIDATION_FAMILY == 0 ?
            build_variant(base, w, l2, Knobs{.name = "prune24", .prune_m = 24}) : ComposedGraph{};
        Csr csr = flatten(baseline, w);
        DenseScratch dense(base.n);
        AcornScratch acorn_scratch(base.n);
        const char* names[]{"merged", "merged_flat", "merged_flat_owner", "merged_bitmap",
            "merged_dense", "merged_flat_owner_dense", "merged_prune16_dense", "merged_prune24_dense",
            "nodeann_shared", "nodeann_pergraph", "global_acorn1", "global_prefilter"};
        std::ofstream arms(out + "/arms-" + semantics + ".csv");
        require(bool(arms), "cannot write arms.csv");
        arms << "arm,ef,ef_local,query,eligible,truth,hits,latency_us,distances,expansions,graphs,bitmap_build_us,"
                "returned,rejected_distances,item_checks,owner_checks,adjacency_entries,normal_verified\n"
             << std::setprecision(12);
        for (size_t ef : efs) for (size_t i = 0; i < NQ; ++i) {
            const auto& q = workload[i];
            size_t graphs = 0;
            for (Id c = 0; c < q.allowed.size(); ++c) if (q.allowed[c] && baseline.local[c]) ++graphs;
            const size_t split = graphs ? std::max<size_t>(10, ef / graphs) : ef;
            ItemBitmapKernel items;
            double bitmap_build_us = 0;
            if (DAG_VALIDATION_FAMILY == 0) {
                auto bitmap_begin = Clock::now();
                items.words.assign((base.n + 63) / 64, 0);
                for (Id u = 0; u < base.n; ++u)
                    if (q.allowed[w.owner[u]]) items.words[u >> 6] |= UINT64_C(1) << (u & 63);
                bitmap_build_us = us(bitmap_begin);
            }
            for (int arm = 0; arm < 12; ++arm) {
                if ((DAG_VALIDATION_FAMILY == 0 && arm >= 8) ||
                    (DAG_VALIDATION_FAMILY == 1 && arm != 8 && arm != 9) ||
                    (DAG_VALIDATION_FAMILY == 2 && arm != 10) ||
                    (DAG_VALIDATION_FAMILY == 3 && arm != 11)) continue;
                const size_t local_ef = arm == 8 ? split : ef;
                auto execute = [&](auto counted, SearchStats& stats) {
                    constexpr bool Count = decltype(counted)::value;
                    std::vector<Id> result;
                    if (!q.count) return result;
                    const auto& entry = query_entries(baseline, recursive);
                    if (arm == 0 || arm == 3) {
                        if (arm == 0)
                            return search_counted<Count>(10, baseline, base, l2, queries.at(i), q.roots,
                                std::min(ef, q.count), scratch,
                                [&](Id u) { return bool(q.allowed[w.owner[u]]); }, stats, &entry);
                        return search_counted<Count>(10, baseline, base, l2, queries.at(i), q.roots,
                            std::min(ef, q.count), scratch, [&](Id u) { return items(u); }, stats, &entry);
                    }
                    if (arm == 1 || arm == 2) {
                        if (arm == 1)
                            return search_csr<false, Count>(10, csr, baseline, w, base, l2, queries.at(i),
                                q.roots, std::min(ef, q.count), scratch,
                                [&](Id, Id owner) { return bool(q.allowed[owner]); }, stats, &entry);
                        return search_csr<true, Count>(10, csr, baseline, w, base, l2, queries.at(i),
                            q.roots, std::min(ef, q.count), scratch,
                            [&](Id, Id owner) { return bool(q.allowed[owner]); }, stats, &entry);
                    }
                    if (arm >= 4 && arm <= 7) {
                        const auto& source = arm == 6 ? pruned : arm == 7 ? pruned24 : baseline;
                        const auto& source_entry = query_entries(source, recursive);
                        if (arm == 5) {
                            CsrEdges edges{&csr, &w};
                            return search_dense<Count>(10, edges, baseline, base, l2, queries.at(i), q.roots,
                                std::min(ef, q.count), dense,
                                [&](Id, Id owner) { return bool(q.allowed[owner]); }, stats, &source_entry);
                        }
                        VecEdges edges{&source.edges, &w};
                        return search_dense<Count>(10, edges, source, base, l2, queries.at(i), q.roots,
                            std::min(ef, q.count), dense,
                            [&](Id, Id owner) { return bool(q.allowed[owner]); }, stats, &source_entry);
                    }
#if DAG_VALIDATION_FAMILY == 1
                    if (arm == 8 || arm == 9)
                        return search_nodeann_topn<Count>(baseline, w, q.allowed,
                            queries.at(i), 10, 10, local_ef, stats);
#endif
                    auto item_ok = [&](Id u) {
                        if constexpr (Count) ++stats.item_checks;
                        return bool(q.allowed[w.owner[u]]);
                    };
                    auto owner_ok = [&](Id c) {
                        if constexpr (Count) ++stats.owner_checks;
                        return bool(q.allowed[c]);
                    };
                    auto global_search = [&]() {
                        NoopAcornObserver observer;
                        if (arm == 10) {
                            if constexpr (Count) {
                                struct Observer : NoopAcornObserver {
                                    SearchStats& stats;
                                    explicit Observer(SearchStats& s) : stats(s) {}
                                    void on_adjacency(size_t n) { stats.adjacency_entries += n; }
                                } counted_observer(stats);
                                return search_global_acorn1(global, entries, queries.at(i), 10, ef,
                                    acorn_scratch, item_ok, owner_ok, counted_observer);
                            }
                            return search_global_acorn1(global, entries, queries.at(i), 10, ef,
                                acorn_scratch, item_ok, owner_ok, observer);
                        }
                        return search_global_prefilter(global, entries, queries.at(i), 10, ef,
                            scratch, item_ok, owner_ok);
                    };
                    if constexpr (Count) {
                        NativeCounter counter(global, w, q.allowed);
                        result = global_search();
                        stats.distances = counter.distances;
                        stats.rejected_distances = counter.rejected;
                    } else result = global_search();
                    return result;
                };
                timings.clear();
                std::vector<Id> normal;
                for (int repeat = 0; repeat < 11; ++repeat) {
                    SearchStats unused;
                    auto begin = Clock::now();
                    normal = execute(std::false_type{}, unused);
                    const double elapsed = us(begin);
                    if (repeat) timings.push_back(elapsed);
                }
                SearchStats stats;
                auto counted_result = execute(std::true_type{}, stats);
                require(normal == counted_result, "normal/count result IDs differ");
                require(stats.rejected_distances == 0, "query distance to unqualified item");
                const size_t hits = checked_hits(10, normal, q, w, base, l2, queries.at(i));
                arms << names[arm] << ',' << ef << ',';
                if (arm == 8 || arm == 9) arms << local_ef;
                arms << ',' << i << ',' << q.count << ',' << q.truth.size() << ',' << hits << ','
                     << median_us(timings) << ',' << stats.distances << ',';
                if (arm < 8) arms << stats.expansions;
                arms << ',' << (arm == 8 || arm == 9 ? graphs : 0) << ','
                     << (arm == 3 ? bitmap_build_us : 0.0) << ',' << normal.size() << ','
                     << stats.rejected_distances << ',' << stats.item_checks << ',' << stats.owner_checks << ',';
                if (arm < 8 || arm == 10) arms << stats.adjacency_entries;
                arms << ",1\n";
            }
        }
    }
    if (DAG_VALIDATION_FAMILY != 0) return 0;

    // ------------------------------------------- qualification kernel comparison
    std::ofstream kern(out + "/kernels-" + semantics + ".csv");
    require(bool(kern), "cannot write kernels.csv");
    kern << "kernel,pattern,checks,build_us,ns_per_check,matched\n" << std::setprecision(12);
    {
        // Real access pattern: every qualification test issued by one baseline query.
        volatile size_t sink = 0;
        auto built = build_variant(base, w, l2, Knobs{});
        for (size_t qi = 0; qi < NQ; ++qi) {
            const auto& q = workload[qi];
            if (q.truth.empty()) continue;
            Scratch local_scratch(base.n);
            SearchStats stats;
            stats.trace_enabled = true;
            {
                // Baseline trace for this query (ef=512); build is outside the loop.
                search_counted(10, built, base, l2, queries.at(qi), q.roots, std::min<size_t>(512, q.count),
                    local_scratch, [&](Id u) { return bool(q.allowed[w.owner[u]]); }, stats, &query_entries(built, recursive));
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
        query_bits[3] = 0xF0F0; query_bits[17] = 0x0F0F; // OR membership in 16 selected labels
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
    std::ofstream scope(out + "/scope-" + semantics + ".csv");
    require(bool(scope), "cannot write scope.csv");
    scope << "query,dirs,enumerated_writes,word_ors,enumerate_us,bitmap_us,mismatch,source_scope,eligible,truth,roots\n" << std::setprecision(12);
    {
        const size_t words = (w.members.size() + 63) / 64;
        std::vector<std::vector<uint64_t>> dir_bits(w.members.size(), std::vector<uint64_t>(words, 0));
        for (Id c = 0; c < w.members.size(); ++c) {
            if (recursive) for (Id d : w.desc[c]) dir_bits[c][d >> 6] |= UINT64_C(1) << (d & 63);
            else dir_bits[c][c >> 6] |= UINT64_C(1) << (c & 63);
        }
        std::vector<uint64_t> acc(words);
        volatile uint64_t sink2 = 0;
        for (size_t qi = 0; qi < NQ; ++qi) {
            const std::vector<Id> dirs = qdirs[qi];
            size_t writes = 0;
            for (Id c : dirs) writes += recursive ? w.desc[c].size() : 1;
            std::vector<double> te, tb;
            size_t mismatch = 0;
            for (int rep = 0; rep < 9; ++rep) {
                auto t0 = now_ns();
                std::vector<char> mask(w.members.size(), 0);
                for (Id c : dirs) {
                    if (recursive) for (Id d : w.desc[c]) mask[d] = 1;
                    else mask[c] = 1;
                }
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
                  << median_us(te) << ',' << median_us(tb) << ',' << mismatch << ','
                  << qi * (w.scopes.size() - 1) / (NQ - 1) << ',' << workload[qi].count << ','
                  << workload[qi].truth.size() << ',';
            write_roots(scope, workload[qi].roots);
            scope << '\n';
        }
        std::cout << "SCOPE sink=" << sink2 << '\n';
    }
    // Full one-mask-per-batch cost, including allocation, initialization and
    // release. Reuse only within a batch of K distinct query vectors.
    std::ofstream amort(out + "/bitmap-amortisation-" + semantics + ".csv");
    require(bool(amort), "cannot write bitmap-amortisation.csv");
    amort << "scope_query,eligible,K,char_total_us,bitmap_total_us,bitmap_build_us,"
             "per_query_char_us,per_query_bitmap_us,correctness_verified,counts_equal,"
             "allocation_included,normal_timing\n" << std::setprecision(12);
    {
        auto graph = build_variant(base, w, l2, Knobs{});
        const auto& entry = query_entries(graph, recursive);
        size_t scope_query = 0;
        for (size_t i = 1; i < NQ; ++i)
            if (workload[i].count > workload[scope_query].count) scope_query = i;
        const auto& q = workload[scope_query];
        for (size_t K : {1u, 2u, 4u, 8u, 16u, 32u}) {
            if (K > NQ) break;
            std::vector<std::vector<Id>> expected(K), observed(K);
            ItemBitmapKernel verified;
            verified.words.assign((base.n + 63) / 64, 0);
            for (Id u = 0; u < base.n; ++u)
                if (q.allowed[w.owner[u]]) verified.words[u >> 6] |= UINT64_C(1) << (u & 63);
            for (size_t k = 0; k < K; ++k) {
                SearchStats bytes, bits;
                const auto* vector = queries.at((scope_query + k) % NQ);
                expected[k] = search_counted(10, graph, base, l2, vector, q.roots,
                    std::min<size_t>(512, q.count), scratch,
                    [&](Id u) { return bool(q.allowed[w.owner[u]]); }, bytes, &entry);
                const auto result = search_counted(10, graph, base, l2, vector, q.roots,
                    std::min<size_t>(512, q.count), scratch, [&](Id u) { return verified(u); }, bits, &entry);
                require(result == expected[k] && bytes.distances == bits.distances &&
                    bytes.expansions == bits.expansions && bytes.item_checks == bits.item_checks,
                    "bitmap reuse changed results or work");
            }
            std::vector<double> tc, tb, builds;
            for (int repeat = 0; repeat < 6; ++repeat) {
                double t_char = 0, t_bits = 0, t_build = 0;
                for (int phase = 0; phase < 2; ++phase) {
                    const bool bitmap = ((repeat + phase) % 2) != 0;
                    auto begin = Clock::now();
                    if (bitmap) {
                        {
                            ItemBitmapKernel items;
                            items.words.assign((base.n + 63) / 64, 0);
                            for (Id u = 0; u < base.n; ++u)
                                if (q.allowed[w.owner[u]]) items.words[u >> 6] |= UINT64_C(1) << (u & 63);
                            t_build = us(begin);
                            for (size_t k = 0; k < K; ++k) {
                                SearchStats unused;
                                observed[k] = search_counted<false>(10, graph, base, l2,
                                    queries.at((scope_query + k) % NQ), q.roots,
                                    std::min<size_t>(512, q.count), scratch,
                                    [&](Id u) { return items(u); }, unused, &entry);
                            }
                        }
                        t_bits = us(begin);
                    } else {
                        for (size_t k = 0; k < K; ++k) {
                            SearchStats unused;
                            observed[k] = search_counted<false>(10, graph, base, l2,
                                queries.at((scope_query + k) % NQ), q.roots,
                                std::min<size_t>(512, q.count), scratch,
                                [&](Id u) { return bool(q.allowed[w.owner[u]]); }, unused, &entry);
                        }
                        t_char = us(begin);
                    }
                    require(observed == expected, "timed bitmap batch result mismatch");
                }
                if (repeat) { tc.push_back(t_char); tb.push_back(t_bits); builds.push_back(t_build); }
            }
            amort << scope_query << ',' << q.count << ',' << K << ',' << median_us(tc) << ','
                  << median_us(tb) << ',' << median_us(builds) << ','
                  << median_us(tc) / K << ',' << median_us(tb) / K << ",1,1,1,1\n";
        }
    }

    return 0;
} catch (const std::exception& e) {
    std::cerr << "dag_composition_bench: " << e.what() << '\n';
    return 1;
}

#ifndef DAG_COMPOSITION_LIBRARY
int main(int argc, char** argv) { return bench_main(argc, argv); }
#endif
