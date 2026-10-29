// churn_probe: local-ANN quality under attribute churn (tombstone vs compact).
//
// Single-factor prototype for direction 08 stage 3. All raw FP32 squared-L2
// distances go through RawL2Space (hnswlib L2Space::get_dist_func()).
//
// CLI:
//   churn_probe --data DIR --dim D --min-node-items T --ef E --k K
//               --churn-frac 0.5 --checkpoints 0,0.1,0.2,0.5,0.8
//               --arm tombstone|compact --compact-threshold R
//               --eval-queries Q --seed S --csv PREFIX
//
// Churn model: move a random active item out of a random HNSW node into another
// random HNSW node (attribute change). Old index: markDelete. New index:
// addPoint with a fresh local label. Arm `compact` rebuilds a node's index when
// dead/size > threshold. Arm `tombstone` never rebuilds.
//
// Measured per checkpoint: per-node recall@k (HNSW vs exact scan inside node),
// per-node search latency, dead ratio, cumulative maintenance time, rebuild time.

#include "raw_l2_space.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

using Clock = std::chrono::steady_clock;
static double g_last_lat = 0.0;
static double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
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
    double getd(const std::string& k, double d) const { return std::atof(get(k, std::to_string(d)).c_str()); }
    std::vector<double> doubles(const std::string& k) const {
        std::vector<double> out;
        std::string s = get(k), it;
        size_t p = 0;
        while (p <= s.size()) {
            size_t c = s.find(',', p);
            std::string t = s.substr(p, c == std::string::npos ? std::string::npos : c - p);
            if (!t.empty()) out.push_back(std::atof(t.c_str()));
            if (c == std::string::npos) break;
            p = c + 1;
        }
        return out;
    }
};

// per-node local index with tombstones
struct NodeIndex {
    std::unique_ptr<hnswlib::HierarchicalNSW<float>> ix;
    std::vector<int> active;                 // local label -> global item id
    std::vector<char> dead;                  // per local label
    size_t dead_n = 0;
    size_t size() const { return active.size(); }
    double dead_ratio() const { return active.empty() ? 0.0 : (double)dead_n / active.size(); }
};

