// SIMD retained. Exact nonempty-owner cohorts, not per-query timing estimates.
#define DAG_COMPOSITION_LIBRARY
#include "../../2026-11-02-exps/experiments/dag_composition_bench.cpp"
#include <map>

int main(int argc, char** argv) try {
    require(argc == 6, "usage: dag_subgraph_efficiency BASE QUERY DAG OUT recursive|direct");
    const bool recursive = std::string(argv[5]) == "recursive";
    require(recursive || std::string(argv[5]) == "direct", "invalid semantics");
    constexpr size_t nq = 120, passes = 16, rounds = 10;
    const std::string out = argv[4];
    Workload w = load_workload(argv[3]);
    Vectors base = load_vectors(argv[1], w.owner.size());
    Vectors vectors = load_vectors(argv[2], nq);
    RawL2Space l2(base.dim);
    const auto fn = l2.get_dist_func();
    const char* kernel = "unknown";
#if defined(USE_SSE)
    if (fn == hnswlib::L2SqrSIMD16ExtSSE) kernel = "SSE";
#endif
#if defined(USE_AVX)
    if (fn == hnswlib::L2SqrSIMD16ExtAVX) kernel = "AVX";
#endif
#if defined(USE_AVX512)
    if (fn == hnswlib::L2SqrSIMD16ExtAVX512) kernel = "AVX512";
#endif
    require(std::string(kernel) != "unknown", "SIMD distance kernel not selected");
    std::cout << "kernel=" << kernel << " dim=" << base.dim << std::endl;
    auto graph = build_variant(base, w, l2, Knobs{});
    for (const auto& g : graph.local) if (g)
        require(g->M_ == 64 && g->ef_construction_ == 200, "native width mismatch");
    const auto& entries = query_entries(graph, recursive);
    std::vector<Query> queries(nq);
    std::vector<size_t> groups(nq);
    std::map<size_t, std::vector<size_t>> subsets;
    for (size_t i = 0; i < nq; ++i) {
        auto& q = queries[i];
        q = resolve_scope(w, w.scopes[i * (w.scopes.size() - 1) / (nq - 1)], recursive);
        for (Id c = 0; c < q.allowed.size(); ++c)
            groups[i] += q.allowed[c] && !w.members[c].empty();
        std::priority_queue<Pair> truth;
        for (Id u = 0; u < base.n; ++u) if (q.allowed[w.owner[u]]) {
            ++q.count;
            truth.emplace(l2.dist(vectors.at(i), base.at(u)), u);
            if (truth.size() > 100) truth.pop();
        }
        while (!truth.empty()) { q.truth.push_back(truth.top().second); truth.pop(); }
        require(bool(q.count) == bool(groups[i]), "nonempty graph count mismatch");
        if (q.count) subsets[groups[i]].push_back(i);
    }
#if DAG_VALIDATION_FAMILY == 0
    const char* arm = "dag";
#else
    const char* arm = "nodeann_nk";
#endif
    Scratch scratch(base.n);
    auto execute = [&](auto counted, size_t i, size_t k, size_t ef, SearchStats& stats) {
        constexpr bool Count = decltype(counted)::value;
        const auto& q = queries[i];
        if (!q.count) return std::vector<Id>{};
#if DAG_VALIDATION_FAMILY == 0
        auto qualifies = [&](Id u) { return bool(q.allowed[w.owner[u]]); };
        return search_counted<Count>(k, graph, base, l2, vectors.at(i), q.roots,
            std::min(ef, q.count), scratch, qualifies, stats, &entries);
#else
        return search_nodeann_topn<Count>(graph, w, q.allowed, vectors.at(i), k, k, ef, stats);
#endif
    };
    std::ofstream rows(out + "/queries.csv"), batches(out + "/batches.csv"), summary(out + "/summary.csv");
    require(bool(rows) && bool(batches) && bool(summary), "cannot write evidence");
    rows << "arm,k,ef,query,groups,eligible,hits,truth,returned,distances,item_checks,rejected_distances\n";
    batches << "arm,k,ef,groups,queries,passes,sample,batch_us,checksum\n" << std::setprecision(15);
    summary << "arm,k,ef,groups,queries,us_per_query,qps,recall,distances_mean,eligible_mean\n" << std::setprecision(15);
    std::mt19937 rng(20261103);
    for (const auto& cell : {std::pair<size_t,size_t>{10,32}, {100,128}}) {
        const size_t k = cell.first, ef = cell.second;
        std::vector<std::vector<Id>> expected(nq);
        std::vector<SearchStats> work(nq);
        std::vector<size_t> hits(nq);
        for (size_t i = 0; i < nq; ++i) {
            SearchStats unused;
            expected[i] = execute(std::false_type{}, i, k, ef, unused);
            require(expected[i] == execute(std::true_type{}, i, k, ef, work[i]), "normal/count IDs differ");
            require(work[i].rejected_distances == 0, "unqualified query distance");
            hits[i] = checked_hits(k, expected[i], queries[i], w, base, l2, vectors.at(i));
            rows << arm << ',' << k << ',' << ef << ',' << i << ',' << groups[i] << ',' << queries[i].count
                 << ',' << hits[i] << ',' << std::min(k, queries[i].count) << ',' << expected[i].size()
                 << ',' << work[i].distances << ',' << work[i].item_checks << ',' << work[i].rejected_distances << '\n';
        }
        std::vector<size_t> order;
        for (const auto& entry : subsets) order.push_back(entry.first);
        std::shuffle(order.begin(), order.end(), rng);
        for (size_t g : order) {
            const auto& subset = subsets.at(g);
            uint64_t expected_hash = UINT64_C(1469598103934665603);
            for (size_t i : subset) expected_hash = result_checksum(expected_hash, i, expected[i]);
            auto batch = [&](size_t n) {
                uint64_t consumed = 0;
                for (size_t pass = 0; pass < n; ++pass) {
                    uint64_t hash = UINT64_C(1469598103934665603);
                    for (size_t i : subset) {
                        SearchStats unused;
                        hash = result_checksum(hash, i, execute(std::false_type{}, i, k, ef, unused));
                    }
                    require(hash == expected_hash, "timed pass differs from audited output");
                    consumed ^= hash + pass;
                }
                return consumed;
            };
            (void)batch(1);
            std::array<double, rounds> timing;
            for (size_t r = 0; r < rounds; ++r) {
                const auto begin = Clock::now();
                const auto checksum = batch(passes);
                timing[r] = us(begin);
                batches << arm << ',' << k << ',' << ef << ',' << g << ',' << subset.size() << ','
                        << passes << ',' << r << ',' << timing[r] << ',' << checksum << '\n';
            }
            std::sort(timing.begin(), timing.end());
            const double time = timing[rounds/2] / passes / subset.size();
            double recall = 0, distances = 0, eligible = 0;
            for (size_t i : subset) {
                recall += double(hits[i]) / std::min(k, queries[i].count);
                distances += work[i].distances;
                eligible += queries[i].count;
            }
            summary << arm << ',' << k << ',' << ef << ',' << g << ',' << subset.size() << ',' << time
                    << ',' << 1e6/time << ',' << recall/subset.size() << ',' << distances/subset.size()
                    << ',' << eligible/subset.size() << '\n';
            std::cout << argv[5] << ' ' << arm << " k=" << k << " ef=" << ef << " G=" << g
                      << " nq=" << subset.size() << " us=" << time << " R=" << recall/subset.size() << std::endl;
        }
    }
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
