// Scope-resolution probe for a DAG attribute hierarchy with single direct
// terminal labels. Measures only metadata work: no vectors, no ANN.
//
// Strategies: ONLINE (merge direct postings per query), MAT_ALL (materialise
// every non-empty concept), MAT_SELECT (materialise concepts whose expansion
// cost exceeds one bitmap copy, best-first within a byte budget).
//
// Updates relabel objects and maintain either ancestor-propagated counters and
// bitmaps (MAT_*) or nothing (ONLINE), compared against a rebuild baseline that
// recomputes each affected materialised bitmap from scratch.
#include "dag_scope_common.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::string dir = "../../work/dag-scope/workload-run1";
    std::string out = "../../work/dag-scope/probe-run1";
    size_t budget_bytes = 64ull << 20;
    size_t relabels = 50000;
    double heavy_fraction = 0.0;  // fraction of relabels aimed at high-degree concepts
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--data" && i + 1 < argc) dir = argv[++i];
        else if (arg == "--out" && i + 1 < argc) out = argv[++i];
        else if (arg == "--budget-mb" && i + 1 < argc) budget_bytes = std::stoull(argv[++i]) << 20;
        else if (arg == "--relabels" && i + 1 < argc) relabels = std::stoull(argv[++i]);
        else if (arg == "--heavy" && i + 1 < argc) heavy_fraction = std::stod(argv[++i]);
        else fail("unknown argument " + arg);
    }

    Graph graph = build_graph(dir);
    Workload work = load(dir, 20261101);
    const size_t n = work.objects;
    const size_t words = (n + 63) / 64;
    if (graph.active() != work.postings.size())
        fail("concept count mismatch between edges and objects");

    std::cout << "objects " << n << " concepts " << graph.active()
              << " queries " << work.queries.size() << " words " << words << "\n";

    // Expansion cost per concept: one posting entry per descendant concept plus
    // one bit set per directly labelled object inside the descendant set.
    std::vector<uint64_t> expansion(graph.active(), 0);
    for (size_t c = 0; c < graph.active(); ++c) {
        uint64_t cost = 0;
        for (Id d : graph.descendants[c]) cost += 1 + work.postings[d].size();
        expansion[c] = cost;
    }

    // Truth scope sizes and a reference bitmap per queried concept.
    std::vector<Strategy> strategies(3);
    strategies[0].name = "online";
    strategies[1].name = "mat_all";
    strategies[2].name = "mat_select";
    for (auto& strategy : strategies) {
        strategy.cardinality.assign(graph.active(), 0);
        strategy.materialised.assign(graph.active(), Bitmap{});
    }
    for (size_t c = 0; c < graph.active(); ++c) {
        for (Id d : graph.descendants[c]) strategies[0].cardinality[c] += work.postings[d].size();
    }
    for (auto& strategy : strategies) strategy.cardinality = strategies[0].cardinality;

    // MAT_ALL materialises every non-empty concept.
    for (size_t c = 0; c < graph.active(); ++c) {
        if (strategies[0].cardinality[c] == 0) continue;
        Bitmap bitmap;
        bitmap.words.assign(words, 0);
        for (Id d : graph.descendants[c])
            for (Id object : work.postings[d]) bitmap.set(object);
        strategies[1].materialised[c] = std::move(bitmap);
    }
    // MAT_SELECT materialises wide concepts best-first within the byte budget.
    {
        std::vector<size_t> order;
        for (size_t c = 0; c < graph.active(); ++c)
            if (expansion[c] > words) order.push_back(c);
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return expansion[a] != expansion[b] ? expansion[a] > expansion[b] : a < b;
        });
        size_t used = 0;
        for (size_t c : order) {
            if (used + words * sizeof(uint64_t) > budget_bytes) break;
            Bitmap bitmap;
            bitmap.words.assign(words, 0);
            for (Id d : graph.descendants[c])
                for (Id object : work.postings[d]) bitmap.set(object);
            strategies[2].materialised[c] = std::move(bitmap);
            used += words * sizeof(uint64_t);
        }
        strategies[2].words = used / sizeof(uint64_t);
    }

    size_t mat_all_bytes = 0, mat_select_bytes = 0;
    for (const auto& bitmap : strategies[1].materialised) mat_all_bytes += bitmap.words.size() * sizeof(uint64_t);
    for (const auto& bitmap : strategies[2].materialised) mat_select_bytes += bitmap.words.size() * sizeof(uint64_t);
    std::cout << "materialised all " << mat_all_bytes << " bytes, selective " << mat_select_bytes
              << " bytes over " << strategies[2].words << " words\n";

    // Scope resolution. Every strategy must produce identical member sets.
    std::vector<Bitmap> reference(work.queries.size());
    std::vector<double> timings[3];
    std::vector<size_t> sizes;
    for (size_t q = 0; q < work.queries.size(); ++q) {
        const auto& query = work.queries[q];
        Bitmap expected;
        expected.words.assign(words, 0);
        for (Id c : query)
            for (Id d : graph.descendants[c])
                for (Id object : work.postings[d]) expected.set(object);
        reference[q].words = expected.words;
        sizes.push_back(expected.count());

        for (int s = 0; s < 3; ++s) {
            Strategy& strategy = strategies[s];
            Bitmap scratch;
            scratch.words.assign(words, 0);
            const auto start = Clock::now();
            bool materialised_path = true;
            for (Id c : query) {
                if (strategy.name != "online" && !strategy.materialised[c].words.empty()) {
                    const auto& source = strategy.materialised[c].words;
                    for (size_t w = 0; w < words; ++w) scratch.words[w] |= source[w];
                } else {
                    materialised_path = false;
                    for (Id d : graph.descendants[c])
                        for (Id object : work.postings[d]) scratch.set(object);
                }
            }
            timings[s].push_back(micros(start));
            if (scratch.words != expected.words)
                fail("strategy " + strategy.name + " disagrees on query " + std::to_string(q));
            (void)materialised_path;
        }
    }

    // Relabelling: move an object from its terminal concept to another concept.
    std::vector<Id> movable;
    for (Id object = 0; object < n; ++object) movable.push_back(object);
    std::mt19937_64 rng(7);
    std::shuffle(movable.begin(), movable.end(), rng);
    std::vector<Id> heavy;
    if (heavy_fraction > 0) {
        for (Id object = 0; object < n; ++object)
            if (graph.ancestors[work.terminal[object]].size() >= 8) heavy.push_back(object);
    }
    std::vector<double> update[3], rebuild;
    size_t relabelled = 0;
    for (size_t i = 0; i < relabels && i < movable.size(); ++i) {
        Id object = (heavy_fraction > 0 && i * 4 < relabels * heavy_fraction)
                        ? heavy[rng() % heavy.size()]
                        : movable[i];
        Id from = work.terminal[object];
        Id to = static_cast<Id>(rng() % graph.active());
        if (to == from || graph.descendants[to].empty()) continue;

        // Ancestor-propagated maintenance (both materialised strategies).
        for (int s = 1; s < 3; ++s) {
            Strategy& strategy = strategies[s];
            const auto start = Clock::now();
            for (Id c : graph.ancestors[from]) {
                if (strategy.cardinality[c]) --strategy.cardinality[c];
                auto& bitmap = strategy.materialised[c];
                if (!bitmap.words.empty()) bitmap.words[object >> 6] &= ~(UINT64_C(1) << (object & 63));
            }
            for (Id c : graph.ancestors[to]) {
                ++strategy.cardinality[c];
                auto& bitmap = strategy.materialised[c];
                if (!bitmap.words.empty()) bitmap.words[object >> 6] |= UINT64_C(1) << (object & 63);
            }
            update[s].push_back(micros(start));
        }
        // Rebuild baseline: recompute each affected materialised bitmap.
        {
            const auto start = Clock::now();
            for (Id c : graph.ancestors[to]) {
                auto& bitmap = strategies[2].materialised[c];
                if (bitmap.words.empty()) continue;
                std::fill(bitmap.words.begin(), bitmap.words.end(), 0);
                for (Id d : graph.descendants[c])
                    for (Id other : work.postings[d]) bitmap.set(other);
                for (Id other : work.postings[from]) bitmap.words[other >> 6] &= ~(UINT64_C(1) << (other & 63));
            }
            rebuild.push_back(micros(start));
        }

        auto& src = work.postings[from];
        src.erase(std::lower_bound(src.begin(), src.end(), object));
        auto& dst = work.postings[to];
        dst.insert(std::lower_bound(dst.begin(), dst.end(), object), object);
        work.terminal[object] = to;
        ++relabelled;
    }

    // Report.
    std::ostringstream report;
    report << "{\n";
    report << "  \"kind\": \"dag_scope_resolution_probe\",\n";
    report << "  \"new_ann_runs\": 0,\n";
    report << "  \"objects\": " << n << ",\n";
    report << "  \"concepts\": " << graph.active() << ",\n";
    report << "  \"queries\": " << work.queries.size() << ",\n";
    report << "  \"relabel_events\": " << relabelled << ",\n";
    report << "  \"budget_bytes\": " << budget_bytes << ",\n";
    report << "  \"materialised_bytes\": {\"all\": " << mat_all_bytes
           << ", \"select\": " << mat_select_bytes << "},\n";
    report << "  \"materialised_concepts\": {\"all\": " << [&] {
        size_t c = 0; for (const auto& b : strategies[1].materialised) c += !b.words.empty(); return c; }()
           << ", \"select\": " << [&] {
        size_t c = 0; for (const auto& b : strategies[2].materialised) c += !b.words.empty(); return c; }()
           << "},\n";
    report << "  \"scope_size\": {\"mean\": " << [&] {
        double total = 0; for (size_t s : sizes) total += s; return sizes.empty() ? 0.0 : total / sizes.size(); }()
           << ", \"max\": " << (sizes.empty() ? 0 : *std::max_element(sizes.begin(), sizes.end())) << "},\n";
    const char* names[3] = {"online", "mat_all", "mat_select"};
    report << "  \"query_us\": {";
    for (int s = 0; s < 3; ++s) {
        double total = 0;
        for (double v : timings[s]) total += v;
        report << (s ? ", " : "") << "\"" << names[s] << "\": {\"mean\": "
               << (timings[s].empty() ? 0.0 : total / timings[s].size())
               << ", \"p50\": " << percentile(timings[s], .5)
               << ", \"p95\": " << percentile(timings[s], .95) << "}";
    }
    report << "},\n  \"update_us\": {\"mat_all\": {\"mean\": " << [&] {
        double t = 0; for (double v : update[1]) t += v; return update[1].empty() ? 0.0 : t / update[1].size(); }()
           << ", \"p95\": " << percentile(update[1], .95)
           << "}, \"mat_select\": {\"mean\": " << [&] {
        double t = 0; for (double v : update[2]) t += v; return update[2].empty() ? 0.0 : t / update[2].size(); }()
           << ", \"p95\": " << percentile(update[2], .95)
           << "}, \"rebuild_select\": {\"mean\": " << [&] {
        double t = 0; for (double v : rebuild) t += v; return rebuild.empty() ? 0.0 : t / rebuild.size(); }()
           << ", \"p95\": " << percentile(rebuild, .95) << "}},\n";
    report << "  \"equivalence\": \"every strategy matched the reference bitmap on all queries\",\n";
    report << "  \"limits\": [\"metadata only; no vectors or ANN calls\", "
              "\"single direct terminal label per object\", "
              "\"object ids are shuffled so postings are not contiguous\", "
              "\"relabel destinations are random concepts, not a workload model\"]\n";
    report << "}\n";

    std::filesystem::create_directories(out);
    std::ofstream handle(out + "/probe.json");
    handle << report.str();
    std::cout << report.str();
    return 0;
}
