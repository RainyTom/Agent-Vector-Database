// Native result-filter reference and explicit qualification-first global search on the same index.
#define main merge_benchmark_main
#include "../../2026-11-01-exps/experiments/merge_arms.cpp"
#undef main
#include "global_acorn1_hnsw.h"
#include <atomic>
#include <array>

namespace cost {
static double elapsed_ns(Clock::time_point start) {
    return std::chrono::duration<double, std::nano>(Clock::now() - start).count();
}
struct Stats {
    size_t distances = 0, qualified_distances = 0, rejected_distances = 0, unique_distances = 0;
    size_t item_checks = 0, item_rejects = 0, owner_checks = 0, owner_rejects = 0;
    size_t adjacency_entries = 0, bridge_expansions = 0, qualified_candidates = 0;
    double distance_ns = 0, item_ns = 0, owner_ns = 0, scope_us = 0;
    bool same_counts(const Stats& other) const {
        return distances == other.distances && qualified_distances == other.qualified_distances &&
            rejected_distances == other.rejected_distances && unique_distances == other.unique_distances &&
            item_checks == other.item_checks && item_rejects == other.item_rejects &&
            owner_checks == other.owner_checks && owner_rejects == other.owner_rejects &&
            adjacency_entries == other.adjacency_entries && bridge_expansions == other.bridge_expansions &&
            qualified_candidates == other.qualified_candidates;
    }
};
enum class Mode { Off, Count, Time, Trace };
struct Probe {
    RawL2Space& raw;
    const Vectors& base;
    const Workload& workload;
    const std::vector<char>* allowed = nullptr;
    Mode mode = Mode::Off;
    Stats stats;
    std::vector<uint32_t> seen;
    uint32_t epoch = 0;
    Graph* native = nullptr;
    const char* native_origin = nullptr;
    ptrdiff_t native_stride = 1;
    std::vector<const float*> distance_trace;
    std::vector<Id> item_trace, owner_trace;
    Probe(RawL2Space& distance, const Vectors& vectors, const Workload& w)
        : raw(distance), base(vectors), workload(w), seen(vectors.n) {}
    void begin(Mode m, const std::vector<char>& mask) {
        mode = m; allowed = &mask; stats = {}; ++epoch;
        if (m == Mode::Trace) { distance_trace.clear(); item_trace.clear(); owner_trace.clear(); }
    }
    void on_adjacency(size_t count) { stats.adjacency_entries += count; }
    void on_bridge() { ++stats.bridge_expansions; }
    void on_candidate() { ++stats.qualified_candidates; }
    float evaluate(const void* query, const void* vector, Id item) {
        // Attribution lookup is diagnostic bookkeeping, NOT an algorithmic predicate call.
        ++stats.distances;
        if (mode == Mode::Trace) distance_trace.push_back(static_cast<const float*>(vector));
        if ((*allowed)[workload.owner[item]]) ++stats.qualified_distances;
        else ++stats.rejected_distances;
        if (seen[item] != epoch) { seen[item] = epoch; ++stats.unique_distances; }
        float result;
        if (mode == Mode::Time) {
            auto begin = Clock::now();
            std::atomic_signal_fence(std::memory_order_seq_cst);
            result = raw.dist(static_cast<const float*>(query), static_cast<const float*>(vector));
            std::atomic_signal_fence(std::memory_order_seq_cst);
            stats.distance_ns += elapsed_ns(begin);
        } else result = raw.dist(static_cast<const float*>(query), static_cast<const float*>(vector));
        return result;
    }
    float dist(const float* query, const float* vector) {
        Id item = Id((vector - base.x.data()) / ptrdiff_t(base.dim));
        return evaluate(query, vector, item);
    }
    static float native_distance(const void* query, const void* vector, const void* context) {
        auto& self = *static_cast<Probe*>(const_cast<void*>(context));
        ptrdiff_t delta = static_cast<const char*>(vector) - self.native_origin;
        require(delta >= 0 && delta % self.native_stride == 0, "invalid native distance address");
        size_t internal = size_t(delta / self.native_stride);
        require(internal < self.native->cur_element_count, "invalid native distance id");
        return self.evaluate(query, vector, Id(self.native->getExternalLabel(internal)));
    }
    bool item_allowed(Id item) {
        ++stats.item_checks;
        if (mode == Mode::Trace) item_trace.push_back(item);
        bool result;
        if (mode == Mode::Time) {
            auto begin = Clock::now();
            std::atomic_signal_fence(std::memory_order_seq_cst);
            result = bool((*allowed)[workload.owner[item]]);
            std::atomic_signal_fence(std::memory_order_seq_cst);
            stats.item_ns += elapsed_ns(begin);
        } else result = bool((*allowed)[workload.owner[item]]);
        stats.item_rejects += !result;
        return result;
    }
    bool owner_allowed(Id owner) {
        ++stats.owner_checks;
        if (mode == Mode::Trace) owner_trace.push_back(owner);
        bool result;
        if (mode == Mode::Time) {
            auto begin = Clock::now();
            std::atomic_signal_fence(std::memory_order_seq_cst);
            result = bool((*allowed)[owner]);
            std::atomic_signal_fence(std::memory_order_seq_cst);
            stats.owner_ns += elapsed_ns(begin);
        } else result = bool((*allowed)[owner]);
        stats.owner_rejects += !result;
        return result;
    }
};
// Temporarily wrap a native graph's existing distance pointer; restore on all exits.
// No dependency edits, no reimplemented HNSW traversal, no changed predicate order.
struct NativeScope {
    Graph& graph;
    Probe& probe;
    hnswlib::DISTFUNC<float> saved_function;
    void* saved_parameter;
    explicit NativeScope(Graph& g, Probe& p) : graph(g), probe(p),
        saved_function(g.fstdistfunc_), saved_parameter(g.dist_func_param_) {
        require(g.cur_element_count > 0, "empty native graph");
        probe.native = &g;
        probe.native_origin = g.getDataByInternalId(0);
        probe.native_stride = g.cur_element_count > 1 ?
            g.getDataByInternalId(1) - g.getDataByInternalId(0) : 1;
        graph.fstdistfunc_ = Probe::native_distance;
        graph.dist_func_param_ = &probe;
    }
    ~NativeScope() {
        graph.fstdistfunc_ = saved_function; graph.dist_func_param_ = saved_parameter;
        probe.native = nullptr;
    }
};
struct ItemFilter : hnswlib::BaseFilterFunctor {
    Probe& probe;
    explicit ItemFilter(Probe& p) : probe(p) {}
    bool operator()(hnswlib::labeltype id) override { return probe.item_allowed(Id(id)); }
};
static double calibrate_timer() {
    // Empty bracket, same compiler fences, median of five 20000-sample means.
    std::vector<double> means;
    for (size_t batch = 0; batch < 5; ++batch) {
        double total = 0;
        for (size_t i = 0; i < 20000; ++i) {
            auto begin = Clock::now();
            std::atomic_signal_fence(std::memory_order_seq_cst);
            std::atomic_signal_fence(std::memory_order_seq_cst);
            total += elapsed_ns(begin);
        }
        means.push_back(total / 20000);
    }
    std::sort(means.begin(), means.end());
    return means[2];
}
struct Runner {
    const Vectors& base;
    const Vectors& queries;
    const Workload& w;
    RawL2Space& raw;
    Merged& merged;
    Graph& global;
    QualifiedOwnerEntries qualified_entries;
    Probe probe;
    Scratch scratch;
    AcornScratch acorn_scratch;
    std::vector<char> allowed, ancestor;
    std::vector<Id> roots;
    Runner(const Vectors& b, const Vectors& q, const Workload& workload, RawL2Space& l2,
           Merged& m, Graph& g) : base(b), queries(q), w(workload), raw(l2), merged(m), global(g),
           qualified_entries(g, workload), probe(l2, b, workload), scratch(b.n), acorn_scratch(b.n),
           allowed(w.members.size()), ancestor(w.members.size()) {
        roots.reserve(w.members.size());
    }
    void resolve(size_t query) {
        std::fill(allowed.begin(), allowed.end(), 0);
        std::fill(ancestor.begin(), ancestor.end(), 0);
        roots.clear();
        for (Id c : w.scopes[query * (w.scopes.size() - 1) / 23])
            for (Id d : w.desc[c]) allowed[d] = 1;
        for (Id c : w.topo) if (allowed[c]) {
            if (!ancestor[c]) roots.push_back(c);
            for (Id d : w.children[c]) ancestor[d] = 1;
        }
    }
    std::vector<Id> run(int arm, size_t query, size_t ef, size_t count, Mode mode) {
        probe.begin(mode, allowed);
        if (mode == Mode::Off) resolve(query);
        else { auto begin = Clock::now(); resolve(query); probe.stats.scope_us = us(begin); }
        if (!count) return {};
        if (arm == 4) {
            if (mode == Mode::Off) {
                NoopAcornObserver observer;
                return search_global_acorn1(global, qualified_entries, queries.at(query), 10, ef, acorn_scratch,
                    [&](Id u) { return bool(allowed[w.owner[u]]); },
                    [&](Id c) { return bool(allowed[c]); }, observer);
            }
            NativeScope hook(global, probe);
            return search_global_acorn1(global, qualified_entries, queries.at(query), 10, ef, acorn_scratch,
                [&](Id u) { return probe.item_allowed(u); },
                [&](Id c) { return probe.owner_allowed(c); }, probe);
        }
        if (arm == 3) {
            if (mode == Mode::Off)
                return search_global_prefilter(global, qualified_entries, queries.at(query), 10, ef, scratch,
                    [&](Id u) { return bool(allowed[w.owner[u]]); },
                    [&](Id c) { return bool(allowed[c]); });
            NativeScope hook(global, probe);
            return search_global_prefilter(global, qualified_entries, queries.at(query), 10, ef, scratch,
                [&](Id u) { return probe.item_allowed(u); },
                [&](Id c) { return probe.owner_allowed(c); });
        }
        if (arm == 0) {
            size_t calls = 0;
            if (mode == Mode::Off)
                return search(merged, w, base, raw, queries.at(query), allowed, roots,
                              std::min(ef, count), scratch, calls);
            auto result = search_impl(merged, base, probe, queries.at(query), roots,
                std::min(ef, count), scratch, calls, [&](Id u) { return probe.item_allowed(u); });
            require(calls == probe.stats.distances, "merged counter mismatch");
            return result;
        }
        if (arm == 1) {
            global.setEf(ef);
            if (mode == Mode::Off) {
                Filter filter(w, allowed);
                return labels(global.searchKnn(queries.at(query), 10, &filter));
            }
            NativeScope hook(global, probe);
            ItemFilter filter(probe);
            return labels(global.searchKnn(queries.at(query), 10, &filter));
        }
        std::priority_queue<Pair> top;
        for (Id c = 0; c < allowed.size(); ++c) {
            bool accepted = mode == Mode::Off ? bool(allowed[c]) : probe.owner_allowed(c);
            if (!accepted || !merged.local[c]) continue;
            auto& g = *merged.local[c];
            g.setEf(ef);
            std::priority_queue<std::pair<float, hnswlib::labeltype>> local;
            if (mode == Mode::Off) local = g.searchKnn(queries.at(query), 10);
            else { NativeScope hook(g, probe); local = g.searchKnn(queries.at(query), 10); }
            while (!local.empty()) {
                top.emplace(local.top().first, Id(local.top().second)); local.pop();
                if (top.size() > 10) top.pop();
            }
        }
        std::vector<Id> result;
        while (!top.empty()) { result.push_back(top.top().second); top.pop(); }
        return result;
    }
};
static void same_result(std::vector<Id> a, std::vector<Id> b) {
    std::sort(a.begin(), a.end()); std::sort(b.begin(), b.end());
    require(a == b, "instrumentation changed result IDs");
}
} // namespace cost

