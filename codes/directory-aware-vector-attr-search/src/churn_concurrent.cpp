// churn_concurrent: consistency of NodeANN local indexes under concurrent
// attribute updates (direction 08, workstream 1).
//
// Move protocols decide what a concurrent query can observe:
//   add-del : add to destination, then mark-delete at source
//             -> the item is briefly resident in BOTH nodes (duplicate window)
//   del-add : mark-delete at source, then add to destination
//             -> the item is briefly resident NOWHERE (missing window)
// Candidate validation closes the stale-copy window: a hit is accepted only if
// the item's current node-of-record is the node that produced it.
//
// The anomaly window is measured directly (wall time between the two steps),
// because a race sample would rarely catch a microsecond window. Exposure is
// reported per op and in total; with a query rate Q the expected number of
// affected queries is exposure_total * Q / elapsed.
//
// CLI:
//   churn_concurrent --data DIR --dim D --min-node-items T --ef E --k K
//                     --protocol add-del|del-add --validate 0|1
//                     --update-ops N --queries M --seed S --csv PREFIX

#include "raw_l2_space.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

using Clock = std::chrono::steady_clock;
static double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}
static long long ns_now() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now().time_since_epoch()).count();
}

static std::vector<float> load_f32(const std::string& path, int dim) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("cannot open " + path);
    size_t bytes = (size_t)f.tellg();
    if (bytes % ((size_t)dim * 4)) throw std::runtime_error("bad size " + path);
    f.seekg(0);
    std::vector<float> x(bytes / 4);
    f.read((char*)x.data(), bytes);
    return x;
}

struct Args {
    std::map<std::string, std::string> kv;
    static Args parse(int argc, char** argv) {
        Args a;
        for (int i = 1; i < argc; ++i) {
            std::string s = argv[i];
            if (s.rfind("--", 0) == 0 && i + 1 < argc) a.kv[s.substr(2)] = argv[++i];
        }
        return a;
    }
    std::string get(const std::string& k, const std::string& d = "") const {
        auto it = kv.find(k);
        return it == kv.end() ? d : it->second;
    }
    int geti(const std::string& k, int d) const { return std::atoi(get(k, std::to_string(d)).c_str()); }
};

struct NodeIndex {
    std::unique_ptr<hnswlib::HierarchicalNSW<float>> ix;
    std::vector<int> items;   // local label -> global item id
    std::vector<char> dead;
    size_t dead_n = 0;
    size_t size() const { return items.size(); }
};

