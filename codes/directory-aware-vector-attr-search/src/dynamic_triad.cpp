// Dynamic A/B/C replay. The frozen static/autoresearch runners are independent.
#include "raw_l2_space.h"
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
    std::string data, trace, out;
    char arm = 0;
    size_t ef = 256, scan = 512, k = 10;
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
        else if (key == "--arm") {
            if (value.size() != 1) fail("arm must be A, B or C");
            a.arm = value[0];
        } else if (key == "--ef") a.ef = std::stoull(value);
        else if (key == "--scan-threshold") a.scan = std::stoull(value);
        else if (key == "--compact-threshold") a.compact = std::stod(value);
        else if (key == "--k") a.k = std::stoull(value);
        else fail("unknown option " + key);
    }
    if (a.data.empty() || a.trace.empty() || a.out.empty() ||
        (a.arm != 'A' && a.arm != 'B' && a.arm != 'C') || !a.ef || !a.k ||
        !std::isfinite(a.compact) || a.compact <= 0 || a.compact > 1)
        fail("usage: dynamic_triad --data DIR --trace FILE --arm A|B|C --out PREFIX "
             "[--ef 256 --scan-threshold 512 --compact-threshold .2 --k 10]");
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
struct Update {
    uint64_t items = 0, nodes = 0, writes = 0, insertions = 0, deletions = 0, compactions = 0;
    uint64_t path_bytes = 0;
    double compact_us = 0;
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
    size_t dim = 0, root = NONE, version = 0, query_index = 0, update_count = 0;
    uint64_t invalid = 0, duplicates = 0, violations = 0, distances = 0, hops = 0;
    uint64_t total_gt = 0, total_hits = 0, zero_gt = 0;
    uint64_t build_insertions = 0;
    uint64_t total_fanout = 0, total_scan_nodes = 0;
    double build_s = 0, query_total = 0, update_total = 0, gt_total = 0, recall_sum = 0;
    Update totals;
    std::vector<double> query_times, update_times;
    std::ofstream qout, uout;

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
    void reconcile(size_t n, Update& u) {
        auto& g = local[n].graph;
        bool wanted = direct[n].size() > args.scan;
        bool deleted = g && g->cur_element_count &&
            double(g->num_deleted_) / double(g->cur_element_count) >= args.compact;
        if ((!wanted && g) || (wanted && (!g || deleted))) {
            auto start = Clock::now();
            if (!wanted) g.reset();
            else {
                auto fresh = graph(std::max(size_t(16), direct[n].size() * 2));
                for (Id id : direct[n]) insertGraph(*fresh, id, u);
                g = std::move(fresh);
            }
            u.compact_us += elapsed(start);
            ++u.compactions;
        }
    }
    void eraseMembership(Id id, size_t n, Update& u) {
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
    }

