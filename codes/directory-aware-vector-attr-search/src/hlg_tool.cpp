// HLG (hierarchical lineage graph): laminar connectors over a FAISS HNSW graph.
//
// Standalone tool (does not share symbols with repro.cpp).
// Theory: researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-28-exps/hlg-theory.md
//
//   index : build a FAISS IndexHNSWFlat(M=16, efConstruction=200) and persist it
//           (same constructor parameters as repro.cpp's PG-hnsw-M16 executor)
//   build : derive the HLG connectors (E_loc + E_up) and write overlay.bin/reps.bin
//   check : BFS/component + directed-reachability audit of G[S(u)] per sampled node
//   bench : self-implemented layer-0 beam search, connectors on/off, vs faiss engine
//   move  : incremental connector maintenance timing for MOVE ops (定理 2)
//
// Entry-ID layout: entries are renumbered so that S(u) is the contiguous interval
// [lo[u], hi[u]) and L(u) = [lo[u], lo[u]+nloc[u]). This makes scope tests O(1)
// (理论笔记 §2 引理 1) and the scope-cardinality column directly checkable against
// the frozen GT file.

#include <faiss/IndexHNSW.h>
#include <faiss/impl/IDSelector.h>
#include <faiss/index_io.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;
static double us_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}

// ------------------------------------------------------------------ args ----
struct Args {
    std::map<std::string, std::string> kv;
    static Args parse(int argc, char** argv, int from = 2) {
        Args a;
        for (int i = from; i < argc; ++i) {
            std::string s = argv[i];
            if (s.rfind("--", 0) == 0 && i + 1 < argc) a.kv[s.substr(2)] = argv[++i];
        }
        return a;
    }
    std::string get(const std::string& k, const std::string& def = "") const {
        auto it = kv.find(k);
        return it == kv.end() ? def : it->second;
    }
    int geti(const std::string& k, int def) const {
        auto it = kv.find(k);
        return it == kv.end() ? def : std::atoi(it->second.c_str());
    }
    double getd(const std::string& k, double def) const {
        auto it = kv.find(k);
        return it == kv.end() ? def : std::atof(it->second.c_str());
    }
    bool has(const std::string& k) const { return kv.count(k) > 0; }
    std::vector<int> ints(const std::string& k) const {
        std::vector<int> out;
        std::stringstream ss(get(k));
        std::string it;
        while (std::getline(ss, it, ','))
            if (!it.empty()) out.push_back(std::atoi(it.c_str()));
        return out;
    }
};

template <class F>
static void parallel_for(long long n, F f) {
    unsigned nth = std::max(1u, std::thread::hardware_concurrency());
    if (const char* e = getenv("HLG_THREADS")) nth = std::max(1, std::atoi(e));
    if (n < 4096) nth = 1;
    std::atomic<long long> next{0};
    auto worker = [&]() {
        const long long chunk = 256;
        for (;;) {
            long long i0 = next.fetch_add(chunk);
            if (i0 >= n) break;
            long long i1 = std::min(n, i0 + chunk);
            for (long long i = i0; i < i1; ++i) f(i);
        }
    };
    std::vector<std::thread> th;
    for (unsigned t = 1; t < nth; ++t) th.emplace_back(worker);
    worker();
    for (auto& x : th) x.join();
}

// ------------------------------------------------------------------ data ----
struct Tree {
    int m = 0;
    std::vector<int> parent;               // -1 root, -2 removed
    std::vector<std::vector<int>> ch;
    std::vector<int> depth;
    std::vector<int> pre;                  // preorder list of nodes
    std::vector<int> lo, hi;               // S(u) = [lo,hi) in entry-ID space
    std::vector<int> nloc;                 // |L(u)|
    int size(int u) const { return hi[u] - lo[u]; }
};

static void load_tree(const std::string& path, Tree& t) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open " + path);
    f >> t.m;
    t.parent.assign(t.m, -1);
    for (int i = 0; i < t.m; ++i) {
        int v, p;
        f >> v >> p;
        t.parent[v] = p;
    }
    t.ch.assign(t.m, {});
    for (int v = 1; v < t.m; ++v)
        if (t.parent[v] >= 0) t.ch[t.parent[v]].push_back(v);
    t.depth.assign(t.m, 0);
}

// entry i (base vector id i) is bound to node v; single binding assumed
static void load_bindings(const std::string& path, std::vector<int>& node_of_entry) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open " + path);
    node_of_entry.clear();
    long long x;
    while (f >> x) node_of_entry.push_back((int)x);
}

// preorder + interval layout (declared order: L(u) first, then children blocks)
static void compute_layout(Tree& t) {
    t.lo.assign(t.m, 0);
    t.hi.assign(t.m, 0);
    t.pre.clear();
    t.pre.push_back(0);
    int cursor = 0;
    t.lo[0] = cursor;
    cursor += t.nloc[0];
    std::vector<std::pair<int, size_t>> st;
    st.push_back({0, 0});
    while (!st.empty()) {
        auto& top = st.back();
        int u = top.first;
        if (top.second < t.ch[u].size()) {
            int c = t.ch[u][top.second++];
            t.depth[c] = t.depth[u] + 1;
            t.lo[c] = cursor;
            cursor += t.nloc[c];
            t.pre.push_back(c);
            st.push_back({c, 0});
        } else {
            t.hi[u] = cursor;
            st.pop_back();
        }
    }
}

struct Corpus {
    int dim = 0;
    size_t n = 0;
    std::vector<int32_t> orig;   // eID -> base vector id
    std::vector<int32_t> eid;    // base vector id -> eID
    std::vector<float> x;        // eID-ordered vectors (n*dim)
};

static long long file_bytes(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("cannot open " + p);
    return (long long)f.tellg();
}

// scatter base.f32 into eID order without keeping the original copy
static void load_vectors_scattered(const std::string& path, int dim, size_t n,
                                  const std::vector<int32_t>& eid, std::vector<float>& x) {
    x.assign(n * (size_t)dim, 0.0f);
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    const size_t chunk = 16384;
    std::vector<float> buf(chunk * dim);
    size_t i0 = 0;
    while (i0 < n) {
        size_t cnt = std::min(chunk, n - i0);
        f.read(reinterpret_cast<char*>(buf.data()), (std::streamsize)(cnt * dim * sizeof(float)));
        if ((size_t)f.gcount() != cnt * dim * sizeof(float))
            throw std::runtime_error("short read on " + path);
        for (size_t j = 0; j < cnt; ++j)
            std::memcpy(&x[(size_t)eid[i0 + j] * dim], &buf[j * dim], (size_t)dim * sizeof(float));
        i0 += cnt;
    }
}

struct Dataset {
    Tree t;
    Corpus c;
};

static Dataset load_dataset(const Args& a) {
    Dataset d;
    std::string dir = a.get("data");
    std::string prefix = a.get("ns", "wiki");
    int dim = a.geti("dim", 1024);
    d.c.dim = dim;
    load_tree(dir + "/" + prefix + "_tree.txt", d.t);
    std::vector<int> node_of_entry;
    load_bindings(dir + "/" + prefix + "_bindings.txt", node_of_entry);
    long long bytes = file_bytes(dir + "/base.f32");
    size_t n = (size_t)(bytes / (sizeof(float) * (long long)dim));
    if (node_of_entry.size() != n)
        throw std::runtime_error("bindings size " + std::to_string(node_of_entry.size()) +
                                 " != n " + std::to_string(n) + " (dim=" +
                                 std::to_string(dim) + ")");
    d.c.n = n;
    d.t.nloc.assign(d.t.m, 0);
    for (size_t i = 0; i < n; ++i) {
        int v = node_of_entry[i];
        if (v < 0 || v >= d.t.m) throw std::runtime_error("binding out of range");
        d.t.nloc[v]++;
    }
    compute_layout(d.t);
    d.c.orig.assign(n, -1);
    d.c.eid.assign(n, -1);
    std::vector<int> slot(d.t.m);
    for (int u = 0; u < d.t.m; ++u) slot[u] = d.t.lo[u];
    for (size_t i = 0; i < n; ++i) {
        int v = node_of_entry[i];
        int e = slot[v]++;
        d.c.eid[i] = e;
        d.c.orig[e] = (int32_t)i;
    }
    for (int u = 0; u < d.t.m; ++u)
        if (slot[u] != d.t.lo[u] + d.t.nloc[u]) throw std::runtime_error("layout mismatch");
    load_vectors_scattered(dir + "/base.f32", dim, n, d.c.eid, d.c.x);
    return d;
}

