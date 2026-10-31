// Frozen neighborhood experiment runner: no replay updates or GT in search.
#include "neighborhood_index.h"
#include <iomanip>
#include <iostream>
#include <set>
#include <sys/resource.h>

namespace {
using namespace npi;
struct Args {
    std::string data, queries, out, arm;
    size_t ef = 64, e1_centers = 0;
    bool self_check = false;
};
Args arguments(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--self-check") { a.self_check = true; continue; }
        if (++i == argc) throw std::runtime_error("missing argument value");
        const std::string value = argv[i];
        if (key == "--data") a.data = value;
        else if (key == "--queries") a.queries = value;
        else if (key == "--out") a.out = value;
        else if (key == "--arm") a.arm = value;
        else if (key == "--ef") a.ef = std::stoull(value);
        else if (key == "--e1-centers") a.e1_centers = std::stoull(value);
        else throw std::runtime_error("unknown option: " + key);
    }
    if (!a.self_check && (a.data.empty() || a.queries.empty() || a.out.empty() ||
                         (a.arm != "B" && a.arm != "C") || !a.ef))
        throw std::runtime_error("--data DIR --queries QTRACE --arm B|C --ef N --out PREFIX [--e1-centers 8]");
    return a;
}
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void self_check() {
    Dataset d;
    d.n = 521; d.dim = 2; d.nq = 1;
    d.parent = {-1, 0, 0, 1}; d.rank_tree();
    d.base.resize(d.n * d.dim); d.tests = {0.2f, 0.8f};
    d.items.resize(d.n);
    for (Id i = 0; i < d.n; ++i) {
        d.items[i] = {i % 4, int64_t(i % 3) - 2, int64_t(i % 17) - 8,
                      i % 11 == 0 ? 0 : uint64_t(1) << (i % 64), bool(i % 3)};
        d.base[2 * i] = float(i % 29); d.base[2 * i + 1] = float(i % 13);
    }
    d.items[1].numeric = INT64_MIN; d.items[520].numeric = INT64_MAX;
    // Independent scalar/page checks, including last partial page/sparse live IDs.
    Qualifier qualification(d);
    std::vector<Query> cases;
    for (int combine : {0, 1}) for (int tags = 0; tags <= 4; ++tags) {
        Query q; q.recursive = 1; q.anchor = 0; q.category = 0;
        q.lo = -1; q.hi = 3; q.tagmode = tags;
        q.tags = tags % 2 ? 0 : uint64_t(1) << 63; q.combine = combine;
        cases.push_back(q);
    }
    Query q; q.recursive = 1; q.anchor = 1; cases.push_back(q);
    q.lo = q.hi = INT64_MIN; cases.push_back(q);
    q.anchor = 0; q.lo = q.hi = INT64_MAX; cases.push_back(q);
    uint64_t eligibility_checks = 0;
    for (const auto& query : cases) {
        QualStats st; qualification.begin(query, st);
        for (Id i = 0; i < d.n; ++i) {
            const bool expected = d.eligible(i, query);
            const auto& mask = qualification.page(i / 256);
            require(bool((mask[(i % 256) / 64] >> (i % 64)) & 1) == expected, "page/canonical mismatch");
            require(qualification.point(i) == expected, "point/canonical mismatch");
            ++eligibility_checks;
        }
    }
    NeighborhoodIndex index(d, true);
    uint64_t centers_checked = 0;
    for (const auto& query : cases) {
        Work bwork, cwork;
        std::vector<Internal> btrace, ctrace;
        const auto b = index.search(query, false, 64, bwork, &btrace);
        const auto c = index.search(query, true, 64, cwork, &ctrace);
        require(b == c && btrace == ctrace && bwork.distance_calls == cwork.distance_calls,
                "B/C result, path or distance mismatch");
        if (btrace.size() > 8) btrace.resize(8);
        Work bw, cw;
        std::vector<std::vector<Id>> bs, cs;
        index.candidate_replay(query, btrace, false, bw, bs);
        index.candidate_replay(query, btrace, true, cw, cs);
        require(bs == cs, "B/C qualified two-hop set mismatch");
        centers_checked += btrace.size();
    }
    std::cout << "{\"eligibility_checks\":" << eligibility_checks
              << ",\"candidate_centers_checked\":" << centers_checked
              << ",\"B_C_result_path_distance_identical\":true,\"errors\":0}\n";
}
std::string ids(const std::vector<Neighbor>& input) {
    std::string s;
    for (const auto& p : input) { if (!s.empty()) s += ';'; s += std::to_string(p.second); }
    return s;
}
void work_header(std::ostream& o) {
    o << "onehop_ids,twohop_ids,page_containers,sparse_offsets,dense_words,empty_pages,duplicate_checks,"
         "eligible_expansions,pages_prepared,point_tests,point_cache_hits,owner_tests,owner_cache_hits,"
         "mask_words,visited_checks,expanded,qualification_us,neighborhood_us";
}
void work_values(std::ostream& o, const Work& w) {
    o << w.onehop_ids << ',' << w.twohop_ids << ',' << w.page_containers << ',' << w.sparse_offsets << ','
      << w.dense_words << ',' << w.empty_pages << ',' << w.duplicate_checks << ',' << w.eligible_expansions << ','
      << w.qualification.pages_prepared << ',' << w.qualification.point_tests << ',' << w.qualification.point_cache_hits << ','
      << w.qualification.owner_tests << ',' << w.qualification.owner_cache_hits << ',' << w.qualification.mask_words << ','
      << w.visited_checks << ',' << w.expanded << ',' << w.qualification_us << ',' << w.neighborhood_us;
}
void e1(const Args& args, Dataset& data, const std::vector<Query>& queries, NeighborhoodIndex& index) {
    std::ofstream bout(args.out + ".B.csv"), cout(args.out + ".C.csv");
    require(bool(bout) && bool(cout), "cannot open E1 outputs");
    for (std::ostream* stream : {static_cast<std::ostream*>(&bout), static_cast<std::ostream*>(&cout)}) {
        *stream << std::setprecision(12) << "query_index,qidx,center_count,acquire_ns,qual_us,";
        work_header(*stream); *stream << '\n';
    }
    uint64_t total_centers = 0;
    for (size_t i = 0; i < queries.size(); ++i) {
        const auto& q = queries[i];
        Work trace_work; std::vector<Internal> centers;
        index.search(q, false, args.ef, trace_work, &centers);
        if (centers.size() > args.e1_centers) centers.resize(args.e1_centers);
        Work bw, cw; std::vector<std::vector<Id>> b, c;
        double bus, cus;
        auto run = [&](bool paged, Work& work, std::vector<std::vector<Id>>& values) {
            const auto start = Clock::now(); index.candidate_replay(q, centers, paged, work, values);
            return micros(start);
        };
        // Alternate acquisition order; both caches cold at begin and identical centers.
        if (i % 2 == 0) { bus = run(false, bw, b); cus = run(true, cw, c); }
        else { cus = run(true, cw, c); bus = run(false, bw, b); }
        require(b == c, "E1 candidate set/order mismatch");
        total_centers += centers.size();
        bout << i << ',' << q.qidx << ',' << centers.size() << ',' << bus * 1000 << ',' << bw.qualification_us << ',';
        work_values(bout, bw); bout << '\n';
        cout << i << ',' << q.qidx << ',' << centers.size() << ',' << cus * 1000 << ',' << cw.qualification_us << ',';
        work_values(cout, cw); cout << '\n';
    }
    require(bool(bout) && bool(cout), "E1 CSV write failure");
    std::ofstream summary(args.out + ".summary.json");
    summary << "{\"complete\":true,\"queries\":" << queries.size() << ",\"candidate_centers_checked\":" << total_centers
            << ",\"candidate_set_order_mismatches\":0,\"graph_sha256\":\"" << index.graph_sha256()
            << "\",\"initial_n\":" << data.n << ",\"not_e2e\":true}\n";
    require(bool(summary), "E1 summary write failure");
    std::cout << "E1 candidate centers checked=" << total_centers << " mismatches=0\n";
}
int run(const Args& args) {
    Dataset data = Dataset::load(args.data);
    const auto queries = load_queries(args.queries, data);
    const auto bstart = Clock::now();
    NeighborhoodIndex index(data, args.arm == "C" || args.e1_centers != 0);
    const double build_s = micros(bstart) / 1e6;
    if (args.e1_centers) { e1(args, data, queries, index); return 0; }
    const auto graph_sha = index.graph_sha256();
    std::ofstream out(args.out + ".queries.csv");
    require(bool(out), "cannot open query output");
    out << std::setprecision(12)
        << "query_index,qidx,gt_count,hit_count,recall,invalid,duplicate,state_errors,e2e_us,distance_calls,";
    work_header(out);
    out << ",search_us,gt_ids,result_ids\n";
    uint64_t total_gt = 0, total_hit = 0;
    double macro = 0;
    for (size_t i = 0; i < queries.size(); ++i) {
        const auto& q = queries[i];
        Work work;
        const auto start = Clock::now();
        const auto hits = index.search(q, args.arm == "C", args.ef, work);
        const double e2e = micros(start);
        const auto gt = index.ground_truth(q); // strictly after the measured search
        std::set<Id> exact, seen;
        for (const auto& p : gt) exact.insert(p.second);
        size_t hit = 0, invalid = 0, duplicate = 0;
        for (const auto& p : hits) {
            hit += exact.count(p.second);
            invalid += !data.eligible(p.second, q);
            duplicate += !seen.insert(p.second).second;
        }
        const double recall = gt.empty() ? double(hits.empty()) : double(hit) / gt.size();
        out << i << ',' << q.qidx << ',' << gt.size() << ',' << hit << ',' << recall << ','
            << invalid << ',' << duplicate << ',' << invalid + duplicate << ',' << e2e << ',' << work.distance_calls << ',';
        work_values(out, work);
        out << ',' << std::max(0., e2e - work.neighborhood_us) << ',' << ids(gt) << ',' << ids(hits) << '\n';
        require(!invalid && !duplicate, "search result legality violation");
        total_gt += gt.size(); total_hit += hit; macro += recall;
    }
    out.close(); require(bool(out), "query output write failure");
    rusage ru{}; require(getrusage(RUSAGE_SELF, &ru) == 0, "getrusage failed");
    std::ofstream summary(args.out + ".summary.json");
    summary << std::setprecision(12) << "{\"complete\":true,\"n\":" << data.n
        << ",\"n_live\":" << index.n_live() << ",\"dim\":" << data.dim << ",\"n_queries\":" << queries.size()
        << ",\"macro_recall\":" << (queries.empty() ? 1 : macro / queries.size())
        << ",\"pair_recall\":" << (total_gt ? double(total_hit) / total_gt : 1)
        << ",\"invalid\":0,\"duplicate\":0,\"state_errors\":0,\"build_s\":" << build_s
        << ",\"graph_build_s\":" << index.graph_build_s << ",\"neighborhood_build_s\":" << index.neighborhood_build_s
        << ",\"build_graph_distance_calls\":" << index.graph_build_distances
        << ",\"neighborhood_bytes\":" << index.neighborhood_bytes() << ",\"graph_bytes\":" << index.graph_bytes()
        << ",\"qualifier_workspace_mapping_bytes\":" << index.workspace_bytes()
        << ",\"peak_rss_bytes\":" << uint64_t(ru.ru_maxrss) * 1024
        << ",\"graph_sha256\":\"" << graph_sha << "\",\"graph_hash_kind\":\"canonical_payload_and_ordered_all_layer_adjacency\""
        << ",\"object_vectors\":\"single_HNSW_payload_input_array_released\",\"qualification_us_scope\":\"twohop_subset_onehop_predicate_in_search_us\"}\n";
    require(bool(summary), "summary write failure");
    std::cout << args.arm << " queries=" << queries.size() << " pair_recall="
              << (total_gt ? double(total_hit) / total_gt : 1) << '\n';
    return 0;
}
} // namespace
int main(int argc, char** argv) {
    try {
        const Args a = arguments(argc, argv);
        if (a.self_check) { self_check(); return 0; }
        return run(a);
    } catch (const std::exception& e) { std::cerr << "neighborhood_research: " << e.what() << '\n'; return 1; }
}
