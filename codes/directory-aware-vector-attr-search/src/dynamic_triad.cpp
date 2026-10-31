// Dynamic A/B/C/P replay. The frozen static/autoresearch runners are independent.
#include "raw_l2_space.h"
#include "eligibility_index.h"
#include "packed_index.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
using Id = size_t;
using Posting = std::set<Id>;
using Graph = hnswlib::HierarchicalNSW<float>;
using Neighbor = std::pair<float, Id>;
constexpr size_t NONE = std::numeric_limits<size_t>::max();
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

// Counts actual graph distance invocations, not hnswlib's neighbor-list-size
// approximation. GT/direct scans still use exactly the shared RawL2Space kernel.
class CountedL2 : public RawL2Space {
public:
    explicit CountedL2(size_t dim) : RawL2Space(dim) {}
    mutable uint64_t calls = 0;
    hnswlib::DISTFUNC<float> get_dist_func() override { return counted; }
    void* get_dist_func_param() override { return this; }
private:
    static float counted(const void* a, const void* b, const void* param) {
        const auto* self = static_cast<const CountedL2*>(param);
        ++self->calls;
        return self->dist(static_cast<const float*>(a), static_cast<const float*>(b));
    }
};
struct Args {
    std::string data, trace, out, gt_in, static_batch;
    char arm = 0;
    size_t ef = 256, scan = 512, k = 10, eligible_scan_threshold = 512;
    std::string eligibility_policy = "off";
    std::string pack_layout = "dfs";
    size_t pack_size = 1024;
    bool pack_final_repack = false;
    double compact = .2;
};
Args arguments(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 == argc) fail("missing value for " + std::string(argv[i]));
        std::string key = argv[i], value = argv[i + 1];
        if (key == "--data") a.data = value;
        else if (key == "--trace") a.trace = value;
        else if (key == "--out") a.out = value;
        else if (key == "--gt-in") a.gt_in = value;
        else if (key == "--static-batch") a.static_batch = value;
        else if (key == "--arm") {
            if (value.size() != 1) fail("arm must be A, B, C or P");
            a.arm = value[0];
        } else if (key == "--ef") a.ef = std::stoull(value);
        else if (key == "--scan-threshold") a.scan = std::stoull(value);
        else if (key == "--eligibility-policy") a.eligibility_policy = value;
        else if (key == "--eligible-scan-threshold") a.eligible_scan_threshold = std::stoull(value);
        else if (key == "--pack-layout") a.pack_layout = value;
        else if (key == "--pack-size") {
            if (value.empty() || value[0] == '-') fail("pack-size must be positive");
            size_t used = 0;
            a.pack_size = std::stoull(value, &used);
            if (used != value.size() || !a.pack_size) fail("pack-size must be positive");
        } else if (key == "--pack-final-repack") {
            if (value != "0" && value != "1") fail("pack-final-repack must be 0 or 1");
            a.pack_final_repack = value == "1";
        }
        else if (key == "--compact-threshold") a.compact = std::stod(value);
        else if (key == "--k") a.k = std::stoull(value);
        else fail("unknown option " + key);
    }
    if (a.eligibility_policy != "off" && a.eligibility_policy != "cap" &&
        a.eligibility_policy != "hybrid" && a.eligibility_policy != "exact")
        fail("eligibility-policy must be off, cap, hybrid or exact");
    if (a.pack_layout != "dfs" && a.pack_layout != "shuffle" && a.pack_layout != "node")
        fail("pack-layout must be dfs, shuffle or node");
    if (a.arm == 'P' && a.eligibility_policy == "off") fail("P requires eligibility-policy cap, hybrid or exact");
    if (a.arm != 'P' && a.pack_final_repack) fail("pack-final-repack requires arm P");
    if (!a.static_batch.empty()) {
        if (a.arm != 'A' && a.arm != 'P') fail("static-batch requires arm A or P");
        if (a.gt_in.empty()) fail("static-batch requires precomputed --gt-in");
        if (a.pack_final_repack) fail("static-batch forbids final repack");
        if (a.arm == 'A' && (a.scan || a.eligibility_policy != "off"))
            fail("static-batch A requires scan-threshold 0 and eligibility-policy off");
        if (a.arm == 'P' && a.eligibility_policy != "cap")
            fail("static-batch P requires initial eligibility-policy cap");
    }
    if (a.data.empty() || a.trace.empty() || (a.out.empty() && a.static_batch.empty()) ||
        (a.arm != 'A' && a.arm != 'B' && a.arm != 'C' && a.arm != 'P') || !a.ef || !a.k ||
        !std::isfinite(a.compact) || a.compact <= 0 || a.compact > 1)
        fail("usage: dynamic_triad --data DIR --trace FILE --arm A|B|C|P --out PREFIX "
             "[--ef 256 --scan-threshold 512 --compact-threshold .2 --k 10 --gt-in PREFIX.queries.csv "
             "--eligibility-policy off|cap|hybrid|exact --eligible-scan-threshold 512 "
             "--pack-layout dfs|shuffle|node --pack-size 1024 --pack-final-repack 0|1 --static-batch FILE]");
    return a;
}
std::vector<float> floats(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) fail("cannot open " + path);
    auto bytes = in.tellg();
    if (bytes < 0 || static_cast<uint64_t>(bytes) % sizeof(float)) fail("bad float32 size: " + path);
    std::vector<float> result(static_cast<size_t>(bytes) / sizeof(float));
    in.seekg(0);
    if (bytes && !in.read(reinterpret_cast<char*>(result.data()), bytes)) fail("short read: " + path);
    for (float v : result) if (!std::isfinite(v)) fail("nonfinite vector in " + path);
    return result;
}
struct Item { size_t node = 0; int64_t category = 0, numeric = 0; uint64_t tags = 0; bool alive = false; };
struct Query {
    size_t qidx = 0, anchor = 0;
    int recursive = 0, tagmode = 0, combine = 0;
    int64_t category = -1, lo = -1, hi = -1;
    uint64_t tags = 0;
};
bool extra(const Item& item, const Query& q) {
    bool any = false, conjunction = true, disjunction = false;
    auto add = [&](bool value) { any = true; conjunction &= value; disjunction |= value; };
    if (q.category != -1) add(item.category == q.category);
    if (q.lo != -1 || q.hi != -1) add(item.numeric >= q.lo && item.numeric <= q.hi);
    if (q.tagmode == 1) add((item.tags & q.tags) == q.tags);
    else if (q.tagmode == 2) add((item.tags & q.tags) != 0);
    else if (q.tagmode == 3) add(item.tags == q.tags);
    else if (q.tagmode == 4) add((item.tags & q.tags) == item.tags);
    return !any || (q.combine ? disjunction : conjunction);
}

// Exact ranks are IDs, not search neighbors with synthetic distances. The
// legacy queries CSV is also the tape format; no oracle data is read until the
// measured search and merge have finished.
constexpr const char* query_columns =
    "event,version,query_index,qidx,recursive,anchor,anchor_depth,scope_card,eligible_card,dir_us,search_us,e2e_us,gt_us,gt_count,hit_count,recall,distance_calls,ann_hops,local_indexes,valid_count,invalid_count,duplicate_count,gt_ids,result_ids,fanout,scan_nodes";
class GtTape {
    std::ifstream in;
    bool opened = false;
    size_t column_count = 0, calls_column = NONE;
    static std::vector<std::string> fields(std::string line) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> result;
        size_t start = 0;
        for (;;) {
            size_t end = line.find(',', start);
            result.push_back(line.substr(start, end == std::string::npos ? end : end - start));
            if (end == std::string::npos) return result;
            start = end + 1;
        }
    }
    void open(const std::string& path) {
        if (opened) return;
        in.open(path);
        if (!in) fail("GT tape: cannot open " + path);
        opened = true;
        std::string header;
        if (!std::getline(in, header)) fail("GT tape: missing header");
        if (!header.empty() && header.back() == '\r') header.pop_back();
        const auto names = fields(header);
        const auto required = fields(query_columns);
        if (names.size() < required.size() ||
            !std::equal(required.begin(), required.end(), names.begin()))
            fail("GT tape: invalid queries CSV schema");
        std::set<std::string> unique;
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i].empty() || !unique.insert(names[i]).second)
                fail("GT tape: empty or duplicate column");
            if (names[i] == "gt_distance_calls") calls_column = i;
        }
        column_count = names.size();
    }