static inline double dist2(const float* a, const float* b, int d) {
    double s = 0;
    for (int j = 0; j < d; ++j) {
        double t = (double)a[j] - (double)b[j];
        s += t * t;
    }
    return s;
}

// --------------------------------------------------------------- overlay ----
struct Overlay {
    std::vector<std::pair<uint32_t, uint32_t>> edges;  // base vector ids
    std::vector<int32_t> rep;                          // per node, base id or -1
};

static void write_overlay(const std::string& dir, const std::vector<std::pair<uint32_t, uint32_t>>& e,
                          const std::vector<int32_t>& rep) {
    {
        std::ofstream f(dir + "/overlay.bin", std::ios::binary);
        uint32_t n = (uint32_t)e.size();
        f.write(reinterpret_cast<const char*>(&n), 4);
        for (auto& p : e) {
            f.write(reinterpret_cast<const char*>(&p.first), 4);
            f.write(reinterpret_cast<const char*>(&p.second), 4);
        }
    }
    {
        std::ofstream f(dir + "/reps.bin", std::ios::binary);
        uint32_t m = (uint32_t)rep.size();
        f.write(reinterpret_cast<const char*>(&m), 4);
        f.write(reinterpret_cast<const char*>(rep.data()), (std::streamsize)(4 * rep.size()));
    }
}

static void read_overlay(const std::string& dir, std::vector<std::pair<uint32_t, uint32_t>>& e,
                         std::vector<int32_t>& rep, uint32_t m_expect) {
    {
        std::ifstream f(dir + "/overlay.bin", std::ios::binary);
        if (!f) throw std::runtime_error("cannot open " + dir + "/overlay.bin");
        uint32_t n = 0;
        f.read(reinterpret_cast<char*>(&n), 4);
        e.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            f.read(reinterpret_cast<char*>(&e[i].first), 4);
            f.read(reinterpret_cast<char*>(&e[i].second), 4);
        }
    }
    {
        std::ifstream f(dir + "/reps.bin", std::ios::binary);
        if (!f) throw std::runtime_error("cannot open " + dir + "/reps.bin");
        uint32_t m = 0;
        f.read(reinterpret_cast<char*>(&m), 4);
        if (m != m_expect) throw std::runtime_error("reps.bin node count mismatch");
        rep.resize(m);
        f.read(reinterpret_cast<char*>(rep.data()), (std::streamsize)(4 * m));
    }
}

// ------------------------------------------------------------- hnsw graph ----
struct Graph {
    std::vector<uint32_t> foff, fadj;  // layer-0 out edges (eID space)
    std::vector<uint32_t> roff, radj;  // layer-0 in edges (eID space)
    std::vector<uint32_t> coff, cadj;  // connectors, symmetric (eID space)
};

static void build_hnsw_graph(const faiss::IndexHNSW& ix, const std::vector<int32_t>& eid,
                            size_t n, Graph& g) {
    const auto& h = ix.hnsw;
    std::vector<uint32_t> deg(n + 1, 0), rdeg(n + 1, 0);
    for (size_t i = 0; i < n; ++i) {
        int L = h.levels[i];
        size_t begin = h.offsets[i] + h.cum_nb_neighbors(L);
        size_t end = h.offsets[i] + h.cum_nb_neighbors(L + 1);
        for (size_t j = begin; j < end; ++j) {
            int v = h.neighbors[j];
            if (v < 0) break;
            deg[i]++;
            rdeg[(size_t)v]++;
        }
    }
    g.foff.assign(n + 1, 0);
    g.roff.assign(n + 1, 0);
    for (size_t i = 0; i < n; ++i) {
        g.foff[i + 1] = g.foff[i] + deg[i];
        g.roff[i + 1] = g.roff[i] + rdeg[i];
    }
    g.fadj.assign(g.foff[n], 0);
    g.radj.assign(g.roff[n], 0);
    std::vector<uint32_t> fp(g.foff.begin(), g.foff.end() - 1), rp(g.roff.begin(), g.roff.end() - 1);
    for (size_t i = 0; i < n; ++i) {
        int L = h.levels[i];
        size_t begin = h.offsets[i] + h.cum_nb_neighbors(L);
        size_t end = h.offsets[i] + h.cum_nb_neighbors(L + 1);
        for (size_t j = begin; j < end; ++j) {
            int v = h.neighbors[j];
            if (v < 0) break;
            uint32_t e = (uint32_t)eid[(size_t)v];
            g.fadj[fp[i]++] = e;
            g.radj[rp[(size_t)v]++] = (uint32_t)eid[i];
        }
    }
}

static void build_connector_csr(const std::vector<std::pair<uint32_t, uint32_t>>& edges,
                               const std::vector<int32_t>& eid, size_t n, Graph& g) {
    std::vector<uint32_t> deg(n + 1, 0);
    for (auto& e : edges) {
        deg[eid[e.first]]++;
        deg[eid[e.second]]++;
    }
    g.coff.assign(n + 1, 0);
    for (size_t i = 0; i < n; ++i) g.coff[i + 1] = g.coff[i] + deg[i];
    g.cadj.assign(g.coff[n], 0);
    std::vector<uint32_t> p(g.coff.begin(), g.coff.end() - 1);
    for (auto& e : edges) {
        uint32_t a = (uint32_t)eid[e.first], b = (uint32_t)eid[e.second];
        g.cadj[p[a]++] = b;
        g.cadj[p[b]++] = a;
    }
}

// ------------------------------------------------------------------ MST -----
// exact Euclidean Prim over the given point list (eIDs), ties by ascending id
static void prim_mst(const float* X, int dim, const std::vector<uint32_t>& pts,
                     std::vector<std::pair<uint32_t, uint32_t>>& out) {
    const int k = (int)pts.size();
    if (k < 2) return;
    const double INF = std::numeric_limits<double>::infinity();
    std::vector<double> best(k, INF);
    std::vector<int> from(k, -1);
    std::vector<char> in(k, 0);
    best[0] = 0.0;
    for (int it = 0; it < k; ++it) {
        int bi = -1;
        double bd = INF;
        for (int i = 0; i < k; ++i)
            if (!in[i] && best[i] < bd) {
                bd = best[i];
                bi = i;
            }
        in[bi] = 1;
        if (from[bi] >= 0) out.push_back({pts[from[bi]], pts[bi]});
        const float* xb = X + (size_t)pts[bi] * dim;
        for (int i = 0; i < k; ++i) {
            if (in[i]) continue;
            double dd = dist2(xb, X + (size_t)pts[i] * dim, dim);
            if (dd < best[i]) {
                best[i] = dd;
                from[i] = bi;
            }
        }
    }
}

