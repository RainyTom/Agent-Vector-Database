#include "closure_index.h"
#include "raw_l2_space.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <sys/resource.h>
#include <unordered_set>

namespace {
using npi::Id;
using npi::Item;
using npi::Query;
using closure::Neighbor;
using Clock = std::chrono::steady_clock;
constexpr const char* query_columns =
    "event,version,query_index,qidx,recursive,anchor,anchor_depth,scope_card,eligible_card,dir_us,search_us,e2e_us,gt_us,gt_count,hit_count,recall,distance_calls,ann_hops,local_indexes,valid_count,invalid_count,duplicate_count,gt_ids,result_ids,fanout,scan_nodes";
[[noreturn]] void fail(const std::string& s) { throw std::runtime_error(s); }
void require(bool condition, const std::string& s) { if (!condition) fail(s); }
double elapsed(Clock::time_point t) { return std::chrono::duration<double, std::micro>(Clock::now() - t).count(); }
template<class T> T number(const std::string& s) {
    T n{};
    const auto r = std::from_chars(s.data(), s.data() + s.size(), n);
    require(!s.empty() && r.ec == std::errc() && r.ptr == s.data() + s.size(), "invalid integer: " + s);
    return n;
}
template<class... T> void fields(std::istream& in, T&... values) {
    ((values = npi::filter_detail::integer<std::remove_reference_t<T>>(in, "trace")), ...);
    npi::filter_detail::end(in, "trace");
}
std::vector<std::string> split(std::string s, char sep) {
    std::vector<std::string> v;
    size_t begin = 0;
    for (;;) {
        auto end = s.find(sep, begin);
        v.push_back(s.substr(begin, end == std::string::npos ? end : end - begin));
        if (end == std::string::npos) return v;
        begin = end + 1;
    }
}
std::string ids(const std::vector<Id>& v) {
    std::ostringstream s;
    for (size_t i = 0; i < v.size(); ++i) { if (i) s << ';'; s << v[i]; }
    return s.str();
}
std::vector<Id> labels(const std::vector<Neighbor>& v) {
    std::vector<Id> out; out.reserve(v.size());
    for (auto p : v) out.push_back(p.second);
    return out;
}
std::string json(const std::string& s) {
    std::ostringstream out; out << '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') out << '\\' << char(c);
        else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
        else out << char(c);
    }
    out << '"'; return out.str();
}
struct Args {
    std::string data, trace, out, arm = "H", gt;
    size_t ef = 100, k = 10, pool = 64, audit_count = 8;
    bool oracle = false, self = false, audit = false;
    closure::Mode mode() const {
        if (arm == "U") return closure::Mode::Unpruned;
        if (arm == "G") return closure::Mode::Geometric;
        if (arm == "R") return closure::Mode::Interval;
        return closure::Mode::Hierarchy;
    }
};
void parse(Args& a, int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string flag = argv[i];
        if (flag == "--self-check") { a.self = true; continue; }
        if (flag == "--oracle-only") { a.oracle = true; continue; }
        if (flag == "--audit-only") { a.audit = true; continue; }
        require(i + 1 < argc, "missing value for " + flag);
        std::string value = argv[++i];
        if (flag == "--data") a.data = value;
        else if (flag == "--trace") a.trace = value;
        else if (flag == "--out") a.out = value;
        else if (flag == "--arm") a.arm = value;
        else if (flag == "--gt-in") a.gt = value;
        else if (flag == "--ef") a.ef = number<size_t>(value);
        else if (flag == "--k") a.k = number<size_t>(value);
        else if (flag == "--pool-size") a.pool = number<size_t>(value);
        else if (flag == "--audit-count") a.audit_count = number<size_t>(value);
        else fail("unknown option " + flag);
    }
    require(a.arm == "U" || a.arm == "G" || a.arm == "R" || a.arm == "H" || a.arm == "H0", "invalid arm");
    require(a.k && a.ef >= a.k && a.pool && a.pool <= 64 && a.audit_count, "require ef >= k > 0, pool in [1,64], audit-count > 0");
    require(!(a.oracle && a.audit) && (!(a.oracle || a.audit) || a.gt.empty()), "oracle/audit/gt-in options conflict");
    require(a.self || (!a.data.empty() && !a.trace.empty() && !a.out.empty()), "require --data --trace --out (or --self-check)");
    require(a.gt.empty() || a.gt != a.out + ".queries.csv", "GT input must not be overwritten by output");
}
struct Event {
    char op = 0;
    Query q;
    Id id = 0, dst = 0;
    int64_t category = 0, numeric = 0;
    uint64_t tags = 0;
};
Event parse_event(const std::string& line) {
    std::istringstream in(line);
    std::string op; in >> op;
    require(op.size() == 1, "missing/invalid event operation");
    Event e; e.op = op[0];
    switch (e.op) {
    case 'Q': fields(in, e.q.qidx, e.q.recursive, e.q.anchor, e.q.category, e.q.lo, e.q.hi, e.q.tagmode, e.q.tags, e.q.combine); break;
    case 'V': fields(in, e.id, e.category, e.numeric); break;
    case 'T': fields(in, e.id, e.tags); break;
    case 'M': case 'B': case 'R': fields(in, e.id, e.dst); break;
    case 'D': fields(in, e.id); break;
    case 'I': fields(in, e.id, e.dst, e.category, e.numeric, e.tags); break;
    default: fail("unknown event operation " + op);
    }
    return e;
}
// Independent canonical metadata: never reads Index state or its rank/cache structures.
struct Canonical {
    std::vector<Item> items;
    std::vector<int> parent;
    size_t nq;
    explicit Canonical(const npi::Dataset& d) : items(d.items), parent(d.parent), nq(d.nq) {}
    void node(Id n) const { require(n < parent.size(), "directory ID out of range"); }
    void item(Id id, bool alive) const {
        require(id < items.size() && items[id].alive == alive, "item ID/liveness violation");
    }
    bool beneath(Id n, Id anchor) const {
        node(n); node(anchor);
        for (size_t hops = 0; hops < parent.size(); ++hops) {
            if (n == anchor) return true;
            if (parent[n] == -1) return false;
            require(parent[n] >= 0 && size_t(parent[n]) < parent.size(), "invalid canonical parent");
            n = Id(parent[n]);
        }
        fail("canonical parent cycle");
    }
    size_t depth(Id n) const {
        node(n); size_t depth = 0;
        while (parent[n] != -1) {
            require(++depth < parent.size(), "canonical parent cycle");
            n = Id(parent[n]); node(n);
        }
        return depth;
    }
    Id root() const {
        for (size_t i = 0; i < parent.size(); ++i) if (parent[i] == -1) return Id(i);
        fail("missing root");
    }
    bool scope(Id id, const Query& q) const {
        return id < items.size() && items[id].alive &&
            (q.recursive ? beneath(items[id].owner, q.anchor) : items[id].owner == q.anchor);
    }
    bool eligible(Id id, const Query& q) const { return scope(id, q) && npi::filter_detail::extra(items[id], q); }
    void validate(const Event& e) const {
        if (e.op == 'Q') {
            const auto& q = e.q; node(q.anchor);
            require(q.qidx < nq && (q.recursive == 0 || q.recursive == 1) && q.tagmode >= 0 && q.tagmode <= 4 &&
                    (q.combine == 0 || q.combine == 1) && ((q.lo == -1 && q.hi == -1) || q.lo <= q.hi), "invalid query fields");
        } else if (e.op == 'B' || e.op == 'R') {
            node(e.id); node(e.dst);
            require(!beneath(e.dst, e.id), "B/R destination inside source subtree");
            if (e.op == 'R') require(parent[e.id] != -1, "cannot reparent root");
        } else {
            item(e.id, e.op != 'I');
            if (e.op == 'I' || e.op == 'M') node(e.dst);
        }
    }
    void apply(const Event& e) {
        validate(e);
        switch (e.op) {
        case 'V': items[e.id].category = e.category; items[e.id].numeric = e.numeric; break;
        case 'T': items[e.id].tags = e.tags; break;
        case 'M': items[e.id].owner = e.dst; break;
        case 'B': for (auto& v : items) if (v.alive && beneath(v.owner, e.id)) v.owner = e.dst; break;
        case 'R': parent[e.id] = int(e.dst); break;
        case 'D': items[e.id].alive = false; break;
        case 'I': items[e.id] = Item{e.dst, e.category, e.numeric, e.tags, true}; break;
        default: fail("not an update");
        }
    }
    std::pair<size_t, size_t> counts(const Query& q) const {
        size_t scope_count = 0, eligible_count = 0;
        for (size_t i = 0; i < items.size(); ++i) if (scope(Id(i), q)) {
            ++scope_count; eligible_count += npi::filter_detail::extra(items[i], q);
        }
        return {scope_count, eligible_count};
    }
    std::vector<Neighbor> exact(const npi::Dataset& d, const Query& q, size_t k, uint64_t& calls) const {
        RawL2Space l2(d.dim);
        std::vector<Neighbor> result;
        for (size_t i = 0; i < items.size(); ++i) if (eligible(Id(i), q)) {
            result.emplace_back(l2.dist(d.tests.data() + q.qidx * d.dim, d.base.data() + i * d.dim), Id(i));
            ++calls;
        }
        if (result.size() > k) {
            std::nth_element(result.begin(), result.begin() + k, result.end()); result.resize(k);
        }
        std::sort(result.begin(), result.end()); return result;
    }
};
void apply(closure::Index& index, const Event& e, closure::UpdateWork& w) {
    switch (e.op) {
    case 'V': index.apply_value(e.id, e.category, e.numeric, w); break;
    case 'T': index.apply_tags(e.id, e.tags, w); break;
    case 'M': index.apply_move(e.id, e.dst, w); break;
    case 'B': index.apply_batch(e.id, e.dst, w); break;
    case 'R': index.apply_reparent(e.id, e.dst, w); break;
    case 'D': index.apply_delete(e.id, w); break;
    case 'I': index.apply_insert(e.id, e.dst, e.category, e.numeric, e.tags, w); break;
    default: fail("not an index update");
    }
}
class GtTape {
    std::ifstream in;
    bool calls = false;
public:
    explicit GtTape(const std::string& path) : in(path) {
        require(bool(in), "cannot open GT tape " + path);
        std::string header; require(bool(std::getline(in, header)), "missing GT header");
        if (!header.empty() && header.back() == '\r') header.pop_back();
        calls = header == std::string(query_columns) + ",gt_distance_calls";
        require(header == query_columns || calls, "invalid GT tape schema");
    }
    std::vector<Id> next(size_t event, size_t version, size_t query_index, const Query& q,
                         const Canonical& shadow, size_t k,
                         size_t scope, size_t eligible) {
        std::string line; require(bool(std::getline(in, line)), "GT tape missing query row");
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto r = split(line, ',');
        require(r.size() == (calls ? 27 : 26), "GT tape column count mismatch");
        for (size_t i = 0; i < r.size(); ++i) {
            if (i == 22 || i == 23) continue;
            if ((i >= 9 && i <= 12) || i == 15) {
                size_t used = 0; double x = std::stod(r[i], &used);
                require(used == r[i].size() && std::isfinite(x) && x >= 0 && (i != 15 || x <= 1), "invalid GT diagnostic");
            } else (void)number<uint64_t>(r[i]);
        }
        uint64_t expected[] = {event, version, query_index, q.qidx, uint64_t(q.recursive), q.anchor,
                               shadow.depth(q.anchor), scope, eligible};
        for (size_t i = 0; i < 9; ++i) require(number<uint64_t>(r[i]) == expected[i], "GT identity/state/order mismatch column " + std::to_string(i));
        const size_t count = std::min(k, eligible);
        require(number<uint64_t>(r[13]) == count && !number<uint64_t>(r[20]) && !number<uint64_t>(r[21]), "GT counts invalid");
        if (calls) require(number<uint64_t>(r[26]) == eligible, "GT inline distance count invalid");
        auto parse_ids = [&](const std::string& s) {
            std::vector<Id> result; std::unordered_set<Id> seen;
            if (!s.empty()) for (const auto& token : split(s, ';')) {
                Id id = number<Id>(token);
                require(shadow.eligible(id, q) && seen.insert(id).second, "GT ID invalid, duplicate or ineligible");
                result.push_back(id);
            }
            return result;
        };
        auto gt = parse_ids(r[22]), result = parse_ids(r[23]);
        require(gt.size() == count && result.size() <= count && number<uint64_t>(r[19]) == result.size(), "GT/result IDs count mismatch");
        std::unordered_set<Id> wanted(gt.begin(), gt.end());
        size_t hits = 0; for (Id id : result) hits += wanted.count(id);
        double recall = count ? double(hits) / count : 1;
        require(number<uint64_t>(r[14]) == hits && std::abs(std::stod(r[15]) - recall) <= 1e-9, "GT hit/recall mismatch");
        // Ranking is trusted from the separately generated immutable oracle tape.
        // No vector reads or distance checks here: they would warm timed replay.
        return gt;
    }
    void finish() {
        std::string line; require(!std::getline(in, line), "GT tape extra query row");
        require(in.eof(), "GT tape read failure");
    }
};
double percentile(std::vector<double> values, double p) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    double pos = p * (values.size() - 1); size_t i = size_t(pos), j = std::min(i + 1, values.size() - 1);
    return values[i] + (values[j] - values[i]) * (pos - i);
}
struct Runner {
    Args args;
    npi::Dataset data;
    Canonical shadow;
    std::unique_ptr<closure::Index> index;
    std::unique_ptr<GtTape> tape;
    std::ofstream qout, uout, aout;
    size_t event_number = 0, version = 0, queries = 0, updates = 0, audit_rows = 0;
    uint64_t total_gt = 0, total_hits = 0, nonempty = 0, invalid = 0, duplicates = 0, state_errors = 0;
    uint64_t gt_calls = 0, distance_calls = 0, update_geometry_calls = 0, compactions = 0;
    double recall_sum = 0, nonempty_recall_sum = 0, query_us = 0, update_us = 0, validation_us = 0;
    std::vector<double> query_times, update_times;
    explicit Runner(const Args& a) : args(a), data(npi::Dataset::load(a.data)), shadow(data) {
        qout.open(a.out + ".queries.csv"); uout.open(a.out + ".updates.csv");
        require(bool(qout) && bool(uout), "cannot open output CSV");
        qout << std::setprecision(17) << query_columns;
        if (a.oracle) qout << ",gt_distance_calls\n";
        else qout << ",gt_distance_calls,expanded,visited_checks,pool_candidates,scope_rejected,pruned,retained,witness_tests,hierarchy_steps,backbone_ids,qualification_tests,compile_us,state_errors,validation_us\n";
        uout << std::setprecision(17) << "event,version,op,latency_us,geometry_calls,pools_rebuilt,graph_insertions,graph_deletions,compactions,metadata_writes,affected_items,rebuild_us,validation_us\n";
        if (a.audit) {
            aout.open(a.out + ".audit.csv"); require(bool(aout), "cannot open audit CSV");
            aout << "event,qidx,source,scope_anchor,local_H_edges,restricted_root_H_edges,G_edges,R_edges,H0_edges,U_edges,H_vs_R_diff,H_vs_G_diff,H_vs_H0_diff,heredity_mismatch,heredity_applicable\n";
        }
        if (!a.gt.empty()) tape = std::make_unique<GtTape>(a.gt);
        if (!a.oracle) index = std::make_unique<closure::Index>(data, a.pool);
    }
    void audit(const Query& q) {
        require(index->scope_size(q) == shadow.counts(q).first, "offline scope cardinality mismatch");
        for (size_t i = 0; i < shadow.items.size(); ++i)
            require(index->in_scope(Id(i), q) == shadow.scope(Id(i), q), "offline scope membership mismatch");
        Query root = q; root.anchor = shadow.root(); root.recursive = 1;
        size_t selected = 0;
        for (size_t i = 0; i < shadow.items.size() && selected < args.audit_count; ++i) {
            Id source = Id(i); if (!shadow.scope(source, q)) continue; ++selected;
            auto h = index->neighbors_for_test(source, q, closure::Mode::Hierarchy);
            auto restricted = index->neighbors_for_test(source, root, closure::Mode::Hierarchy);
            restricted.erase(std::remove_if(restricted.begin(), restricted.end(), [&](Id id) { return !shadow.scope(id, q); }), restricted.end());
            auto g = index->neighbors_for_test(source, q, closure::Mode::Geometric);
            auto r = index->neighbors_for_test(source, q, closure::Mode::Interval);
            auto h0 = index->neighbors_for_test(source, q, closure::Mode::Hierarchy, false);
            auto u = index->neighbors_for_test(source, q, closure::Mode::Unpruned);
            bool mismatch = h != restricted;
            aout << event_number << ',' << q.qidx << ',' << source << ',' << q.anchor << ',' << ids(h) << ',' << ids(restricted)
                 << ',' << ids(g) << ',' << ids(r) << ',' << ids(h0) << ',' << ids(u) << ',' << (h != r) << ',' << (h != g) << ',' << (h != h0) << ',' << mismatch << ',' << q.recursive << '\n';
            ++audit_rows; state_errors += q.recursive && mismatch;
            require(!q.recursive || !mismatch, "hierarchy heredity mismatch in offline audit");
        }
        ++queries;
    }
    void query(const Query& q) {
        if (args.audit) { audit(q); return; }
        closure::Work work;
        std::vector<Neighbor> result;
        size_t scope = 0;
        double dir_us = 0, search_us = 0, e2e_us = 0;
        if (index) {
            auto start = Clock::now(); scope = index->scope_size(q); dir_us = elapsed(start);
            auto search_start = Clock::now();
            result = index->search(q, args.mode(), args.ef, args.k, work, args.arm != "H0");
            search_us = elapsed(search_start); e2e_us = elapsed(start);
        }
        // Tape I/O, oracle scans, result validation and state checks are all outside e2e.
        auto validation_start = Clock::now();
        auto counts = shadow.counts(q);
        auto gt_start = Clock::now();
        uint64_t calls = 0;
        std::vector<Id> gt;
        if (tape) gt = tape->next(event_number, version, queries, q, shadow, args.k, counts.first, counts.second);
        else gt = labels(shadow.exact(data, q, args.k, calls));
        double gt_us = elapsed(gt_start);
        if (args.oracle) { scope = counts.first; result.clear(); for (Id id : gt) result.emplace_back(0, id); }
        size_t bad = 0, dup = 0, valid = 0, errors = scope != counts.first, hits = 0;
        std::unordered_set<Id> seen, wanted(gt.begin(), gt.end());
        for (auto p : result) {
            bool ok = shadow.eligible(p.second, q); valid += ok; bad += !ok;
            dup += !seen.insert(p.second).second; hits += wanted.count(p.second);
        }
        errors += bad + dup + (result.size() > gt.size());
        double recall = gt.empty() ? (result.empty() ? 1 : 0) : double(hits) / gt.size();
        double checking_us = elapsed(validation_start);
        qout << event_number << ',' << version << ',' << queries << ',' << q.qidx << ',' << q.recursive << ',' << q.anchor << ','
             << shadow.depth(q.anchor) << ',' << scope << ',' << counts.second << ',' << dir_us << ',' << search_us << ',' << e2e_us << ','
             << gt_us << ',' << gt.size() << ',' << hits << ',' << recall << ',' << work.distance_calls << ',' << work.expanded << ",0,"
             << valid << ',' << bad << ',' << dup << ',' << ids(gt) << ',' << ids(labels(result)) << ',' << size_t(scope != 0) << ",0," << calls;
        if (!args.oracle) qout << ',' << work.expanded << ',' << work.visited_checks << ',' << work.pool_candidates << ',' << work.scope_rejected
             << ',' << work.pruned << ',' << work.retained << ',' << work.witness_tests << ',' << work.hierarchy_steps << ',' << work.backbone_ids
             << ',' << work.qualification_tests << ',' << work.compile_us << ',' << errors << ',' << checking_us;
        qout << '\n'; qout.flush(); require(bool(qout), "query CSV write failed");
        ++queries; query_times.push_back(e2e_us); query_us += e2e_us; validation_us += checking_us;
        total_gt += gt.size(); total_hits += hits; recall_sum += recall;
        if (!gt.empty()) { ++nonempty; nonempty_recall_sum += recall; }
        invalid += bad; duplicates += dup; state_errors += errors; gt_calls += calls; distance_calls += work.distance_calls;
        require(!errors, "query violates independent canonical state");
    }
    void update(const Event& e) {
        closure::UpdateWork work;
        bool metadata_only = e.op == 'V' || e.op == 'T' || e.op == 'M' || e.op == 'B' || e.op == 'R';
        uint64_t before = args.audit && metadata_only ? index->pool_fingerprint() : 0;
        auto start = Clock::now();
        if (index) apply(*index, e, work);
        double latency = elapsed(start);
        auto validation_start = Clock::now(); shadow.apply(e);
        uint64_t after = args.audit && metadata_only ? index->pool_fingerprint() : 0;
        size_t errors = metadata_only && (work.geometry_calls != 0 || before != after);
        double checking_us = elapsed(validation_start);
        ++version; ++updates; state_errors += errors; update_geometry_calls += work.geometry_calls; compactions += work.compactions;
        update_times.push_back(latency); update_us += latency; validation_us += checking_us;
        uout << event_number << ',' << version << ',' << e.op << ',' << latency << ',' << work.geometry_calls << ',' << work.pools_rebuilt << ','
             << work.graph_insertions << ',' << work.graph_deletions << ',' << work.compactions << ',' << work.metadata_writes << ','
             << work.affected_items << ',' << work.rebuild_us << ',' << checking_us << '\n';
        uout.flush(); require(bool(uout), "update CSV write failed");
        require(!errors, "metadata-only update changed geometry/pools");
    }
    void run() {
        std::ifstream in(args.trace); require(bool(in), "cannot open trace");
        std::string line; require(bool(std::getline(in, line)), "missing event count");
        std::istringstream header(line); size_t count; fields(header, count);
        for (size_t i = 0; i < count; ++i) {
            event_number = i + 1;
            require(bool(std::getline(in, line)), "trace shorter than event count");
            Event e = parse_event(line); shadow.validate(e);
            if (e.op == 'Q') query(e.q); else update(e);
        }
        require(!std::getline(in, line), "trace has extra event lines"); require(in.eof(), "trace read failure");
        if (tape) tape->finish();
        qout.flush(); uout.flush(); if (aout.is_open()) aout.flush();
        require(bool(qout) && bool(uout) && (!aout.is_open() || bool(aout)), "output write failure");
    }
    void summary(bool complete, const std::string& error = "") const {
        std::ofstream out(args.out + ".summary.json"); require(bool(out), "cannot open summary");
        rusage usage{}; require(getrusage(RUSAGE_SELF, &usage) == 0, "getrusage failed");
        closure::BuildStats build; if (index) build = index->build_stats();
        out << std::setprecision(17) << "{\n";
        auto num = [&](const char* key, auto value) { out << "  " << json(key) << ": " << value << ",\n"; };
        auto text = [&](const char* key, const std::string& value) { out << "  " << json(key) << ": " << json(value) << ",\n"; };
        text("arm", args.arm); num("ef", args.ef); num("k", args.k); num("pool_size", args.pool);
        num("oracle_only", args.oracle ? "true" : "false"); num("audit_only", args.audit ? "true" : "false"); num("audit_rows", audit_rows);
        text("gt_mode", args.oracle ? "oracle_only" : args.audit ? "offline_audit" : tape ? "precomputed" : "inline_debug");
        num("n_vectors", data.n); num("n_nodes", shadow.parent.size()); num("dim", data.dim);
        num("n_queries", queries); num("n_updates", updates); num("version", version); num("last_event", event_number);
        num("nonempty_queries", nonempty); num("empty_gt_queries", args.audit ? 0 : queries - nonempty);
        num("recall_macro", queries ? recall_sum / queries : 0); num("recall_nonempty_macro", nonempty ? nonempty_recall_sum / nonempty : 0);
        num("recall_pair", total_gt ? double(total_hits) / total_gt : 0); num("total_gt", total_gt); num("total_hits", total_hits);
        num("invalid", invalid); num("duplicates", duplicates); num("state_errors", state_errors);
        num("build_s", build.total_s); num("build_graph_s", build.graph_s); num("build_pools_s", build.pools_s); num("build_geometry_calls", build.geometry_calls);
        num("distance_calls", distance_calls); num("gt_distance_calls", gt_calls);
        num("update_geometry_calls", update_geometry_calls); num("compaction_count", compactions);
        num("query_total_us", query_us); num("update_total_us", update_us); num("total_workload_us", query_us + update_us);
        num("validation_total_us", validation_us);
        text("gt_tape_trust", "precomputed tape identity, metadata, IDs, counts and diagnostics checked; ranking/exactness trusted from standalone canonical oracle; no GT vector reads in measured replay");
        text("timing_policy", "query e2e includes scope_size and search only; tape I/O, canonical scans, GT, membership/result validation, pool fingerprint and CSV I/O excluded; update latency includes Index apply only; oracle/audit runs are not performance samples");
        num("query_p50_us", percentile(query_times, .5)); num("query_p95_us", percentile(query_times, .95)); num("query_p99_us", percentile(query_times, .99));
        num("update_p50_us", percentile(update_times, .5)); num("update_p95_us", percentile(update_times, .95)); num("update_p99_us", percentile(update_times, .99));
        num("pool_fingerprint", index ? index->pool_fingerprint() : 0); text("pool_fingerprint_kind", "noncryptographic_state_identity_not_SHA");
        num("index_bytes", index ? index->index_bytes() : 0); text("index_bytes_kind", "core_structure_capacity_estimate_not_RSS");
        num("peak_rss_bytes", uint64_t(usage.ru_maxrss) * 1024); text("error", error);
        out << "  \"complete\": " << (complete ? "true" : "false") << "\n}\n"; out.flush(); require(bool(out), "summary write failed");
    }
};

