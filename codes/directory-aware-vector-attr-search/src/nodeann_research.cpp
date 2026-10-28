// NodeANN research runner on the pinned hnswlib backend.
//
// Frozen backend contract (2026-10-29 plan):
//   * hnswlib revision ca426729609b3221563047ceb269cc1213e0d376
//     (vendored at deps/hnswlib, git submodule).
//   * Every raw FP32 squared-L2 distance goes through RawL2Space, which calls
//     hnswlib::L2Space(dim).get_dist_func(). No other distance kernel exists
//     in this binary.
//   * Retrieval is always single-threaded. --threads only parallelizes the
//     per-node index construction phase (each node index is built with one
//     OS thread inside the process; the driver may also shard across
//     processes).
//   * Result ordering ties are broken by (distance, global_id). An empty
//     scope yields ten -1 ids.
//
// Modes:
//   exact   : scan every object in the query scope with the shared distance
//             (B3 diagnostic / ground truth generator).
//   nodeann : NodeANN. Per non-empty direct node L(v): |L(v)| <= T -> shared
//             distance exact scan; else a per-node hnswlib HierarchicalNSW
//             (M=16, efConstruction=200, fixed seed). T=0 reproduces B0 (pure
//             NodeANN), T>0 is B1.
//
// Inputs under --data:
//   base.f32     LE float32 row-major, n*dim values, no header
//   test.f32     same, m*dim
//   tree.txt     first line: node count N; then N lines "id parent" (root=-1)
//   bindings.txt one node id per line; line i = direct node of global id i
//   workload.txt first line "nq 0"; then "qidx rec anchor" per line

#include "raw_l2_space.h"

#include <sys/resource.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace {

constexpr int kK = 10;              // fixed top-k
constexpr size_t kHnswM = 16;       // frozen B0/B1 parameter
constexpr size_t kEfConstruction = 200;
constexpr size_t kHnswSeed = 100;   // frozen seed

using Clock = std::chrono::steady_clock;
static double us_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}

// ------------------------------------------------------------------ args ----
struct Args {
    std::string data;
    int dim = 0;
    std::string mode;          // "nodeann" | "exact"
    size_t scan_threshold = 0; // T
    size_t ef = 64;
    int threads = 1;           // build parallelism only
    std::string csv;           // output path prefix
};

[[noreturn]] void die(const std::string& msg) {
    std::fprintf(stderr, "nodeann_research: %s\n", msg.c_str());
    std::exit(2);
}

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) die(std::string("missing value for ") + name);
            return argv[++i];
        };
        if (k == "--data") a.data = next("--data");
        else if (k == "--dim") a.dim = std::stoi(next("--dim"));
        else if (k == "--mode") a.mode = next("--mode");
        else if (k == "--scan-threshold")
            a.scan_threshold = std::stoull(next("--scan-threshold"));
        else if (k == "--ef") a.ef = std::stoull(next("--ef"));
        else if (k == "--threads") a.threads = std::stoi(next("--threads"));
        else if (k == "--csv") a.csv = next("--csv");
        else if (k == "--help" || k == "-h") {
            std::printf(
                "usage: nodeann_research --data DIR --dim D "
                "--mode nodeann|exact [--scan-threshold T] [--ef N] "
                "[--threads P] --csv PREFIX\n");
            std::exit(0);
        } else die("unknown argument: " + k);
    }
    if (a.data.empty() || a.dim <= 0 || a.mode.empty() || a.csv.empty())
        die("required: --data --dim --mode --csv");
    if (a.mode != "nodeann" && a.mode != "exact")
        die("--mode must be nodeann or exact");
    if (a.threads < 1) a.threads = 1;
    return a;
}

// ----------------------------------------------------------------- data ----
struct Tree {
    int n = 0;
    std::vector<int> parent;               // -1 = root
    std::vector<std::vector<int>> children;
    std::vector<int> depth;
};

struct WorkloadQuery {
    int qidx = 0;      // row in test.f32
    int recursive = 0;
    int anchor = 0;
};