// ----------------------------------------------------------------- json ------
static std::string read_file(const std::string& p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static size_t find_key(const std::string& s, const std::string& key, size_t from) {
    size_t p = s.find("\"" + key + "\"", from);
    if (p == std::string::npos) return std::string::npos;
    p = s.find(':', p);
    if (p == std::string::npos) return std::string::npos;
    return p + 1;
}

static long long json_int(const std::string& s, size_t from) {
    size_t p = from;
    while (p < s.size() && (s[p] == ' ' || s[p] == '\n' || s[p] == '\r' || s[p] == '\t')) ++p;
    return std::atoll(s.c_str() + p);
}

static bool json_bool(const std::string& s, size_t from) {
    size_t p = from;
    while (p < s.size() && (s[p] == ' ' || s[p] == '\n' || s[p] == '\t')) ++p;
    return s.compare(p, 4, "true") == 0;
}

// frozen GT: {"queries":[{"query_index":..,"recursive":..,"anchor":..,"scope_card":..,"gt_ids":[..]}]}
struct GtQuery {
    int qidx = -1, anchor = -1, scope_card = -1;
    bool rec = false;
    std::vector<int> gt;
};

static std::vector<GtQuery> load_reference(const std::string& path) {
    std::string s = read_file(path);
    std::vector<GtQuery> out;
    size_t pos = 0;
    while (true) {
        size_t p = find_key(s, "query_index", pos);
        if (p == std::string::npos) break;
        GtQuery q;
        q.qidx = (int)json_int(s, p);
        size_t pr = find_key(s, "recursive", p);
        q.rec = json_bool(s, pr);
        size_t pa = find_key(s, "anchor", pr);
        q.anchor = (int)json_int(s, pa);
        size_t pc = find_key(s, "scope_card", pa);
        q.scope_card = (int)json_int(s, pc);
        size_t pg = find_key(s, "gt_ids", pc);
        size_t lb = s.find('[', pg), rb = s.find(']', lb);
        if (lb == std::string::npos || rb == std::string::npos)
            throw std::runtime_error("bad gt_ids in reference");
        std::stringstream ss(s.substr(lb + 1, rb - lb - 1));
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            if (tok.empty() || tok == " ") continue;
            q.gt.push_back(std::atoi(tok.c_str()));
        }
        out.push_back(std::move(q));
        pos = rb;
    }
    return out;
}

// ------------------------------------------------------------------ misc ----
struct Interval {
    uint32_t b = 0, e = 0;
    bool has(uint32_t x) const { return x >= b && x < e; }
    uint64_t card() const { return (uint64_t)(e - b); }
};

// --------------------------------------------------------------- index ------
static int cmd_index(const Args& a) {
    std::string dir = a.get("data"), out = a.get("out");
    int dim = a.geti("dim", 1024);
    int M = a.geti("M", 16);
    int efc = a.geti("efc", 200);
    long long bytes = file_bytes(dir + "/base.f32");
    size_t n = (size_t)(bytes / (sizeof(float) * (long long)dim));
    std::printf("index build: n=%zu dim=%d M=%d efConstruction=%d\n", n, dim, M, efc);
    faiss::IndexHNSWFlat ix(dim, M, faiss::METRIC_L2);
    ix.hnsw.efConstruction = efc;
    ix.hnsw.efSearch = 16;
    std::ifstream f(dir + "/base.f32", std::ios::binary);
    const size_t chunk = a.geti("chunk", 50000);
    std::vector<float> buf(chunk * (size_t)dim);
    auto t0 = Clock::now();
    size_t i0 = 0;
    while (i0 < n) {
        size_t cnt = std::min(chunk, n - i0);
        f.read(reinterpret_cast<char*>(buf.data()), (std::streamsize)(cnt * dim * sizeof(float)));
        if ((size_t)f.gcount() != cnt * dim * sizeof(float)) throw std::runtime_error("short read");
        ix.add((faiss::idx_t)cnt, buf.data());
        i0 += cnt;
        std::fprintf(stderr, "\r  added %zu / %zu (%.0f s)", i0, n, us_since(t0) / 1e6);
    }
    std::fprintf(stderr, "\n");
    faiss::write_index(&ix, out.c_str());
    double secs = us_since(t0) / 1e6;
    std::printf("{\"index\":\"%s\",\"n\":%zu,\"dim\":%d,\"M\":%d,\"efConstruction\":%d,"
                "\"build_seconds\":%.1f}\n", out.c_str(), n, dim, M, efc, secs);
    return 0;
}

// ----------------------------------------------------------------- build ----
struct HlgModel {
    std::vector<int32_t> rep_loc;  // per node (eID), -1 if empty
    std::vector<int32_t> rep_up;   // per node (eID), -1 if empty
    std::vector<double> sum;       // per node centroid sum, m*dim
    std::vector<double> lsum;      // scratch: per-thread local sum buffer (dim)
};

// centroid of S(u) = sum[u]/cnt[u]
static void compute_sums(Tree& t, const Corpus& c, std::vector<double>& sum) {
    const int d = c.dim, m = t.m;
    sum.assign((size_t)m * d, 0.0);
    parallel_for(m, [&](long long ui) {
        int u = (int)ui;
        double* s = &sum[(size_t)u * d];
        for (int e = t.lo[u]; e < t.lo[u] + t.nloc[u]; ++e) {
            const float* x = &c.x[(size_t)e * d];
            for (int j = 0; j < d; ++j) s[j] += x[j];
        }
    });
    // bottom-up accumulate children (reverse preorder)
    for (int i = m - 1; i >= 0; --i) {
        int u = t.pre[i];
        if (t.parent[u] < 0) continue;
        double* dst = &sum[(size_t)t.parent[u] * d];
        const double* src = &sum[(size_t)u * d];
        for (int j = 0; j < d; ++j) dst[j] += src[j];
    }
}

static void compute_rep_loc(Tree& t, const Corpus& c, std::vector<int32_t>& rep_loc) {
    const int d = c.dim, m = t.m;
    rep_loc.assign(m, -1);
    parallel_for(m, [&](long long ui) {
        int u = (int)ui;
        int nl = t.nloc[u];
        if (nl == 0) return;
        // local centroid (exact, from local members only)
        std::vector<double> cs(d, 0.0);
        for (int e = t.lo[u]; e < t.lo[u] + nl; ++e) {
            const float* x = &c.x[(size_t)e * d];
            for (int j = 0; j < d; ++j) cs[j] += x[j];
        }
        for (int j = 0; j < d; ++j) cs[j] /= nl;
        double best = std::numeric_limits<double>::infinity();
        int bi = t.lo[u];
        for (int e = t.lo[u]; e < t.lo[u] + nl; ++e) {  // ascending eID == ascending base id
            const float* x = &c.x[(size_t)e * d];
            double s = 0;
            for (int j = 0; j < d; ++j) {
                double tt = (double)x[j] - cs[j];
                s += tt * tt;
            }
            if (s < best) {
                best = s;
                bi = e;
            }
        }
        rep_loc[u] = bi;
    });
}

// candidate representative points of C(u): {rep_loc(u)} U {rep_up(c)}, sorted by eID.
// Cluster emptiness is taken from the static layout unless `scnt` is given: after a MOVE
// the static lo/hi layout is stale and a node can legitimately become empty, so the
// dynamic subtree count must be used there (otherwise a -1 representative leaks in).
static void cluster_reps(Tree& t, int u, const std::vector<int32_t>& rep_loc,
                         const std::vector<int32_t>& rep_up, const std::vector<int>* scnt,
                         std::vector<uint32_t>& out) {
    out.clear();
    if (t.nloc[u] > 0) out.push_back((uint32_t)rep_loc[u]);
    for (int c : t.ch[u]) {
        bool nonempty = scnt ? ((*scnt)[c] > 0) : (t.hi[c] > t.lo[c]);
        if (!nonempty) continue;
        if (rep_up[c] < 0)
            throw std::runtime_error("cluster_reps: nonempty cluster without representative (c=" +
                                     std::to_string(c) + " u=" + std::to_string(u) + ")");
        out.push_back((uint32_t)rep_up[c]);
    }
    std::sort(out.begin(), out.end());
}

