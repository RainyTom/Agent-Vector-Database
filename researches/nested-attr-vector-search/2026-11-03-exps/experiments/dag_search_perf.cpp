// Real SIFT workload: same graph/entries/ef; normal timing never counts calls.
#define DAG_COMPOSITION_LIBRARY
#include "../../2026-11-02-exps/experiments/dag_composition_bench.cpp"
#include "search_reference.h"

int main(int argc, char** argv) try {
    require(argc == 6, "usage: dag_search_perf BASE QUERY DAG OUT recursive|direct");
    const bool recursive = std::string(argv[5]) == "recursive";
    require(recursive || std::string(argv[5]) == "direct", "invalid semantics");
    constexpr size_t nq = 120, passes = 16, rounds = 10;
    const std::string out = argv[4];
    Workload w = load_workload(argv[3]);
    Vectors base = load_vectors(argv[1], w.owner.size());
    Vectors vectors = load_vectors(argv[2], nq);
    RawL2Space l2(base.dim);
    auto graph = build_variant(base, w, l2, Knobs{});
    for (const auto& g : graph.local) if (g)
        require(g->M_ == 64 && g->ef_construction_ == 200, "native width mismatch");
    const auto& entries = query_entries(graph, recursive);
    std::vector<Query> queries(nq);
    std::vector<size_t> groups(nq);
    std::array<std::vector<size_t>, 3> subsets;
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
        if (!q.count) continue;
        subsets[0].push_back(i);
        subsets[groups[i] == 1 ? 1 : 2].push_back(i);
    }
#if DAG_VALIDATION_FAMILY == 0
    const std::vector<std::string> names = {"before", "memo_rejected", "after"};
#else
    const std::vector<std::string> names = {"nodeann_nk"};