int main(int argc, char** argv) {
    Args a = Args::parse(argc, argv);
    const std::string data = a.get("data");
    const int dim = a.geti("dim", 128);
    const int min_items = a.geti("min-node-items", 64);
    const int ef = a.geti("ef", 64);
    const int k = a.geti("k", 10);
    const double churn_frac = a.getd("churn-frac", 0.5);
    const std::string arm = a.get("arm", "tombstone");
    const double compact_thr = a.getd("compact-threshold", 0.2);
    const int eval_q = a.geti("eval-queries", 200);
    const unsigned seed = (unsigned)a.geti("seed", 100);
    const std::string csv = a.get("csv");
    std::vector<double> checkpoints = a.doubles("checkpoints");
    if (checkpoints.empty()) checkpoints = {0.0, 0.1, 0.2, 0.5, 0.8};

    // ---- load fixture
    std::vector<float> base = load_f32(data + "/base.f32", dim);
    std::vector<float> test = load_f32(data + "/test.f32", dim);
    const size_t n = base.size() / dim, nq_all = test.size() / dim;
    std::vector<int> bindings;
    {
        std::ifstream f(data + "/bindings.txt");
        if (!f) throw std::runtime_error("cannot open bindings");
        int v;
        while (f >> v) bindings.push_back(v);
    }
    if (bindings.size() != n) throw std::runtime_error("bindings size != n");
    int m_nodes = 0;
    {
        std::ifstream f(data + "/tree.txt");
        if (!f) throw std::runtime_error("cannot open tree");
        f >> m_nodes;
    }

    std::vector<std::vector<int>> node_items(m_nodes);
    for (size_t i = 0; i < n; ++i) node_items[bindings[i]].push_back((int)i);

    RawL2Space l2(dim);
    // build indexes for nodes with |L| >= min_items
    std::vector<int> big_nodes;
    for (int v = 0; v < m_nodes; ++v)
        if ((int)node_items[v].size() >= min_items) big_nodes.push_back(v);
    std::vector<NodeIndex> nix(m_nodes);
    auto t_build0 = Clock::now();
    for (int v : big_nodes) {
        NodeIndex& ni = nix[v];
        ni.ix = std::make_unique<hnswlib::HierarchicalNSW<float>>(&l2, node_items[v].size(), 16, 200, 100);
        ni.ix->setEf(std::max(ef, k));
        ni.active = node_items[v];
        ni.dead.assign(ni.active.size(), 0);
        for (size_t j = 0; j < ni.active.size(); ++j)
            ni.ix->addPoint(&base[(size_t)ni.active[j] * dim], (hnswlib::labeltype)j);
    }
    const double build_ms = ms_since(t_build0);
    size_t n_hnsw = 0, items_hnsw = 0;
    for (int v : big_nodes) { ++n_hnsw; items_hnsw += nix[v].size(); }
    std::fprintf(stderr, "built %zu local indexes (%zu items) in %.1f ms; min_items=%d\n",
                 n_hnsw, items_hnsw, build_ms, min_items);

    // ---- evaluation (per-node recall of HNSW vs exact scan)
    std::mt19937 rng(seed);
    std::vector<size_t> qs(eval_q);
    for (auto& q : qs) q = rng() % nq_all;

    auto eval = [&](const char* tag, double alpha, double maint_ms, double rebuild_ms) {
        // sample nodes and queries; recall vs exact scan inside node
        double hit = 0, tot = 0, lat_us = 0;
        size_t evals = 0;
        double dead_ratio = 0, dead_sum = 0, size_sum = 0;
        for (int v : big_nodes) {
            NodeIndex& ni = nix[v];
            if (ni.active.empty()) continue;
            dead_sum += (double)ni.dead_n; size_sum += (double)ni.active.size();
            dead_ratio += ni.dead_ratio();
            for (int qi = 0; qi < eval_q / 10; ++qi) {
                const float* q = &test[qs[(qi + v) % eval_q] * dim];
                int kk = std::min<int>(k, (int)(ni.active.size() - ni.dead_n));
                if (kk <= 0) continue;
                auto t0 = Clock::now();
                auto res = ni.ix->searchKnn(q, kk);
                lat_us += std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
                std::vector<int> got;
                while (!res.empty()) {
                    int lbl = (int)res.top().second; res.pop();
                    if (!ni.dead[lbl]) got.push_back(ni.active[lbl]);
                }
                // exact scan over live items
                std::vector<std::pair<float, int>> all;
                for (size_t j = 0; j < ni.active.size(); ++j) {
                    if (ni.dead[j]) continue;
                    all.push_back({l2.dist(q, &base[(size_t)ni.active[j] * dim]), ni.active[j]});
                }
                std::sort(all.begin(), all.end(), [](auto& x, auto& y) {
                    return x.first != y.first ? x.first < y.first : x.second < y.second;
                });
                all.resize(std::min<size_t>(kk, all.size()));
                size_t h = 0;
                for (auto& [d, id] : all)
                    if (std::find(got.begin(), got.end(), id) != got.end()) ++h;
                hit += (double)h; tot += (double)all.size();
                ++evals;
            }
        }
        double recall = tot > 0 ? hit / tot : 0.0;
        double avg_lat = evals > 0 ? lat_us / evals : 0.0;
        g_last_lat = avg_lat;
        std::printf("CHURN %s alpha=%.2f recall=%.5f lat_us=%.2f dead_ratio=%.4f "
                    "maint_ms=%.1f rebuild_ms=%.1f nodes=%zu\n",
                    tag, alpha, recall, avg_lat,
                    size_sum > 0 ? dead_sum / size_sum : 0.0, maint_ms, rebuild_ms, n_hnsw);
        return recall;
    };

    std::ofstream out(csv + ".csv");
    out << "arm,alpha,recall,lat_us,dead_ratio,maint_ms,rebuild_ms,nodes\n";

    // per-checkpoint bookkeeping
    size_t total_moves = (size_t)(churn_frac * (double)std::min(n, (size_t)items_hnsw));
    size_t done = 0;
    double maint_ms = 0, rebuild_ms = 0;
    size_t next_cp = 0;

    auto emit = [&](double alpha) {
        double dead_ratio = 0, sum = 0;
        for (int v : big_nodes) { dead_ratio += (double)nix[v].dead_n; sum += (double)nix[v].active.size(); }
        double recall = eval(arm.c_str(), alpha, maint_ms, rebuild_ms);
        out << arm << "," << alpha << "," << recall << "," << g_last_lat << ","
            << (sum > 0 ? dead_ratio / sum : 0.0) << "," << maint_ms << ","
            << rebuild_ms << "," << n_hnsw << "\n";
    };
    emit(0.0); ++next_cp;

    // ---- churn loop
    std::uniform_int_distribution<size_t> pick_node(0, big_nodes.size() - 1);
    while (done < total_moves) {
        double alpha = (double)done / (double)std::max<size_t>(1, total_moves);
        while (next_cp < checkpoints.size() && alpha >= checkpoints[next_cp]) {
            emit(checkpoints[next_cp]); ++next_cp;
        }
        // pick a live item from a random big node, move it to another big node
        bool moved = false;
        for (int attempt = 0; attempt < 8 && !moved; ++attempt) {
            int src = big_nodes[pick_node(rng)];
            int dst = big_nodes[pick_node(rng)];
            if (src == dst || nix[src].active.size() - nix[src].dead_n < 2) continue;
            // find a live label in src
            size_t lbl = rng() % nix[src].active.size();
            if (nix[src].dead[lbl]) continue;
            int gid = nix[src].active[lbl];
            auto t0 = Clock::now();
            nix[src].ix->markDelete((hnswlib::labeltype)lbl);
            nix[src].dead[lbl] = 1; ++nix[src].dead_n;
            NodeIndex& nd = nix[dst];
            size_t nl = nd.active.size();
            if (nl >= nd.ix->getMaxElements())
                nd.ix->resizeIndex(std::max<size_t>(nl + 1, nd.ix->getMaxElements() * 2));
            nd.active.push_back(gid);
            nd.dead.push_back(0);
            nd.ix->addPoint(&base[(size_t)gid * dim], (hnswlib::labeltype)nl);
            maint_ms += ms_since(t0);
            bindings[gid] = dst;
            moved = true;
            // compaction arm
            if (arm == "compact") {
                std::vector<int> to_compact;
                for (int v : big_nodes)
                    if (nix[v].dead_ratio() > compact_thr &&
                        nix[v].active.size() - nix[v].dead_n >= (size_t)min_items)
                        to_compact.push_back(v);
                for (int v : to_compact) {
                    auto t1 = Clock::now();
                    std::vector<int> live;
                    for (size_t j = 0; j < nix[v].active.size(); ++j)
                        if (!nix[v].dead[j]) live.push_back(nix[v].active[j]);
                    NodeIndex& ni = nix[v];
                    ni.ix = std::make_unique<hnswlib::HierarchicalNSW<float>>(
                        &l2, live.size(), 16, 200, 100);
                    ni.ix->setEf(std::max(ef, k));
                    ni.active = live;
                    ni.dead.assign(live.size(), 0);
                    ni.dead_n = 0;
                    for (size_t j = 0; j < live.size(); ++j)
                        ni.ix->addPoint(&base[(size_t)live[j] * dim], (hnswlib::labeltype)j);
                    rebuild_ms += ms_since(t1);
                }
            }
        }
        ++done;
    }
    while (next_cp < checkpoints.size()) { emit(checkpoints[next_cp]); ++next_cp; }
    emit(1.0);
    out.close();
    std::fprintf(stderr, "done: arm=%s moves=%zu maint_ms=%.0f rebuild_ms=%.0f\n",
                 arm.c_str(), done, maint_ms, rebuild_ms);
    return 0;
}