std::vector<float> load_f32(const std::string& path, size_t expect_elems) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) die("cannot open " + path);
    const std::streamoff bytes = in.tellg();
    if (bytes % 4 != 0) die("bad f32 file size: " + path);
    const size_t elems = static_cast<size_t>(bytes) / 4;
    if (expect_elems && elems != expect_elems)
        die("unexpected element count in " + path);
    std::vector<float> v(elems);
    in.seekg(0);
    in.read(reinterpret_cast<char*>(v.data()),
            static_cast<std::streamsize>(bytes));
    if (!in) die("short read: " + path);
    return v;
}

Tree load_tree(const std::string& path) {
    std::ifstream in(path);
    if (!in) die("cannot open " + path);
    Tree t;
    size_t n;
    in >> n;
    t.n = static_cast<int>(n);
    t.parent.assign(t.n, -1);
    t.children.assign(t.n, {});
    for (size_t i = 0; i < n; ++i) {
        int id, p;
        in >> id >> p;
        if (!in || id < 0 || id >= t.n) die("bad tree line in " + path);
        t.parent[id] = p;
        if (p >= 0) {
            if (p >= t.n) die("bad parent in " + path);
            t.children[p].push_back(id);
        }
    }
    // Depth via DFS from roots (node ids need not be topological).
    t.depth.assign(t.n, 0);
    std::vector<int> stack;
    for (int i = 0; i < t.n; ++i)
        if (t.parent[i] < 0) stack.push_back(i);
    if (stack.empty() && t.n > 0) die("tree has no root: " + path);
    while (!stack.empty()) {
        int v = stack.back();
        stack.pop_back();
        for (int c : t.children[v]) {
            t.depth[c] = t.depth[v] + 1;
            stack.push_back(c);
        }
    }
    return t;
}

std::vector<int> load_bindings(const std::string& path, size_t n_base,
                               int n_nodes) {
    std::ifstream in(path);
    if (!in) die("cannot open " + path);
    std::vector<int> bind(n_base, -1);
    std::string line;
    size_t i = 0;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (i >= n_base) die("bindings longer than base: " + path);
        int node = std::stoi(line);
        if (node < 0 || node >= n_nodes)
            die("binding out of range at line " + std::to_string(i + 1));
        bind[i] = node;
        ++i;
    }
    if (i != n_base)
        die("bindings (" + std::to_string(i) + ") != base rows (" +
            std::to_string(n_base) + "): " + path);
    return bind;
}

std::vector<WorkloadQuery> load_workload(const std::string& path) {
    std::ifstream in(path);
    if (!in) die("cannot open " + path);
    std::vector<WorkloadQuery> w;
    size_t nq = 0;
    int dummy = 0;
    in >> nq >> dummy;  // first line is "nq 0"
    if (!in) die("bad workload header: " + path);
    w.reserve(nq);
    for (size_t i = 0; i < nq; ++i) {
        WorkloadQuery q;
        in >> q.qidx >> q.recursive >> q.anchor;
        if (!in) die("bad workload row " + std::to_string(i + 1));
        w.push_back(q);
    }
    return w;
}

// --------------------------------------------------------------- scoring ----
// Item ordering: smaller (distance, id) is better. Heap top keeps the worst.
struct Item {
    float dist;
    int id;
};
struct Worse {
    bool operator()(const Item& a, const Item& b) const {
        if (a.dist != b.dist) return a.dist < b.dist;
        return a.id < b.id;
    }
};
using WorstHeap = std::priority_queue<Item, std::vector<Item>, Worse>;