public:
    void rewind(const std::string& path) {
        in.close();
        in.clear();
        opened = false;
        column_count = 0;
        calls_column = NONE;
        open(path);
    }
    static uint64_t integer(const std::string& value) {
        if (value.empty()) fail("GT tape: empty integer");
        uint64_t result = 0;
        for (char c : value) {
            if (c < '0' || c > '9' || result > (UINT64_MAX - unsigned(c - '0')) / 10)
                fail("GT tape: malformed integer " + value);
            result = result * 10 + unsigned(c - '0');
        }
        return result;
    }
    std::vector<std::string> next(const std::string& path) {
        open(path);
        std::string line;
        if (!std::getline(in, line)) fail("GT tape: missing query row");
        auto row = fields(std::move(line));
        if (row.size() != column_count) fail("GT tape: malformed query row column count");
        // Validate the stable GT contract; appended query diagnostics may include
        // policy/path strings and are not oracle evidence.
        for (size_t i = 0; i < 26; ++i) {
            if (i == 22 || i == 23) continue;
            if ((i >= 9 && i <= 12) || i == 15) {
                size_t used = 0;
                double value;
                try { value = std::stod(row[i], &used); }
                catch (const std::exception&) { fail("GT tape: malformed numeric diagnostic"); }
                if (used != row[i].size() || !std::isfinite(value) || value < 0 || (i == 15 && value > 1))
                    fail("GT tape: invalid numeric diagnostic");
            } else integer(row[i]);
        }
        if (integer(row[20]) || integer(row[21])) fail("GT tape: oracle reports invalid or duplicate results");
        if (calls_column != NONE && integer(row[calls_column]) != integer(row[8]))
            fail("GT tape: expected inline oracle distance calls");
        return row;
    }
    void finish(const std::string& path) {
        open(path);
        std::string line;
        if (std::getline(in, line)) fail("GT tape: extra query row");
        if (!in.eof()) fail("GT tape: read failure");
    }
};
constexpr const char* maintenance_names[] = {"tombstone_compaction", "threshold_build", "threshold_drop"};
struct Maintenance {
    uint64_t count = 0, insertions = 0;
    double us = 0;
};
struct GraphStats {
    uint64_t hnsw_nodes = 0, live = 0, dead = 0;
};
struct Update {
    uint64_t items = 0, nodes = 0, writes = 0, insertions = 0, deletions = 0, compactions = 0;
    uint64_t path_bytes = 0;
    double compact_us = 0;
    Maintenance maintenance[3];
    double eligibility_us = 0;
    packed::UpdateWork packed_work;
};
struct Local { std::unique_ptr<Graph> graph; };

class Runner {
    Args args;
    std::vector<float> base, tests;
    std::vector<Item> items;
    std::vector<int> parent;
    std::vector<std::set<size_t>> children;
    std::vector<std::string> paths;
    std::vector<Posting> direct;  // A only. Not an oracle or a C direct-posting shortcut.
    std::map<std::string, Posting> catalog; // B direct; C inclusive.
    std::vector<Local> local;
    std::unique_ptr<CountedL2> space;
    std::unique_ptr<Graph> global;
    std::unique_ptr<eligibility::Index> eligibility_index;
    std::unique_ptr<packed::Index> packed_index;
    std::vector<size_t> local_eligible_counts;
    size_t dim = 0, root = NONE, version = 0, query_index = 0, update_count = 0;
    uint64_t invalid = 0, duplicates = 0, violations = 0, distances = 0, hops = 0;
    uint64_t total_gt = 0, total_hits = 0, zero_gt = 0;
    uint64_t gt_distance_calls = 0;
    uint64_t build_insertions = 0;
    uint64_t total_fanout = 0, total_scan_nodes = 0;
    uint64_t total_graph_calls = 0, total_scan_groups = 0, total_scanned_live = 0;
    uint64_t total_graph_distances = 0, total_scan_distances = 0, total_merged_candidates = 0;
    uint64_t total_directory_visits = 0, total_qualification_directories = 0;
    uint64_t total_qualification_postings = 0, total_qualification_attributes = 0;
    uint64_t total_mask_mismatches = 0;
    size_t effective_ef_min = NONE, effective_ef_max = 0;
    double eligibility_build_us = 0, eligibility_update_us_total = 0;
    uint64_t build_distances = 0, update_distances = 0, initial_pack_fingerprint = 0;
    uint64_t packed_qualified_ids = 0;
    double final_repack_us = 0;
    packed::UpdateWork final_repack_work;
    bool final_repack_complete = false;
    double eligibility_us_total = 0, eligible_scan_us_total = 0;
    double build_s = 0, query_total = 0, update_total = 0, gt_total = 0, recall_sum = 0;
    Update totals;
    GraphStats initial_graph_stats;
    std::vector<double> query_times, update_times;
    std::ofstream qout, uout;
    GtTape gt_tape;
    struct StaticPass {
        size_t ef;
        std::string policy, out;
    };
    std::vector<StaticPass> static_passes;
    std::vector<Query> static_queries;
    size_t static_pass = 0;

    static npi::Item eligibilityItem(const Item& item) {
        return {static_cast<npi::Id>(item.node), item.category, item.numeric, item.tags, item.alive};
    }
    static npi::Query eligibilityQuery(const Query& q) {
        return {q.qidx, q.recursive, static_cast<npi::Id>(q.anchor),
                q.category, q.lo, q.hi, q.tagmode, q.tags, q.combine};
    }
    void syncEligibility(Id id, Update& u) {
        if (packed_index)
            packed_index->update(static_cast<npi::Id>(id), eligibilityItem(items[id]), u.packed_work);
        if (!eligibility_index) return;
        const auto start = Clock::now();
        eligibility_index->update(static_cast<npi::Id>(id), eligibilityItem(items[id]));
        u.eligibility_us += elapsed(start);
    }
    const float* vector(Id id) const { return base.data() + id * dim; }
    void node(size_t n) const { if (n >= parent.size()) fail("node out of range: " + std::to_string(n)); }
    void item(Id id, bool live) const {
        if (id >= items.size()) fail("item out of range: " + std::to_string(id));
        if (items[id].alive != live) fail("item " + std::to_string(id) + (live ? " is not live" : " is not dead"));
    }
    bool beneath(size_t n, size_t ancestor) const {
        for (size_t visits = 0; visits <= parent.size(); ++visits) {
            if (n == ancestor) return true;
            if (parent[n] < 0) return false;
            n = static_cast<size_t>(parent[n]);
        }
        fail("canonical topology cycle");
    }
    std::vector<size_t> ancestors(size_t n) const {
        std::vector<size_t> result;
        for (;;) {
            result.push_back(n);
            if (parent[n] < 0) return result;
            n = static_cast<size_t>(parent[n]);
            if (result.size() > parent.size()) fail("canonical topology cycle");
        }
    }
    std::vector<size_t> subtree(size_t n) const {
        std::vector<size_t> result{n};
        for (size_t i = 0; i < result.size(); ++i)
            for (size_t child : children[result[i]]) result.push_back(child);
        return result;
    }
    std::string pathFor(size_t n) const {
        auto chain = ancestors(n);
        std::string path;
        for (auto i = chain.rbegin(); i != chain.rend(); ++i) path += "/n" + std::to_string(*i);
        return path;
    }
    size_t depth(size_t n) const { return ancestors(n).size() - 1; }
    bool matches(Id id, const Query& q) const {
        if (id >= items.size()) return false;
        const auto& v = items[id];
        return v.alive && (q.recursive ? beneath(v.node, q.anchor) : v.node == q.anchor) && extra(v, q);
    }
    std::unique_ptr<Graph> graph(size_t capacity) {
        auto result = std::make_unique<Graph>(space.get(), std::max(size_t(1), capacity), 16, 200, 100, false);
        result->setEf(args.ef);
        return result;
    }
    void insertGraph(Graph& g, Id id, Update& u) {
        // Vectors are immutable for every potential ID in this replay. Revisit
        // the same tombstoned label identically in local and global graphs:
        // restore visibility, without re-running addPoint's vector-update repair.
        auto existing = g.label_lookup_.find(id);
        if (existing != g.label_lookup_.end()) {
            if (!g.isMarkedDeleted(existing->second)) fail("duplicate live graph label");
            g.unmarkDelete(id);
        } else {
            if (g.cur_element_count == g.max_elements_)
                g.resizeIndex(std::max(g.max_elements_ * 2, size_t(g.cur_element_count) + 1));
            g.addPoint(vector(id), id, false);
        }
        ++u.insertions;
    }
    GraphStats graphStats() const {
        GraphStats result;
        auto add = [&](const Graph& g) {
            ++result.hnsw_nodes;
            result.dead += g.num_deleted_;
            result.live += g.cur_element_count - g.num_deleted_;
        };
        if (global) add(*global);
        for (const auto& l : local) if (l.graph) add(*l.graph);
        if (packed_index) {
            result.hnsw_nodes = packed_index->graph_count();
            result.live = packed_index->live_count();
            result.dead = packed_index->dead_count();
        }
        return result;
    }
    void reconcile(size_t n, Update& u) {
        auto& g = local[n].graph;
        bool wanted = direct[n].size() > args.scan;
        bool deleted = g && g->cur_element_count &&
            double(g->num_deleted_) / double(g->cur_element_count) >= args.compact;
        if ((!wanted && g) || (wanted && (!g || deleted))) {
            // Classify before mutation, preserving reconciliation priority.
            auto& category = u.maintenance[!wanted ? 2 : (!g ? 1 : 0)];
            const auto insertions_before = u.insertions;
            auto start = Clock::now();
            if (!wanted) g.reset();
            else {
                auto fresh = graph(std::max(size_t(16), direct[n].size() * 2));
                for (Id id : direct[n]) insertGraph(*fresh, id, u);
                g = std::move(fresh);
            }
            const double duration = elapsed(start);
            u.compact_us += duration;
            category.us += duration;
            ++category.count;
            category.insertions += u.insertions - insertions_before;
            ++u.compactions;
        }
    }
    void eraseMembership(Id id, size_t n, Update& u) {
        if (args.arm == 'P') return; // Logical membership belongs to shared eligibility.
        if (args.arm == 'A') {
            if (direct[n].erase(id) != 1) fail("missing A direct membership");
            ++u.writes;
            if (local[n].graph) { local[n].graph->markDelete(id); ++u.deletions; }
            reconcile(n, u);
        } else if (args.arm == 'B') {
            if (catalog.at(paths[n]).erase(id) != 1) fail("missing B direct membership");
            ++u.writes;
        } else {
            for (size_t a : ancestors(n)) {
                if (catalog.at(paths[a]).erase(id) != 1) fail("missing C inclusive membership");
                ++u.writes;
            }
        }
    }
    void addMembership(Id id, size_t n, Update& u) {
        if (args.arm == 'P') return;
        if (args.arm == 'A') {
            if (!direct[n].insert(id).second) fail("duplicate A direct membership");
            ++u.writes;
            if (local[n].graph) insertGraph(*local[n].graph, id, u);
            reconcile(n, u);
        } else if (args.arm == 'B') {
            if (!catalog.at(paths[n]).insert(id).second) fail("duplicate B direct membership");
            ++u.writes;
        } else {
            for (size_t a : ancestors(n)) {
                if (!catalog.at(paths[a]).insert(id).second) fail("duplicate C inclusive membership");
                ++u.writes;
            }
        }
    }
    void move(Id id, size_t destination, Update& u) {
        size_t old = items[id].node;
        if (old == destination) return;
        if (args.arm == 'C') {
            auto from = ancestors(old), to = ancestors(destination);
            std::set<size_t> f(from.begin(), from.end()), t(to.begin(), to.end());
            for (size_t a : f) if (!t.count(a)) {
                if (catalog.at(paths[a]).erase(id) != 1) fail("missing C old move membership");
                ++u.writes;
            }
            for (size_t a : t) if (!f.count(a)) {
                if (!catalog.at(paths[a]).insert(id).second) fail("duplicate C new move membership");
                ++u.writes;
            }
        } else if (args.arm == 'A') {
            addMembership(id, destination, u);
            eraseMembership(id, old, u);
        } else {
            eraseMembership(id, old, u);
            addMembership(id, destination, u);
        }
        items[id].node = destination;
        ++u.writes;
        syncEligibility(id, u);
    }

