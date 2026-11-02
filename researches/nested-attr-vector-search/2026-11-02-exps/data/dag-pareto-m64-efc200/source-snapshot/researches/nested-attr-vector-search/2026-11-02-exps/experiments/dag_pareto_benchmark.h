#pragma once
// Measurement only: every search calls the executable's method-local fork.
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
    size_t ef, local_n;
    bool dense_visited = false;
    double recall = 0, pair_recall = 0, minimum_recall = 1;
    size_t hits = 0, truth = 0, returned = 0;
    SearchStats work;
    uint64_t checksum = 0;
    std::vector<double> batch_us;
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
    constexpr size_t k = 10, batch_passes = 16, repetitions = 10;
    const std::vector<size_t> efs{10, 16, 32, 64, 128, 256, 512, 2048, base.n};
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
    const Csr csr = DAG_VALIDATION_FAMILY == 0 ? flatten(baseline, w) : Csr{};
    Scratch scratch(base.n);
    DenseScratch dense(base.n);
    AcornScratch acorn_scratch(base.n);
    write_index_parameters(out, global, baseline);

    std::vector<size_t> nonempty;
    for (size_t i = 0; i < workload.size(); ++i) if (workload[i].count) nonempty.push_back(i);
    require(!nonempty.empty(), "Pareto workload has no nonempty queries");
    std::vector<ParetoPoint> points;
    for (size_t ef : efs) {
#if DAG_VALIDATION_FAMILY == 0
        points.push_back({"merged", ef, 0});
        points.push_back({"merged_flat_owner_dense", ef, 0, true});
#elif DAG_VALIDATION_FAMILY == 1
        for (size_t n = 1; n <= k; ++n) points.push_back({"nodeann_pergraph", ef, n});
#elif DAG_VALIDATION_FAMILY == 2
        points.push_back({"global_acorn1", ef, 0});
#else
        points.push_back({"global_prefilter", ef, 0});
#endif
    }
    auto execute = [&](auto counted, const ParetoPoint& point, size_t i, SearchStats& stats) {
        constexpr bool Count = decltype(counted)::value;
        const auto& q = workload[i];
        if (!q.count) return std::vector<Id>{};
#if DAG_VALIDATION_FAMILY == 0
        const auto& entry = query_entries(baseline, recursive);
        if (!point.dense_visited)
            return search_counted<Count>(baseline, base, l2, vectors.at(i), q.roots,
                std::min(point.ef, q.count), scratch,
                [&](Id u) { return bool(q.allowed[w.owner[u]]); }, stats, &entry);
        CsrEdges edges{&csr, &w};
        return search_dense<Count>(edges, baseline, base, l2, vectors.at(i), q.roots,
            std::min(point.ef, q.count), dense,
            [&](Id, Id owner) { return bool(q.allowed[owner]); }, stats, &entry);
#elif DAG_VALIDATION_FAMILY == 1
        return search_nodeann_topn<Count>(baseline, w, q.allowed, vectors.at(i),
                                         k, point.local_n, point.ef, stats);
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
                return search_global_acorn1(global, entries, vectors.at(i), k, point.ef,
                    acorn_scratch, item_ok, owner_ok, observer);
            } else {
                NoopAcornObserver observer;
                return search_global_acorn1(global, entries, vectors.at(i), k, point.ef,
                    acorn_scratch, item_ok, owner_ok, observer);
            }
