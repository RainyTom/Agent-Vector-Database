#define QUERY_COST_PROFILE_TEST
#include "query_cost_profile.cpp"

int main() try {
    Workload w;
    w.children = {{1, 2}, {}, {}};
    w.topo = {0, 1, 2};
    w.desc = {{0, 1, 2}, {1}, {2}};
    w.scopes = {{1}};
    w.members.resize(3);
    w.owner.assign(128, 2); w.owner[0] = 1;
    for (Id i = 0; i < w.owner.size(); ++i) w.members[w.owner[i]].push_back(i);
    Vectors base, query;
    base.n = 128; base.dim = 16; base.x.resize(base.n * base.dim);
    query.n = 1; query.dim = 16; query.x.resize(16);
    std::mt19937 rng(42);
    std::normal_distribution<float> random;
    for (float& value : base.x) value = random(rng);
    for (float& value : query.x) value = random(rng);
    RawL2Space raw(16);
    Merged m = build(base, w, raw);
    Graph global(&raw, base.n, 16, 200, 100);
    for (Id i = 0; i < base.n; ++i) global.addPoint(base.at(i), i);
    cost::Runner runner(base, query, w, raw, m, global);
    auto original_function = global.fstdistfunc_;
    auto original_parameter = global.dist_func_param_;
    for (int arm = 0; arm < 4; ++arm) {
        auto normal = runner.run(arm, 0, 32, 1, cost::Mode::Off);
        auto counted = runner.run(arm, 0, 32, 1, cost::Mode::Count);
        auto stats = runner.probe.stats;
        auto timed = runner.run(arm, 0, 32, 1, cost::Mode::Time);
        cost::same_result(normal, counted); cost::same_result(normal, timed);
        require(stats.same_counts(runner.probe.stats), "timers changed search operations");
        auto traced = runner.run(arm, 0, 32, 1, cost::Mode::Trace);
        cost::same_result(normal, traced);
        require(stats.same_counts(runner.probe.stats), "tracing changed search operations");
        require(runner.probe.distance_trace.size() == stats.distances &&
                runner.probe.item_trace.size() == stats.item_checks &&
                runner.probe.owner_trace.size() == stats.owner_checks, "trace omitted operation");
        require(normal == std::vector<Id>{0}, "single eligible result not preserved");
        if (arm == 1) {
            require(stats.unique_distances == base.n, "native sparse filter did not visit all items");
            require(stats.rejected_distances >= base.n - 1, "unqualified native navigation distances omitted");
            require(stats.distances >= stats.unique_distances, "native upper-layer repeats incorrectly deduplicated");
        } else {
            require(stats.unique_distances == 1 && stats.rejected_distances == 0,
                    "qualified-only distance attribution incorrect");
            if (arm == 0) require(stats.distances == 1, "merged recomputed a visited distance");
        }
        if (arm == 2) require(stats.item_checks == 0 && stats.owner_checks == 3,
                              "NodeANN owner checks confused with per-item checks");
        if (arm == 3) require(stats.owner_checks == 3 && stats.rejected_distances == 0,
                              "global prefilter did not qualify entries and neighbors");
        if (arm == 0) require(stats.item_rejects > 0, "merged rejected neighbors not counted");
        require(global.fstdistfunc_ == original_function && global.dist_func_param_ == original_parameter,
                "native distance hook was not restored");
    }
    try {
        cost::NativeScope hook(global, runner.probe);
        throw std::runtime_error("unwind probe");
    } catch (const std::runtime_error&) {}
    require(global.fstdistfunc_ == original_function && global.dist_func_param_ == original_parameter,
            "native hook not restored on exception");
    std::cout << "PASS actual navigation distances, disqualified classification, owner/item separation, result stability, hook restoration\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