static void compute_rep_up(Tree& t, const Corpus& c, const std::vector<int32_t>& rep_loc,
                           const std::vector<double>& sum, std::vector<int32_t>& rep_up) {
    const int d = c.dim, m = t.m;
    rep_up.assign(m, -1);
    std::vector<uint32_t> cands;
    for (int i = m - 1; i >= 0; --i) {  // bottom-up
        int u = t.pre[i];
        int cnt = t.size(u);
        if (cnt == 0) continue;
        cluster_reps(t, u, rep_loc, rep_up, nullptr, cands);
        if (cands.empty()) continue;
        const double* s = &sum[(size_t)u * d];
        double best = std::numeric_limits<double>::infinity();
        int bi = (int)cands[0];
        for (uint32_t e : cands) {  // cands ascending: strict < keeps smallest id on ties
            const float* x = &c.x[(size_t)e * d];
            double acc = 0;
            for (int j = 0; j < d; ++j) {
                double tt = (double)x[j] * (double)cnt - s[j];
                acc += tt * tt;
            }
            if (acc < best) {
                best = acc;
                bi = (int)e;
            }
        }
        rep_up[u] = bi;
    }
}

static int cmd_build(const Args& a) {
    auto t_start = Clock::now();
    Dataset ds = load_dataset(a);
    std::string outdir = a.get("out");
    std::string idx_path = a.get("index");
    std::printf("build: nodes=%d entries=%zu dim=%d load_s=%.1f\n", ds.t.m, ds.c.n, ds.c.dim,
                us_since(t_start) / 1e6);
    // sanity: overlay must agree with the index size (skipped when --index is omitted,
    // which lets the connector build run while the index is still being built)
    if (!idx_path.empty()) {
        std::unique_ptr<faiss::Index> ixp(faiss::read_index(idx_path.c_str()));
        auto* hix = dynamic_cast<faiss::IndexHNSWFlat*>(ixp.get());
        if (!hix) throw std::runtime_error("index is not IndexHNSWFlat");
        if ((size_t)hix->ntotal != ds.c.n)
            throw std::runtime_error("index ntotal != entry count");
        std::printf("build: index loaded (ntotal=%zd, max_level=%d) load_total_s=%.1f\n",
                    (size_t)hix->ntotal, hix->hnsw.max_level, us_since(t_start) / 1e6);
    }

    Tree& t = ds.t;
    Corpus& c = ds.c;
    auto t_sums = Clock::now();
    std::vector<double> sum;
    compute_sums(t, c, sum);
    double sums_s = us_since(t_sums) / 1e6;
    auto t_reploc = Clock::now();
    std::vector<int32_t> rep_loc;
    compute_rep_loc(t, c, rep_loc);
    double reploc_s = us_since(t_reploc) / 1e6;
    auto t_repup = Clock::now();
    std::vector<int32_t> rep_up;
    compute_rep_up(t, c, rep_loc, sum, rep_up);
    double repup_s = us_since(t_repup) / 1e6;
    std::printf("build: sums_s=%.1f rep_loc_s=%.1f rep_up_s=%.1f\n", sums_s, reploc_s, repup_s);

    // MSTs per node
    auto t_mst = Clock::now();
    std::vector<std::vector<std::pair<uint32_t, uint32_t>>> per_node(t.m);
    long long n_edge = 0;
    parallel_for(t.m, [&](long long ui) {
        int u = (int)ui;
        std::vector<uint32_t> pts;
        if (t.nloc[u] >= 2) {
            pts.resize(t.nloc[u]);
            for (int k = 0; k < t.nloc[u]; ++k) pts[k] = (uint32_t)(t.lo[u] + k);
            prim_mst(c.x.data(), c.dim, pts, per_node[u]);
        }
        std::vector<uint32_t> cands;
        cluster_reps(t, u, rep_loc, rep_up, nullptr, cands);
        if (cands.size() >= 2) prim_mst(c.x.data(), c.dim, cands, per_node[u]);
    });
    std::vector<std::pair<uint32_t, uint32_t>> edges;
    for (int u = 0; u < t.m; ++u) {
        n_edge += (long long)per_node[u].size();
        for (auto& p : per_node[u]) {
            uint32_t a = (uint32_t)c.orig[p.first], b = (uint32_t)c.orig[p.second];
            if (a == b) throw std::runtime_error("self loop in MST");
            edges.push_back({a, b});
        }
    }
    double mst_s = us_since(t_mst) / 1e6;

    // added-degree statistics + representative recomputation counter
    std::vector<int32_t> adddeg(c.n, 0);
    for (auto& e : edges) {
        adddeg[c.eid[e.first]]++;
        adddeg[c.eid[e.second]]++;
    }
    long long maxdeg = 0, sumdeg = 0, nz = 0;
    for (size_t i = 0; i < c.n; ++i) {
        maxdeg = std::max<long long>(maxdeg, adddeg[i]);
        sumdeg += adddeg[i];
        if (adddeg[i]) ++nz;
    }
    long long nodes_with_conn = 0, rep_recomp = 0, sumk = 0, sumk2 = 0;
    for (int u = 0; u < t.m; ++u) {
        std::vector<uint32_t> cands;
        cluster_reps(t, u, rep_loc, rep_up, nullptr, cands);
        if (!per_node[u].empty()) ++nodes_with_conn;
        rep_recomp += (long long)cands.size();
        long long kk = (long long)cands.size();  // k_u = #clusters of u
        sumk += kk;
        sumk2 += kk * kk;
    }
    std::vector<int32_t> rep_orig(t.m, -1);
    for (int u = 0; u < t.m; ++u)
        if (rep_up[u] >= 0) rep_orig[u] = c.orig[rep_up[u]];
    write_overlay(outdir, edges, rep_orig);

    double build_s = us_since(t_start) / 1e6;
    std::printf(
        "{\"nodes\":%d,\"entries\":%zu,\"edges\":%zu,\"edges_expected_n_minus_1\":%zu,"
        "\"nodes_with_connectors\":%lld,\"max_added_degree\":%lld,\"mean_added_degree\":%.4f,"
        "\"points_with_added_edges\":%lld,\"mean_added_degree_nonzero\":%.4f,"
        "\"representative_recomputations\":%lld,\"sum_k\":%lld,\"sum_k2\":%lld,"
        "\"mst_seconds\":%.1f,\"build_seconds\":%.1f}\n",
        t.m, c.n, edges.size(), c.n - 1, nodes_with_conn, maxdeg,
        (double)sumdeg / (double)c.n, nz, nz ? (double)sumdeg / (double)nz : 0.0, rep_recomp,
        sumk, sumk2, mst_s, build_s);
    if ((long long)edges.size() != (long long)c.n - 1)
        std::fprintf(stderr,
                     "WARNING: |E_conn|=%zu != n-1=%zu (theorem 1(ii) expects equality)\n",
                     edges.size(), c.n - 1);
    return 0;
}

// ---------------------------------------------------------------- check -----
struct BfsScratch {
    std::vector<uint32_t> mark;
    uint32_t epoch = 0;
    std::vector<uint32_t> queue;
};

static void new_epoch(BfsScratch& s) {
    if (++s.epoch == 0) {
        std::fill(s.mark.begin(), s.mark.end(), 0);
        s.epoch = 1;
    }
}

static void bfs_undirected(const Graph& g, Interval iv, uint32_t src, bool with_conn,
                           BfsScratch& s, std::vector<uint32_t>& comp) {
    comp.clear();
    if (!iv.has(src)) return;
    s.queue.clear();
    s.queue.push_back(src);
    s.mark[src] = s.epoch;
    for (size_t qi = 0; qi < s.queue.size(); ++qi) {
        uint32_t v = s.queue[qi];
        comp.push_back(v);
        for (int dir = 0; dir < 2; ++dir) {
            const std::vector<uint32_t>& off = dir == 0 ? g.foff : g.roff;
            const std::vector<uint32_t>& adj = dir == 0 ? g.fadj : g.radj;
            for (uint32_t i = off[v]; i < off[v + 1]; ++i) {
                uint32_t w = adj[i];
                if (!iv.has(w) || s.mark[w] == s.epoch) continue;
                s.mark[w] = s.epoch;
                s.queue.push_back(w);
            }
        }
        if (with_conn) {
            for (uint32_t i = g.coff[v]; i < g.coff[v + 1]; ++i) {
                uint32_t w = g.cadj[i];
                if (!iv.has(w) || s.mark[w] == s.epoch) continue;
                s.mark[w] = s.epoch;
                s.queue.push_back(w);
            }
        }
    }
}

