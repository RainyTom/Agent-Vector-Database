// Scope-resolution probe for a DAG attribute hierarchy with single direct
// terminal labels. Measures only metadata work: no vectors, no ANN.
//
// Strategies: ONLINE (merge direct postings per query), MAT_ALL (materialise
// every non-empty concept), MAT_SELECT (materialise concepts whose expansion
// cost exceeds one bitmap copy, best-first within a byte budget).
//
// Updates replay the same single-label moves in independent strategy states.
// Timings cover only the bitmap/counter update kernel, not posting maintenance,
// reference recomputation, vector-index maintenance, or end-to-end updates.
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
#include <utility>
#include <vector>

struct UpdateState {
    Strategy strategy;
    std::vector<Id> labels;
    std::vector<std::vector<Id>> postings;
};

struct Relabel {
    Id object, from, to;
};

static const Bitmap& resolve_scope(const UpdateState& state, const Graph& graph,
                                   Id concept, Bitmap& scratch) {
    const auto& materialised = state.strategy.materialised[concept];
    if (!materialised.words.empty()) return materialised;
    std::fill(scratch.words.begin(), scratch.words.end(), 0);
    for (Id descendant : graph.descendants[concept])
        for (Id object : state.postings[descendant]) scratch.set(object);
    return scratch;
}

// This reference uses replayed labels and raw child edges, not maintained
// postings, ancestor counters, cached closures, or materialised bitmaps.
static void verify_states(const Graph& graph, const std::vector<Id>& labels,
                          const std::vector<UpdateState>& states, size_t words) {
    std::vector<std::vector<Id>> postings(graph.active());
    for (size_t object = 0; object < labels.size(); ++object)
        postings[labels[object]].push_back(static_cast<Id>(object));
    for (const auto& state : states) {
        if (state.labels != labels || state.postings != postings)
            fail("post-update labels/postings disagree for " + state.strategy.name);
    }
    Bitmap expected, scratch;
    expected.words.assign(words, 0);
    scratch.words.assign(words, 0);
    std::vector<char> visited(graph.active());
    std::vector<Id> pending;
    for (size_t concept = 0; concept < graph.active(); ++concept) {
        std::fill(expected.words.begin(), expected.words.end(), 0);
        std::fill(visited.begin(), visited.end(), 0);
        pending.push_back(static_cast<Id>(concept));
        while (!pending.empty()) {
            Id current = pending.back();
            pending.pop_back();
            if (visited[current]) continue;
            visited[current] = 1;
            for (Id object : postings[current]) expected.set(object);
            for (Id child : graph.children[current]) pending.push_back(child);
        }
        const size_t cardinality = expected.count();
        for (const auto& state : states) {
            if (state.strategy.cardinality[concept] != cardinality)
                fail("post-update cardinality disagrees for " + state.strategy.name
                     + " concept " + std::to_string(concept));
            if (resolve_scope(state, graph, static_cast<Id>(concept), scratch).words
                != expected.words)
                fail("post-update membership disagrees for " + state.strategy.name
                     + " concept " + std::to_string(concept));
        }
    }
}

static void write_ids(std::ostream& stream, const std::vector<Id>& ids) {
    stream << "[";
    for (size_t i = 0; i < ids.size(); ++i)
        stream << (i ? ", " : "") << ids[i];
    stream << "]";
}