    // Same pinned hnswlib searchKnn algorithm, enabling its base-layer hop
    // instrumentation (searchKnn's default disables it). No exact ANN fallback.
    std::vector<Neighbor> searchGraph(Graph& g, const float* q, size_t k,
                                      hnswlib::BaseFilterFunctor& filter, uint64_t& query_hops,
                                      size_t effective_ef) {
        std::vector<Neighbor> result;
        if (!k || !g.cur_element_count) return result;
        long h0 = g.metric_hops.load();
        auto current = g.enterpoint_node_;
        float best = space->get_dist_func()(q, g.getDataByInternalId(current), space.get());
        for (int level = g.maxlevel_; level > 0; --level) {
            bool changed = true;
            while (changed) {
                changed = false;
                auto* links = reinterpret_cast<unsigned int*>(g.get_linklist(current, level));
                size_t count = g.getListCount(links);
                ++g.metric_hops;
                auto* ids = reinterpret_cast<hnswlib::tableint*>(links + 1);
                for (size_t i = 0; i < count; ++i) {
                    auto candidate = ids[i];
                    if (candidate >= g.cur_element_count) fail("HNSW internal link out of range");
                    float d = space->get_dist_func()(q, g.getDataByInternalId(candidate), space.get());
                    if (d < best) { best = d; current = candidate; changed = true; }
                }
            }
        }
        auto found = g.searchBaseLayerST<false, true>(current, q, effective_ef, &filter);
        while (found.size() > k) found.pop();
        while (!found.empty()) {
            result.emplace_back(found.top().first, g.getExternalLabel(found.top().second));
            found.pop();
        }
        query_hops += static_cast<uint64_t>(g.metric_hops.load() - h0);
        return result;
    }
    struct Filter : hnswlib::BaseFilterFunctor {
        const Runner& runner;
        const Query& query;
        const Posting* allowed;
        size_t direct_node;
        const eligibility::Result* eligible;
        Filter(const Runner& r, const Query& q, const Posting* a, size_t n,
               const eligibility::Result* e = nullptr)
            : runner(r), query(q), allowed(a), direct_node(n), eligible(e) {}
        bool operator()(hnswlib::labeltype id) override {
            if (eligible)
                return eligible->contains(static_cast<npi::Id>(id)) &&
                    (direct_node == NONE || runner.eligibility_index->owner(static_cast<npi::Id>(id)) == direct_node);
            if (id >= runner.items.size()) return false;
            const Item& v = runner.items[id];
            return v.alive && extra(v, query) &&
                (allowed ? allowed->count(id) != 0 : v.node == direct_node);
        }
    };
    static void top(std::vector<Neighbor>& result, size_t k) {
        if (result.size() > k) {
            std::nth_element(result.begin(), result.begin() + k, result.end());
            result.resize(k);
        }
        std::sort(result.begin(), result.end());
    }
    static std::string rankedIds(const std::vector<Id>& values) {
        std::ostringstream out;
        for (size_t i = 0; i < values.size(); ++i) { if (i) out << ';'; out << values[i]; }
        return out.str();
    }
    std::vector<Id> tapedGt(size_t event, const Query& q, size_t anchor_depth,
                            size_t scope, size_t eligible) {
        const auto row = gt_tape.next(args.gt_in);
        const uint64_t expected[] = {event, version, query_index, q.qidx,
                                    uint64_t(q.recursive), q.anchor, anchor_depth, scope, eligible};
        for (size_t i = 0; i < 9; ++i)
            if (GtTape::integer(row[i]) != expected[i])
                fail("GT tape: query identity/current-state mismatch at column " + std::to_string(i));
        const size_t count = std::min(args.k, eligible);
        if (GtTape::integer(row[13]) != count) fail("GT tape: GT count does not match k/current eligible count");
        auto parse_ids = [&](const std::string& value) {
            std::vector<Id> result;
            std::unordered_set<Id> seen;
            if (!value.empty()) {
                size_t start = 0;
                for (;;) {
                    size_t end = value.find(';', start);
                    uint64_t parsed = GtTape::integer(value.substr(start, end == std::string::npos ? end : end - start));
                    if (parsed >= items.size()) fail("GT tape: ID out of range");
                    Id id = static_cast<Id>(parsed);
                    if (!seen.insert(id).second) fail("GT tape: duplicate ID");
                    if (!matches(id, q)) fail("GT tape: ID violates current predicate");
                    result.push_back(id);
                    if (end == std::string::npos) break;
                    start = end + 1;
                }
            }
            return result;
        };
        auto result = parse_ids(row[22]);
        if (result.size() != count) fail("GT tape: ranked ID count mismatch");
        const auto oracle_result = parse_ids(row[23]);
        if (oracle_result.size() > count || GtTape::integer(row[19]) != oracle_result.size())
            fail("GT tape: oracle result count mismatch");
        Posting gt_set(result.begin(), result.end());
        size_t hits = 0;
        for (Id id : oracle_result) hits += gt_set.count(id);
        double recall = count ? double(hits) / count : 1.0;
        if (GtTape::integer(row[14]) != hits || std::abs(std::stod(row[15]) - recall) > 1e-9)
            fail("GT tape: oracle hit/recall mismatch");
        return result;
    }
    static std::string ids(const std::vector<Neighbor>& values) {
        std::ostringstream out;
        for (size_t i = 0; i < values.size(); ++i) { if (i) out << ';'; out << values[i].second; }
        return out.str();
    }
    void validateQuery(const Query& q) const {
        node(q.anchor);
        if (q.qidx >= tests.size() / dim || (q.recursive != 0 && q.recursive != 1) ||
            q.tagmode < 0 || q.tagmode > 4 ||
            (q.combine != 0 && q.combine != 1) ||
            ((q.lo != -1 || q.hi != -1) && q.lo > q.hi)) fail("invalid Q fields");
    }
    void query(size_t event, const Query& q) {
        validateQuery(q);
        const float* qvec = tests.data() + q.qidx * dim;
        size_t anchor_depth = depth(q.anchor); // Output-only diagnostic, as in the frozen runner.
        auto e2e_start = Clock::now();
        double eligibility_us = 0, eligible_scan_us = 0;
        const eligibility::Result* eligible = nullptr;
        bool exact_scan = false;
        size_t effective_ef = std::max(args.ef, args.k);
        if (eligibility_index) {
            const auto qualification_start = Clock::now();
            eligible = &eligibility_index->prepare(eligibilityQuery(q));
            effective_ef = std::min(eligible->count, std::max(args.ef, std::min(args.k, eligible->count)));
            exact_scan = args.eligibility_policy == "exact" ||
                (args.eligibility_policy == "hybrid" &&
                 eligible->count <= std::max({args.k, args.ef,
                     args.arm == 'P' ? size_t(512) : args.eligible_scan_threshold}));
            if (args.arm == 'A' && !exact_scan) {
                // Bucket only qualified set bits using the index's stored owner;
                // never scan canonical objects to discover eligibility.
                for (size_t word = 0; word < eligible->eligible.size(); ++word)
                    for (uint64_t bits = eligible->eligible[word]; bits; bits &= bits - 1) {
                        const auto id = static_cast<npi::Id>(word * 64 + __builtin_ctzll(bits));
                        ++local_eligible_counts[eligibility_index->owner(id)];
                    }
            }
            eligibility_us = elapsed(qualification_start);
        }
        const bool bypass = eligible && (exact_scan || !eligible->count);
        const char* execution_path = !eligible ? "legacy" :
            (!eligible->count ? "empty" : (exact_scan ? "exact" : "graph"));
        size_t scope = bypass ? eligible->scope_count : 0;
        size_t directory_visits = 0;
        std::vector<std::pair<size_t, size_t>> local_work;
        Posting candidates;
        const Posting* allowed = &candidates;
        if (bypass) {
            // Exact/empty execution needs no local fanout or catalog compilation.
        } else if (args.arm == 'P') {
            scope = eligible->scope_count;
        } else if (args.arm == 'A') {
            auto nodes = q.recursive ? subtree(q.anchor) : std::vector<size_t>{q.anchor};
            directory_visits = nodes.size();
            for (size_t n : nodes) {
                if (direct[n].empty()) continue;
                scope += direct[n].size();
                const size_t count = eligible ? local_eligible_counts[n] : direct[n].size();
                if (eligible) local_eligible_counts[n] = 0;
                if (count) local_work.emplace_back(n, count);
            }
        } else if (args.arm == 'B') {
            const std::string& key = paths[q.anchor];
            directory_visits = 1;
            if (!q.recursive) allowed = &catalog.at(key);
            else {
                const auto own = catalog.find(key);
                candidates.insert(own->second.begin(), own->second.end());
                const std::string prefix = key + '/';
                for (auto it = catalog.lower_bound(prefix);
                     it != catalog.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it) {
                    ++directory_visits;
                    candidates.insert(it->second.begin(), it->second.end());
                }
            }
        } else {
            directory_visits = 1 + (q.recursive ? 0 : children[q.anchor].size());
            if (q.recursive) allowed = &catalog.at(paths[q.anchor]);
            else {
                candidates = catalog.at(paths[q.anchor]);
                for (size_t child : children[q.anchor])
                    for (Id id : catalog.at(paths[child])) candidates.erase(id);
            }
        }
        if ((args.arm == 'B' || args.arm == 'C') && !bypass) scope = allowed->size();
        if (!eligible) effective_ef = scope ? std::max(args.ef, std::min(args.k, scope)) : 0;
        double dir_us = elapsed(e2e_start);
        auto search_start = Clock::now();
        uint64_t dc0 = space->calls, scan_calls = 0, query_hops = 0;
        size_t local_indexes = 0, graph_calls = 0, scan_groups = 0, scanned_live = 0;
        size_t local_ef_min = NONE, local_ef_max = 0;
        packed::Work packed_query;
        auto record_ef = [&](size_t ef) {
            local_ef_min = std::min(local_ef_min, ef);
            local_ef_max = std::max(local_ef_max, ef);
        };
        std::vector<Neighbor> result;
        if (bypass && eligible->count) {
            const auto scan_start = Clock::now();
            const size_t k = std::min(args.k, eligible->count);
            result.reserve(k);
            for (size_t word = 0; word < eligible->eligible.size(); ++word)
                for (uint64_t bits = eligible->eligible[word]; bits; bits &= bits - 1) {
                    const Id id = word * 64 + __builtin_ctzll(bits);
                    const Neighbor candidate{space->dist(qvec, vector(id)), id};
                    ++scan_calls; ++scanned_live;
                    if (result.size() < k) {
                        result.push_back(candidate);
                        std::push_heap(result.begin(), result.end());
                    } else if (candidate < result.front()) {
                        std::pop_heap(result.begin(), result.end());
                        result.back() = candidate;
                        std::push_heap(result.begin(), result.end());
                    }
                }
            eligible_scan_us = elapsed(scan_start);
        } else if (!bypass && args.arm == 'A') {
            for (auto [n, count] : local_work) {
                size_t k = std::min(args.k, count);
                std::vector<Neighbor> found;
                if (local[n].graph) {
                    ++local_indexes; ++graph_calls;
                    const size_t ef = eligible ? std::max(k, std::min(args.ef, count)) : std::max(args.ef, k);
                    record_ef(ef);
                    Filter filter(*this, q, nullptr, n, eligible);
                    found = searchGraph(*local[n].graph, qvec, k, filter, query_hops, ef);
                } else {
                    if (direct[n].size() > args.scan) fail("missing A index above scan threshold");
                    ++scan_groups;
                    for (Id id : direct[n]) {
                        ++scanned_live;
                        if (!(eligible ? eligible->contains(static_cast<npi::Id>(id)) :
                              items[id].alive && extra(items[id], q))) continue;
                        found.emplace_back(space->dist(qvec, vector(id)), id);
                        ++scan_calls;
                    }
                    top(found, k);
                }
                result.insert(result.end(), found.begin(), found.end());
            }
        } else if (!bypass && args.arm == 'P') {
            const auto found = packed_index->search(qvec, args.k, args.ef, *eligible, packed_query);
            result.assign(found.begin(), found.end());
            graph_calls = local_indexes = packed_query.graph_calls;
            query_hops = packed_query.hops;
            if (graph_calls) {
                local_ef_min = packed_query.effective_ef_min;
                local_ef_max = packed_query.effective_ef_max;
            }
        } else if (!bypass && scope) {
            ++graph_calls;
            const size_t k = std::min(args.k, eligible ? eligible->count : scope);
            const size_t ef = eligible ? effective_ef : std::max(args.ef, k);
            record_ef(ef);
            Filter filter(*this, q, allowed, NONE, eligible);
            result = searchGraph(*global, qvec, k, filter, query_hops, ef);
        }
        const size_t merged_candidates = args.arm == 'P' && !bypass ?
            packed_query.merged_candidates : result.size();
        // Validate before merge so deduplication never hides a broken local arm.
        std::unordered_set<Id> seen;
        size_t bad = 0, duplicate = 0;
        for (const auto& v : result) {
            // Predicate validation is outside the measured search boundary.
            if (!seen.insert(v.second).second) ++duplicate;
        }
        if (duplicate) {
            seen.clear();
            result.erase(std::remove_if(result.begin(), result.end(), [&](const Neighbor& v) {
                return !seen.insert(v.second).second;
            }), result.end());
        }
        top(result, args.k);
        double search_us = elapsed(search_start), e2e_us = elapsed(e2e_start);
        const uint64_t graph_distances = args.arm == 'P' ? packed_query.distance_calls : space->calls - dc0;
        uint64_t dc = graph_distances + scan_calls;
        // Independent current-state oracle, never consulting paths/postings.
        auto gt_start = Clock::now();
        std::vector<Neighbor> exact;
        Posting oracle_scope_ids;
        size_t oracle_scope = 0, oracle_eligible = 0;
        uint64_t query_gt_calls = 0, mask_mismatches = 0;
        for (Id id = 0; id < items.size(); ++id) {
            const auto& v = items[id];
            const bool in_scope = v.alive && (q.recursive ? beneath(v.node, q.anchor) : v.node == q.anchor);
            if (eligible) {
                mask_mismatches += eligible->in_scope(static_cast<npi::Id>(id)) != in_scope;
                mask_mismatches += eligible->contains(static_cast<npi::Id>(id)) != (in_scope && extra(v, q));
            }
            if (!in_scope) continue;
            ++oracle_scope;
            oracle_scope_ids.insert(id);
            if (!extra(v, q)) continue;
            ++oracle_eligible;
            if (args.gt_in.empty()) {
                exact.emplace_back(space->dist(qvec, vector(id)), id);
                ++query_gt_calls;
            }
        }
        std::vector<Id> gt;
        if (args.gt_in.empty()) {
            top(exact, args.k);
            gt.reserve(exact.size());
            for (const auto& neighbor : exact) gt.push_back(neighbor.second);
        } else {
            gt = tapedGt(event, q, anchor_depth, oracle_scope, oracle_eligible);
        }
        double gt_us = elapsed(gt_start);
        size_t hit = 0;
        Posting gt_set;
        for (Id id : gt) gt_set.insert(id);
        for (auto v : result) if (gt_set.count(v.second)) ++hit;
        // A mismatch is counted, recorded and fatal: no stale, silently filtered,
        // duplicated, or undercounted directory state is accepted as a data point.
        for (const auto& v : result) if (!matches(v.second, q)) ++bad;
        size_t state_errors = (scope != oracle_scope) + bad + duplicate + mask_mismatches;
        if (eligible)
            state_errors += (eligible->scope_count != oracle_scope) + (eligible->count != oracle_eligible);
        if (args.arm == 'A') {
            for (Id id : oracle_scope_ids)
                if (!direct[items[id].node].count(id)) ++state_errors;
        } else if (args.arm != 'P' && !bypass && *allowed != oracle_scope_ids) {
            // Full set equality, not merely matching cardinalities. Oracle work
            // remains outside dir/search/e2e timing.
            ++state_errors;
        }
        if (result.size() > gt.size()) ++state_errors;
        size_t valid_count = 0;
        for (const auto& v : result) valid_count += matches(v.second, q);
        double recall = gt.empty() ? (result.empty() ? 1.0 : 0.0) : double(hit) / gt.size();
        const size_t fanout = args.arm == 'A' ? local_work.size() : graph_calls;
        const size_t scan_nodes = scan_groups;
        qout << event << ',' << version << ',' << query_index << ',' << q.qidx << ',' << q.recursive << ','
             << q.anchor << ',' << anchor_depth << ',' << scope << ',' << oracle_eligible << ',' << dir_us << ','
             << search_us << ',' << e2e_us << ',' << gt_us << ',' << gt.size() << ',' << hit << ',' << recall << ','
             << dc << ',' << query_hops << ',' << local_indexes << ',' << valid_count
             << ',' << bad << ',' << duplicate << ',' << rankedIds(gt) << ',' << ids(result)
             << ',' << fanout << ',' << scan_nodes << ',' << query_gt_calls
             << ',' << eligibility_us << ',' << eligible_scan_us << ',' << args.eligibility_policy
             << ',' << execution_path << ',' << effective_ef
             << ',' << (eligible ? eligible->directory_visits : 0)
             << ',' << (eligible ? eligible->posting_words : 0)
             << ',' << (eligible ? eligible->attribute_words : 0)
             << ',' << directory_visits << ',' << graph_calls << ',' << scan_groups << ',' << scanned_live
             << ',' << graph_distances << ',' << scan_calls << ',' << merged_candidates
             << ',' << (local_ef_min == NONE ? 0 : local_ef_min) << ',' << local_ef_max
             << ',' << mask_mismatches << ',' << ((args.arm == 'A' || args.arm == 'P') ? graph_calls : 0)
             << ',' << (packed_index ? packed_index->block_count() : 0)
             << ',' << packed_query.qualified_ids << ',' << args.pack_layout << ',' << args.pack_size << '\n';
        qout.flush();
        ++query_index;
        query_times.push_back(e2e_us);
        query_total += e2e_us; gt_total += gt_us; recall_sum += recall;
        total_gt += gt.size(); total_hits += hit; zero_gt += gt.empty();
        gt_distance_calls += query_gt_calls;
        packed_qualified_ids += packed_query.qualified_ids;
        invalid += bad; duplicates += duplicate; violations += state_errors;
        distances += dc; hops += query_hops;
        total_fanout += fanout; total_scan_nodes += scan_nodes;
        total_mask_mismatches += mask_mismatches;
        total_graph_calls += graph_calls; total_scan_groups += scan_groups; total_scanned_live += scanned_live;
        total_graph_distances += graph_distances; total_scan_distances += scan_calls;
        total_merged_candidates += merged_candidates; total_directory_visits += directory_visits;
        eligibility_us_total += eligibility_us; eligible_scan_us_total += eligible_scan_us;
        if (local_ef_min != NONE) effective_ef_min = std::min(effective_ef_min, local_ef_min);
        effective_ef_max = std::max(effective_ef_max, local_ef_max);
        if (eligible) {
            total_qualification_directories += eligible->directory_visits;
            total_qualification_postings += eligible->posting_words;
            total_qualification_attributes += eligible->attribute_words;
        }
        if (state_errors) fail("query state violation: scope=" + std::to_string(scope) + "/" + std::to_string(oracle_scope) +
            " eligible=" + std::to_string(oracle_eligible) +
            " invalid=" + std::to_string(bad) + " duplicate=" + std::to_string(duplicate));
    }