// directed reachability from src inside the interval (out-edges only, as search does)
// caller must call new_epoch(s) first
static size_t reach_directed(const Graph& g, Interval iv, uint32_t src, bool with_conn,
                             BfsScratch& s) {
    if (!iv.has(src)) return 0;
    s.queue.clear();
    s.queue.push_back(src);
    s.mark[src] = s.epoch;
    for (size_t qi = 0; qi < s.queue.size(); ++qi) {
        uint32_t v = s.queue[qi];
        for (uint32_t i = g.foff[v]; i < g.foff[v + 1]; ++i) {
            uint32_t w = g.fadj[i];
            if (!iv.has(w) || s.mark[w] == s.epoch) continue;
            s.mark[w] = s.epoch;
            s.queue.push_back(w);
        }
        if (with_conn)
            for (uint32_t i = g.coff[v]; i < g.coff[v + 1]; ++i) {
                uint32_t w = g.cadj[i];
                if (!iv.has(w) || s.mark[w] == s.epoch) continue;
                s.mark[w] = s.epoch;
                s.queue.push_back(w);
            }
    }
    return s.queue.size();
}

static int count_components_undirected(const Graph& g, Interval iv, BfsScratch& s, bool with_conn,
                                      std::vector<uint32_t>& comp) {
    new_epoch(s);
    int ncomp = 0;
    for (uint32_t v = iv.b; v < iv.e; ++v) {
        if (s.mark[v] == s.epoch) continue;
        bfs_undirected(g, iv, v, with_conn, s, comp);
        ++ncomp;
    }
    return ncomp;
}

static int cmd_check(const Args& a) {
    Dataset ds = load_dataset(a);
    std::vector<std::pair<uint32_t, uint32_t>> edges;
    std::vector<int32_t> rep_orig;
    read_overlay(a.get("overlay"), edges, rep_orig, ds.t.m);
    std::unique_ptr<faiss::Index> ixp(faiss::read_index(a.get("index").c_str()));
    auto* hix = dynamic_cast<faiss::IndexHNSWFlat*>(ixp.get());
    if (!hix) throw std::runtime_error("index is not IndexHNSWFlat");
    Graph g;
    build_hnsw_graph(*hix, ds.c.eid, ds.c.n, g);
    build_connector_csr(edges, ds.c.eid, ds.c.n, g);

    // stratified sample of nodes by |S(u)|
    int per = a.geti("per-bucket", 25);
    std::vector<std::vector<int>> bucket(4);
    for (int u = 0; u < ds.t.m; ++u) {
        int s = ds.t.size(u);
        int b = s < 100 ? 0 : s < 1000 ? 1 : s < 10000 ? 2 : 3;
        bucket[b].push_back(u);
    }
    std::mt19937 rng(20261028u);
    std::vector<int> sample;
    for (int b = 0; b < 4; ++b) {
        auto v = bucket[b];
        for (int k = 0; k < per && !v.empty(); ++k) {
            std::uniform_int_distribution<size_t> dist(0, v.size() - 1);
            size_t i = dist(rng);
            sample.push_back(v[i]);
            v.erase(v.begin() + (long)i);
        }
    }
    BfsScratch sc;
    sc.mark.assign(ds.c.n, 0);
    std::vector<uint32_t> comp;
    std::printf("node,size,bucket,components_base,components_hlg,reach_base,reach_hlg,"
                "reach_frac_base,reach_frac_hlg,entry_eid,entry_orig\n");
    int stat_base_gt1 = 0, stat_hlg_gt1 = 0;
    for (int u : sample) {
        Interval iv{(uint32_t)ds.t.lo[u], (uint32_t)ds.t.hi[u]};
        int cb = count_components_undirected(g, iv, sc, false, comp);
        int ch = count_components_undirected(g, iv, sc, true, comp);
        uint32_t entry = rep_orig[u] >= 0 ? (uint32_t)ds.c.eid[rep_orig[u]] : iv.b;
        new_epoch(sc);
        size_t rb = reach_directed(g, iv, entry, false, sc);
        new_epoch(sc);
        size_t rh = reach_directed(g, iv, entry, true, sc);
        int b = iv.card() < 100 ? 0 : iv.card() < 1000 ? 1 : iv.card() < 10000 ? 2 : 3;
        std::printf("%d,%llu,%d,%d,%d,%zu,%zu,%.5f,%.5f,%u,%d\n", u,
                    (unsigned long long)iv.card(), b, cb, ch, rb, rh,
                    (double)rb / (double)iv.card(), (double)rh / (double)iv.card(), entry,
                    ds.c.orig[entry]);
        if (cb > 1) ++stat_base_gt1;
        if (ch > 1) ++stat_hlg_gt1;
    }
    std::fprintf(stderr, "check: sampled=%zu base_gt1=%d hlg_gt1=%d\n", sample.size(),
                 stat_base_gt1, stat_hlg_gt1);
    return 0;
}

// ---------------------------------------------------------------- bench -----
struct SearchOut {
    std::vector<int32_t> ids;   // orig ids
    std::vector<float> dist;
    int visited = 0;
};

struct Beam {
    std::vector<uint32_t> mark;
    uint32_t epoch = 0;
    std::vector<std::pair<float, uint32_t>> cand, res;  // min-heap / max-heap
};

// Self-implemented HNSW layer-0 beam search. The traversal beam contains every
// visited point; `filtered` contains only scope members. This separation matters
// for the off arm: its global entry and routing points are normally outside scope.
static void search_layer0(const Graph& g, const Corpus& c, const float* q, Interval iv,
                          bool use_conn, bool strict_scope, uint32_t entry, int ef, int k,
                          Beam& b, SearchOut& out) {
    out.ids.clear();
    out.dist.clear();
    out.visited = 0;
    if (iv.card() == 0 || entry >= c.n || (strict_scope && !iv.has(entry))) return;
    ef = std::max(ef, k);
    if (++b.epoch == 0) {
        std::fill(b.mark.begin(), b.mark.end(), 0);
        b.epoch = 1;
    }
    b.cand.clear();
    b.res.clear();  // max-heap of the ef best traversal points
    std::vector<std::pair<float, uint32_t>> filtered;  // max-heap, at most k
    auto dist = [&](uint32_t e) {
        return (float)dist2(q, &c.x[(size_t)e * c.dim], c.dim);
    };
    auto cmp_min = [](const std::pair<float, uint32_t>& A,
                      const std::pair<float, uint32_t>& B) {
        return A.first > B.first || (A.first == B.first && A.second > B.second);
    };
    auto cmp_max = [](const std::pair<float, uint32_t>& A,
                      const std::pair<float, uint32_t>& B) {
        return A.first < B.first || (A.first == B.first && A.second < B.second);
    };
    auto better = [](float d, uint32_t id, const std::pair<float, uint32_t>& worst) {
        return d < worst.first || (d == worst.first && id < worst.second);
    };
    auto add_filtered = [&](uint32_t e, float d) {
        if (!iv.has(e)) return;
        if ((int)filtered.size() < k || better(d, e, filtered.front())) {
            filtered.push_back({d, e});
            std::push_heap(filtered.begin(), filtered.end(), cmp_max);
            if ((int)filtered.size() > k) {
                std::pop_heap(filtered.begin(), filtered.end(), cmp_max);
                filtered.pop_back();
            }
        }
    };

    float d0 = dist(entry);
    b.cand.push_back({d0, entry});
    b.res.push_back({d0, entry});
    b.mark[entry] = b.epoch;
    add_filtered(entry, d0);
    out.visited = 1;
    std::make_heap(b.cand.begin(), b.cand.end(), cmp_min);
    std::make_heap(b.res.begin(), b.res.end(), cmp_max);
    while (!b.cand.empty()) {
        std::pop_heap(b.cand.begin(), b.cand.end(), cmp_min);
        auto [dcur, v] = b.cand.back();
        b.cand.pop_back();
        if ((int)b.res.size() >= ef && dcur > b.res.front().first) break;
        auto visit = [&](uint32_t w) {
            if (b.mark[w] == b.epoch) return;
            b.mark[w] = b.epoch;
            if (strict_scope && !iv.has(w)) return;
            ++out.visited;
            float d = dist(w);
            add_filtered(w, d);
            if ((int)b.res.size() < ef || better(d, w, b.res.front())) {
                b.cand.push_back({d, w});
                std::push_heap(b.cand.begin(), b.cand.end(), cmp_min);
                b.res.push_back({d, w});
                std::push_heap(b.res.begin(), b.res.end(), cmp_max);
                if ((int)b.res.size() > ef) {
                    std::pop_heap(b.res.begin(), b.res.end(), cmp_max);
                    b.res.pop_back();
                }
            }
        };
        for (uint32_t i = g.foff[v]; i < g.foff[v + 1]; ++i) visit(g.fadj[i]);
        if (use_conn)
            for (uint32_t i = g.coff[v]; i < g.coff[v + 1]; ++i) visit(g.cadj[i]);
    }
    std::sort(filtered.begin(), filtered.end(),
              [](const auto& A, const auto& B) {
                  return A.first < B.first || (A.first == B.first && A.second < B.second);
              });
    for (auto& [d, e] : filtered) {
        out.dist.push_back(d);
        out.ids.push_back(c.orig[e]);
    }
}