#else
            return search_global_prefilter(global, entries, vectors.at(i), k, point.ef,
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
    require(bool(rows), "cannot write Pareto query rows");
    rows << "arm,ef,n,query,eligible,truth,hits,returned,distances,rejected_distances,"
            "item_checks,owner_checks,adjacency_entries,eligible_owner_groups,normal_verified\n";
    for (auto& point : points) {
        uint64_t checksum = UINT64_C(1469598103934665603);
        for (size_t i = 0; i < workload.size(); ++i) {
            SearchStats unused, counted;
            const auto normal = execute(std::false_type{}, point, i, unused);
            const auto measured = execute(std::true_type{}, point, i, counted);
            require(normal == measured, "normal/count Pareto IDs differ");
            require(counted.rejected_distances == 0, "Pareto query computed unqualified distance");
            const auto& q = workload[i];
            const size_t hits = checked_hits(normal, q, w, base, l2, vectors.at(i));
            size_t groups = 0;
            for (Id attr = 0; attr < q.allowed.size(); ++attr)
                groups += q.allowed[attr] && !w.members[attr].empty();
            rows << point.arm << ',' << point.ef << ',' << point.local_n << ',' << i << ','
                 << q.count << ',' << q.truth.size() << ',' << hits << ',' << normal.size() << ','
                 << counted.distances << ',' << counted.rejected_distances << ','
                 << counted.item_checks << ',' << counted.owner_checks << ',';
            if (DAG_VALIDATION_FAMILY == 0 || DAG_VALIDATION_FAMILY == 2)
                rows << counted.adjacency_entries;
            rows << ',' << groups << ",1\n";
            if (!q.count) { require(normal.empty() && counted.distances == 0, "empty query did work"); continue; }
            checksum = result_checksum(checksum, i, normal);
            const double recall = double(hits) / q.truth.size();
            point.recall += recall; point.minimum_recall = std::min(point.minimum_recall, recall);
            point.hits += hits; point.truth += q.truth.size(); point.returned += normal.size();
            point.work.distances += counted.distances;
            point.work.item_checks += counted.item_checks; point.work.owner_checks += counted.owner_checks;
            point.work.adjacency_entries += counted.adjacency_entries;
        }
        point.checksum = checksum;
        point.recall /= nonempty.size(); point.pair_recall = double(point.hits) / point.truth;
    }
    rows.close();

    // One untimed batch per point; then interleave configurations in seeded order.
    // Timed batches do no predicate/distance instrumentation or per-query clocks.
    auto batch = [&](const ParetoPoint& point, size_t passes) {
        uint64_t last = 0;
        for (size_t pass = 0; pass < passes; ++pass) {
            uint64_t checksum = UINT64_C(1469598103934665603);
            for (size_t i : nonempty) {
                SearchStats unused;
                const auto result = execute(std::false_type{}, point, i, unused);
                checksum = result_checksum(checksum, i, result);
            }
            require(checksum == point.checksum, "batch results differ from audited normal results");
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
            points[p].batch_us.push_back(us(begin));
        }
    }
    std::ofstream result(out + "/pareto-points.csv");
    require(bool(result), "cannot write Pareto points");
    result << "arm,ef,n,k,hnsw_M,ef_construction,nonempty,empty,recall_mean,pair_recall,recall_min,"
              "returned_mean,qps,median_batch_us,batch_passes,repetitions,distances_mean,item_checks_mean,"
              "owner_checks_mean,adjacency_entries_mean,batch_min_us,batch_max_us\n" << std::setprecision(15);
    std::ofstream samples(out + "/pareto-batches.csv");
    require(bool(samples), "cannot write batch samples");
    samples << "arm,ef,n,sample,batch_us\n" << std::setprecision(15);
    for (const auto& point : points) {
        const double elapsed = median_us(point.batch_us), count = double(nonempty.size());
        for (size_t sample = 0; sample < point.batch_us.size(); ++sample)
            samples << point.arm << ',' << point.ef << ',' << point.local_n << ',' << sample << ',' << point.batch_us[sample] << '\n';
        result << point.arm << ',' << point.ef << ',' << point.local_n << ',' << k << ','
               << kHnswM << ',' << kHnswEfConstruction << ',' << nonempty.size() << ','
               << workload.size() - nonempty.size() << ',' << point.recall << ',' << point.pair_recall << ','
               << point.minimum_recall << ',' << point.returned / count << ','
               << 1e6 * count * batch_passes / elapsed << ',' << elapsed << ',' << batch_passes << ',' << repetitions << ','
               << point.work.distances / count << ',' << point.work.item_checks / count << ','
               << point.work.owner_checks / count << ',';
        if (DAG_VALIDATION_FAMILY == 0 || DAG_VALIDATION_FAMILY == 2)
            result << point.work.adjacency_entries / count;
        result << ',' << *std::min_element(point.batch_us.begin(), point.batch_us.end()) << ','
               << *std::max_element(point.batch_us.begin(), point.batch_us.end()) << '\n';
    }
    std::cout << "PARETO family=" << DAG_VALIDATION_FAMILY << " points=" << points.size()
              << " nonempty=" << nonempty.size() << " M=" << kHnswM
              << " efConstruction=" << kHnswEfConstruction << " build_s=" << build_seconds
              << " checksum=" << consumed << std::endl;
}
