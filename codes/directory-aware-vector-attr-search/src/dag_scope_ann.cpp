// End-to-end probe: DAG scope resolution feeding exact or filtered HNSW search.
// Geometry is built once and reused across measurement passes. Ground truth,
// correctness checks and measurement-file serialization are outside query timing.
#include "dag_scope_common.h"

#include "raw_l2_space.h"

#include "hnswlib/hnswlib.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <set>

namespace {

struct Vectors {
    size_t dim = 0;
    std::vector<float> data;   // row-major, count * dim
    size_t count() const { return dim ? data.size() / dim : 0; }
};

Vectors load_fvecs(const std::string& path, size_t limit) {
    std::ifstream in(path, std::ios::binary);
    if (!in) fail("cannot open " + path);
    Vectors out;
    std::vector<float> buffer;
    while (out.count() < limit) {
        int32_t dim = 0;
        if (!in.read(reinterpret_cast<char*>(&dim), sizeof(dim))) break;
        if (dim <= 0) fail("bad fvecs record header in " + path);
        if (!out.dim) out.dim = static_cast<size_t>(dim);
        if (static_cast<size_t>(dim) != out.dim) fail("inconsistent fvecs dimension");
        buffer.assign(out.dim, 0.0f);
        if (!in.read(reinterpret_cast<char*>(buffer.data()), out.dim * sizeof(float)))
            fail("truncated fvecs record in " + path);
        out.data.insert(out.data.end(), buffer.begin(), buffer.end());
    }
    if (!out.count()) fail("empty fvecs file " + path);
    return out;
}

struct ScopeFilter : hnswlib::BaseFilterFunctor {
    const Bitmap& bitmap;
    explicit ScopeFilter(const Bitmap& b) : bitmap(b) {}
    bool operator()(hnswlib::labeltype label) override { return bitmap.test(static_cast<Id>(label)); }
};

// Same pattern as the packed runner: manual upper-layer descent, then the
// pinned base-layer search with the eligibility filter and hop metrics.
std::vector<std::pair<float, Id>> search_graph(hnswlib::HierarchicalNSW<float>& graph,
                                               const float* query, size_t k, size_t ef,
                                               ScopeFilter& filter) {
    std::vector<std::pair<float, Id>> result;
    if (!k || !graph.cur_element_count) return result;
    auto current = graph.enterpoint_node_;
    float best = graph.fstdistfunc_(query, graph.getDataByInternalId(current), graph.dist_func_param_);
    for (int level = graph.maxlevel_; level > 0; --level) {
        bool changed = true;
        while (changed) {
            changed = false;
            auto* links = graph.get_linklist(current, level);
            const size_t count = graph.getListCount(links);
            const auto* neighbors = reinterpret_cast<const hnswlib::tableint*>(links + 1);
            for (size_t i = 0; i < count; ++i) {
                const auto candidate = neighbors[i];
                const float distance = graph.fstdistfunc_(query, graph.getDataByInternalId(candidate),
                                                          graph.dist_func_param_);
                if (distance < best) {
                    best = distance;
                    current = candidate;
                    changed = true;
                }
            }
        }
    }
    auto found = graph.searchBaseLayerST<false, true>(current, query, ef, &filter);
    while (!found.empty()) {
        result.emplace_back(found.top().first, static_cast<Id>(graph.getExternalLabel(found.top().second)));
        found.pop();
    }
    std::sort(result.begin(), result.end());
    if (result.size() > k) result.resize(k);
    return result;
}

std::vector<std::pair<float, Id>> exact_search(const Bitmap& scope, const float* query,
                                               const Vectors& base, size_t k,
                                               RawL2Space& space) {
    std::vector<std::pair<float, Id>> result;
    for (size_t w = 0; w < scope.words.size(); ++w)
        for (uint64_t bits = scope.words[w]; bits; bits &= bits - 1) {
            const Id object = static_cast<Id>(w * 64 + static_cast<unsigned>(__builtin_ctzll(bits)));
            result.emplace_back(space.dist(query, base.data.data() + size_t(object) * base.dim), object);
        }
    std::sort(result.begin(), result.end());
    if (result.size() > k) result.resize(k);
    return result;
}

// Independent reference: traverse direct edges, then inspect direct object labels.
// This does not use the descendants closure, postings, or materialised bitmaps.
Bitmap reference_scope(const std::vector<Id>& query, const Graph& graph, const Workload& work) {
    std::vector<char> included(graph.active(), 0);
    std::vector<Id> pending = query;
    while (!pending.empty()) {
        const Id c = pending.back();
        pending.pop_back();
        if (c >= graph.active()) fail("query concept outside graph");
        if (included[c]) continue;
        included[c] = 1;
        for (Id child : graph.children[c]) pending.push_back(child);
    }
    Bitmap result;
    result.words.assign((work.objects + 63) / 64, 0);
    for (Id object = 0; object < work.objects; ++object)
        if (included[work.terminal[object]]) result.set(object);
    return result;
}

struct QueryResult {
    Bitmap scope;
    std::vector<std::pair<float, Id>> neighbors;
    size_t eligible = 0, touched = 0;
    const char* path = "empty";
    double scope_us = 0, routing_us = 0, search_us = 0, e2e_us = 0;
};

QueryResult run_query(const std::vector<Id>& query, const float* qvec, const Graph& graph,
                      const Workload& work, const Vectors& base, RawL2Space& space,
                      const std::vector<Bitmap>* plan, size_t k, size_t ef, size_t tau,
                      hnswlib::HierarchicalNSW<float>& index,
                      const std::vector<Id>& part_of,
                      std::vector<std::unique_ptr<hnswlib::HierarchicalNSW<float>>>* parts) {
    const auto scope_start = Clock::now();
    QueryResult output;
    const size_t words = (work.objects + 63) / 64;
    output.scope.words.assign(words, 0);
    for (Id c : query) {
        if (plan && !(*plan)[c].words.empty()) {
            for (size_t w = 0; w < words; ++w)
                output.scope.words[w] |= (*plan)[c].words[w];
        } else {
            for (Id d : graph.descendants[c])
                for (Id object : work.postings[d]) output.scope.set(object);
        }
    }
    output.scope_us = micros(scope_start);
    const auto routing_start = Clock::now();
    output.eligible = output.scope.count();
    std::vector<size_t> per_partition;
    if (parts) {
        per_partition.assign(parts->size(), 0);
        for (size_t w = 0; w < words; ++w)
            for (uint64_t bits = output.scope.words[w]; bits; bits &= bits - 1) {
                const Id object = static_cast<Id>(w * 64 + static_cast<unsigned>(__builtin_ctzll(bits)));
                ++per_partition[part_of[object]];
            }
        for (size_t count : per_partition) output.touched += count != 0;
    }
    output.routing_us = micros(routing_start);
    const auto search_start = Clock::now();
    if (output.eligible && output.eligible <= tau) {
        output.path = "exact";
        output.neighbors = exact_search(output.scope, qvec, base, k, space);
    } else if (output.eligible) {
        output.path = "graph";
        ScopeFilter filter(output.scope);
        if (!parts) {
            output.neighbors = search_graph(index, qvec, std::min(k, output.eligible), ef, filter);
        } else {
            for (size_t b = 0; b < parts->size(); ++b) {
                if (!per_partition[b]) continue;
                const size_t local_k = std::min(k, per_partition[b]);
                const size_t local_ef = std::min(per_partition[b], ef);
                auto found = search_graph(*(*parts)[b], qvec, local_k, local_ef, filter);
                output.neighbors.insert(output.neighbors.end(), found.begin(), found.end());
            }
            // Object ownership is disjoint: duplicates are errors, not silently removed.
            std::sort(output.neighbors.begin(), output.neighbors.end());
            if (output.neighbors.size() > k) output.neighbors.resize(k);
        }
    }
    output.search_us = micros(search_start);
    return output;
}

struct Stats {
    std::vector<double> scope, routing, search, e2e, fanout, recall;
    size_t empty = 0, exact = 0, graph = 0, hits = 0, denominator = 0;
    size_t shortage_queries = 0, shortage_results = 0;
    void add(const QueryResult& result, size_t query_hits, size_t query_denominator) {
        scope.push_back(result.scope_us);
        routing.push_back(result.routing_us);
        search.push_back(result.search_us);
        e2e.push_back(result.e2e_us);
        fanout.push_back(static_cast<double>(result.touched));
        if (!query_denominator) ++empty;
        else recall.push_back(double(query_hits) / query_denominator);
        exact += result.path[0] == 'e' && result.path[1] == 'x';
        graph += result.path[0] == 'g';
        hits += query_hits;
        denominator += query_denominator;
        if (result.neighbors.size() < query_denominator) {
            ++shortage_queries;
            shortage_results += query_denominator - result.neighbors.size();
        }
    }
};

double mean(const std::vector<double>& values) {
    return values.empty() ? 0.0 : std::accumulate(values.begin(), values.end(), 0.0) / values.size();
}

void write_stats(std::ostream& out, const Stats& stats, bool partitioned) {
    out << "{\"queries\": " << stats.e2e.size()
        << ", \"scope_us\": " << mean(stats.scope)
        << ", \"routing_us\": " << mean(stats.routing)
        << ", \"search_us\": " << mean(stats.search)
        << ", \"e2e_us\": " << mean(stats.e2e)
        << ", \"e2e_p95_us\": " << percentile(stats.e2e, .95)
        << ", \"nonempty_queries\": " << stats.recall.size()
        << ", \"empty_queries\": " << stats.empty
        << ", \"nonempty_macro_recall\": ";
    if (stats.recall.empty()) out << "null";
    else out << mean(stats.recall);
    out << ", \"pair_recall\": ";
    if (!stats.denominator) out << "null";
    else out << double(stats.hits) / stats.denominator;
    out << ", \"hits\": " << stats.hits << ", \"recall_denominator\": " << stats.denominator
        << ", \"fanout\": ";
    if (partitioned) out << mean(stats.fanout);
    else out << "null";
    out << ", \"fanout_p95\": ";
    if (partitioned) out << percentile(stats.fanout, .95);
    else out << "null";
    out << ", \"exact_queries\": " << stats.exact << ", \"graph_queries\": " << stats.graph
        << ", \"shortage_queries\": " << stats.shortage_queries
        << ", \"shortage_results\": " << stats.shortage_results << "}";
}

void write_ids(std::ostream& out, const std::vector<std::pair<float, Id>>& result) {
    out << '"';
    for (size_t i = 0; i < result.size(); ++i) out << (i ? ";" : "") << result[i].second;
    out << '"';
}

void write_json_string(std::ostream& out, const std::string& value) {
    constexpr char hex[] = "0123456789abcdef";
    out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << char(c);
        else if (c < 32) out << "\\u00" << hex[c >> 4] << hex[c & 15];
        else out << char(c);
    }
    out << '"';
}