// Bounded best-k collector over the shared hnswlib distance.
class TopKCollector {
public:
    explicit TopKCollector(size_t k) : k_(k) {}
    void offer(float d, int id, uint64_t* offers, uint64_t* pops) {
        if (k_ == 0) return;
        ++*offers;
        if (heap_.size() < k_) {
            heap_.push({d, id});
        } else if (Worse{}(Item{d, id}, heap_.top())) {
            heap_.pop();
            ++*pops;
            heap_.push({d, id});
        }
    }
    // Ascending by (distance, id).
    std::vector<Item> sorted() const {
        std::vector<Item> v;
        auto h = heap_;
        while (!h.empty()) {
            v.push_back(h.top());
            h.pop();
        }
        std::sort(v.begin(), v.end(), [](const Item& a, const Item& b) {
            if (a.dist != b.dist) return a.dist < b.dist;
            return a.id < b.id;
        });
        return v;
    }
    size_t size() const { return heap_.size(); }

private:
    size_t k_;
    WorstHeap heap_;
};

// ------------------------------------------------------------- node index ----
struct NodeIndex {
    std::unique_ptr<hnswlib::HierarchicalNSW<float>> hnsw;
    size_t serialized_bytes = 0;
};

// Per-query counters.
struct QueryStats {
    int qidx = 0;
    int recursive = 0;
    int anchor = 0;
    int anchor_depth = 0;
    size_t scope_card = 0;
    size_t candidate_nodes = 0;
    size_t scan_nodes = 0;
    size_t hnsw_nodes = 0;
    size_t scan_items = 0;
    size_t hnsw_items = 0;
    uint64_t distance_calls = 0;   // shared-distance computations
    uint64_t hnsw_hops = 0;        // graph neighbor-list expansions
    uint64_t local_results = 0;    // items fed into the global merge
    uint64_t heap_offers = 0;      // global merge heap offers
    uint64_t heap_pops = 0;        // global merge heap evictions
    double dir_us = 0, scan_us = 0, hnsw_us = 0, merge_us = 0;
    double e2e_ms = 0;
    std::vector<Item> result;
    int gt_count = 0;
};

std::string join_ids(const std::vector<Item>& v, bool dists) {
    std::ostringstream os;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) os << ";";
        if (dists) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.9g", static_cast<double>(v[i].dist));
            os << buf;
        } else {
            os << v[i].id;
        }
    }
    return os.str();
}

}  // namespace

