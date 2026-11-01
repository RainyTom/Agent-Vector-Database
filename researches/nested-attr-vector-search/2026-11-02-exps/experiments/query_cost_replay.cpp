// Actual search traces replayed without per-operation clocks.
// Replay isolates kernels; it is NOT an additive decomposition of query latency.
#define QUERY_COST_PROFILE_TEST
#include "query_cost_profile.cpp"

namespace {
template<class Operation>
double replay(size_t size, Operation operation) {
    if (!size) return 0;
    size_t rounds = std::max(size_t(1), std::min(size_t(256), (65536 + size - 1) / size));
    auto run = [&]() {
        float checksum = 0;
        for (size_t round = 0; round < rounds; ++round)
            for (size_t i = 0; i < size; ++i) checksum += operation(i);
        volatile float sink = checksum;
        (void)sink;
    };
    run(); // warm-up
    std::vector<double> samples;
    for (size_t repeat = 0; repeat < 5; ++repeat) {
        auto start = Clock::now(); run();
        samples.push_back(us(start) / rounds);
    }
    std::sort(samples.begin(), samples.end());
    return samples[2];
}
}
int main(int argc, char** argv) try {
    require(argc == 5 || (argc == 6 && (std::string(argv[5]) == "--qualification-first" ||
            std::string(argv[5]) == "--acorn1")),
            "usage: query_cost_replay BASE QUERY DAG_DIR OUT.csv [--qualification-first|--acorn1]");
    const bool acorn = argc == 6 && std::string(argv[5]) == "--acorn1";
    Workload w = load_workload(argv[3]);
    Vectors base = load_vectors(argv[1], w.owner.size()), queries = load_vectors(argv[2], 24);
    RawL2Space raw(base.dim);
    Merged m = build(base, w, raw);
    Graph global(&raw, base.n, 16, 200, 100);
    std::vector<Id> order(base.n); std::iota(order.begin(), order.end(), 0);
    std::mt19937 rng(20261101); std::shuffle(order.begin(), order.end(), rng);
    for (Id u : order) global.addPoint(base.at(u), u);
    cost::Runner runner(base, queries, w, raw, m, global);
    std::ofstream csv(argv[4]); require(bool(csv), "cannot write replay CSV");
    csv << "arm,ef,query,eligible,distance_calls,item_checks,owner_checks,l2_replay_us,item_replay_us,owner_replay_us";
    if (acorn) csv << ",truth,returned,hits,distance_qualified,distance_rejected,unique_distance_items,"
                       "item_rejects,owner_rejects,adjacency_entries,bridge_expansions,qualified_candidates";
    csv << '\n';
    csv << std::setprecision(12);
    std::vector<size_t> efs{512, 32, 32};
    std::vector<const char*> names{"merged", "global_filtered", "nodeann"};
    if (argc == 6) {
        names[1] = "global_result_filter";
        names.push_back("global_prefilter"); efs.push_back(32);
    }
    if (acorn) { names.push_back("global_acorn1"); efs.push_back(32); }
    size_t checked = 0;
    for (size_t arm = 0; arm < names.size(); ++arm) for (size_t query = 0; query < 24; ++query) {
        runner.resolve(query);
        size_t eligible = 0;
        for (Id c = 0; c < w.members.size(); ++c) if (runner.allowed[c]) eligible += w.members[c].size();
        std::priority_queue<Pair> truth;
        if (acorn) for (Id u = 0; u < base.n; ++u) if (runner.allowed[w.owner[u]]) {
            truth.emplace(raw.dist(queries.at(query), base.at(u)), u);
            if (truth.size() > 10) truth.pop();
        }
        auto normal = runner.run(arm, query, efs[arm], eligible, cost::Mode::Off);
        auto counted = runner.run(arm, query, efs[arm], eligible, cost::Mode::Count);
        auto stats = runner.probe.stats;
        cost::same_result(normal, counted);
        require(stats.distances == stats.qualified_distances + stats.rejected_distances, "replay attribution mismatch");
        if (arm != 1) require(stats.rejected_distances == 0, "replay computed disqualified distance");
        runner.probe.distance_trace.reserve(stats.distances);
        runner.probe.item_trace.reserve(stats.item_checks);
        runner.probe.owner_trace.reserve(stats.owner_checks);
        auto traced = runner.run(arm, query, efs[arm], eligible, cost::Mode::Trace);
        cost::same_result(normal, traced);
        require(stats.same_counts(runner.probe.stats), "trace changed operation counts");
        const auto& trace = runner.probe;
        require(trace.distance_trace.size() == stats.distances && trace.item_trace.size() == stats.item_checks &&
                trace.owner_trace.size() == stats.owner_checks, "trace omitted operation");
        double l2_us = replay(trace.distance_trace.size(), [&](size_t i) {
            return raw.dist(queries.at(query), trace.distance_trace[i]);
        });
        double item_us = replay(trace.item_trace.size(), [&](size_t i) {
            return float(runner.allowed[w.owner[trace.item_trace[i]]]);
        });
        double owner_us = replay(trace.owner_trace.size(), [&](size_t i) {
            return float(runner.allowed[trace.owner_trace[i]]);
        });
        csv << names[arm] << ',' << efs[arm] << ',' << query << ',' << eligible << ',' << stats.distances << ','
            << stats.item_checks << ',' << stats.owner_checks << ',' << l2_us << ',' << item_us << ',' << owner_us;
        if (acorn) {
            size_t hits = 0;
            for (Id u : normal) {
                require(runner.allowed[w.owner[u]], "replay result violates attributes");
                if (!truth.empty() && raw.dist(queries.at(query), base.at(u)) <= truth.top().first) ++hits;
            }
            if (arm == 3 || arm == 4)
                require(normal.size() <= std::min(size_t(10), eligible), "replay returned too many results");
            else require(normal.size() == std::min(size_t(10), eligible), "replay unexpected result count");
            csv << ',' << truth.size() << ',' << normal.size() << ',' << hits
                << ',' << stats.qualified_distances << ',' << stats.rejected_distances << ',' << stats.unique_distances
                << ',' << stats.item_rejects << ',' << stats.owner_rejects;
            if (arm == 4) csv << ',' << stats.adjacency_entries << ',' << stats.bridge_expansions << ',' << stats.qualified_candidates;
            else csv << ",,,";
        }
        csv << '\n';
        ++checked;
    }
    require(bool(csv), "replay CSV write failed");
    std::cout << "PASS " << checked << " exact search traces; replay clocks amortized over batches, five repeats\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "query_cost_replay: " << e.what() << '\n';
    return 1;
}