int main(int argc, char** argv) {
    std::string dir = "../../work/dag-scope/workload-run1";
    std::string out = "../../work/dag-scope/probe-run1";
    size_t budget_bytes = 64ull << 20;
    size_t relabels = 50000;
    double heavy_fraction = 0.0;  // fraction of relabels aimed at high-degree concepts
    std::string replay_path;
    size_t verify_batch = 1024;
    bool dump_scopes = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--data" && i + 1 < argc) dir = argv[++i];
        else if (arg == "--out" && i + 1 < argc) out = argv[++i];
        else if (arg == "--budget-mb" && i + 1 < argc) budget_bytes = std::stoull(argv[++i]) << 20;
        else if (arg == "--relabels" && i + 1 < argc) relabels = std::stoull(argv[++i]);
        else if (arg == "--heavy" && i + 1 < argc) heavy_fraction = std::stod(argv[++i]);
        else if (arg == "--replay" && i + 1 < argc) replay_path = argv[++i];
        else if (arg == "--verify-batch" && i + 1 < argc) verify_batch = std::stoull(argv[++i]);
        else if (arg == "--dump-scopes") dump_scopes = true;
        else fail("unknown argument " + arg);
    }
    if (verify_batch == 0) fail("--verify-batch must be positive");
    if (!(heavy_fraction >= 0.0 && heavy_fraction <= 1.0))
        fail("--heavy must be between zero and one");

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
    std::vector<double> timings[3];
    std::vector<size_t> sizes;
    for (size_t q = 0; q < work.queries.size(); ++q) {
        const auto& query = work.queries[q];
        Bitmap expected;
        expected.words.assign(words, 0);
        for (Id c : query)
            for (Id d : graph.descendants[c])
                for (Id object : work.postings[d]) expected.set(object);
        sizes.push_back(expected.count());

        for (int s = 0; s < 3; ++s) {
            Strategy& strategy = strategies[s];
            Bitmap scratch;
            scratch.words.assign(words, 0);
            const auto start = Clock::now();
            for (Id c : query) {
                if (strategy.name != "online" && !strategy.materialised[c].words.empty()) {
                    const auto& source = strategy.materialised[c].words;
                    for (size_t w = 0; w < words; ++w) scratch.words[w] |= source[w];
                } else {
                    for (Id d : graph.descendants[c])
                        for (Id object : work.postings[d]) scratch.set(object);
                }
            }
            timings[s].push_back(micros(start));
            if (scratch.words != expected.words)
                fail("strategy " + strategy.name + " disagrees on query " + std::to_string(q));
        }
    }

    // Build one immutable operation stream before touching any strategy state.
    // --replay rows are "object_id destination_concept"; ids are post-shuffle.
    std::vector<Relabel> operations;
    std::vector<Id> planned_labels = work.terminal;
    if (!replay_path.empty()) {
        std::ifstream replay(replay_path);
        if (!replay) fail("cannot open replay file");
        std::string line;
        while (std::getline(replay, line)) {
            if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
            std::istringstream row(line);
            uint64_t object = 0, to = 0;
            std::string extra;
            if (!(row >> object >> to) || (row >> extra)
                || object >= n || to >= graph.active())
                fail("invalid replay row: expected object_id destination_concept");
            operations.push_back({static_cast<Id>(object), planned_labels[object],
                                  static_cast<Id>(to)});
            planned_labels[object] = static_cast<Id>(to);
        }
    } else {
        std::vector<Id> movable(n);
        std::iota(movable.begin(), movable.end(), 0U);
        std::mt19937_64 rng(7);
        std::shuffle(movable.begin(), movable.end(), rng);
        std::vector<Id> heavy;
        if (heavy_fraction > 0) {
            for (Id object : movable)
                if (graph.ancestors[work.terminal[object]].size() >= 8)
                    heavy.push_back(object);
        }
        std::bernoulli_distribution choose_heavy(heavy_fraction);
        for (size_t i = 0; i < std::min(relabels, movable.size()); ++i) {
            Id object = !heavy.empty() && choose_heavy(rng)
                            ? heavy[rng() % heavy.size()] : movable[i];
            Id to = static_cast<Id>(rng() % graph.active());
            Id from = planned_labels[object];
            if (to == from) continue;
            operations.push_back({object, from, to});
            planned_labels[object] = to;
        }
    }

    std::vector<UpdateState> states;
    states.reserve(strategies.size());
    for (auto& strategy : strategies)
        states.push_back({std::move(strategy), work.terminal, work.postings});
    std::vector<Id> reference_labels = work.terminal;
    std::vector<double> update[3];
    verify_states(graph, reference_labels, states, words);
    size_t checkpoints = 1;
    for (size_t i = 0; i < operations.size(); ++i) {
        const auto [object, from, to] = operations[i];
        const auto& old_ancestors = graph.ancestors[from];
        const auto& new_ancestors = graph.ancestors[to];
        for (size_t s = 0; s < states.size(); ++s) {
            auto& state = states[s];
            if (state.labels[object] != from) fail("operation stream diverged");
            auto& source = state.postings[from];
            auto found = std::lower_bound(source.begin(), source.end(), object);
            if (found == source.end() || *found != object)
                fail("object missing from source posting");
            source.erase(found);
            auto& destination = state.postings[to];
            destination.insert(std::lower_bound(destination.begin(), destination.end(), object), object);
            state.labels[object] = to;

            // Posting erase/insert above is deliberately outside this timer.
            // Each shared ancestor retains this object exactly once.
            auto& strategy = state.strategy;
            const auto start = s ? Clock::now() : Clock::time_point{};
            for (Id c : old_ancestors) {
                if (std::binary_search(new_ancestors.begin(), new_ancestors.end(), c)) continue;
                if (strategy.cardinality[c] == 0) fail("cardinality underflow");
                --strategy.cardinality[c];
                auto& bitmap = strategy.materialised[c];
                if (!bitmap.words.empty())
                    bitmap.words[object >> 6] &= ~(UINT64_C(1) << (object & 63));
            }
            for (Id c : new_ancestors) {
                if (std::binary_search(old_ancestors.begin(), old_ancestors.end(), c)) continue;
                ++strategy.cardinality[c];
                auto& bitmap = strategy.materialised[c];
                if (!bitmap.words.empty()) bitmap.set(object);
            }
            if (s) update[s].push_back(micros(start));
        }
        if (reference_labels[object] != from) fail("reference replay diverged");
        reference_labels[object] = to;
        if ((i + 1) % verify_batch == 0 || i + 1 == operations.size()) {
            verify_states(graph, reference_labels, states, words);
            ++checkpoints;
        }
    }
    const size_t relabelled = operations.size();

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
        size_t c = 0; for (const auto& b : states[1].strategy.materialised) c += !b.words.empty(); return c; }()
           << ", \"select\": " << [&] {
        size_t c = 0; for (const auto& b : states[2].strategy.materialised) c += !b.words.empty(); return c; }()
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
    report << "},\n  \"bitmap_counter_update_kernel_us\": {\"mat_all\": {\"mean\": " << [&] {
        double t = 0; for (double v : update[1]) t += v; return update[1].empty() ? 0.0 : t / update[1].size(); }()
           << ", \"p95\": " << percentile(update[1], .95)
           << "}, \"mat_select\": {\"mean\": " << [&] {
        double t = 0; for (double v : update[2]) t += v; return update[2].empty() ? 0.0 : t / update[2].size(); }()
           << ", \"p95\": " << percentile(update[2], .95) << "}},\n";
    report << "  \"update_measurement\": {\"unit\": \"microseconds per relabel\", "
              "\"includes\": \"ancestor-set difference, cardinality counters and materialised bitmap edits\", "
              "\"excludes\": \"base posting maintenance, label assignment, replay generation, verification and vector-index maintenance\"},\n";
    report << "  \"label_model\": \"one direct terminal label per object; destinations may be internal concepts\",\n";
    report << "  \"update_source\": \"" << (replay_path.empty() ? "seeded_random" : "replay") << "\",\n";
    report << "  \"verification\": {\"checkpoints\": " << checkpoints
           << ", \"batch_size\": " << verify_batch
           << ", \"scope_membership_checks\": " << checkpoints * graph.active() * states.size()
           << ", \"cardinality_checks\": " << checkpoints * graph.active() * states.size()
           << ", \"posting_state_checks\": " << checkpoints * states.size()
           << ", \"mismatches\": 0, \"initial_and_final_states\": true, "
              "\"reference\": \"replayed labels and raw child-edge traversal, independent of maintained postings and cached closures\"},\n";
    if (dump_scopes) {
        report << "  \"replay_state\": {\"initial_labels\": ";
        write_ids(report, work.terminal);
        report << ", \"final_labels\": ";
        write_ids(report, reference_labels);
        report << ", \"strategies\": {";
        for (size_t s = 0; s < states.size(); ++s) {
            const auto& state = states[s];
            Bitmap scratch;
            scratch.words.assign(words, 0);
            report << (s ? ", " : "") << "\"" << state.strategy.name << "\": {\"labels\": ";
            write_ids(report, state.labels);
            report << ", \"scopes\": [";
            for (size_t c = 0; c < graph.active(); ++c) {
                const Bitmap& scope = resolve_scope(state, graph, static_cast<Id>(c), scratch);
                std::vector<Id> members;
                for (size_t object = 0; object < n; ++object)
                    if (scope.test(static_cast<Id>(object))) members.push_back(static_cast<Id>(object));
                report << (c ? ", " : "") << "{\"concept\": " << c
                       << ", \"cardinality\": " << state.strategy.cardinality[c]
                       << ", \"materialised\": "
                       << (state.strategy.materialised[c].words.empty() ? "false" : "true")
                       << ", \"members\": ";
                write_ids(report, members);
                report << "}";
            }
            report << "]}";
        }
        report << "}},\n";
    }
    report << "  \"limits\": [\"metadata only; no vectors or ANN calls\", "
              "\"one direct terminal label per object, not multilabel membership\", "
              "\"object ids are shuffled so postings are not contiguous\", "
              "\"random destinations are concepts, not a workload model; replay uses explicit destinations\", "
              "\"kernel timings are not end-to-end update latency\"]\n";
    report << "}\n";

    std::filesystem::create_directories(out);
    std::ofstream handle(out + "/probe.json");
    handle << report.str();
    std::cout << report.str();
    return 0;
}