int main(int argc, char** argv) {
    Args a = Args::parse(argc, argv);
    const std::string data = a.get("data");
    const int dim = a.geti("dim", 128);
    const int min_items = a.geti("min-node-items", 128);
    const int ef = a.geti("ef", 64);
    const int k = a.geti("k", 10);
    const std::string protocol = a.get("protocol", "add-del");
    const int validate = a.geti("validate", 0);
    const long long update_ops = a.geti("update-ops", 200000);
    const int n_queries = a.geti("queries", 2000);
    const int fanout = std::max(1, a.geti("fanout", 1));  // nodes per query
    const int hot_n = a.geti("hot-nodes", 0);             // 0 = uniform access
    const unsigned seed = (unsigned)a.geti("seed", 100);
    const std::string csv = a.get("csv");

    std::vector<float> base = load_f32(data + "/base.f32", dim);
    std::vector<float> test = load_f32(data + "/test.f32", dim);
    const size_t n = base.size() / dim, nq_all = test.size() / dim;
    std::vector<int> node_of(n);
    {
        std::ifstream f(data + "/bindings.txt");
        if (!f) throw std::runtime_error("cannot open bindings");
        int v; size_t i = 0;
        while (f >> v) { if (i >= n) throw std::runtime_error("bindings too long"); node_of[i++] = v; }
        if (i != n) throw std::runtime_error("bindings too short");
    }
    int m_nodes = 0;
    { std::ifstream f(data + "/tree.txt"); f >> m_nodes; }

    std::vector<std::vector<int>> node_items(m_nodes);
    for (size_t i = 0; i < n; ++i) node_items[node_of[i]].push_back((int)i);
    std::vector<int> big;
    for (int v = 0; v < m_nodes; ++v)
        if ((int)node_items[v].size() >= min_items) big.push_back(v);

    RawL2Space l2(dim);
    std::vector<NodeIndex> nix(m_nodes);
    auto t0 = Clock::now();
    for (int v : big) {
        NodeIndex& ni = nix[v];
        ni.ix = std::make_unique<hnswlib::HierarchicalNSW<float>>(&l2, node_items[v].size(), 16, 200, 100);
        ni.ix->setEf(std::max(ef, k));
        ni.items = node_items[v];
        ni.dead.assign(ni.items.size(), 0);
        for (size_t j = 0; j < ni.items.size(); ++j)
            ni.ix->addPoint(&base[(size_t)ni.items[j] * dim], (hnswlib::labeltype)j);
    }
    std::fprintf(stderr, "built %zu indexes in %.0f ms\n", big.size(), ms_since(t0));

    std::vector<std::mutex> nlock(m_nodes);
    std::atomic<bool> stop{false};
    std::atomic<long long> updates_done{0}, queries_done{0};
    std::atomic<long long> dup_hits{0}, stale_hits{0}, cand_total{0}, val_ns{0};
    std::atomic<long long> lat_ns{0}, exposure_ns{0};

    // hotspot locality: when hot_n > 0, queries and updates concentrate on the
    // hot_n largest nodes (the realistic case where concurrency anomalies matter)
    std::vector<int> hot;
    if (hot_n > 0) {
        std::vector<std::pair<size_t, int>> by_size;
        for (int v : big) by_size.push_back({node_items[v].size(), v});
        std::sort(by_size.rbegin(), by_size.rend());
        for (int i = 0; i < std::min<int>(hot_n, (int)by_size.size()); ++i)
            hot.push_back(by_size[i].second);
    }
    auto pick = [&](std::mt19937_64& rng) {
        if (!hot.empty()) return hot[rng() % hot.size()];
        return big[rng() % big.size()];
    };

    auto add_to = [&](int dst, int gid) {
        std::lock_guard<std::mutex> g(nlock[dst]);
        size_t nl = nix[dst].size();
        if (nl >= nix[dst].ix->getMaxElements())
            nix[dst].ix->resizeIndex(std::max<size_t>(nl + 1, nix[dst].ix->getMaxElements() * 2));
        nix[dst].items.push_back(gid);
        nix[dst].dead.push_back(0);
        nix[dst].ix->addPoint(&base[(size_t)gid * dim], (hnswlib::labeltype)nl);
    };

    std::thread updater([&] {
        std::mt19937_64 rng(seed + 1);
        while (updates_done.load(std::memory_order_relaxed) < update_ops && !stop.load()) {
            int src = pick(rng), dst = pick(rng);
            if (src == dst) continue;
            // take a live item out of src; the src lock is held across the
            // protocol so its two steps are visible in order
            std::unique_lock<std::mutex> gs(nlock[src]);
            if (nix[src].size() - nix[src].dead_n < 2) continue;
            size_t lbl = rng() % nix[src].size();
            if (nix[src].dead[lbl]) continue;
            int gid = nix[src].items[lbl];

            if (protocol == "add-del") {
                gs.unlock();
                add_to(dst, gid);            // window opens: resident twice
                node_of[gid] = dst;
                long long t_mid = ns_now();
                {
                    std::lock_guard<std::mutex> g2(nlock[src]);
                    nix[src].ix->markDelete((hnswlib::labeltype)lbl);
                    nix[src].dead[lbl] = 1; ++nix[src].dead_n;
                }
                exposure_ns.fetch_add(ns_now() - t_mid, std::memory_order_relaxed);
            } else {
                nix[src].ix->markDelete((hnswlib::labeltype)lbl);
                nix[src].dead[lbl] = 1; ++nix[src].dead_n;
                gs.unlock();
                long long t_mid = ns_now();   // window opens: resident nowhere
                node_of[gid] = dst;
                add_to(dst, gid);
                exposure_ns.fetch_add(ns_now() - t_mid, std::memory_order_relaxed);
            }
            updates_done.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::thread querier([&] {
        std::mt19937_64 rng(seed + 2);
        std::vector<size_t> qs(n_queries);
        for (auto& q : qs) q = rng() % nq_all;
        for (int qi = 0; qi < n_queries; ++qi) {
            const float* q = &test[qs[qi] * dim];
            long long t_start = ns_now();
            std::vector<int> got;
            // fan-out over `fanout` DISTINCT nodes (with-replacement sampling
            // would create self-duplicates that have nothing to do with churn)
            std::vector<int> nodes;
            while ((int)nodes.size() < fanout) {
                int cand = pick(rng);
                if (std::find(nodes.begin(), nodes.end(), cand) == nodes.end())
                    nodes.push_back(cand);
            }
            for (int f = 0; f < fanout; ++f) {
                int node = nodes[f];
                std::lock_guard<std::mutex> g(nlock[node]);
                int kk = std::min<int>(k, (int)(nix[node].size() - nix[node].dead_n));
                if (kk <= 0) continue;
                auto res = nix[node].ix->searchKnn(q, kk);
                std::vector<int> cand;
                while (!res.empty()) { cand.push_back((int)res.top().second); res.pop(); }
                cand_total.fetch_add((long long)cand.size());
                for (int lbl : cand) {
                    if (nix[node].dead[lbl]) continue;
                    int gid = nix[node].items[lbl];
                    if (validate) {
                        long long v0 = ns_now();
                        bool ok = (node_of[gid] == node);
                        val_ns.fetch_add(ns_now() - v0);
                        if (!ok) { stale_hits.fetch_add(1); continue; }
                    }
                    got.push_back(gid);
                }
            }
            std::unordered_set<int> uniq(got.begin(), got.end());
            if (uniq.size() != got.size())
                dup_hits.fetch_add((long long)(got.size() - uniq.size()));
            lat_ns.fetch_add(ns_now() - t_start);
            queries_done.fetch_add(1);
        }
        stop.store(true);
    });

    updater.join();
    querier.join();

    long long q = std::max<long long>(1, queries_done.load());
    long long u = std::max<long long>(1, updates_done.load());
    double dup_rate = 1000.0 * dup_hits.load() / q;
    double stale_rate = 1000.0 * stale_hits.load() / q;
    double cand_pq = (double)cand_total.load() / q;
    double val_ns_pc = cand_total.load() ? (double)val_ns.load() / cand_total.load() : 0.0;
    double lat_us_pq = (double)lat_ns.load() / q / 1000.0;
    double exp_per_op = (double)exposure_ns.load() / u;
    double exp_total_ms = (double)exposure_ns.load() / 1e6;

    std::ofstream out(csv + ".csv");
    out << "protocol,validate,update_ops,queries,dup_per_1k,stale_per_1k,"
           "cand_per_query,val_ns_per_cand,lat_us_per_query,"
           "exposure_ns_per_op,exposure_total_ms\n";
    out << protocol << "," << validate << "," << updates_done.load() << "," << q << ","
        << dup_rate << "," << stale_rate << "," << cand_pq << "," << val_ns_pc << ","
        << lat_us_pq << "," << exp_per_op << "," << exp_total_ms << "\n";
    out.close();
    std::printf("RESULT protocol=%s validate=%d updates=%lld queries=%lld "
                "dup_per_1k=%.2f stale_per_1k=%.2f cand_per_q=%.2f "
                "val_ns_per_cand=%.1f lat_us_per_q=%.2f "
                "exposure_ns_per_op=%.0f exposure_total_ms=%.2f\n",
                protocol.c_str(), validate, updates_done.load(), q, dup_rate,
                stale_rate, cand_pq, val_ns_pc, lat_us_pq, exp_per_op, exp_total_ms);
    return 0;
}