size_t positive_option(const std::string& value, const std::string& option, bool allow_zero = false) {
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        fail("invalid nonnegative integer for " + option);
    size_t parsed = 0;
    try {
        const auto number = std::stoull(value);
        if (number > std::numeric_limits<size_t>::max()) fail("value too large for " + option);
        parsed = static_cast<size_t>(number);
    } catch (const std::exception&) { fail("value too large for " + option); }
    if (!allow_zero && !parsed) fail(option + " must be positive");
    return parsed;
}

} // namespace

int main(int argc, char** argv) {
    std::string dir = "../../work/dag-scope/workload-run1";
    std::string out = "../../work/dag-scope/ann-run1";
    std::string vector_path, query_path, partition_mode = "none";
    size_t ef = 20, k = 10, tau = 512, max_queries = 1234, repeats = 1;
    size_t budget_bytes = 64ull << 20, partition_depth = 1, sample_queries = 0;
    size_t seed = 20261101;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() { if (i + 1 >= argc) fail("missing value for " + arg); return std::string(argv[++i]); };
        if (arg == "--data") dir = next();
        else if (arg == "--out") out = next();
        else if (arg == "--vectors") vector_path = next();
        else if (arg == "--queries") query_path = next();
        else if (arg == "--ef") ef = positive_option(next(), arg);
        else if (arg == "--k") k = positive_option(next(), arg);
        else if (arg == "--tau") tau = positive_option(next(), arg, true);
        else if (arg == "--max-queries") max_queries = positive_option(next(), arg);
        else if (arg == "--repeats") repeats = positive_option(next(), arg);
        else if (arg == "--sample-queries") sample_queries = positive_option(next(), arg);
        else if (arg == "--seed") seed = positive_option(next(), arg, true);
        else if (arg == "--budget-mb") {
            const size_t mb = positive_option(next(), arg, true);
            if (mb > (std::numeric_limits<size_t>::max() >> 20)) fail("--budget-mb is too large");
            budget_bytes = mb << 20;
        }
        else if (arg == "--partition") partition_mode = next();
        else if (arg == "--partition-depth") partition_depth = positive_option(next(), arg, true);
        else fail("unknown argument " + arg);
    }
    if (vector_path.empty() || query_path.empty()) fail("--vectors and --queries are required");
    if (partition_mode != "none" && partition_mode != "top") fail("--partition must be none or top");
    if (ef < k) fail("--ef must be at least --k");

    Graph graph = build_graph(dir);
    Workload work = load(dir, 20261101); // Object/vector association is independent of query sampling seed.
    const size_t n = work.objects, words = (n + 63) / 64;
    if (!graph.active() || work.queries.empty()) fail("graph and scope workload must be nonempty");
    Vectors base = load_fvecs(vector_path, n);
    if (base.count() < n) fail("not enough vectors for objects");
    Vectors queries = load_fvecs(query_path, std::numeric_limits<size_t>::max());
    if (queries.dim != base.dim) fail("query/vector dimension mismatch");
    const size_t dim = base.dim;
    std::vector<size_t> selected(work.queries.size());
    std::iota(selected.begin(), selected.end(), 0);
    if (sample_queries) {
        std::mt19937_64 rng(seed);
        std::shuffle(selected.begin(), selected.end(), rng);
        selected.resize(std::min(sample_queries, selected.size()));
    }
    selected.resize(std::min(max_queries, selected.size()));
    const size_t nq = selected.size();
    for (size_t row : selected)
        if (row >= queries.count()) fail("selected scope row has no corresponding vector query row");

    RawL2Space space(dim);
    const auto build_start = Clock::now();
    hnswlib::HierarchicalNSW<float> index(&space, n, 16, 200);
    for (Id object = 0; object < n; ++object)
        index.addPoint(base.data.data() + size_t(object) * dim, object);
    index.setEf(ef);
    const double build_s = micros(build_start) / 1e6;

    // Shortest BFS depths are grouping keys, not a DAG-separator frontier.
    // Above-cut concepts own their key. Otherwise choose the minimum stable
    // numeric ID among ancestors at the cut's shortest BFS depth, not a parent path.
    std::vector<Id> part_of(n, 0), part_of_concept(graph.active(), 0);
    size_t partitions = 0, nonempty_partitions = 0;
    double partition_build_s = 0;
    std::vector<std::unique_ptr<hnswlib::HierarchicalNSW<float>>> part_graphs;
    const bool partitioned = partition_mode == "top";
    if (partitioned) {
        const auto partition_start = Clock::now();
        const size_t unseen = std::numeric_limits<size_t>::max();
        std::vector<size_t> depth(graph.active(), unseen);
        std::vector<Id> frontier;
        for (Id c = 0; c < graph.active(); ++c)
            if (graph.parents[c].empty()) { depth[c] = 0; frontier.push_back(c); }
        for (size_t i = 0; i < frontier.size(); ++i)
            for (Id child : graph.children[frontier[i]])
                if (depth[child] == unseen) {
                    depth[child] = depth[frontier[i]] + 1;
                    frontier.push_back(child);
                }
        std::vector<Id> dense(graph.active(), std::numeric_limits<Id>::max());
        for (Id c = 0; c < graph.active(); ++c) {
            if (depth[c] == unseen) fail("partitioning requires a rooted DAG");
            Id key = c;
            if (depth[c] >= partition_depth) {
                key = std::numeric_limits<Id>::max();
                for (Id ancestor : graph.ancestors[c])
                    if (depth[ancestor] == partition_depth) key = std::min(key, ancestor);
                if (key == std::numeric_limits<Id>::max()) fail("no ancestor on partition cut");
            }
            if (dense[key] == std::numeric_limits<Id>::max())
                dense[key] = static_cast<Id>(partitions++);
            part_of_concept[c] = dense[key];
        }
        std::vector<size_t> part_sizes(partitions, 0);
        for (Id object = 0; object < n; ++object) {
            part_of[object] = part_of_concept[work.terminal[object]];
            ++part_sizes[part_of[object]];
        }
        part_graphs.resize(partitions);
        for (size_t b = 0; b < partitions; ++b) {
            if (!part_sizes[b]) continue;
            ++nonempty_partitions;
            part_graphs[b] = std::make_unique<hnswlib::HierarchicalNSW<float>>(&space, part_sizes[b], 16, 200);
        }
        for (Id object = 0; object < n; ++object)
            part_graphs[part_of[object]]->addPoint(base.data.data() + size_t(object) * dim, object);
        for (auto& ptr : part_graphs) if (ptr) ptr->setEf(ef);
        partition_build_s = micros(partition_start) / 1e6;
    }

    std::vector<uint64_t> expansion(graph.active(), 0);
    std::vector<Bitmap> mat_all(graph.active()), mat_select(graph.active());
    for (size_t c = 0; c < graph.active(); ++c) {
        bool nonempty = false;
        for (Id d : graph.descendants[c]) {
            expansion[c] += 1 + work.postings[d].size();
            nonempty |= !work.postings[d].empty();
        }
        if (!nonempty) continue;
        mat_all[c].words.assign(words, 0);
        for (Id d : graph.descendants[c])
            for (Id object : work.postings[d]) mat_all[c].set(object);
    }
    {
        std::vector<size_t> order;
        for (size_t c = 0; c < graph.active(); ++c) if (expansion[c] > words) order.push_back(c);
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return expansion[a] != expansion[b] ? expansion[a] > expansion[b] : a < b;
        });
        size_t used = 0;
        for (size_t c : order) {
            if (words * sizeof(uint64_t) > budget_bytes - used) break;
            mat_select[c] = mat_all[c];
            used += words * sizeof(uint64_t);
        }
    }

    const size_t strategy_count = partitioned ? 4 : 3;
    const char* names[] = {"online", "mat_all", "mat_select", "partition_top"};
    const std::vector<Bitmap>* plans[] = {nullptr, &mat_all, &mat_select, nullptr};
    std::vector<Stats> total(strategy_count);
    std::vector<std::vector<Stats>> passes(repeats, std::vector<Stats>(strategy_count));
    std::filesystem::create_directories(out);
    std::ofstream csv(out + "/ann_queries.csv");
    if (!csv) fail("cannot create per-query CSV");
    csv.precision(12);
    csv << "scope_row,vector_row,pass,order,strategy,path,eligible_count,touched_partitions,"
           "scope_us,routing_us,search_us,e2e_us,hits,denominator,result_count,shortage,result_ids,truth_ids\n";
    for (size_t pass = 0; pass < repeats; ++pass) {
        for (size_t q = 0; q < nq; ++q) {
            const size_t row = selected[q], vector_row = row;
            const auto& query = work.queries[row];
            const float* qvec = queries.data.data() + vector_row * dim;
            const Bitmap reference = reference_scope(query, graph, work);
            const auto truth = exact_search(reference, qvec, base, k, space);
            for (size_t slot = 0; slot < strategy_count; ++slot) {
                const size_t s = (q + pass + slot) % strategy_count;
                const auto query_start = Clock::now();
                QueryResult result = run_query(query, qvec, graph, work, base, space, plans[s],
                                               k, ef, tau, index, part_of,
                                               s == 3 ? &part_graphs : nullptr);
                result.e2e_us = micros(query_start);
                if (result.scope.words != reference.words) fail("strategy scope differs from independent reference");
                std::set<Id> ids;
                size_t hits = 0;
                for (const auto& entry : result.neighbors) {
                    if (entry.second >= n || !reference.test(entry.second)) fail("returned ineligible object");
                    if (!ids.insert(entry.second).second) fail("returned duplicate object");
                    for (const auto& target : truth)
                        if (target.second == entry.second) { ++hits; break; }
                }
                if (result.neighbors.size() > truth.size()) fail("too many returned objects");
                const size_t shortage = truth.size() - result.neighbors.size();
                total[s].add(result, hits, truth.size());
                passes[pass][s].add(result, hits, truth.size());
                csv << row << ',' << vector_row << ',' << pass << ',' << slot << ',' << names[s] << ','
                    << result.path << ',' << result.eligible << ',';
                if (s == 3) csv << result.touched;
                csv << ',' << result.scope_us << ',' << result.routing_us << ',' << result.search_us << ','
                    << result.e2e_us << ',' << hits << ',' << truth.size() << ',' << result.neighbors.size()
                    << ',' << shortage << ',';
                write_ids(csv, result.neighbors);
                csv << ',';
                write_ids(csv, truth);
                csv << '\n';
            }
        }
    }
    csv.close();
    if (!csv) fail("failed writing per-query CSV");

    std::ostringstream report;
    report.precision(12);
    report << "{\n  \"kind\": \"dag_scope_ann_probe\",\n"
           << "  \"partition_mode\": \"" << partition_mode << "\", \"partition_depth\": ";
    if (partitioned) report << partition_depth;
    else report << "null";
    report << ", \"partition_slots\": " << partitions << ", \"nonempty_partitions\": " << nonempty_partitions
           << ", \"allocated_partition_graphs\": " << nonempty_partitions << ",\n"
           << "  \"partition_rule\": \"above-cut concepts own their key; otherwise minimum stable numeric ancestor ID at cut shortest BFS depth from all roots; levels are grouping keys, not DAG separators\",\n"
           << "  \"objects\": " << n << ", \"concepts\": " << graph.active()
           << ", \"queries\": " << nq << ", \"pass_queries\": " << nq * repeats
           << ", \"workload_queries\": " << work.queries.size()
           << ", \"vector_queries\": " << queries.count() << ", \"ef\": " << ef << ", \"k\": " << k
           << ", \"tau\": " << tau << ", \"repeats\": " << repeats
           << ", \"sample_queries_requested\": " << sample_queries << ", \"sample_seed\": " << seed << ",\n"
           << "  \"query_selection\": \"shuffle full workload without replacement if sampling, then apply max-queries; vector_row=scope_row, missing vector rows are errors; all row/pass/order indices zero-based\",\n"
           << "  \"build_s\": " << build_s << ", \"partition_build_s\": " << partition_build_s << ",\n"
           << "  \"build_scope\": \"one global geometry build and, when enabled, one partition geometry build before all passes; partition time includes routing setup\",\n"
           << "  \"timing\": \"outer wall e2e includes scope allocation/reset, union, popcount, partition counts and dispatch, searches, merge and result materialization; GT, validation and measurement serialization excluded; scope/routing/search are subintervals\",\n"
           << "  \"recall\": \"nonempty_macro_recall averages hits/min(k,eligible) over nonempty scopes only; pair_recall sums hits and denominators; empty-only recall is null; ANN shortages are counted, not rejected\",\n"
           << "  \"fanout_definition\": \"touched_partitions and fanout count partitions containing eligible objects, including exact paths; blank/null for nonpartition arms\",\n"
           << "  \"validation\": \"each measured scope equals an independent direct-edge/direct-label reference; returned IDs are eligible and distinct; result size is bounded by min(k,eligible)\",\n"
           << "  \"query_csv\": \"ann_queries.csv\",\n  \"argv\": [";
    for (int i = 0; i < argc; ++i) {
        if (i) report << ',';
        write_json_string(report, argv[i]);
    }
    report << "],\n  \"selected_scope_rows\": [";
    for (size_t i = 0; i < selected.size(); ++i) report << (i ? "," : "") << selected[i];
    report << "],\n  \"strategies\": {";
    for (size_t s = 0; s < strategy_count; ++s) {
        report << (s ? "," : "") << "\n    \"" << names[s] << "\": ";
        write_stats(report, total[s], s == 3);
    }
    report << "\n  },\n  \"passes\": [";
    for (size_t pass = 0; pass < repeats; ++pass) {
        report << (pass ? "," : "") << "\n    {\"pass\": " << pass << ", \"strategies\": {";
        for (size_t s = 0; s < strategy_count; ++s) {
            report << (s ? "," : "") << "\"" << names[s] << "\": ";
            write_stats(report, passes[pass][s], s == 3);
        }
        report << "}}";
    }
    report << "\n  ],\n  \"limits\": ["
           << "\"vectors and direct labels may have no semantic correlation\", "
           << "\"resident geometry reuse, no cold-start or independent rebuilds\"]\n}\n";
    std::ofstream handle(out + "/ann.json");
    if (!handle || !(handle << report.str())) fail("failed writing ann.json");
    std::cout << report.str();
    return 0;
}