int main(int argc, char** argv) {
    const Args args = parse_args(argc, argv);
    const std::string& D = args.data;

    std::vector<float> base = load_f32(D + "/base.f32", 0);
    if (base.size() % static_cast<size_t>(args.dim) != 0)
        die("base.f32 size not divisible by dim");
    const size_t n_base = base.size() / args.dim;
    std::vector<float> test = load_f32(D + "/test.f32", 0);
    if (test.size() % static_cast<size_t>(args.dim) != 0)
        die("test.f32 size not divisible by dim");
    const size_t n_test = test.size() / args.dim;

    Tree tree = load_tree(D + "/tree.txt");
    std::vector<int> bind = load_bindings(D + "/bindings.txt", n_base, tree.n);
    std::vector<WorkloadQuery> work = load_workload(D + "/workload.txt");
    for (const auto& q : work) {
        if (q.qidx < 0 || static_cast<size_t>(q.qidx) >= n_test)
            die("workload qidx out of range");
        if (q.anchor < 0 || q.anchor >= tree.n)
            die("workload anchor out of range");
    }

    // Direct node lists L(v).
    std::vector<std::vector<int>> direct(tree.n);
    for (size_t id = 0; id < n_base; ++id)
        direct[bind[id]].push_back(static_cast<int>(id));
    size_t n_nonempty = 0;
    for (const auto& l : direct)
        if (!l.empty()) ++n_nonempty;

    // The single shared raw distance implementation.
    RawL2Space l2(static_cast<size_t>(args.dim));

    // Build per-node hnswlib indexes for nodes larger than the scan threshold.
    std::vector<NodeIndex> indexes(tree.n);
    std::vector<int> build_list;
    if (args.mode == "nodeann") {
        for (int v = 0; v < tree.n; ++v)
            if (direct[v].size() > args.scan_threshold)
                build_list.push_back(v);
    }
    const auto t_build0 = Clock::now();
    const std::clock_t c_build0 = std::clock();
    std::atomic<size_t> built{0};
    {
        std::vector<std::thread> pool;
        std::atomic<size_t> next{0};
        for (int t = 0; t < args.threads; ++t) {
            pool.emplace_back([&] {
                for (;;) {
                    const size_t i = next.fetch_add(1);
                    if (i >= build_list.size()) break;
                    const int v = build_list[i];
                    auto idx = std::make_unique<hnswlib::HierarchicalNSW<float>>(
                        &l2, direct[v].size(), kHnswM, kEfConstruction,
                        kHnswSeed, false);
                    for (int id : direct[v])
                        idx->addPoint(base.data() + static_cast<size_t>(id) *
                                                        args.dim,
                                      id, false);
                    indexes[v].hnsw = std::move(idx);
                    ++built;
                }
            });
        }
        for (auto& th : pool) th.join();
    }
    const double build_wall_s =
        std::chrono::duration<double>(Clock::now() - t_build0).count();
    const double build_cpu_s =
        double(std::clock() - c_build0) / CLOCKS_PER_SEC;

    // Serialized size accounting (stream to a counting sink).
    size_t serialized_bytes = 0;
    {
        struct CountBuf : std::streambuf {
            size_t n = 0;
            std::streamsize xsputn(const char*, std::streamsize m) override {
                n += static_cast<size_t>(m);
                return m;
            }
            int overflow(int c) override {
                ++n;
                return c;
            }
        } buf;
        std::ostream os(&buf);
        for (int v : build_list) {
            const size_t before = buf.n;
            indexes[v].hnsw->saveIndexNoExceptions(os);
            indexes[v].serialized_bytes = buf.n - before;
            serialized_bytes += indexes[v].serialized_bytes;
        }
    }

    // Query loop. Always single-threaded.
    std::vector<QueryStats> stats(work.size());
    std::vector<int> stack;
    for (size_t qi = 0; qi < work.size(); ++qi) {
        const WorkloadQuery& q = work[qi];
        QueryStats s;
        s.qidx = q.qidx;
        s.recursive = q.recursive;
        s.anchor = q.anchor;
        s.anchor_depth = tree.depth[q.anchor];
        const auto t_q0 = Clock::now();

        // Directory enumeration: candidate non-empty direct nodes + scope card.
        const auto t_dir0 = Clock::now();
        std::vector<int> cand;
        if (q.recursive) {
            stack.clear();
            stack.push_back(q.anchor);
            while (!stack.empty()) {
                const int v = stack.back();
                stack.pop_back();
                if (!direct[v].empty()) cand.push_back(v);
                for (int c : tree.children[v]) stack.push_back(c);
            }
        } else if (!direct[q.anchor].empty()) {
            cand.push_back(q.anchor);
        }
        size_t scope_card = 0;
        for (int v : cand) scope_card += direct[v].size();
        s.scope_card = scope_card;
        s.candidate_nodes = cand.size();
        s.dir_us = us_since(t_dir0);

        if (scope_card == 0) {
            s.result.clear();
            s.gt_count = 0;
        } else {
            const float* qvec =
                test.data() + static_cast<size_t>(q.qidx) * args.dim;
            TopKCollector global(kK);

            if (args.mode == "exact") {
                s.scan_nodes = 1;
                const auto t_scan0 = Clock::now();
                for (int v : cand) {
                    for (int id : direct[v]) {
                        const float d = l2.dist(
                            qvec, base.data() + static_cast<size_t>(id) *
                                                    args.dim);
                        ++s.distance_calls;
                        ++s.scan_items;
                        global.offer(d, id, &s.heap_offers, &s.heap_pops);
                    }
                }
                s.scan_us = us_since(t_scan0);
            } else {
                std::vector<Item> local;
                for (int v : cand) {
                    const size_t sz = direct[v].size();
                    const size_t kk = std::min(static_cast<size_t>(kK), sz);
                    if (sz <= args.scan_threshold) {
                        ++s.scan_nodes;
                        s.scan_items += sz;
                        const auto t_scan0 = Clock::now();
                        TopKCollector loc(kk);
                        uint64_t offers = 0, pops = 0;
                        for (int id : direct[v]) {
                            const float d = l2.dist(
                                qvec, base.data() +
                                          static_cast<size_t>(id) * args.dim);
                            ++s.distance_calls;
                            loc.offer(d, id, &offers, &pops);
                        }
                        s.scan_us += us_since(t_scan0);
                        local = loc.sorted();
                    } else {
                        ++s.hnsw_nodes;
                        s.hnsw_items += sz;
                        auto& idx = indexes[v].hnsw;
                        idx->setEf(std::max(args.ef, kk));
                        const long hops0 = idx->metric_hops;
                        const long dc0 = idx->metric_distance_computations;
                        const auto t_hnsw0 = Clock::now();
                        auto res = idx->searchKnn(qvec, kk, nullptr);
                        s.hnsw_us += us_since(t_hnsw0);
                        s.hnsw_hops += uint64_t(idx->metric_hops - hops0);
                        s.distance_calls += uint64_t(idx->metric_distance_computations -
                                                     dc0);
                        local.clear();
                        local.reserve(res.size());
                        while (!res.empty()) {
                            local.push_back({res.top().first,
                                             static_cast<int>(res.top().second)});
                            res.pop();
                        }
                        std::sort(local.begin(), local.end(),
                                  [](const Item& a, const Item& b) {
                                      if (a.dist != b.dist)
                                          return a.dist < b.dist;
                                      return a.id < b.id;
                                  });
                    }
                    const auto t_merge0 = Clock::now();
                    for (const Item& it : local)
                        global.offer(it.dist, it.id, &s.heap_offers,
                                     &s.heap_pops);
                    s.local_results += local.size();
                    s.merge_us += us_since(t_merge0);
                }
            }
            const auto t_merge0 = Clock::now();
            s.result = global.sorted();
            s.merge_us += us_since(t_merge0);
            s.gt_count = static_cast<int>(
                std::min(kK, static_cast<int>(scope_card)));
        }
        s.e2e_ms = std::chrono::duration<double, std::milli>(Clock::now() -
                                                             t_q0)
                       .count();
        stats[qi] = std::move(s);
    }

    // Peak RSS of this process.
    struct rusage ru {};
    getrusage(RUSAGE_SELF, &ru);
    const size_t peak_rss_bytes =
        static_cast<size_t>(ru.ru_maxrss) * 1024;  // Linux: KiB

    // Latency percentiles over e2e_ms.
    std::vector<double> lat;
    lat.reserve(stats.size());
    for (const auto& s : stats) lat.push_back(s.e2e_ms);
    std::sort(lat.begin(), lat.end());
    auto pct = [&](double p) {
        if (lat.empty()) return 0.0;
        const size_t i = static_cast<size_t>(
            std::min(lat.size() - 1,
                     static_cast<size_t>(p * static_cast<double>(lat.size()))));
        return lat[i];
    };
    double mean = 0;
    for (double v : lat) mean += v;
    if (!lat.empty()) mean /= lat.size();

    // ---- CSV ----
    const std::string csv_path = args.csv + ".perquery.csv";
    std::ofstream csv(csv_path);
    if (!csv) die("cannot write " + csv_path);
    csv << "query_index,qidx,recursive,anchor,anchor_depth,scope_card,"
           "candidate_nodes,scan_nodes,hnsw_nodes,scan_items,hnsw_items,"
           "distance_calls,hnsw_hops,local_results,heap_offers,heap_pops,"
           "dir_us,scan_us,hnsw_us,merge_us,e2e_ms,result_ids,result_dists,"
           "hit_count,gt_count\n";
    for (size_t qi = 0; qi < stats.size(); ++qi) {
        const QueryStats& s = stats[qi];
        std::vector<Item> out = s.result;
        if (out.empty()) {
            out.assign(kK, {0.f, -1});
        } else if (out.size() < kK) {
            while (out.size() < kK) out.push_back({0.f, -1});
        }
        csv << qi << "," << s.qidx << "," << s.recursive << "," << s.anchor
            << "," << s.anchor_depth << "," << s.scope_card << ","
            << s.candidate_nodes << "," << s.scan_nodes << "," << s.hnsw_nodes
            << "," << s.scan_items << "," << s.hnsw_items << ","
            << s.distance_calls << "," << s.hnsw_hops << "," << s.local_results
            << "," << s.heap_offers << "," << s.heap_pops << "," << s.dir_us
            << "," << s.scan_us << "," << s.hnsw_us << "," << s.merge_us
            << "," << s.e2e_ms << "," << join_ids(out, false) << ","
            << join_ids(out, true) << ",-1," << s.gt_count << "\n";
    }

    // ---- summary ----
    const std::string sum_path = args.csv + ".summary.json";
    std::ofstream js(sum_path);
    if (!js) die("cannot write " + sum_path);
    uint64_t total_dist = 0, total_hops = 0;
    for (const auto& s : stats) {
        total_dist += s.distance_calls;
        total_hops += s.hnsw_hops;
    }
    js << "{\n"
       << "  \"mode\": \"" << args.mode << "\",\n"
       << "  \"scan_threshold\": " << args.scan_threshold << ",\n"
       << "  \"ef\": " << args.ef << ",\n"
       << "  \"threads\": " << args.threads << ",\n"
       << "  \"dim\": " << args.dim << ",\n"
       << "  \"k\": " << kK << ",\n"
       << "  \"hnswlib_revision\": "
          "\"ca426729609b3221563047ceb269cc1213e0d376\",\n"
       << "  \"hnsw_M\": " << kHnswM << ",\n"
       << "  \"hnsw_ef_construction\": " << kEfConstruction << ",\n"
       << "  \"hnsw_seed\": " << kHnswSeed << ",\n"
       << "  \"n_base\": " << n_base << ",\n"
       << "  \"n_test\": " << n_test << ",\n"
       << "  \"n_nodes\": " << tree.n << ",\n"
       << "  \"n_nonempty_nodes\": " << n_nonempty << ",\n"
       << "  \"n_hnsw_nodes_built\": " << build_list.size() << ",\n"
       << "  \"n_queries\": " << stats.size() << ",\n"
       << "  \"build_wall_s\": " << build_wall_s << ",\n"
       << "  \"build_cpu_s\": " << build_cpu_s << ",\n"
       << "  \"peak_rss_bytes\": " << peak_rss_bytes << ",\n"
       << "  \"serialized_bytes\": " << serialized_bytes << ",\n"
       << "  \"mean_e2e_ms\": " << mean << ",\n"
       << "  \"p50_e2e_ms\": " << pct(0.50) << ",\n"
       << "  \"p95_e2e_ms\": " << pct(0.95) << ",\n"
       << "  \"p99_e2e_ms\": " << pct(0.99) << ",\n"
       << "  \"total_distance_calls\": " << total_dist << ",\n"
       << "  \"total_hnsw_hops\": " << total_hops << ",\n"
       << "  \"macro_recall\": -1,\n"
       << "  \"global_recall\": -1,\n"
       << "  \"notes\": \"hit/recall computed by driver against exact mode; "
          "queries single-threaded; ru_maxrss on this host includes a "
          "~614MiB environment baseline (observed with a trivial binary)\"\n"
       << "}\n";

    std::fprintf(stderr,
                 "nodeann_research: mode=%s T=%zu ef=%zu built=%zu/%zu "
                 "queries=%zu build_wall=%.2fs mean=%.3fms csv=%s\n",
                 args.mode.c_str(), args.scan_threshold, args.ef,
                 built.load(), build_list.size(), stats.size(), build_wall_s,
                 mean, csv_path.c_str());
    return 0;
}