    void reparent(size_t src, size_t dst, Update& u) {
        node(src); node(dst);
        if (src == root || beneath(dst, src)) fail("R requires nonroot source and parent outside its subtree");
        if (static_cast<size_t>(parent[src]) == dst) return;
        auto moved_nodes = subtree(src);
        std::vector<Id> moved_items;
        if (args.arm == 'C') {
            const auto& membership = catalog.at(paths[src]);
            moved_items.assign(membership.begin(), membership.end());
            u.items = moved_items.size();
        } else if (args.arm == 'P') {
            const auto qualification_start = Clock::now();
            const npi::Query scope_query{0, 1, static_cast<npi::Id>(src), -1, -1, -1, 0, 0, 0};
            u.items = eligibility_index->prepare(scope_query).scope_count;
            u.eligibility_us += elapsed(qualification_start);
        } else {
            for (size_t n : moved_nodes)
                u.items += args.arm == 'A' ? direct[n].size() : catalog.at(paths[n]).size();
        }
        std::set<size_t> touched(moved_nodes.begin(), moved_nodes.end());
        touched.insert(static_cast<size_t>(parent[src])); touched.insert(dst);
        std::vector<size_t> old_anc = args.arm == 'C' ?
            ancestors(static_cast<size_t>(parent[src])) : std::vector<size_t>{};
        children[static_cast<size_t>(parent[src])].erase(src);
        children[dst].insert(src);
        parent[src] = static_cast<int>(dst);
        u.writes += 3;
        std::vector<size_t> new_anc = args.arm == 'C' ? ancestors(dst) : std::vector<size_t>{};
        // A/P only relink: no path maintenance or inclusive catalogs.
        if (args.arm == 'B' || args.arm == 'C') {
            std::vector<std::pair<size_t, std::map<std::string, Posting>::node_type>> extracted;
            extracted.reserve(moved_nodes.size());
            for (size_t n : moved_nodes) extracted.emplace_back(n, catalog.extract(paths[n]));
            for (auto& entry : extracted) {
                size_t n = entry.first;
                paths[n] = pathFor(n);
                entry.second.key() = paths[n];
                if (!catalog.insert(std::move(entry.second)).inserted) fail("reparent path collision");
                ++u.writes; u.path_bytes += paths[n].size();
            }
            if (args.arm == 'C') {
                std::set<size_t> old(old_anc.begin(), old_anc.end()), now(new_anc.begin(), new_anc.end());
                for (size_t n : old) if (!now.count(n)) {
                    touched.insert(n);
                    for (Id id : moved_items) {
                        if (catalog.at(paths[n]).erase(id) != 1) fail("missing C reparent old membership");
                        ++u.writes;
                    }
                }
                for (size_t n : now) if (!old.count(n)) {
                    touched.insert(n);
                    for (Id id : moved_items) {
                        if (!catalog.at(paths[n]).insert(id).second) fail("duplicate C reparent new membership");
                        ++u.writes;
                    }
                }
            }
        }
        u.nodes = touched.size();
        if (packed_index) packed_index->reparent(static_cast<npi::Id>(src), static_cast<npi::Id>(dst));
        if (eligibility_index) {
            const auto start = Clock::now();
            eligibility_index->reparent(static_cast<npi::Id>(src), static_cast<npi::Id>(dst));
            u.eligibility_us += elapsed(start);
        }
    }
    template<class... T> static void fields(std::istringstream& in, T&... values) {
        auto parse = [&](auto& value) {
            using Value = std::remove_reference_t<decltype(value)>;
            in >> std::ws;
            if constexpr (std::is_unsigned_v<Value>)
                if (in.peek() == '-') fail("negative unsigned event field");
            if (!(in >> value)) fail("missing or malformed event fields");
        };
        (parse(values), ...);
        std::string extra_field;
        if (in >> extra_field) fail("extra event field: " + extra_field);
    }
    void event(size_t number, const std::string& line) {
        std::istringstream in(line);
        char op = 0; in >> op;
        if (op == 'Q') {
            Query q;
            fields(in, q.qidx, q.recursive, q.anchor, q.category, q.lo, q.hi, q.tagmode, q.tags, q.combine);
            query(number, q); return;
        }
        auto start = Clock::now();
        const uint64_t update_dc0 = space->calls;
        Update u;
        if (op == 'V') {
            Id id; int64_t cat, num; fields(in, id, cat, num); item(id, true);
            u.writes = (items[id].category != cat) + (items[id].numeric != num);
            if (u.writes) {
                items[id].category = cat; items[id].numeric = num;
                u.items = u.nodes = 1;
                syncEligibility(id, u);
            }
        } else if (op == 'T') {
            Id id; uint64_t tags; fields(in, id, tags); item(id, true);
            if (items[id].tags != tags) {
                items[id].tags = tags; u.items = u.nodes = u.writes = 1;
                syncEligibility(id, u);
            }
        } else if (op == 'M') {
            Id id; size_t dst; fields(in, id, dst); item(id, true); node(dst);
            size_t old = items[id].node;
            if (old != dst) {
                u.items = 1; u.nodes = 2; move(id, dst, u);
            }
        } else if (op == 'B') {
            size_t src, dst; fields(in, src, dst); node(src); node(dst);
            if (beneath(dst, src)) fail("B destination must be outside source subtree");
            std::vector<Id> affected;
            std::set<size_t> touched{dst};
            if (args.arm == 'C') {
                const auto& membership = catalog.at(paths[src]);
                affected.assign(membership.begin(), membership.end());
                for (Id id : affected) touched.insert(items[id].node);
            } else if (args.arm == 'P') {
                const auto qualification_start = Clock::now();
                const npi::Query scope_query{0, 1, static_cast<npi::Id>(src), -1, -1, -1, 0, 0, 0};
                const auto& scope_mask = eligibility_index->prepare(scope_query);
                for (size_t word = 0; word < scope_mask.eligible.size(); ++word)
                    for (uint64_t bits = scope_mask.eligible[word]; bits; bits &= bits - 1) {
                        const Id id = word * 64 + __builtin_ctzll(bits);
                        affected.push_back(id);
                        touched.insert(items[id].node);
                    }
                u.eligibility_us += elapsed(qualification_start);
            } else {
                for (size_t n : subtree(src)) {
                    const auto& membership = args.arm == 'A' ? direct[n] : catalog.at(paths[n]);
                    affected.insert(affected.end(), membership.begin(), membership.end());
                    if (!membership.empty()) touched.insert(n);
                }
            }
            for (Id id : affected) move(id, dst, u);
            u.items = affected.size(); u.nodes = affected.empty() ? 0 : touched.size();
        } else if (op == 'R') {
            size_t src, dst; fields(in, src, dst); reparent(src, dst, u);
        } else if (op == 'D') {
            Id id; fields(in, id); item(id, true);
            eraseMembership(id, items[id].node, u);
            if (global) { global->markDelete(id); ++u.deletions; }
            items[id].alive = false; ++u.writes; u.items = u.nodes = 1;
            syncEligibility(id, u);
        } else if (op == 'I') {
            Id id; size_t n; int64_t cat, num; uint64_t tags;
            fields(in, id, n, cat, num, tags); item(id, false); node(n);
            items[id] = Item{n, cat, num, tags, true}; u.writes += 5;
            addMembership(id, n, u);
            if (global) insertGraph(*global, id, u);
            u.items = u.nodes = 1;
            syncEligibility(id, u);
        } else fail("unknown event op " + std::string(1, op));
        const uint64_t event_distances = packed_index ? u.packed_work.distance_calls : space->calls - update_dc0;
        u.insertions += u.packed_work.graph_insertions;
        u.deletions += u.packed_work.graph_deletions;
        u.compactions += u.packed_work.compactions;
        u.compact_us += u.packed_work.rebuild_us;
        double latency = elapsed(start);
        ++version; ++update_count;
        uout << number << ',' << version << ',' << op << ',' << latency << ',' << u.items << ',' << u.nodes << ','
             << u.writes << ',' << u.insertions << ',' << u.deletions << ',' << u.compact_us << ',' << u.compactions
             << ',' << u.path_bytes;
        for (const auto& category : u.maintenance)
            uout << ',' << category.us << ',' << category.count << ',' << category.insertions;
        uout << ',' << u.eligibility_us << ',' << event_distances << ',' << u.packed_work.splits
             << ',' << u.packed_work.rebuild_us
             << ',' << (packed_index ? packed_index->block_count() : 0)
             << ',' << args.pack_layout << ',' << args.pack_size << '\n';
        uout.flush();
        update_times.push_back(latency); update_total += latency;
        eligibility_update_us_total += u.eligibility_us;
        update_distances += event_distances;
        totals.packed_work.splits += u.packed_work.splits;
        totals.packed_work.rebuild_us += u.packed_work.rebuild_us;
        totals.writes += u.writes; totals.insertions += u.insertions; totals.deletions += u.deletions;
        totals.compactions += u.compactions; totals.compact_us += u.compact_us; totals.path_bytes += u.path_bytes;
        for (size_t i = 0; i < 3; ++i) {
            totals.maintenance[i].us += u.maintenance[i].us;
            totals.maintenance[i].count += u.maintenance[i].count;
            totals.maintenance[i].insertions += u.maintenance[i].insertions;
        }
    }
    static double percentile(std::vector<double> v, double p) {
        if (v.empty()) return 0;
        std::sort(v.begin(), v.end());
        double pos = p * (v.size() - 1);
        size_t lo = static_cast<size_t>(pos), hi = std::min(lo + 1, v.size() - 1);
        return v[lo] + (v[hi] - v[lo]) * (pos - lo);
    }
    uint64_t canonicalFingerprint() const {
        uint64_t hash = 14695981039346656037ULL;
        auto mix = [&](uint64_t value) {
            for (unsigned byte = 0; byte < 8; ++byte) {
                hash ^= (value >> (byte * 8)) & 255;
                hash *= 1099511628211ULL;
            }
        };
        mix(items.size()); mix(parent.size());
        for (int p : parent) mix(static_cast<uint64_t>(p));
        for (const auto& v : items) {
            mix(v.node); mix(static_cast<uint64_t>(v.category)); mix(static_cast<uint64_t>(v.numeric));
            mix(v.tags); mix(v.alive);
        }
        return hash;
    }
    void auditPacked() {
        if (!packed_index) return;
        std::vector<npi::Item> canonical;
        canonical.reserve(items.size());
        size_t live = 0;
        for (const auto& v : items) {
            canonical.push_back(eligibilityItem(v));
            live += v.alive;
        }
        if (global || !local.empty() || !catalog.empty() || !direct.empty() ||
            !packed_index->matches(canonical, parent) || live != packed_index->live_count() ||
            !packed_index->valid()) {
            ++violations;
            fail("packed index disagrees with independent canonical state or structural invariants");
        }
    }
    void finalMaintenance() {
        if (!packed_index) return;
        auditPacked(); // Independent checks and fingerprints are never charged as maintenance.
        if (!args.pack_final_repack) return;
        const auto start = Clock::now();
        try {
            packed_index->repack(final_repack_work);
        } catch (...) {
            final_repack_us = elapsed(start);
            throw;
        }
        final_repack_us = elapsed(start);
        auditPacked();
        final_repack_complete = true;
    }
    void summary(bool complete, const std::string& error = "", size_t failed_event = 0) {
        uint64_t graph_bytes = global ? global->indexFileSize() : 0;
        for (const auto& l : local) if (l.graph) graph_bytes += l.graph->indexFileSize();
        if (packed_index) graph_bytes = packed_index->graph_bytes();
        // Explicit logical/container estimate, not RSS or a claim about allocator
        // capacity. Raw input/query vectors are excluded; graph vectors are in
        // indexFileSize. 3 pointers + color/padding estimates an ordered-set node.
        const size_t set_entry = sizeof(Id) + 4 * sizeof(void*);
        uint64_t metadata = items.size() * sizeof(Item) + parent.size() * sizeof(int) +
            children.size() * sizeof(std::set<size_t>);
        for (const auto& c : children) metadata += c.size() * set_entry;
        metadata += direct.size() * sizeof(Posting) + local.size() * sizeof(Local);
        for (const auto& p : direct) metadata += p.size() * set_entry;
        metadata += paths.size() * sizeof(std::string);
        for (const auto& p : paths) metadata += p.size();
        for (const auto& p : catalog)
            metadata += sizeof(p) + 4 * sizeof(void*) + p.first.size() + p.second.size() * set_entry;
        rusage usage{};
        if (getrusage(RUSAGE_SELF, &usage) != 0) fail("getrusage failed");
        std::ofstream out(args.out + ".summary.json");
        if (!out) fail("cannot write summary");
        out << std::setprecision(12) << "{\n";
        auto text = [&](const std::string& key, const std::string& value) {
            out << "  \"" << key << "\": \"";
            for (char c : value) {
                if (c == '\\' || c == '"') out << '\\' << c;
                else if (c == '\n') out << "\\n";
                else if (static_cast<unsigned char>(c) < 32) out << '?';
                else out << c;
            }
            out << "\",\n";
        };
        auto number = [&](const std::string& key, auto value) { out << "  \"" << key << "\": " << value << ",\n"; };
        text("arm", std::string(1, args.arm)); number("ef", args.ef); number("k", args.k);
        number("scan_threshold", args.scan); number("compact_threshold", args.compact);
        text("eligibility_policy", args.eligibility_policy);
        number("eligible_scan_threshold", args.arm == 'P' ? size_t(512) : args.eligible_scan_threshold);
        number("eligibility_build_us", eligibility_build_us);
        const size_t eligibility_bytes = eligibility_index ? eligibility_index->bytes() : 0;
        number("eligibility_index_bytes", eligibility_bytes);
        number("eligibility_local_count_bytes", local_eligible_counts.capacity() * sizeof(size_t));
        number("eligibility_update_us_total", eligibility_update_us_total);
        number("eligibility_us_total", eligibility_us_total);
        number("eligible_scan_us_total", eligible_scan_us_total);
        number("n_vectors", items.size()); number("n_nodes", parent.size()); number("dim", dim);
        number("n_queries", query_index); number("n_updates", update_count); number("build_s", build_s);
        if (!static_passes.empty()) {
            number("static_batch_pass", static_pass);
            number("static_batch_passes", static_passes.size());
            number("static_index_reused", static_pass ? 1 : 0);
            number("session_build_s", build_s);
            number("build_charged_this_pass", static_pass ? 0 : 1);
        }
        metadata += local_eligible_counts.capacity() * sizeof(size_t);
        const size_t packed_bytes = packed_index ? packed_index->bytes() : 0;
        const size_t packed_metadata = packed_index ? packed_index->metadata_bytes() : 0;
        number("packed_index_bytes", packed_bytes);
        number("packed_metadata_bytes", packed_metadata);
        number("index_bytes", (packed_index ? packed_bytes : graph_bytes) + metadata + eligibility_bytes);
        number("hnsw_serialized_bytes", graph_bytes);
        number("metadata_bytes_estimate", metadata + packed_metadata);
        text("index_bytes_kind", packed_index ?
             "packed_capacity_estimate_plus_canonical_metadata_and_eligibility_not_RSS" :
             "hnsw_indexFileSize_plus_explicit_metadata_and_eligibility_estimate_not_RSS");
        text("metadata_estimate_model", "canonical_Item_and_tree_container_sizes+path_characters+ordered_posting_entries(size_t+4_pointers)+local_eligibility_counts+packed_metadata;eligibility_index_reported_separately;raw_input_vectors_excluded");
        text("hnsw_serialized_bytes_note", "indexFileSize_excludes_fixed_per_graph_label_operation_locks_and_other_resident_capacity_overheads;not_RSS");
        number("peak_rss_bytes", uint64_t(usage.ru_maxrss) * 1024);
        number("query_p50_us", percentile(query_times, .5)); number("query_p95_us", percentile(query_times, .95));
        number("query_p99_us", percentile(query_times, .99)); number("update_p50_us", percentile(update_times, .5));
        number("update_p95_us", percentile(update_times, .95)); number("update_p99_us", percentile(update_times, .99));
        number("query_total_us", query_total); number("update_total_us", update_total);
        number("total_workload_us", query_total + update_total + final_repack_us); number("gt_total_us", gt_total);
        text("gt_mode", args.gt_in.empty() ? "inline" : "precomputed");
        number("gt_distance_calls", gt_distance_calls);
        number("compaction_total_us", totals.compact_us); number("compaction_count", totals.compactions);
        for (size_t i = 0; i < 3; ++i) {
            const std::string prefix = maintenance_names[i];
            number(prefix + "_us", totals.maintenance[i].us);
            number(prefix + "_count", totals.maintenance[i].count);
            number(prefix + "_ann_insertions", totals.maintenance[i].insertions);
        }
        const auto final_graph_stats = graphStats();
        number("initial_hnsw_node_count", initial_graph_stats.hnsw_nodes);
        number("initial_index_live_count", initial_graph_stats.live);
        number("initial_index_dead_count", initial_graph_stats.dead);
        number("final_hnsw_node_count", final_graph_stats.hnsw_nodes);
        number("final_index_live_count", final_graph_stats.live);
        number("final_index_dead_count", final_graph_stats.dead);
        number("initial_local_hnsw_index_count", (args.arm == 'A' || args.arm == 'P') ? initial_graph_stats.hnsw_nodes : 0);
        number("final_local_hnsw_index_count", (args.arm == 'A' || args.arm == 'P') ? final_graph_stats.hnsw_nodes : 0);
        number("initial_indexed_vectors", initial_graph_stats.live + initial_graph_stats.dead);
        number("final_indexed_vectors", final_graph_stats.live + final_graph_stats.dead);
        number("metadata_writes", totals.writes); number("path_key_bytes_written", totals.path_bytes);
        number("ann_insertions", totals.insertions); number("ann_deletions", totals.deletions);
        number("build_ann_insertions", build_insertions);
        number("all_ann_insertions", build_insertions + totals.insertions + final_repack_work.graph_insertions);
        number("build_distance_calls", build_distances);
        number("update_distance_calls", update_distances);
        number("all_graph_distance_calls", build_distances + update_distances +
               total_graph_distances + final_repack_work.distance_calls);
        text("pack_layout", args.pack_layout); number("pack_size", args.pack_size);
        number("pack_final_repack", args.pack_final_repack ? 1 : 0);
        number("final_repack_complete", final_repack_complete ? 1 : 0);
        number("final_repack_us", final_repack_us);
        number("final_repack_distance_calls", final_repack_work.distance_calls);
        number("final_repack_ann_insertions", final_repack_work.graph_insertions);
        number("final_repack_ann_deletions", final_repack_work.graph_deletions);
        number("final_repack_compactions", final_repack_work.compactions);
        number("final_repack_splits", final_repack_work.splits);
        number("packed_splits", totals.packed_work.splits);
        number("packed_rebuild_us", totals.packed_work.rebuild_us);
        number("packed_qualified_ids", packed_qualified_ids);
        number("physical_block_count", packed_index ? packed_index->block_count() : 0);
        number("active_packed_graph_count", packed_index ? packed_index->graph_count() : 0);
        number("max_block_target", packed_index ? packed_index->max_block_target() : 0);
        number("max_block_capacity", packed_index ? packed_index->max_block_capacity() : 0);
        text("pack_layout_definition", args.pack_layout == "node" ?
             "candidate_ablation_one_direct_owner_group_per_initial_graph;frozen_physical_grouping_after_logical_moves;not_pure_NodeANN;target_max_1_initial_live;capacity_max_16_2target" :
             "whole_direct_owner_groups_ID_sorted;DFS_or_seeded_group_shuffle;greedy_coalescing_to_B;large_groups_dedicated;target_max_B_initial_live;capacity_2target");
        number("initial_packed_fingerprint", initial_pack_fingerprint);
        number("final_packed_fingerprint", packed_index ? packed_index->fingerprint() : 0);
        number("canonical_fingerprint", canonicalFingerprint());
        text("maintenance_accounting", "event_rebuilds_inside_update_latency;final_repack_separate_and_in_total_workload_us;no_synthetic_events;audits_outside_timers");
        number("macro_recall", query_index ? recall_sum / query_index : 1);
        number("pair_recall", total_gt ? double(total_hits) / total_gt : 1);
        number("gt_count", total_gt); number("hit_count", total_hits); number("zero_gt_queries", zero_gt);
        number("invalid_results", invalid); number("duplicate_results", duplicates); number("state_violations", violations);
        number("fanout_total", total_fanout); number("scan_nodes_total", total_scan_nodes);
        number("directory_visits_total", total_directory_visits);
        number("graph_calls_total", total_graph_calls);
        number("local_graph_calls_total", (args.arm == 'A' || args.arm == 'P') ? total_graph_calls : 0);
        number("scalar_scan_group_calls_total", total_scan_groups);
        number("scanned_live_objects_total", total_scanned_live);
        number("graph_distance_calls", total_graph_distances);
        number("scan_distance_calls", total_scan_distances);
        number("merged_candidates_total", total_merged_candidates);
        number("effective_ef_min", effective_ef_min == NONE ? 0 : effective_ef_min);
        number("effective_ef_max", effective_ef_max);
        number("qualification_directory_visits", total_qualification_directories);
        number("qualification_posting_words", total_qualification_postings);
        number("qualification_attribute_words", total_qualification_attributes);
        number("eligibility_mask_mismatches", total_mask_mismatches);
        text("eligibility_work_definition", "logical_bitmap_words_and_directory_visits_not_hardware_counters;A_local_counts_and_P_physical_counts_enumerate_eligible_set_bits");
        text("directory_visits_definition", "legacy_scope_compilation_directory_entries_excludes_separately_reported_qualification_traversal");
        text("execution_timing_definition", "e2e_includes_qualification_scope_compilation_all_candidate_generation_dedup_and_topk;canonical_GT_validation_excluded");
        text("exact_policy_geometry", "original_graphs_retained_and_maintained;exact_bypasses_query_graph_search_only");
        number("distance_calls", distances); number("ann_hops", hops);
        text("distance_calls_definition", "actual_query_graph_distance_calls_plus_direct_and_eligible_scan_calls;GT_and_build_excluded");
        text("ann_hops_definition", "expanded_graph_neighbor_lists_all_layers");
        text("visibility", "single_thread_serialized_synchronous_query_start_current_committed_state");
        number("failed_event", failed_event); text("error", error);
        out << "  \"complete\": " << (complete ? "true" : "false") << "\n}\n";
        if (!out) fail("failed writing summary");
    }
    void prepareStaticBatch() {
        if (args.static_batch.empty()) return;
        std::ifstream schedule(args.static_batch);
        if (!schedule) fail("cannot open static-batch schedule");
        std::string line, token, trailing;
        if (!std::getline(schedule, line)) fail("missing static-batch count");
        std::istringstream header(line);
        if (!(header >> token) || (header >> trailing)) fail("invalid static-batch header");
        const uint64_t count = GtTape::integer(token);
        if (!count) fail("empty static-batch schedule");
        std::set<std::filesystem::path> outputs;
        for (uint64_t i = 0; i < count; ++i) {
            if (!std::getline(schedule, line)) fail("static-batch ends before declared count");
            std::istringstream row(line);
            StaticPass pass;
            if (!(row >> token >> pass.policy)) fail("malformed static-batch row");
            const uint64_t ef = GtTape::integer(token);
            if (!ef || ef > std::numeric_limits<size_t>::max()) fail("invalid static-batch ef");
            pass.ef = static_cast<size_t>(ef);
            row >> std::ws;
            if (row.peek() != '"' || !(row >> std::quoted(pass.out)) || pass.out.empty() ||
                (row >> trailing)) fail("static-batch requires quoted output prefix and no extra fields");
            for (unsigned char c : pass.out)
                if (c < 32 || c == 127) fail("static-batch output prefix contains control character");
            if ((args.arm == 'A' && pass.policy != "off") ||
                (args.arm == 'P' && pass.policy != "cap" && pass.policy != "hybrid" && pass.policy != "exact"))
                fail("invalid static-batch eligibility policy");
            // Resolve existing parent symlinks when comparing output aliases.
            const auto prefix = std::filesystem::weakly_canonical(std::filesystem::absolute(pass.out));
            if (!outputs.insert(prefix).second) fail("duplicate static-batch output prefix");
            for (const char* suffix : {"", ".queries.csv", ".updates.csv", ".summary.json"}) {
                const std::filesystem::path path(pass.out + suffix);
                if (std::filesystem::exists(path) || std::filesystem::is_symlink(path))
                    fail("static-batch output already exists: " + path.string());
            }
            static_passes.push_back(std::move(pass));
        }
        if (std::getline(schedule, line)) fail("extra static-batch row");
        if (!schedule.eof()) fail("static-batch read failure");
        std::ifstream trace(args.trace);
        if (!std::getline(trace, line)) fail("missing static trace count");
        std::istringstream trace_header(line);
        if (!(trace_header >> token) || (trace_header >> trailing)) fail("invalid static trace header");
        const uint64_t queries = GtTape::integer(token);
        if (!queries) fail("empty static trace");
        for (uint64_t i = 0; i < queries; ++i) {
            if (!std::getline(trace, line)) fail("static trace ends before declared count");
            std::istringstream row(line);
            char op = 0;
            row >> op;
            if (op != 'Q') fail("static-batch trace must contain only Q events");
            Query q;
            fields(row, q.qidx, q.recursive, q.anchor, q.category, q.lo, q.hi, q.tagmode, q.tags, q.combine);
            validateQuery(q);
            static_queries.push_back(q);
        }
        if (std::getline(trace, line)) fail("extra static trace row");
        if (!trace.eof()) fail("static trace read failure");
        gt_tape.rewind(args.gt_in);
    }
    void resetStaticPass() {
        query_index = update_count = 0;
        invalid = duplicates = violations = distances = hops = 0;
        total_gt = total_hits = zero_gt = gt_distance_calls = 0;
        total_fanout = total_scan_nodes = 0;
        total_graph_calls = total_scan_groups = total_scanned_live = 0;
        total_graph_distances = total_scan_distances = total_merged_candidates = 0;
        total_directory_visits = total_qualification_directories = 0;
        total_qualification_postings = total_qualification_attributes = total_mask_mismatches = 0;
        effective_ef_min = NONE; effective_ef_max = 0;
        eligibility_update_us_total = eligibility_us_total = eligible_scan_us_total = 0;
        update_distances = packed_qualified_ids = 0;
        query_total = update_total = gt_total = recall_sum = 0;
        totals = Update{};
        final_repack_us = 0; final_repack_work = packed::UpdateWork{}; final_repack_complete = false;
        query_times.clear(); update_times.clear();
        // Distance deltas are query-local; keep recorded build provenance.
        space->calls = 0;
        gt_tape.rewind(args.gt_in);
    }
    void runStaticBatch() {
        const uint64_t canonical = canonicalFingerprint();
        for (static_pass = 0; static_pass < static_passes.size(); ++static_pass) {
            const auto& pass = static_passes[static_pass];
            args.ef = pass.ef; args.eligibility_policy = pass.policy; args.out = pass.out;
            resetStaticPass();
            openOutputs();
            size_t current = 0;
            try {
                for (const auto& q : static_queries) {
                    query(++current, q);
                    if (!qout || !uout) fail("CSV write failure");
                }
                gt_tape.finish(args.gt_in);
                auditPacked();
                if (canonicalFingerprint() != canonical) fail("static-batch changed canonical state");
                summary(true);
                qout.close(); uout.close();
                if (!qout || !uout) fail("CSV close failure");
            } catch (const std::exception& e) {
                const std::string context = "event " + std::to_string(current) + ": " + e.what();
                summary(false, context, current);
                throw std::runtime_error(context);
            }
        }
    }
public:
    explicit Runner(Args a) : args(std::move(a)) {}
    void load() {
        std::ifstream binding(args.data + "/bindings.txt");
        if (!binding) fail("cannot open bindings.txt");
        size_t n;
        while (binding >> n) { Item v; v.node = n; items.push_back(v); }
        if (!binding.eof() || items.empty()) fail("malformed/empty bindings.txt");
        base = floats(args.data + "/base.f32");
        if (base.empty() || base.size() % items.size()) fail("base.f32 not divisible by binding count");
        dim = base.size() / items.size();
        tests = floats(args.data + "/test.f32");
        if (tests.empty() || tests.size() % dim) fail("test.f32 dimension mismatch");
        // Optional explicit shape declaration, cross-checked against the raw files.
        std::ifstream meta(args.data + "/meta.txt");
        if (meta) {
            size_t declared_n, declared_dim, declared_queries;
            std::string trailing;
            if (!(meta >> declared_n >> declared_dim >> declared_queries) || (meta >> trailing) ||
                declared_n != items.size() || declared_dim != dim || declared_queries != tests.size() / dim)
                fail("meta.txt disagrees with raw vector/binding sizes");
        }
        std::ifstream tree(args.data + "/tree.txt");
        size_t nodes;
        if (!(tree >> nodes) || !nodes || nodes > size_t(std::numeric_limits<int>::max())) fail("bad tree node count");
        parent.assign(nodes, -2); children.resize(nodes);
        for (size_t i = 0; i < nodes; ++i) {
            size_t id; int p;
            if (!(tree >> id >> p) || id >= nodes || p < -1 || p >= static_cast<int>(nodes) || parent[id] != -2)
                fail("malformed/duplicate tree node");
            parent[id] = p;
            if (p == -1) { if (root != NONE) fail("multiple tree roots"); root = id; }
        }
        std::string surplus;
        if (tree >> surplus) fail("extra tree rows");
        if (root == NONE) fail("missing tree root");
        for (size_t i = 0; i < nodes; ++i) {
            if (parent[i] >= 0) children[static_cast<size_t>(parent[i])].insert(i);
            if (!beneath(i, root)) fail("node disconnected from root");
        }
        std::ifstream attr(args.data + "/attrs.txt");
        if (!attr) fail("cannot open attrs.txt");
        for (auto& v : items) {
            int alive;
            node(v.node);
            if (!(attr >> v.category >> v.numeric >> v.tags >> alive) || (alive != 0 && alive != 1)) fail("malformed attrs row");
            v.alive = alive;
        }
        if (attr >> surplus) fail("extra attrs rows");
        if (args.eligibility_policy != "off" && items.size() > size_t(std::numeric_limits<npi::Id>::max()))
            fail("eligibility index item IDs exceed uint32 range");
        prepareStaticBatch();
        auto start = Clock::now();
        space = std::make_unique<CountedL2>(dim);
        Update build;
        if (args.arm == 'A') {
            direct.resize(nodes); local.resize(nodes);
            for (Id id = 0; id < items.size(); ++id) if (items[id].alive) direct[items[id].node].insert(id);
            for (size_t i = 0; i < nodes; ++i) if (direct[i].size() > args.scan) {
                local[i].graph = graph(std::max(size_t(16), direct[i].size() * 2));
                for (Id id : direct[i]) insertGraph(*local[i].graph, id, build);
            }
        } else if (args.arm == 'P') {
            std::vector<npi::Item> metadata;
            metadata.reserve(items.size());
            for (const auto& v : items) metadata.push_back(eligibilityItem(v));
            packed_index = std::make_unique<packed::Index>(base, dim, metadata, parent, args.pack_size, args.pack_layout);
            build.insertions = packed_index->live_count();
        } else {
            paths.resize(nodes);
            for (size_t i = 0; i < nodes; ++i) { paths[i] = pathFor(i); catalog.emplace(paths[i], Posting{}); }
            global = graph(items.size());
            for (Id id = 0; id < items.size(); ++id) if (items[id].alive) {
                if (args.arm == 'B') catalog.at(paths[items[id].node]).insert(id);
                else for (size_t a : ancestors(items[id].node)) catalog.at(paths[a]).insert(id);
                insertGraph(*global, id, build);
            }
        }
        if (args.eligibility_policy != "off") {
            const auto eligibility_start = Clock::now();
            {
                std::vector<npi::Item> metadata;
                metadata.reserve(items.size());
                for (const auto& v : items) metadata.push_back(eligibilityItem(v));
                eligibility_index = std::make_unique<eligibility::Index>(metadata, parent);
            }
            if (args.arm == 'A') local_eligible_counts.resize(nodes, 0);
            eligibility_build_us = elapsed(eligibility_start);
        }
        build_s = elapsed(start) / 1e6;
        build_insertions = build.insertions;
        build_distances = packed_index ? packed_index->build_distance_calls() : space->calls;
        initial_pack_fingerprint = packed_index ? packed_index->fingerprint() : 0;
        auditPacked();
        initial_graph_stats = graphStats();
        if (static_passes.empty()) openOutputs();
    }
    void openOutputs() {
        qout.clear(); uout.clear();
        auto directory = std::filesystem::path(args.out).parent_path();
        if (!directory.empty()) std::filesystem::create_directories(directory);
        qout.open(args.out + ".queries.csv"); uout.open(args.out + ".updates.csv");
        if (!qout || !uout) fail("cannot open CSV outputs");
        qout << std::setprecision(12);
        uout << std::setprecision(12);
        qout << query_columns << ",gt_distance_calls,eligibility_us,eligible_scan_us,eligibility_policy,execution_path,effective_ef"
             << ",qualification_directory_visits,qualification_posting_words,qualification_attribute_words"
             << ",directory_visits,graph_calls,scalar_scan_group_calls,scanned_live_objects"
             << ",graph_distance_calls,scan_distance_calls,merged_candidates,effective_ef_min,effective_ef_max,eligibility_mask_mismatches,local_graph_calls"
             << ",physical_block_count,packed_qualified_ids,pack_layout,pack_size\n";
        uout << "event,version,op,latency_us,affected_items,affected_nodes,metadata_writes,ann_insertions,ann_deletions,compaction_us,compaction_count,path_key_bytes_written";
        for (const char* name : maintenance_names)
            uout << ',' << name << "_us," << name << "_count," << name << "_ann_insertions";
        uout << ",eligibility_update_us,graph_distance_calls,packed_splits,packed_rebuild_us,physical_block_count,pack_layout,pack_size\n";
    }
    void run() {
        if (!static_passes.empty()) { runStaticBatch(); return; }
        std::ifstream trace(args.trace);
        size_t count = 0, current = 0;
        std::string line;
        try {
            if (!(trace >> count)) fail("cannot read trace event count");
            std::getline(trace, line);
            if (line.find_first_not_of(" \t\r") != std::string::npos) fail("extra trace header fields");
            for (current = 1; current <= count; ++current) {
                if (!std::getline(trace, line)) fail("trace ends before declared event count");
                event(current, line);
                if (!qout || !uout) fail("CSV write failure");
            }
            while (std::getline(trace, line))
                if (line.find_first_not_of(" \t\r") != std::string::npos) fail("extra trace event after declared count");
            if (!args.gt_in.empty()) gt_tape.finish(args.gt_in);
            finalMaintenance();
            summary(true);
        } catch (const std::exception& e) {
            std::string context = "event " + std::to_string(current) + ": " + e.what();
            summary(false, context, current);
            throw std::runtime_error(context);
        }
    }
};
} // namespace
int main(int argc, char** argv) {
    try {
        Runner runner(arguments(argc, argv));
        runner.load(); runner.run();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "dynamic_triad: " << e.what() << '\n';
        return 1;
    }
}
