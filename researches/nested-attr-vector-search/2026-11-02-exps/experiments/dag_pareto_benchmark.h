#pragma once
#include <array>
// Measurement only: each executable uses its own hnswlib and method extension.
static void write_index_parameters(const std::string& out, const Graph& global,
                                   const ComposedGraph& baseline) {
    const Graph* primary = DAG_VALIDATION_FAMILY >= 2 ? &global : nullptr;
    size_t graphs = DAG_VALIDATION_FAMILY >= 2 ? 1 : 0;
    for (const auto& local : baseline.local) if (local) {
        primary = local.get(); ++graphs;
        require(local->M_ == kHnswM && local->ef_construction_ == kHnswEfConstruction,
                "local HNSW parameters differ");
    }
    require(primary && primary->M_ == kHnswM &&
            primary->ef_construction_ == kHnswEfConstruction, "primary HNSW parameters differ");
    std::ofstream parameters(out + "/index-parameters.json");
    require(bool(parameters), "cannot write index parameters");
    parameters << "{\"hnsw_M\":" << primary->M_
               << ",\"ef_construction\":" << primary->ef_construction_
               << ",\"maxM0\":" << primary->maxM0_
               << ",\"primary_native_graphs\":" << graphs << "}\n";
}
struct ParetoPoint {
    const char* arm;
    size_t k, ef_requested, ef_effective, local_n;
    double recall = 0, pair_recall = 0, minimum_recall = 1;
    size_t hits = 0, truth = 0, returned = 0;
    SearchStats work;
    uint64_t checksum = 0;
    std::array<double, 10> batch_us{};
};
static uint64_t result_checksum(uint64_t value, size_t query, const std::vector<Id>& ids) {
    value = (value ^ (query + 1)) * UINT64_C(1099511628211);
    value = (value ^ ids.size()) * UINT64_C(1099511628211);
    for (Id id : ids) value = (value ^ (uint64_t(id) + 1)) * UINT64_C(1099511628211);
    return value;
}
static void run_pareto(const Vectors& base, const Vectors& vectors, const Workload& w,
                       RawL2Space& l2, const std::vector<Query>& workload,
                       bool recursive, const std::string& out) {
    constexpr size_t batch_passes = 16, repetitions = 10;
    // Only the two selected cells; no cross-ef curve or unused cell is measured.
    constexpr std::pair<size_t, size_t> cases[] = {{10, 32}, {100, 128}};
    const auto build_begin = Clock::now();
    Graph global(&l2, DAG_VALIDATION_FAMILY >= 2 ? base.n : 1,
                 kHnswM, kHnswEfConstruction, 100);
    if (DAG_VALIDATION_FAMILY >= 2) {
        std::vector<Id> order(base.n); std::iota(order.begin(), order.end(), 0);
        std::mt19937 rng(20261101); std::shuffle(order.begin(), order.end(), rng);
        for (Id u : order) global.addPoint(base.at(u), u);
    }
    auto baseline = DAG_VALIDATION_FAMILY < 2 ? build_variant(base, w, l2, Knobs{}) : ComposedGraph{};
    const double build_seconds = us(build_begin) / 1e6;
    QualifiedOwnerEntries entries(global, w);
    Scratch scratch(base.n);
    AcornScratch acorn_scratch(base.n);
    write_index_parameters(out, global, baseline);
    std::vector<size_t> nonempty;
    nonempty.reserve(workload.size());
    for (size_t i = 0; i < workload.size(); ++i) if (workload[i].count) nonempty.push_back(i);
    require(!nonempty.empty(), "comparison workload has no nonempty queries");
    std::vector<ParetoPoint> points;
    points.reserve(DAG_VALIDATION_FAMILY == 1 ? 480 : 9);
    for (const auto& cell : cases) {
        const size_t k = cell.first, requested = cell.second;
        // Match actual widths, not only labels, including NodeANN with local n<k.
        const size_t effective = std::max(k, requested);
#if DAG_VALIDATION_FAMILY == 0
        points.push_back({"merged", k, requested, effective, 0});
#elif DAG_VALIDATION_FAMILY == 1
        for (size_t n = 1; n <= k; ++n)
            points.push_back({"nodeann_pergraph", k, requested, effective, n});
#elif DAG_VALIDATION_FAMILY == 2
        points.push_back({"global_acorn1", k, requested, effective, 0});
#else
        points.push_back({"global_prefilter", k, requested, effective, 0});
#endif
    }
    auto execute = [&](auto counted, const ParetoPoint& point, size_t i, SearchStats& stats) {
        constexpr bool Count = decltype(counted)::value;
        const auto& q = workload[i];
        if (!q.count) return std::vector<Id>{};
#if DAG_VALIDATION_FAMILY == 0
        const auto& entry = query_entries(baseline, recursive);
        return search_counted<Count>(point.k, baseline, base, l2, vectors.at(i), q.roots,
            std::min(point.ef_effective, q.count), scratch,
            [&](Id u) { return bool(q.allowed[w.owner[u]]); }, stats, &entry);
#elif DAG_VALIDATION_FAMILY == 1
        return search_nodeann_topn<Count>(baseline, w, q.allowed, vectors.at(i),
            point.k, point.local_n, point.ef_effective, stats);
#else
        auto item_ok = [&](Id u) {
            if constexpr (Count) ++stats.item_checks;
            return bool(q.allowed[w.owner[u]]);
        };
        auto owner_ok = [&](Id attr) {
            if constexpr (Count) ++stats.owner_checks;
            return bool(q.allowed[attr]);
        };
        auto search = [&]() {
#if DAG_VALIDATION_FAMILY == 2
            if constexpr (Count) {
                struct Observer : NoopAcornObserver {
                    SearchStats& stats;
                    explicit Observer(SearchStats& s) : stats(s) {}
                    void on_adjacency(size_t n) { stats.adjacency_entries += n; }
                } observer(stats);
                return search_global_acorn1(global, entries, vectors.at(i), point.k, point.ef_effective,
                    acorn_scratch, item_ok, owner_ok, observer);
            } else {
                NoopAcornObserver observer;
                return search_global_acorn1(global, entries, vectors.at(i), point.k, point.ef_effective,
                    acorn_scratch, item_ok, owner_ok, observer);
            }
#else
            return search_global_prefilter(global, entries, vectors.at(i), point.k, point.ef_effective,
                scratch, item_ok, owner_ok);
#endif
        };
        if constexpr (Count) {
            NativeCounter counter(global, w, q.allowed);
            auto result = search();
            stats.distances = counter.distances; stats.rejected_distances = counter.rejected;
            return result;
        } else return search();
#endif
    };
    std::ofstream rows(out + "/pareto-queries.csv");
    require(bool(rows), "cannot write comparison query rows");
    rows << "arm,k,ef_requested,ef_effective,n,query,eligible,truth,hits,returned,distances,rejected_distances,"
            "item_checks,owner_checks,adjacency_entries,eligible_owner_groups,normal_verified\n";
    for (auto& point : points) {
        uint64_t checksum = UINT64_C(1469598103934665603);
        for (size_t i = 0; i < workload.size(); ++i) {
            SearchStats unused, counted;
            const auto normal = execute(std::false_type{}, point, i, unused);
            const auto measured = execute(std::true_type{}, point, i, counted);
            require(normal == measured, "normal/count comparison IDs differ");
            require(counted.rejected_distances == 0, "comparison computed unqualified distance");
            const auto& q = workload[i];
            const size_t truth_count = std::min(point.k, q.count);
            const size_t hits = checked_hits(point.k, normal, q, w, base, l2, vectors.at(i));
            size_t groups = 0;
            for (Id attr = 0; attr < q.allowed.size(); ++attr)
                groups += q.allowed[attr] && !w.members[attr].empty();
            rows << point.arm << ',' << point.k << ',' << point.ef_requested << ',' << point.ef_effective
                 << ',' << point.local_n << ',' << i << ',' << q.count << ',' << truth_count << ','
                 << hits << ',' << normal.size() << ',' << counted.distances << ','
                 << counted.rejected_distances << ',' << counted.item_checks << ',' << counted.owner_checks << ',';
            if (DAG_VALIDATION_FAMILY == 0 || DAG_VALIDATION_FAMILY == 2) rows << counted.adjacency_entries;
            rows << ',' << groups << ",1\n";
            if (!q.count) { require(normal.empty() && counted.distances == 0, "empty query did work"); continue; }
            checksum = result_checksum(checksum, i, normal);
            const double recall = double(hits) / truth_count;
            point.recall += recall; point.minimum_recall = std::min(point.minimum_recall, recall);
            point.hits += hits; point.truth += truth_count; point.returned += normal.size();
            point.work.distances += counted.distances;
            point.work.item_checks += counted.item_checks; point.work.owner_checks += counted.owner_checks;
            point.work.adjacency_entries += counted.adjacency_entries;
        }
        point.checksum = checksum;
        point.recall /= nonempty.size(); point.pair_recall = double(point.hits) / point.truth;
    }
    rows.close();
    auto batch = [&](const ParetoPoint& point, size_t passes) {
        uint64_t last = 0;
        for (size_t pass = 0; pass < passes; ++pass) {
            uint64_t checksum = UINT64_C(1469598103934665603);
            for (size_t i : nonempty) {
                SearchStats unused;
                const auto result = execute(std::false_type{}, point, i, unused);
                checksum = result_checksum(checksum, i, result);
            }
            require(checksum == point.checksum, "batch differs from audited results");
            last ^= checksum + pass;
        }
        return last;
    };
    uint64_t consumed = 0;
    for (const auto& point : points) consumed ^= batch(point, 1);
    std::vector<size_t> order(points.size()); std::iota(order.begin(), order.end(), 0);
    std::mt19937 timing_order(20261102);
    for (size_t round = 0; round < repetitions; ++round) {
        std::shuffle(order.begin(), order.end(), timing_order);
        for (size_t p : order) {
            auto begin = Clock::now();
            consumed ^= batch(points[p], batch_passes);
            points[p].batch_us[round] = us(begin);
        }
    }
    std::ofstream result(out + "/pareto-points.csv"), samples(out + "/pareto-batches.csv");
    require(bool(result) && bool(samples), "cannot write comparison points/batches");
    result << "arm,k,ef_requested,ef_effective,n,hnsw_M,ef_construction,nonempty,empty,recall_mean,pair_recall,recall_min,"
              "returned_mean,qps,median_batch_us,batch_passes,repetitions,distances_mean,item_checks_mean,"
              "owner_checks_mean,adjacency_entries_mean,batch_min_us,batch_max_us\n" << std::setprecision(15);
    samples << "arm,k,ef_requested,ef_effective,n,sample,batch_us\n" << std::setprecision(15);
    for (const auto& point : points) {
        auto sorted = point.batch_us; std::sort(sorted.begin(), sorted.end());
        const double elapsed = sorted[repetitions / 2], count = double(nonempty.size());
        for (size_t sample = 0; sample < repetitions; ++sample)
            samples << point.arm << ',' << point.k << ',' << point.ef_requested << ',' << point.ef_effective
                    << ',' << point.local_n << ',' << sample << ',' << point.batch_us[sample] << '\n';
        result << point.arm << ',' << point.k << ',' << point.ef_requested << ',' << point.ef_effective << ','
               << point.local_n << ',' << kHnswM << ',' << kHnswEfConstruction << ',' << nonempty.size() << ','
               << workload.size() - nonempty.size() << ',' << point.recall << ',' << point.pair_recall << ','
               << point.minimum_recall << ',' << point.returned / count << ','
               << 1e6 * count * batch_passes / elapsed << ',' << elapsed << ',' << batch_passes << ',' << repetitions << ','
               << point.work.distances / count << ',' << point.work.item_checks / count << ','
               << point.work.owner_checks / count << ',';
        if (DAG_VALIDATION_FAMILY == 0 || DAG_VALIDATION_FAMILY == 2) result << point.work.adjacency_entries / count;
        result << ',' << sorted.front() << ',' << sorted.back() << '\n';
    }
    // Fan-out attribution at the same protocol as the headline: the two query
    // subsets are timed as separate batches, so their QPS combine back to the total.
    std::vector<size_t> one_graph, many_graphs;
    for (size_t i : nonempty) {
        size_t groups = 0;
        for (Id attr = 0; attr < workload[i].allowed.size(); ++attr)
            groups += workload[i].allowed[attr] && !w.members[attr].empty();
        (groups == 1 ? one_graph : many_graphs).push_back(i);
    }
    std::vector<const ParetoPoint*> compared;
    for (const auto& point : points)
        if (std::string_view(point.arm) == "merged" || point.local_n == point.k)
            compared.push_back(&point);
    std::ofstream fanout(out + "/fanout-batches.csv");
    require(bool(fanout), "cannot write fan-out batches");
    fanout << "arm,k,ef_requested,ef_effective,n,group,queries,sample,batch_us\n" << std::setprecision(15);
    for (const auto* point : compared) {
        for (const auto& [label, subset] : {std::pair{"one_graph", &one_graph},
                                           std::pair{"many_graphs", &many_graphs}}) {
            if (subset->empty()) continue;
            uint64_t checksum = UINT64_C(1469598103934665603);
            std::vector<double> timings;
            for (size_t round = 0; round <= repetitions; ++round) {
                auto begin = Clock::now();
                for (size_t pass = 0; pass < batch_passes; ++pass)
                    for (size_t i : *subset) {
                        SearchStats unused;
                        checksum = result_checksum(checksum, i,
                            execute(std::false_type{}, *point, i, unused));
                    }
                timings.push_back(us(begin));
            }
            for (size_t sample = 0; sample < repetitions; ++sample)
                fanout << point->arm << ',' << point->k << ',' << point->ef_requested << ','
                       << point->ef_effective << ',' << point->local_n << ',' << label << ','
                       << subset->size() << ',' << sample << ',' << timings[sample + 1] << '\n';
        }
    }
    std::cout << "MATCHED family=" << DAG_VALIDATION_FAMILY
              << " cases=" << sizeof(cases) / sizeof(cases[0]) << " points=" << points.size()
              << " nonempty=" << nonempty.size() << " M=" << kHnswM
              << " efConstruction=" << kHnswEfConstruction << " build_s=" << build_seconds
              << " checksum=" << consumed << std::endl;
}