// greedy descent of the upper levels starting from the global entry point (faiss semantics:
// HNSW::search with upper_beam == 1 uses greedy_update_nearest on every level >= 1)
static uint32_t greedy_descent_ep(const faiss::IndexHNSW& ix, const Corpus& c, const float* q) {
    const auto& h = ix.hnsw;
    if (h.entry_point < 0) return 0;
    auto dist = [&](int v_orig) {
        return (float)dist2(q, &c.x[(size_t)c.eid[v_orig] * c.dim], c.dim);
    };
    int cur = h.entry_point;
    float dcur = dist(cur);
    for (int level = h.max_level; level >= 1; --level) {
        bool changed = true;
        while (changed) {
            changed = false;
            size_t begin = h.offsets[cur] + h.cum_nb_neighbors(level);
            size_t end = h.offsets[cur] + h.cum_nb_neighbors(level + 1);
            for (size_t j = begin; j < end; ++j) {
                int v = h.neighbors[j];
                if (v < 0) break;
                float d = dist(v);
                if (d < dcur) {
                    dcur = d;
                    cur = v;
                    changed = true;
                }
            }
        }
    }
    return (uint32_t)c.eid[cur];
}

static int cmd_bench(const Args& a) {
    auto t_all = Clock::now();
    Dataset ds = load_dataset(a);
    std::vector<std::pair<uint32_t, uint32_t>> edges;
    std::vector<int32_t> rep_orig;
    read_overlay(a.get("overlay"), edges, rep_orig, ds.t.m);
    std::unique_ptr<faiss::Index> ixp(faiss::read_index(a.get("index").c_str()));
    auto* hix = dynamic_cast<faiss::IndexHNSWFlat*>(ixp.get());
    if (!hix) throw std::runtime_error("index is not IndexHNSWFlat");
    Graph g;
    build_hnsw_graph(*hix, ds.c.eid, ds.c.n, g);
    build_connector_csr(edges, ds.c.eid, ds.c.n, g);
    std::printf("bench: load_s=%.1f graph_edges=%zu conn_edges=%zu\n", us_since(t_all) / 1e6,
                (size_t)g.fadj.size(), edges.size());

    // workload
    std::string wl_path = a.get("workload");
    std::ifstream wf(wl_path);
    if (!wf) throw std::runtime_error("cannot open " + wl_path);
    int nq = 0, dummy = 0;
    wf >> nq >> dummy;
    struct Q {
        int qidx, rec, anchor;
    };
    std::vector<Q> qs(nq);
    for (int i = 0; i < nq; ++i) wf >> qs[i].qidx >> qs[i].rec >> qs[i].anchor;
    // query vectors
    std::string tpath = a.get("data") + "/test.f32";
    long long tb = file_bytes(tpath);
    size_t nq_test = (size_t)(tb / (sizeof(float) * (long long)ds.c.dim));
    std::vector<float> tq((size_t)nq_test * ds.c.dim);
    {
        std::ifstream f(tpath, std::ios::binary);
        f.read(reinterpret_cast<char*>(tq.data()), (std::streamsize)(tb));
    }
    std::printf("bench: queries=%d test_vectors=%zu\n", nq, nq_test);

    std::string gt_path = a.get("gt");
    std::vector<GtQuery> gt;
    if (!gt_path.empty()) gt = load_reference(gt_path);
    if ((int)gt.size() != nq) throw std::runtime_error("GT query count mismatch");

    std::string mode = a.get("connectors", "on");
    std::string engine = a.get("engine", "self");
    int k = a.geti("k", 10);
    std::vector<int> efs = a.ints("efs");
    if (efs.empty()) efs = {16, 32, 64, 128, 256};
    std::string csv_path = a.get("csv");
    FILE* csv = csv_path.empty() ? nullptr : std::fopen(csv_path.c_str(), "w");
    if (csv)
        std::fprintf(csv, "mode,ef,query_index,query_type,anchor,scope_card,gt_count,hit_count,"
                          "valid_return_count,dir_us,search_us,e2e_ms,result_ids\n");
    Beam beam;
    beam.mark.assign(ds.c.n, 0);

    for (int ef : efs) {
        double macro = 0, ghit = 0, gtot = 0;
        double sum_search = 0, sum_dir = 0;
        int zero = 0, card_mismatch = 0, outside = 0, dup = 0;
        for (int qi = 0; qi < nq; ++qi) {
            const Q& q = qs[qi];
            auto t0 = Clock::now();
            Interval iv;
            uint32_t entry = 0;
            if (q.rec) {
                iv = Interval{(uint32_t)ds.t.lo[q.anchor], (uint32_t)ds.t.hi[q.anchor]};
                entry = rep_orig[q.anchor] >= 0 ? (uint32_t)ds.c.eid[rep_orig[q.anchor]] : iv.b;
            } else {
                iv = Interval{(uint32_t)ds.t.lo[q.anchor],
                              (uint32_t)(ds.t.lo[q.anchor] + ds.t.nloc[q.anchor])};
                entry = iv.b;
            }
            double dir_us = us_since(t0);
            if (iv.card() > 0 && !iv.has(entry)) {
                std::fprintf(stderr, "FATAL: entry not in scope qi=%d anchor=%d\n", qi, q.anchor);
                return 2;
            }
            if (iv.card() != (uint64_t)gt[qi].scope_card) {
                ++card_mismatch;
                if (card_mismatch <= 5)
                    std::fprintf(stderr, "SCOPE_CARD_MISMATCH qi=%d ours=%llu ref=%d\n", qi,
                                 (unsigned long long)iv.card(), gt[qi].scope_card);
            }
            SearchOut out;
            double search_us = 0;
            const float* qv = &tq[(size_t)q.qidx * ds.c.dim];
            if (iv.card() == 0) {
                // Empty local scopes are valid workload queries: no entry and no result.
            } else if (engine == "faiss") {
                struct IvSel : faiss::IDSelector {
                    const std::vector<int32_t>* eid;
                    Interval iv;
                    IvSel(const std::vector<int32_t>* e, Interval i) : eid(e), iv(i) {}
                    bool is_member(faiss::idx_t id) const override {
                        return iv.has((uint32_t)(*eid)[(size_t)id]);
                    }
                    bool is_member(faiss::idx_t id, int) const override { return is_member(id); }
                } sel(&ds.c.eid, iv);
                faiss::SearchParametersHNSW sp;
                sp.efSearch = ef;
                sp.sel = &sel;
                std::vector<faiss::idx_t> I(k, -1);
                std::vector<float> D(k, 0);
                auto t1 = Clock::now();
                hix->search(1, qv, k, D.data(), I.data(), &sp);
                search_us = us_since(t1);
                for (int i = 0; i < k; ++i)
                    if (I[i] >= 0) {
                        out.ids.push_back((int32_t)I[i]);
                        out.dist.push_back(D[i]);
                    }
            } else {
                bool use_conn = (mode == "on" || mode == "on-shortcut");
                bool strict = (mode != "off");
                uint32_t ep = strict ? entry : greedy_descent_ep(*hix, ds.c, qv);
                auto t1 = Clock::now();
                search_layer0(g, ds.c, qv, iv, use_conn && g.coff.size() > 1, strict, ep, ef, k,
                              beam, out);
                search_us = us_since(t1);
            }
            int hit = 0, valid = 0;
            std::set<int32_t> seen;
            for (size_t i = 0; i < out.ids.size(); ++i) {
                int32_t id = out.ids[i];
                if (!seen.insert(id).second) {
                    ++dup;
                    continue;
                }
                bool in_scope = id >= 0 && (size_t)id < ds.c.n && iv.has((uint32_t)ds.c.eid[id]);
                if (!in_scope) {
                    ++outside;
                    continue;
                }
                ++valid;
                if (std::find(gt[qi].gt.begin(), gt[qi].gt.end(), (int)id) != gt[qi].gt.end())
                    ++hit;
            }
            if (out.ids.empty()) ++zero;
            int gt_count = (int)gt[qi].gt.size();
            macro += (double)hit / (double)k;
            ghit += hit;
            gtot += gt_count;
            sum_search += search_us;
            sum_dir += dir_us;
            if (csv) {
                std::fprintf(csv, "%s,%d,%d,%s,%d,%llu,%d,%d,%d,%.3f,%.3f,%.4f,", mode.c_str(), ef,
                             qi, q.rec ? "recursive" : "non-recursive", q.anchor,
                             (unsigned long long)iv.card(), gt_count, hit, valid, dir_us,
                             search_us, (search_us + dir_us) / 1000.0);
                for (size_t i = 0; i < out.ids.size(); ++i)
                    std::fprintf(csv, "%s%d", i ? " " : "", out.ids[i]);
                std::fprintf(csv, "\n");
            }
        }
        std::printf("SUMMARY mode=%s engine=%s ef=%d macro_recall=%.6f global_pair_recall=%.6f "
                    "mean_search_us=%.1f mean_dir_us=%.1f zero_results=%d out_of_scope=%d "
                    "duplicates=%d scope_card_mismatch=%d\n",
                    mode.c_str(), engine.c_str(), ef, macro / nq, gtot > 0 ? ghit / gtot : 0.0,
                    sum_search / nq, sum_dir / nq, zero, outside, dup, card_mismatch);
        std::fflush(stdout);
    }
    if (csv) std::fclose(csv);
    return 0;
}