#ifndef QUERY_COST_PROFILE_TEST
int main(int argc, char** argv) try {
    require(argc == 6 || (argc == 7 && (std::string(argv[6]) == "--qualification-first" ||
            std::string(argv[6]) == "--acorn1")),
            "usage: query_cost_profile BASE QUERY DAG_DIR OUT.csv OUT.json [--qualification-first|--acorn1]");
    const bool acorn = argc == 7 && std::string(argv[6]) == "--acorn1";
    Workload w = load_workload(argv[3]);
    Vectors base = load_vectors(argv[1], w.owner.size()), queries = load_vectors(argv[2], 24);
    require(base.dim == 128 && queries.dim == base.dim, "SIFT128 required");
    RawL2Space l2(base.dim);
    Merged merged = build(base, w, l2);
    Graph global(&l2, base.n, 16, 200, 100);
    std::vector<Id> order(base.n); std::iota(order.begin(), order.end(), 0);
    std::mt19937 rng(20261101); std::shuffle(order.begin(), order.end(), rng);
    for (Id u : order) global.addPoint(base.at(u), u);
    cost::Runner runner(base, queries, w, l2, merged, global);
    double timer_ns = cost::calibrate_timer();
    std::ofstream csv(argv[4]); require(bool(csv), "cannot write CSV");
    csv << "arm,ef,query,eligible,truth,hits,returned,normal_us,count_pass_us,profile_pass_us,scope_us,"
           "distance_calls,distance_qualified,distance_rejected,unique_distance_items,item_checks,item_rejects,"
           "owner_checks,owner_rejects,distance_raw_us,item_raw_us,owner_raw_us,distance_est_us,item_est_us,owner_est_us";
    if (acorn) csv << ",adjacency_entries,bridge_expansions,qualified_candidates";
    csv << '\n';
    csv << std::setprecision(12);
    size_t checked = 0, empty_queries = 0;
    std::vector<size_t> efs{512, 32, 32};
    std::vector<const char*> names{"merged", "global_filtered", "nodeann"};
    if (argc == 7) {
        names[1] = "global_result_filter";
        names.push_back("global_prefilter"); efs.push_back(32);
    }
    if (acorn) { names.push_back("global_acorn1"); efs.push_back(32); }
    struct Prepared { size_t eligible = 0, truth_size = 0; float kth_distance = 0; };
    struct Observation {
        std::vector<Id> result;
        double normal_us = 0, count_us = 0;
        cost::Stats counts;
    };
    std::array<Prepared, 24> prepared{};
    std::vector<std::array<Observation, 24>> observations(names.size());
    for (size_t query = 0; query < 24; ++query) {
        runner.resolve(query);
        auto& p = prepared[query];
        std::priority_queue<Pair> truth;
        for (Id u = 0; u < base.n; ++u) if (runner.allowed[w.owner[u]]) {
            ++p.eligible; truth.emplace(l2.dist(queries.at(query), base.at(u)), u);
            if (truth.size() > 10) truth.pop();
        }
        p.truth_size = truth.size();
        if (truth.empty()) ++empty_queries;
        else p.kth_distance = truth.top().first;
    }
    // Finish all ordinary timings before installing any per-operation probes.
    for (size_t arm = 0; arm < names.size(); ++arm) for (size_t query = 0; query < 24; ++query) {
        auto& observation = observations[arm][query];
        std::vector<double> times;
        for (int repeat = 0; repeat < 4; ++repeat) {
            auto begin = Clock::now();
            observation.result = runner.run(arm, query, efs[arm], prepared[query].eligible, cost::Mode::Off);
            if (repeat) times.push_back(us(begin));
        }
        std::sort(times.begin(), times.end());
        observation.normal_us = times[1];
    }
    for (size_t arm = 0; arm < names.size(); ++arm) for (size_t query = 0; query < 24; ++query) {
        auto& observation = observations[arm][query];
        auto begin = Clock::now();
        auto result = runner.run(arm, query, efs[arm], prepared[query].eligible, cost::Mode::Count);
        observation.count_us = us(begin);
        observation.counts = runner.probe.stats;
        cost::same_result(observation.result, result);
    }
    for (size_t arm = 0; arm < names.size(); ++arm) for (size_t query = 0; query < 24; ++query) {
        const auto& observation = observations[arm][query];
        const auto& p = prepared[query];
        std::vector<std::pair<double, cost::Stats>> samples;
        for (int repeat = 0; repeat < 3; ++repeat) {
            auto begin = Clock::now();
            auto profiled = runner.run(arm, query, efs[arm], p.eligible, cost::Mode::Time);
            double total = us(begin);
            cost::same_result(observation.result, profiled);
            require(observation.counts.same_counts(runner.probe.stats), "profile changed operation counts");
            samples.emplace_back(total, runner.probe.stats);
        }
        std::sort(samples.begin(), samples.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        const auto& s = samples[1].second;
        require(s.distances == s.qualified_distances + s.rejected_distances, "distance attribution mismatch");
        if (arm != 1) require(s.rejected_distances == 0, "pre-filtered arm computed disqualified distance");
        if (arm == 0) require(s.distances == s.unique_distances, "merged repeated a distance");
        size_t hits = 0;
        for (Id u : observation.result) {
            require(runner.allowed[w.owner[u]], "result violates attributes");
            if (l2.dist(queries.at(query), base.at(u)) <= p.kth_distance) ++hits;
        }
        if (arm == 3 || arm == 4) require(observation.result.size() <= std::min(size_t(10), p.eligible),
                              "filtered graph returned too many results");
        else require(observation.result.size() == std::min(size_t(10), p.eligible), "unexpected result count");
        csv << names[arm] << ',' << efs[arm] << ',' << query << ',' << p.eligible << ',' << p.truth_size << ',' << hits
            << ',' << observation.result.size() << ',' << observation.normal_us << ',' << observation.count_us
            << ',' << samples[1].first << ',' << s.scope_us
            << ',' << s.distances << ',' << s.qualified_distances << ',' << s.rejected_distances << ',' << s.unique_distances
            << ',' << s.item_checks << ',' << s.item_rejects << ',' << s.owner_checks << ',' << s.owner_rejects
            << ',' << s.distance_ns / 1000 << ',' << s.item_ns / 1000 << ',' << s.owner_ns / 1000
            << ',' << (s.distance_ns - s.distances * timer_ns) / 1000
            << ',' << (s.item_ns - s.item_checks * timer_ns) / 1000
            << ',' << (s.owner_ns - s.owner_checks * timer_ns) / 1000;
        if (acorn) {
            if (arm == 4) csv << ',' << s.adjacency_entries << ',' << s.bridge_expansions << ',' << s.qualified_candidates;
            else csv << ",,,";
        }
        csv << '\n';
        ++checked;
    }
    require(bool(csv), "CSV write failed");
    if (argc == 7) {
        std::ofstream sweep(std::string(argv[4]) + (acorn ? ".acorn1-sweep.csv" : ".sweep.csv"));
        require(bool(sweep), "cannot write filtered graph sweep");
        sweep << "ef,query,eligible,truth,returned,hits,normal_us,distance_calls,distance_rejected,item_checks,owner_checks";
        if (acorn) sweep << ",distance_qualified,unique_distance_items,item_rejects,owner_rejects,"
                            "adjacency_entries,bridge_expansions,qualified_candidates";
        sweep << '\n';
        sweep << std::setprecision(12);
        for (size_t ef : {size_t(32), size_t(128), size_t(512), size_t(2048), base.n})
            for (size_t query = 0; query < 24; ++query) {
                const auto& p = prepared[query];
                std::vector<Id> result;
                std::vector<double> times;
                for (int repeat = 0; repeat < 4; ++repeat) {
                    auto begin = Clock::now();
                    result = runner.run(acorn ? 4 : 3, query, ef, p.eligible, cost::Mode::Off);
                    if (repeat) times.push_back(us(begin));
                }
                std::sort(times.begin(), times.end());
                auto counted = runner.run(acorn ? 4 : 3, query, ef, p.eligible, cost::Mode::Count);
                cost::same_result(result, counted);
                const auto& s = runner.probe.stats;
                require(s.rejected_distances == 0, "sweep computed unqualified distance");
                require(s.distances == s.qualified_distances + s.rejected_distances, "sweep attribution mismatch");
                require(result.size() <= std::min(size_t(10), p.eligible), "sweep returned too many results");
                size_t hits = 0;
                for (Id u : result) {
                    require(runner.allowed[w.owner[u]], "sweep returned disqualified item");
                    if (l2.dist(queries.at(query), base.at(u)) <= p.kth_distance) ++hits;
                }
                sweep << ef << ',' << query << ',' << p.eligible << ',' << p.truth_size << ',' << result.size()
                    << ',' << hits << ',' << times[1] << ',' << s.distances << ',' << s.rejected_distances
                    << ',' << s.item_checks << ',' << s.owner_checks;
                if (acorn) sweep << ',' << s.qualified_distances << ',' << s.unique_distances
                    << ',' << s.item_rejects << ',' << s.owner_rejects << ',' << s.adjacency_entries
                    << ',' << s.bridge_expansions << ',' << s.qualified_candidates;
                sweep << '\n';
            }
        require(bool(sweep), "filtered graph sweep write failed");
    }
    std::ofstream metadata(argv[5]); require(bool(metadata), "cannot write metadata");
    metadata << std::setprecision(12) << "{\n  \"timer_pair_ns\": " << timer_ns
        << ",\n  \"verified_query_arm_rows\": " << checked << ",\n  \"empty_queries\": " << empty_queries
        << ",\n  \"normal_warmups\": 1,\n  \"normal_repeats\": 3,\n  \"count_repeats\": 1,\n  \"profile_repeats\": 3,\n"
           "  \"pass_schedule\": \"all ordinary timings, then all count passes, then all component timing passes\",\n"
           "  \"time_semantics\": \"raw component brackets are from perturbed profiling pass; "
           "estimates subtract empty timer-pair calibration and may be negative; "
           "components must not be added to or subtracted from normal query latency\"\n}\n";
    require(bool(metadata), "metadata write failed");
    std::cout << "PASS " << checked << " query/arm rows; normal/count/time results and operation counts agree; "
        << "timer_pair_ns=" << timer_ns << "; empty_queries=" << empty_queries << '\n';
    return 0;
} catch (const std::exception& e) {
    std::cerr << "query_cost_profile: " << e.what() << '\n';
    return 1;
}
#endif