#endif
    // One reusable workspace per implementation, not per query.
    std::vector<Scratch> spaces;
    for (size_t a = 0; a < names.size(); ++a) spaces.emplace_back(base.n);
    auto execute = [&](auto counted, size_t a, size_t i, size_t k, size_t ef, SearchStats& stats) {
        constexpr bool Count = decltype(counted)::value;
        auto& q = queries[i];
        if (!q.count) return std::vector<Id>{};
#if DAG_VALIDATION_FAMILY == 0
        auto qualifies = [&](Id u) { return bool(q.allowed[w.owner[u]]); };
        const size_t bounded = std::min(ef, q.count);
        if (a == 0) return search_reference<Count>(k, graph, base, l2, vectors.at(i), q.roots,
            bounded, spaces[a], qualifies, stats, &entries);
        if (a == 1) return search_reference<Count, true>(k, graph, base, l2, vectors.at(i), q.roots,
            bounded, spaces[a], qualifies, stats, &entries);
        return search_counted<Count>(k, graph, base, l2, vectors.at(i), q.roots,
            bounded, spaces[a], qualifies, stats, &entries);
#else
        return search_nodeann_topn<Count>(graph, w, q.allowed, vectors.at(i), k, k, ef, stats);
#endif
    };
    std::ofstream rows(out + "/queries.csv"), samples(out + "/batches.csv"), summary(out + "/summary.csv");
    require(bool(rows) && bool(samples) && bool(summary), "cannot write evidence");
    rows << "arm,k,ef,query,groups,eligible,hits,truth,returned,distances,expansions,adjacency,item_checks,rejected_distances\n";
    samples << "arm,k,ef,subset,queries,passes,sample,batch_us,checksum\n" << std::setprecision(15);
    summary << "arm,k,ef,subset,queries,us_per_query,qps,recall,distances_mean,item_checks_mean\n" << std::setprecision(15);
    const char* labels[] = {"all", "one_owner", "many_owners"};
    std::mt19937 rng(20261103);
    for (const auto& cell : {std::pair<size_t,size_t>{10,32}, {100,128}}) {
        const size_t k = cell.first, ef = cell.second;
        std::vector<std::vector<std::vector<Id>>> expected(names.size(), std::vector<std::vector<Id>>(nq));
        std::vector<std::vector<SearchStats>> work(names.size(), std::vector<SearchStats>(nq));
        std::vector<std::vector<size_t>> hits(names.size(), std::vector<size_t>(nq));
        for (size_t a = 0; a < names.size(); ++a) for (size_t i = 0; i < nq; ++i) {
            SearchStats unused;
            auto normal = execute(std::false_type{}, a, i, k, ef, unused);
            const auto counted = execute(std::true_type{}, a, i, k, ef, work[a][i]);
            require(normal == counted, "normal/count IDs differ");
            const auto& s = work[a][i];
            require(s.rejected_distances == 0, "unqualified query distance");
#if DAG_VALIDATION_FAMILY == 0
            if (a) {
                require(normal == expected[0][i], "optimization changed ordered IDs");
                const auto& ref = work[0][i];
                require(s.distances == ref.distances && s.expansions == ref.expansions &&
                        s.adjacency_entries == ref.adjacency_entries, "optimization changed graph work");
            }
#endif
            hits[a][i] = checked_hits(k, normal, queries[i], w, base, l2, vectors.at(i));
            rows << names[a] << ',' << k << ',' << ef << ',' << i << ',' << groups[i] << ','
                 << queries[i].count << ',' << hits[a][i] << ',' << std::min(k, queries[i].count) << ','
                 << normal.size() << ',' << s.distances << ',';
#if DAG_VALIDATION_FAMILY == 0
            rows << s.expansions;
#endif
            rows << ',';
#if DAG_VALIDATION_FAMILY == 0
            rows << s.adjacency_entries;
#endif
            rows << ',' << s.item_checks << ',' << s.rejected_distances << '\n';
            expected[a][i] = std::move(normal);
        }
        for (size_t group = 0; group < subsets.size(); ++group) {
            const auto& subset = subsets[group];
            if (subset.empty()) continue;
            std::vector<uint64_t> hashes(names.size(), UINT64_C(1469598103934665603));
            for (size_t a = 0; a < names.size(); ++a)
                for (size_t i : subset) hashes[a] = result_checksum(hashes[a], i, expected[a][i]);
            auto batch = [&](size_t a, size_t n) {
                uint64_t consumed = 0;
                for (size_t pass = 0; pass < n; ++pass) {
                    uint64_t h = UINT64_C(1469598103934665603);
                    for (size_t i : subset) {
                        SearchStats unused;
                        h = result_checksum(h, i, execute(std::false_type{}, a, i, k, ef, unused));
                    }
                    require(h == hashes[a], "timed pass differs from audited output");
                    consumed ^= h + pass;
                }
                return consumed;
            };
            for (size_t a = 0; a < names.size(); ++a) (void)batch(a, 1);
            std::vector<std::array<double, rounds>> timing(names.size());
            std::vector<size_t> order(names.size()); std::iota(order.begin(), order.end(), 0);
            for (size_t round = 0; round < rounds; ++round) {
                std::shuffle(order.begin(), order.end(), rng);
                for (size_t a : order) {
                    const auto begin = Clock::now();
                    const uint64_t checksum = batch(a, passes);
                    const double elapsed = us(begin);
                    timing[a][round] = elapsed;
                    samples << names[a] << ',' << k << ',' << ef << ',' << labels[group] << ','
                            << subset.size() << ',' << passes << ',' << round << ',' << elapsed << ',' << checksum << '\n';
                }
            }
            for (size_t a = 0; a < names.size(); ++a) {
                auto sorted = timing[a]; std::sort(sorted.begin(), sorted.end());
                const double time = sorted[rounds/2] / passes / subset.size();
                double recall = 0, distances = 0, checks = 0;
                for (size_t i : subset) {
                    recall += double(hits[a][i]) / std::min(k, queries[i].count);
                    distances += work[a][i].distances;
                    checks += work[a][i].item_checks;
                }
                summary << names[a] << ',' << k << ',' << ef << ',' << labels[group] << ',' << subset.size()
                        << ',' << time << ',' << 1e6/time << ',' << recall/subset.size() << ','
                        << distances/subset.size() << ',' << checks/subset.size() << '\n';
                std::cout << argv[5] << " " << names[a] << " k=" << k << " ef=" << ef
                          << " group=" << labels[group] << " us=" << time << " R=" << recall/subset.size()
                          << " checks=" << checks/subset.size() << std::endl;
            }
        }
    }
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