// ----------------------------------------------------------------- move -----
struct MoveState {
    Tree t;
    const Corpus* c = nullptr;
    std::vector<double> sum;
    std::vector<int32_t> rep_loc, rep_up;
    std::vector<std::vector<std::pair<uint32_t, uint32_t>>> per_node;  // eID pairs
    std::vector<int> scnt;
};

static void move_recompute_node(MoveState& s, int u) {
    const Corpus& c = *s.c;
    const int d = c.dim;
    Tree& t = s.t;
    // centroid sum from children sums + local entries (children sums are maintained)
    double* sm = &s.sum[(size_t)u * d];
    std::fill(sm, sm + d, 0.0);
    for (int e = t.lo[u]; e < t.lo[u] + t.nloc[u]; ++e) {
        const float* x = &c.x[(size_t)e * d];
        for (int j = 0; j < d; ++j) sm[j] += x[j];
    }
    for (int ch : t.ch[u]) {
        const double* cs = &s.sum[(size_t)ch * d];
        for (int j = 0; j < d; ++j) sm[j] += cs[j];
    }
    // representative of S(u) (composition rule) and the node's own MST
    std::vector<uint32_t> cands;
    cluster_reps(t, u, s.rep_loc, s.rep_up, &s.scnt, cands);
    int cnt = s.scnt[u];
    if (cnt > 0 && !cands.empty()) {
        double best = std::numeric_limits<double>::infinity();
        int bi = (int)cands[0];
        for (uint32_t e : cands) {
            const float* x = &c.x[(size_t)e * d];
            double acc = 0;
            for (int j = 0; j < d; ++j) {
                double tt = (double)x[j] * (double)cnt - sm[j];
                acc += tt * tt;
            }
            if (acc < best) {
                best = acc;
                bi = (int)e;
            }
        }
        s.rep_up[u] = bi;
    } else {
        s.rep_up[u] = -1;
    }
    auto& out = s.per_node[u];
    out.clear();
    if (t.nloc[u] >= 2) {
        std::vector<uint32_t> pts(t.nloc[u]);
        for (int j = 0; j < t.nloc[u]; ++j) pts[j] = (uint32_t)(t.lo[u] + j);
        prim_mst(c.x.data(), d, pts, out);
    }
    if (cands.size() >= 2) prim_mst(c.x.data(), d, cands, out);
}

static void recompute_scnt(MoveState& s) {
    Tree& t = s.t;
    for (int u = 0; u < t.m; ++u) s.scnt[u] = t.nloc[u];
    for (int i = t.m - 1; i >= 0; --i) {
        int u = t.pre[i];
        if (t.parent[u] >= 0) s.scnt[t.parent[u]] += s.scnt[u];
    }
}