    // Same pinned hnswlib searchKnn algorithm, enabling its base-layer hop
    // instrumentation (searchKnn's default disables it). No exact ANN fallback.
    std::vector<Neighbor> searchGraph(Graph& g, const float* q, size_t k,
                                      hnswlib::BaseFilterFunctor& filter, uint64_t& query_hops) {
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
        auto found = g.searchBaseLayerST<false, true>(current, q, std::max(args.ef, k), &filter);
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
        Filter(const Runner& r, const Query& q, const Posting* a, size_t n)
            : runner(r), query(q), allowed(a), direct_node(n) {}
        bool operator()(hnswlib::labeltype id) override {
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
    static std::string ids(const std::vector<Neighbor>& values) {
        std::ostringstream out;
        for (size_t i = 0; i < values.size(); ++i) { if (i) out << ';'; out << values[i].second; }
        return out.str();
    }
    void query(size_t event, const Query& q) {
        node(q.anchor);
        if (q.qidx >= tests.size() / dim || (q.recursive != 0 && q.recursive != 1) ||
            q.tagmode < 0 || q.tagmode > 4 ||
            (q.combine != 0 && q.combine != 1) ||
            ((q.lo != -1 || q.hi != -1) && q.lo > q.hi)) fail("invalid Q fields");
        const float* qvec = tests.data() + q.qidx * dim;
        size_t anchor_depth = depth(q.anchor);
        auto e2e_start = Clock::now();
        size_t scope = 0;
        std::vector<std::pair<size_t, size_t>> local_work;
        Posting candidates;
        const Posting* allowed = &candidates;
        if (args.arm == 'A') {
            auto nodes = q.recursive ? subtree(q.anchor) : std::vector<size_t>{q.anchor};
            for (size_t n : nodes) {
                if (direct[n].empty()) continue;
                scope += direct[n].size();
                local_work.emplace_back(n, direct[n].size());
            }
        } else if (args.arm == 'B') {
            const std::string& key = paths[q.anchor];
            if (!q.recursive) allowed = &catalog.at(key);
            else {
                const auto own = catalog.find(key);
                candidates.insert(own->second.begin(), own->second.end());
                const std::string prefix = key + '/';
                for (auto it = catalog.lower_bound(prefix);
                     it != catalog.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it)
                    candidates.insert(it->second.begin(), it->second.end());
            }
        } else {
            if (q.recursive) allowed = &catalog.at(paths[q.anchor]);
            else {
                candidates = catalog.at(paths[q.anchor]);
                for (size_t child : children[q.anchor])
                    for (Id id : catalog.at(paths[child])) candidates.erase(id);
            }
        }
        if (args.arm != 'A') scope = allowed->size();
        double dir_us = elapsed(e2e_start);
        auto search_start = Clock::now();
        uint64_t dc0 = space->calls, scan_calls = 0, query_hops = 0;
        size_t local_indexes = 0;
        std::vector<Neighbor> result;
        if (args.arm == 'A') {
            for (auto [n, count] : local_work) {
                size_t k = std::min(args.k, count);
                std::vector<Neighbor> found;
                if (local[n].graph) {
                    ++local_indexes;
                    Filter filter(*this, q, nullptr, n);
                    found = searchGraph(*local[n].graph, qvec, k, filter, query_hops);
                } else {
                    if (direct[n].size() > args.scan) fail("missing A index above scan threshold");
                    for (Id id : direct[n]) if (items[id].alive && extra(items[id], q)) {
                        found.emplace_back(space->dist(qvec, vector(id)), id);
                        ++scan_calls;
                    }
                    top(found, k);
                }
                result.insert(result.end(), found.begin(), found.end());
            }
        } else if (scope) {
            Filter filter(*this, q, allowed, NONE);
            result = searchGraph(*global, qvec, std::min(args.k, scope), filter, query_hops);
        }
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
        uint64_t dc = space->calls - dc0 + scan_calls;
        // Independent current-state oracle, never consulting paths/postings.
        auto gt_start = Clock::now();
        std::vector<Neighbor> gt;
        Posting oracle_scope_ids;
        size_t oracle_scope = 0, oracle_eligible = 0;
        for (Id id = 0; id < items.size(); ++id) {
            const auto& v = items[id];
            if (!v.alive || !(q.recursive ? beneath(v.node, q.anchor) : v.node == q.anchor)) continue;
            ++oracle_scope;
            oracle_scope_ids.insert(id);
            if (!extra(v, q)) continue;
            ++oracle_eligible;
            gt.emplace_back(space->dist(qvec, vector(id)), id);
        }
        top(gt, args.k);
        double gt_us = elapsed(gt_start);
        size_t hit = 0;
        Posting gt_set;
        for (auto v : gt) gt_set.insert(v.second);
        for (auto v : result) if (gt_set.count(v.second)) ++hit;
        // A mismatch is counted, recorded and fatal: no stale, silently filtered,
        // duplicated, or undercounted directory state is accepted as a data point.
        for (const auto& v : result) if (!matches(v.second, q)) ++bad;
        size_t state_errors = (scope != oracle_scope) + bad + duplicate;
        if (args.arm == 'A') {
            for (Id id : oracle_scope_ids)
                if (!direct[items[id].node].count(id)) ++state_errors;
        } else if (*allowed != oracle_scope_ids) {
            // Full set equality, not merely matching cardinalities. Oracle work
            // remains outside dir/search/e2e timing.
            ++state_errors;
        }
        if (result.size() > gt.size()) ++state_errors;
        size_t valid_count = 0;
        for (const auto& v : result) valid_count += matches(v.second, q);
        double recall = gt.empty() ? (result.empty() ? 1.0 : 0.0) : double(hit) / gt.size();
        const size_t fanout = args.arm == 'A' ? local_work.size() : size_t(scope != 0);
        const size_t scan_nodes = args.arm == 'A' ? local_work.size() - local_indexes : 0;
        qout << event << ',' << version << ',' << query_index << ',' << q.qidx << ',' << q.recursive << ','
             << q.anchor << ',' << anchor_depth << ',' << scope << ',' << oracle_eligible << ',' << dir_us << ','
             << search_us << ',' << e2e_us << ',' << gt_us << ',' << gt.size() << ',' << hit << ',' << recall << ','
             << dc << ',' << query_hops << ',' << local_indexes << ',' << valid_count
             << ',' << bad << ',' << duplicate << ',' << ids(gt) << ',' << ids(result)
             << ',' << fanout << ',' << scan_nodes << '\n';
        qout.flush();
        ++query_index;
        query_times.push_back(e2e_us);
        query_total += e2e_us; gt_total += gt_us; recall_sum += recall;
        total_gt += gt.size(); total_hits += hit; zero_gt += gt.empty();
        invalid += bad; duplicates += duplicate; violations += state_errors;
        distances += dc; hops += query_hops;
        total_fanout += fanout; total_scan_nodes += scan_nodes;
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
        // A only relinks: no path maintenance, inclusive postings or DFS labels.
        if (args.arm != 'A') {
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
        Update u;
        if (op == 'V') {
            Id id; int64_t cat, num; fields(in, id, cat, num); item(id, true);
            u.writes = (items[id].category != cat) + (items[id].numeric != num);
            if (u.writes) {
                items[id].category = cat; items[id].numeric = num;
                u.items = u.nodes = 1;
            }
        } else if (op == 'T') {
            Id id; uint64_t tags; fields(in, id, tags); item(id, true);
            if (items[id].tags != tags) {
                items[id].tags = tags; u.items = u.nodes = u.writes = 1;
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
        } else if (op == 'I') {
            Id id; size_t n; int64_t cat, num; uint64_t tags;
            fields(in, id, n, cat, num, tags); item(id, false); node(n);
            items[id] = Item{n, cat, num, tags, true}; u.writes += 5;
            addMembership(id, n, u);
            if (global) insertGraph(*global, id, u);
            u.items = u.nodes = 1;
        } else fail("unknown event op " + std::string(1, op));
        double latency = elapsed(start);
        ++version; ++update_count;
        uout << number << ',' << version << ',' << op << ',' << latency << ',' << u.items << ',' << u.nodes << ','
             << u.writes << ',' << u.insertions << ',' << u.deletions << ',' << u.compact_us << ',' << u.compactions
             << ',' << u.path_bytes << '\n';
        uout.flush();
        update_times.push_back(latency); update_total += latency;
        totals.writes += u.writes; totals.insertions += u.insertions; totals.deletions += u.deletions;
        totals.compactions += u.compactions; totals.compact_us += u.compact_us; totals.path_bytes += u.path_bytes;
    }
    static double percentile(std::vector<double> v, double p) {
        if (v.empty()) return 0;
        std::sort(v.begin(), v.end());
        double pos = p * (v.size() - 1);
        size_t lo = static_cast<size_t>(pos), hi = std::min(lo + 1, v.size() - 1);
        return v[lo] + (v[hi] - v[lo]) * (pos - lo);
    }
    void summary(bool complete, const std::string& error = "", size_t failed_event = 0) {
        uint64_t graph_bytes = global ? global->indexFileSize() : 0;
        for (const auto& l : local) if (l.graph) graph_bytes += l.graph->indexFileSize();
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
        number("n_vectors", items.size()); number("n_nodes", parent.size()); number("dim", dim);
        number("n_queries", query_index); number("n_updates", update_count); number("build_s", build_s);
        number("index_bytes", graph_bytes + metadata); number("hnsw_serialized_bytes", graph_bytes);
        number("metadata_bytes_estimate", metadata);
        text("index_bytes_kind", "hnsw_indexFileSize_plus_explicit_metadata_estimate_not_RSS");
        text("metadata_estimate_model", "canonical_Item_and_tree_container_sizes+path_characters+ordered_posting_entries(size_t+4_pointers);raw_input_vectors_excluded");
        number("peak_rss_bytes", uint64_t(usage.ru_maxrss) * 1024);
        number("query_p50_us", percentile(query_times, .5)); number("query_p95_us", percentile(query_times, .95));
        number("query_p99_us", percentile(query_times, .99)); number("update_p50_us", percentile(update_times, .5));
        number("update_p95_us", percentile(update_times, .95)); number("update_p99_us", percentile(update_times, .99));
        number("query_total_us", query_total); number("update_total_us", update_total);
        number("total_workload_us", query_total + update_total); number("gt_total_us", gt_total);
        number("compaction_total_us", totals.compact_us); number("compaction_count", totals.compactions);
        number("metadata_writes", totals.writes); number("path_key_bytes_written", totals.path_bytes);
        number("ann_insertions", totals.insertions); number("ann_deletions", totals.deletions);
        number("build_ann_insertions", build_insertions);
        number("all_ann_insertions", build_insertions + totals.insertions);
        number("macro_recall", query_index ? recall_sum / query_index : 1);
        number("pair_recall", total_gt ? double(total_hits) / total_gt : 1);
        number("gt_count", total_gt); number("hit_count", total_hits); number("zero_gt_queries", zero_gt);
        number("invalid_results", invalid); number("duplicate_results", duplicates); number("state_violations", violations);
        number("fanout_total", total_fanout); number("scan_nodes_total", total_scan_nodes);
        number("distance_calls", distances); number("ann_hops", hops);
        text("distance_calls_definition", "actual_query_graph_distance_calls_plus_A_direct_scan_calls;GT_and_build_excluded");
        text("ann_hops_definition", "expanded_graph_neighbor_lists_all_layers");
        text("visibility", "single_thread_serialized_synchronous_query_start_current_committed_state");
        number("failed_event", failed_event); text("error", error);
        out << "  \"complete\": " << (complete ? "true" : "false") << "\n}\n";
        if (!out) fail("failed writing summary");
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
        build_s = elapsed(start) / 1e6;
        build_insertions = build.insertions;
        auto directory = std::filesystem::path(args.out).parent_path();
        if (!directory.empty()) std::filesystem::create_directories(directory);
        qout.open(args.out + ".queries.csv"); uout.open(args.out + ".updates.csv");
        if (!qout || !uout) fail("cannot open CSV outputs");
        qout << std::setprecision(12);
        uout << std::setprecision(12);
        qout << "event,version,query_index,qidx,recursive,anchor,anchor_depth,scope_card,eligible_card,dir_us,search_us,e2e_us,gt_us,gt_count,hit_count,recall,distance_calls,ann_hops,local_indexes,valid_count,invalid_count,duplicate_count,gt_ids,result_ids,fanout,scan_nodes\n";
        uout << "event,version,op,latency_us,affected_items,affected_nodes,metadata_writes,ann_insertions,ann_deletions,compaction_us,compaction_count,path_key_bytes_written\n";
    }
    void run() {
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