// Runtime checks use exceptions, never assert (and therefore survive NDEBUG).
void verify(closure::Index& index, const npi::Dataset& d, const Canonical& shadow) {
    const std::array<closure::Mode, 5> modes = {closure::Mode::Unpruned, closure::Mode::Geometric, closure::Mode::Interval, closure::Mode::Hierarchy, closure::Mode::Hierarchy};
    for (size_t anchor = 0; anchor < shadow.parent.size(); ++anchor) for (int recursive = 0; recursive <= 1; ++recursive) {
        for (int predicate = 0; predicate < 9; ++predicate) {
            Query q; q.anchor = Id(anchor); q.recursive = recursive; q.qidx = size_t(predicate) % d.nq;
            if (predicate == 1) { q.lo = -1; q.hi = 2; }
            if (predicate == 2) { q.lo = std::numeric_limits<int64_t>::min(); q.hi = -1; }
            if (predicate >= 3 && predicate <= 6) { q.tagmode = predicate - 2; q.tags = 3; }
            if (predicate == 7) { q.category = 2; q.tagmode = 3; q.tags = 1; q.combine = 1; }
            if (predicate == 8) { q.lo = std::numeric_limits<int64_t>::max(); q.hi = q.lo; }
            auto counts = shadow.counts(q);
            require(index.scope_size(q) == counts.first, "self-check scope cardinality mismatch");
            for (size_t i = 0; i < shadow.items.size(); ++i)
                require(index.in_scope(Id(i), q) == shadow.scope(Id(i), q), "self-check scope membership mismatch");
            uint64_t calls = 0; auto gt = shadow.exact(d, q, 10, calls);
            for (size_t mode = 0; mode < modes.size(); ++mode) {
                closure::Work work;
                auto result = index.search(q, modes[mode], d.n + 10, 10, work, mode != 4);
                require(result == gt, "self-check high-ef exact mismatch mode=" + std::to_string(mode) + " anchor=" + std::to_string(anchor));
            }
            Query root = q; root.anchor = shadow.root(); root.recursive = 1;
            // Heredity is a recursive-subtree property, not a direct-owner property.
            for (size_t i = 0; recursive && i < shadow.items.size(); ++i) if (shadow.scope(Id(i), q)) {
                auto local = index.neighbors_for_test(Id(i), q, closure::Mode::Hierarchy);
                auto global = index.neighbors_for_test(Id(i), root, closure::Mode::Hierarchy);
                global.erase(std::remove_if(global.begin(), global.end(), [&](Id id) { return !shadow.scope(id, q); }), global.end());
                require(local == global, "self-check hierarchy hereditary restriction mismatch");
            }
        }
    }
}
void checked_update(closure::Index& index, Canonical& shadow, const Event& e) {
    shadow.validate(e); auto fingerprint = index.pool_fingerprint();
    closure::UpdateWork work; apply(index, e, work); shadow.apply(e);
    if (e.op != 'D' && e.op != 'I') {
        require(!work.geometry_calls, "self-check metadata update did geometry");
        require(index.pool_fingerprint() == fingerprint, "self-check metadata update changed pools");
    }
}
void self_check() {
    // Literal -1 bounds are active whenever the other bound differs from -1.
    Item v{0, 2, -2, 1, true}; Query q; q.lo = -1; q.hi = 3;
    require(!npi::filter_detail::extra(v, q), "literal -1 lower bound treated as wildcard");
    q.lo = std::numeric_limits<int64_t>::min(); q.hi = -1;
    require(npi::filter_detail::extra(v, q), "literal -1 upper bound/int64 minimum failed");
    q.lo = q.hi = -1; q.tags = 3;
    const bool expected[] = {true, false, true, false, true};
    for (int mode = 0; mode <= 4; ++mode) { q.tagmode = mode; require(npi::filter_detail::extra(v, q) == expected[mode], "tag mode predicate failed"); }
    q.tagmode = 3; q.category = 2; q.combine = 1;
    require(npi::filter_detail::extra(v, q), "OR predicate failed");
    q.combine = 0; require(!npi::filter_detail::extra(v, q), "AND predicate failed");

    npi::Dataset tiny; tiny.n = 3; tiny.dim = 1; tiny.nq = 2;
    tiny.base = {0, 2, 1}; tiny.tests = {.25f, 1.75f}; tiny.parent = {-1, 0, 1, 1, 1, 0};
    tiny.items = {{3, 0, 0, 1, true}, {4, 0, 0, 1, true}, {2, 0, 0, 1, true}}; tiny.rank_tree();
    Canonical canonical(tiny); closure::Index small(tiny, 64);
    Query witness; witness.anchor = 0; witness.recursive = 1; witness.tagmode = 1; witness.tags = 1;
    auto edges = [&](closure::Mode mode, bool attr = true) { return small.neighbors_for_test(0, witness, mode, attr); };
    auto has = [](const std::vector<Id>& v, Id id) { return std::find(v.begin(), v.end(), id) != v.end(); };
    auto original = edges(closure::Mode::Hierarchy);
    require(!has(original, 1) && has(edges(closure::Mode::Interval), 1), "minimal x0,y2,w1 hierarchy/interval witness not active");
    checked_update(small, canonical, parse_event("R 2 5"));
    require(has(edges(closure::Mode::Hierarchy), 1), "reparent did not remove hierarchy witness");
    checked_update(small, canonical, parse_event("R 2 1"));
    require(edges(closure::Mode::Hierarchy) == original, "hierarchy witness reparent restoration failed");
    checked_update(small, canonical, parse_event("T 2 0"));
    require(has(edges(closure::Mode::Hierarchy), 1) && !has(edges(closure::Mode::Hierarchy, false), 1), "attribute-aware witness exclusion failed");
    checked_update(small, canonical, parse_event("T 2 1"));
    require(edges(closure::Mode::Hierarchy) == original, "tag restoration failed");
    verify(small, tiny, canonical);

    npi::Dataset d; d.n = 100; d.dim = 2; d.nq = 3; d.parent = {-1, 0, 1, 0, 2, 1, 3, 0};
    for (size_t i = 0; i < d.n; ++i) {
        d.base.push_back(float(i % 13)); d.base.push_back(float(i * 7 % 29));
        d.items.push_back(Item{Id(i * 5 % d.parent.size()), int64_t(i % 3), int64_t(i) - 40, uint64_t(i % 8), i < 96 && i != 3 && i != 21});
    }
    d.items[0].numeric = std::numeric_limits<int64_t>::min(); d.items[1].numeric = std::numeric_limits<int64_t>::max();
    d.tests = {0, 0, 3, 7, 12, 28}; d.rank_tree();
    Canonical shadow(d); closure::Index index(d, 64); verify(index, d, shadow);
    Query root; root.anchor = 0; root.recursive = 1;
    require(index.neighbors_for_test(0, root, closure::Mode::Unpruned).size() == 64, "64-candidate cap not exercised with >64 live objects");
    const std::vector<std::string> trace = {"V 0 -9223372036854775808 9223372036854775807", "T 1 18446744073709551615", "M 0 4", "B 2 3", "R 1 6", "R 1 0", "D 1", "I 1 2 2 -1 3", "I 99 5 1 8 7", "M 99 5", "V 99 1 8", "T 99 7"};
    for (const auto& line : trace) {
        auto e = parse_event(line);
        checked_update(index, shadow, e);
        verify(index, d, shadow);
    }
    // Validation failures must leave the actual index and independent state unchanged.
    for (const auto& line : {"R 0 1", "R 1 2", "B 1 2", "D 3", "I 0 0 0 0 0", "M 3 0"}) {
        auto fingerprint = index.pool_fingerprint(); bool rejected = false;
        try { closure::UpdateWork work; apply(index, parse_event(line), work); } catch (const std::exception&) { rejected = true; }
        require(rejected && index.pool_fingerprint() == fingerprint, "invalid update accepted or mutated pools");
        verify(index, d, shadow);
    }
    uint64_t compactions = 0;
    for (size_t i = 0; i < shadow.items.size(); ++i) if (shadow.items[i].alive) {
        Event e; e.op = 'D'; e.id = Id(i); closure::UpdateWork work;
        apply(index, e, work); shadow.apply(e); compactions += work.compactions;
    }
    require(compactions > 0, "delete compaction lifecycle not exercised"); verify(index, d, shadow);
    checked_update(index, shadow, parse_event("I 97 7 1 -1 3")); verify(index, d, shadow);
    checked_update(index, shadow, parse_event("D 97")); verify(index, d, shadow);
    checked_update(index, shadow, parse_event("I 97 0 2 4 1")); verify(index, d, shadow);
    npi::Dataset empty = tiny; for (auto& item : empty.items) item.alive = false;
    Canonical empty_shadow(empty); closure::Index empty_index(empty, 64); verify(empty_index, empty, empty_shadow);
    checked_update(empty_index, empty_shadow, parse_event("I 2 1 0 0 1")); verify(empty_index, empty, empty_shadow);
    std::cout << "closure self-check passed: runtime core, five arms, literal predicates, hierarchy heredity, witness restoration, lifecycle, compaction, sparse IDs, empty-first-insert, high-ef exact\n";
}
} // namespace
int main(int argc, char** argv) {
    Args args; std::unique_ptr<Runner> runner;
    try {
        parse(args, argc, argv);
        if (args.self) { self_check(); return 0; }
        runner = std::make_unique<Runner>(args); runner->run(); runner->summary(true); return 0;
    } catch (const std::exception& e) {
        std::cerr << "closure_research: " << e.what() << '\n';
        try {
            if (runner) runner->summary(false, e.what());
            else if (!args.out.empty()) {
                std::ofstream out(args.out + ".summary.json");
                out << "{\"complete\":false,\"error\":" << json(e.what()) << "}\n";
                out.flush(); require(bool(out), "cannot write failure summary");
            }
        } catch (const std::exception& failure) { std::cerr << "failure summary: " << failure.what() << '\n'; }
        return 1;
    }
}