static int cmd_move(const Args& a) {
    auto t_all = Clock::now();
    Dataset ds = load_dataset(a);
    MoveState s;
    s.t = ds.t;
    s.c = &ds.c;
    compute_sums(s.t, ds.c, s.sum);
    compute_rep_loc(s.t, ds.c, s.rep_loc);
    s.scnt.assign(s.t.m, 0);
    recompute_scnt(s);
    // initial connector state (same routines as the incremental path, bottom-up)
    s.rep_up.assign(s.t.m, -1);
    s.per_node.assign(s.t.m, {});
    for (int i = s.t.m - 1; i >= 0; --i) move_recompute_node(s, s.t.pre[i]);
    std::printf("move: init_s=%.1f nodes=%d entries=%zu\n", us_since(t_all) / 1e6, s.t.m, ds.c.n);
    std::string ops_path = a.get("ops");
    std::ifstream of(ops_path);
    if (!of) throw std::runtime_error("cannot open " + ops_path);
    int nop = 0, dummy = 0;
    of >> nop >> dummy;
    std::vector<std::pair<std::string, std::pair<int, int>>> ops;
    for (int i = 0; i < nop; ++i) {
        std::string kind;
        int u, w;
        of >> kind >> u >> w;
        ops.push_back({kind, {u, w}});
    }
    int max_ops = a.geti("max-ops", 0);
    std::printf("op_index,op_kind,move_us,subtree_size,affected_nodes,sum_k,sum_k2,"
                "edges_removed,edges_added,scnt_ok\n");
    for (int oi = 0; oi < (int)ops.size() && (max_ops == 0 || oi < max_ops); ++oi) {
        auto& op = ops[oi];
        if (op.first != "MOVE") continue;
        int u = op.second.first, w = op.second.second;
        if (u <= 0 || u == w || u >= s.t.m || w < 0 || w >= s.t.m) continue;
        // w must not be inside subtree(u)
        bool bad = false;
        for (int x = w; x >= 0; x = s.t.parent[x])
            if (x == u) bad = true;
        if (bad) continue;
        int p = s.t.parent[u];
        if (p < 0 || p == w) continue;  // no-op move
        int sub = s.scnt[u];
        auto t0 = Clock::now();
        // ---- structural change
        {
            auto& pc = s.t.ch[p];
            pc.erase(std::remove(pc.begin(), pc.end(), u), pc.end());
            s.t.ch[w].push_back(u);
            s.t.parent[u] = w;
        }
        // ---- affected set (定理 2): {p,w} U {parent(a): a in Delta}
        std::vector<int> anc_p, anc_w;
        for (int x = p; x >= 0; x = s.t.parent[x]) anc_p.push_back(x);
        for (int x = w; x >= 0; x = s.t.parent[x]) anc_w.push_back(x);
        std::set<int> sp(anc_p.begin(), anc_p.end()), sw(anc_w.begin(), anc_w.end());
        std::set<int> affected{p, w};
        for (int x : anc_p)
            if (!sw.count(x) && s.t.parent[x] >= 0) affected.insert(s.t.parent[x]);
        for (int x : anc_w)
            if (!sp.count(x) && s.t.parent[x] >= 0) affected.insert(s.t.parent[x]);
        // reparenting changes S(.) of ancestors on the exclusive paths:
        // p loses S(u), w gains S(u); update their subtree counts
        for (int x : anc_p) s.scnt[x] -= sub;
        for (int x : anc_w) s.scnt[x] += sub;
        // process bottom-up: a node's centroid/MST depend on its children's state
        std::vector<std::pair<int, int>> aff_depth;  // (depth, node)
        for (int x : affected) {
            int dep = 0;
            for (int y = s.t.parent[x]; y >= 0; y = s.t.parent[y]) ++dep;
            aff_depth.push_back({-dep, x});
        }
        std::sort(aff_depth.begin(), aff_depth.end());
        long long sumk = 0, sumk2 = 0, er = 0, ea = 0;
        for (auto& pr : aff_depth) {
            int x = pr.second;
            int kk = 0;
            if (s.t.nloc[x] > 0) ++kk;
            for (int c : s.t.ch[x])
                if (s.scnt[c] > 0) ++kk;
            sumk += kk;
            sumk2 += (long long)kk * kk;
            er += (long long)s.per_node[x].size();
            move_recompute_node(s, x);
            ea += (long long)s.per_node[x].size();
        }
        double us = us_since(t0);
        // ---- untimed verification: subtree counts from scratch on the CURRENT tree.
        // The static preorder is stale after a MOVE, so order by current depth.
        std::vector<int> ref_scnt(s.t.m);
        for (int x = 0; x < s.t.m; ++x) ref_scnt[x] = s.t.nloc[x];
        {
            std::vector<int> ord(s.t.m);
            std::iota(ord.begin(), ord.end(), 0);
            std::vector<int> dep(s.t.m, 0);
            for (int x = 0; x < s.t.m; ++x) {
                int dp = 0;
                for (int y = s.t.parent[x]; y >= 0; y = s.t.parent[y]) ++dp;
                dep[x] = dp;
            }
            std::sort(ord.begin(), ord.end(),
                      [&](int A, int B) { return dep[A] != dep[B] ? dep[A] > dep[B] : A < B; });
            for (int x : ord)
                if (s.t.parent[x] >= 0) ref_scnt[s.t.parent[x]] += ref_scnt[x];
        }
        bool ok = true;
        for (int x = 0; x < s.t.m; ++x)
            if (ref_scnt[x] != s.scnt[x]) ok = false;
        std::printf("%d,MOVE,%.2f,%d,%zu,%lld,%lld,%lld,%lld,%d\n", oi, us, sub, affected.size(),
                    sumk, sumk2, er, ea, (int)ok);
        std::fflush(stdout);
    }
    if (a.geti("verify-rebuild", 0)) {
        // recompute sums / rep_loc / rep_up from scratch on the mutated tree and compare.
        // Uses scnt-based sizes because the static lo/hi layout is stale after MOVE.
        auto tv = Clock::now();
        std::vector<int> dep(s.t.m, 0);
        for (int u = 0; u < s.t.m; ++u) {
            int dp = 0;
            for (int y = s.t.parent[u]; y >= 0; y = s.t.parent[y]) ++dp;
            dep[u] = dp;
        }
        std::vector<int> order(s.t.m);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int A, int B) {
            return dep[A] != dep[B] ? dep[A] > dep[B] : A < B;
        });
        const int d = s.c->dim;
        std::vector<double> vsum((size_t)s.t.m * d, 0.0);
        for (int u : order) {
            double* sm = &vsum[(size_t)u * d];
            for (int e = s.t.lo[u]; e < s.t.lo[u] + s.t.nloc[u]; ++e) {
                const float* x = &s.c->x[(size_t)e * d];
                for (int j = 0; j < d; ++j) sm[j] += x[j];
            }
            for (int ch : s.t.ch[u]) {
                const double* cs = &vsum[(size_t)ch * d];
                for (int j = 0; j < d; ++j) sm[j] += cs[j];
            }
        }
        std::vector<int32_t> vrep(s.t.m, -1);
        // rep_loc is unchanged by MOVE (h fixed); reuse s.rep_loc
        for (int u : order) {
            int cnt = s.scnt[u];
            std::vector<uint32_t> cands;
            if (s.t.nloc[u] > 0) cands.push_back((uint32_t)s.rep_loc[u]);
            for (int ch : s.t.ch[u])
                if (s.scnt[ch] > 0) cands.push_back((uint32_t)vrep[ch]);
            std::sort(cands.begin(), cands.end());
            if (cands.empty() || cnt == 0) continue;
            const double* sm = &vsum[(size_t)u * d];
            double best = std::numeric_limits<double>::infinity();
            int bi = (int)cands[0];
            for (uint32_t e : cands) {
                const float* x = &s.c->x[(size_t)e * d];
                double acc = 0;
                for (int j = 0; j < d; ++j) {
                    double tt = (double)x[j] * (double)cnt - sm[j];
                    acc += tt * tt;
                }
                if (acc < best) {
                    best = acc;
                    bi = (int)e;
                }
            }
            vrep[u] = bi;
        }
        int bad_rep = 0, bad_sum = 0, bad_cnt = 0;
        for (int u = 0; u < s.t.m; ++u) {
            if (vrep[u] != s.rep_up[u]) ++bad_rep;
            for (int j = 0; j < d; ++j)
                if (std::fabs(vsum[(size_t)u * d + j] - s.sum[(size_t)u * d + j]) > 1e-6) {
                    ++bad_sum;
                    break;
                }
            int c = 0;
            for (int e = s.t.lo[u]; e < s.t.lo[u] + s.t.nloc[u]; ++e) ++c;
            for (int ch : s.t.ch[u]) c += s.scnt[ch];
            if (c != s.scnt[u]) ++bad_cnt;
        }
        std::printf("VERIFY nodes=%d rep_mismatch=%d sum_mismatch=%d cnt_mismatch=%d us=%.0f\n",
                    s.t.m, bad_rep, bad_sum, bad_cnt, us_since(tv));
    }
    return 0;
}

// ----------------------------------------------------------------- main -----
int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: hlg_tool <index|build|check|bench|move> [--k v ...]\n"
                     "  index --data DIR --out PATH [--dim 1024] [--M 16] [--efc 200]\n"
                     "  build --data DIR --index PATH --out DIR [--dim 1024]\n"
                     "  check --data DIR --index PATH --overlay DIR [--per-bucket 25]\n"
                     "  bench --data DIR --index PATH --overlay DIR --workload W [--gt P]\n"
                     "        [--connectors on|off] [--engine self|faiss] [--efs 16,32,..] [--csv P]\n"
                     "  move  --data DIR --ops P [--max-ops N]\n");
        return 1;
    }
    std::string cmd = argv[1];
    Args a = Args::parse(argc, argv);
    try {
        if (cmd == "index") return cmd_index(a);
        if (cmd == "build") return cmd_build(a);
        if (cmd == "check") return cmd_check(a);
        if (cmd == "bench") return cmd_bench(a);
        if (cmd == "move") return cmd_move(a);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ERROR: %s\n", e.what());
        return 1;
    }
    std::fprintf(stderr, "unknown subcommand %s\n", cmd.c_str());
    return 1;
}
