// boundary_probe.cpp -- structure probes for HNSW-vs-directory-tree boundaries.
//
// Modes:
//   build    : build faiss::IndexHNSWFlat on (a subset of) wiki_full/base.f32,
//              report load/build/write seconds and peak RSS, write index.
//   export   : load index, write level-0 adjacency as CSR plus out-degree table.
//   analyze  : join the level-0 graph with wiki tree + bindings + DSM MOVE ops;
//              per-op boundary statistics and induced-subgraph components.
//
// HNSW storage convention (verified against deps/PathSeer/faiss/faiss/impl/
// HNSW.h and HNSW.cpp):
//   neighbors[] is one flat int32 array; for node i the slots are
//   [offsets[i], offsets[i+1]) laid out per level in ascending level order,
//   level L occupying [offsets[i] + cum_nb_neighbors(L),
//                       offsets[i] + cum_nb_neighbors(L+1)).
//   set_default_probas() pushes cum_nneighbor_per_level[0] = 0 and gives level 0
//   exactly 2*M slots, level>=1 M slots. Unused slots stay -1.
//   storage_idx_t == insertion id == row id here (sequential add).
//   levels[i] >= 1 for every node, so every node owns level-0 slots.
//
// No recursion anywhere (iterative DFS/BFS with explicit stacks).

#include <faiss/IndexHNSW.h>
#include <faiss/IndexFlatCodes.h>
#include <faiss/index_io.h>
#include <faiss/utils/distances.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <numeric>
#include <random>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;
static double secs_since(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

static double peak_rss_gb() {
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    double v = -1;
    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, "VmHWM:", 6)) { v = atol(line + 6) / 1024.0 / 1024.0; break; }
    }
    fclose(f);
    return v;
}

static std::vector<std::string> split_ws(const char* s) {
    std::vector<std::string> out;
    size_t i = 0, n = strlen(s);
    while (i < n) {
        while (i < n && isspace((unsigned char)s[i])) ++i;
        size_t j = i;
        while (j < n && !isspace((unsigned char)s[j])) ++j;
        if (j > i) out.emplace_back(s + i, j - i);
        i = j;
    }
    return out;
}

struct Args {
    std::map<std::string, std::string> kv;
    std::string get(const std::string& k, const std::string& def = "") const {
        auto it = kv.find(k);
        return it == kv.end() ? def : it->second;
    }
    long long geti(const std::string& k, long long def) const {
        auto it = kv.find(k);
        return it == kv.end() ? def : atoll(it->second.c_str());
    }
    double getd(const std::string& k, double def) const {
        auto it = kv.find(k);
        return it == kv.end() ? def : atof(it->second.c_str());
    }
};

static Args parse_args(int argc, char** argv, int start) {
    Args a;
    for (int i = start; i + 1 < argc; i += 2) a.kv[argv[i]] = argv[i + 1];
    return a;
}

static double quantile(std::vector<double> v, double q) {
    if (v.empty()) return NAN;
    std::sort(v.begin(), v.end());
    if (v.size() == 1) return v[0];
    double pos = q * (double)(v.size() - 1);
    size_t lo = (size_t)std::floor(pos);
    size_t hi = std::min(lo + 1, v.size() - 1);
    return v[lo] * (1 - (pos - lo)) + v[hi] * (pos - lo);
}

static double mean_of(const std::vector<double>& v) {
    if (v.empty()) return NAN;
    return std::accumulate(v.begin(), v.end(), 0.0) / (double)v.size();
}

// ------------------------------------------------------------------ build ---
static int mode_build(const Args& a) {
    const std::string base_path = a.get("--base");
    const std::string out_path = a.get("--out");
    const std::string json_path = a.get("--json");
    const std::string map_path = a.get("--pilot-map");
    const int d = (int)a.geti("--d", 1024);
    const int M = (int)a.geti("--M", 16);
    const int efC = (int)a.geti("--efC", 200);
    const long long N = a.geti("--n", -1);
    const long long K = a.geti("--sample-n", -1);
    const uint64_t seed = (uint64_t)a.geti("--seed", 42);

    FILE* f = fopen(base_path.c_str(), "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", base_path.c_str()); return 1; }
    fseeko(f, 0, SEEK_END);
    long long nrows = (long long)ftello(f) / d / 4;
    if (N > 0 && N < nrows) nrows = N;
    const bool pilot = (K > 0 && K < nrows);
    const long long n = pilot ? K : nrows;

    std::vector<int64_t> rows((size_t)n);
    if (pilot) {
        std::vector<int64_t> all((size_t)nrows);
        std::iota(all.begin(), all.end(), 0);
        std::mt19937_64 rng(seed);
        std::shuffle(all.begin(), all.end(), rng);
        std::copy(all.begin(), all.begin() + n, rows.begin());
        std::sort(rows.begin(), rows.end());
        if (!map_path.empty()) {
            FILE* mf = fopen(map_path.c_str(), "w");
            for (long long i = 0; i < n; ++i) fprintf(mf, "%lld\n", (long long)rows[i]);
            fclose(mf);
        }
    } else {
        for (long long i = 0; i < n; ++i) rows[i] = i;
    }

    auto t_load = Clock::now();
    std::vector<float> data((size_t)n * d);
    if (!pilot) {
        fseeko(f, 0, SEEK_SET);
        if (fread(data.data(), 4, (size_t)n * d, f) != (size_t)n * d) {
            fprintf(stderr, "short read\n"); return 1;
        }
    } else {
        for (long long i = 0; i < n; ++i) {
            if (fseeko(f, (off_t)rows[i] * d * 4, SEEK_SET) != 0) return 1;
            if (fread(data.data() + (size_t)i * d, 4, d, f) != (size_t)d) {
                fprintf(stderr, "short read row %lld\n", (long long)rows[i]); return 1;
            }
        }
    }
    fclose(f);
    const double load_s = secs_since(t_load);

    faiss::IndexHNSWFlat* ix = new faiss::IndexHNSWFlat(d, M);
    ix->hnsw.efConstruction = efC;
    ix->verbose = false;
    const double rss_before = peak_rss_gb();

    auto t_build = Clock::now();
    ix->add(n, data.data());
    const double build_s = secs_since(t_build);
    const double rss_after_build = peak_rss_gb();

    auto t_write = Clock::now();
    faiss::write_index(ix, out_path.c_str());
    const double write_s = secs_since(t_write);
    const double rss_final = peak_rss_gb();

    std::vector<size_t> lvl_hist(ix->hnsw.cum_nneighbor_per_level.size() + 2, 0);
    for (int lv : ix->hnsw.levels)
        if (lv >= 0 && (size_t)lv + 1 < lvl_hist.size()) lvl_hist[lv + 1]++;

    FILE* jf = fopen(json_path.c_str(), "w");
    fprintf(jf, "{\n  \"mode\": \"build\",\n  \"pilot\": %s,\n", pilot ? "true" : "false");
    fprintf(jf, "  \"rows_in_base\": %lld,\n  \"n_indexed\": %lld,\n", nrows, n);
    fprintf(jf, "  \"d\": %d,\n  \"M\": %d,\n  \"efConstruction\": %d,\n", d, M, efC);
    fprintf(jf, "  \"seed\": %llu,\n", (unsigned long long)seed);
    fprintf(jf, "  \"load_s\": %.3f,\n  \"build_s\": %.3f,\n  \"write_s\": %.3f,\n",
            load_s, build_s, write_s);
    fprintf(jf, "  \"build_us_per_vector\": %.2f,\n", 1e6 * build_s / (double)n);
    fprintf(jf, "  \"rss_before_build_gb\": %.2f,\n  \"rss_peak_after_build_gb\": %.2f,\n",
            rss_before, rss_after_build);
    fprintf(jf, "  \"rss_peak_total_gb\": %.2f,\n", rss_final);
    fprintf(jf, "  \"level0_slots_per_node\": %d,\n",
            ix->hnsw.cum_nneighbor_per_level.size() > 1
                ? ix->hnsw.cum_nneighbor_per_level[1] : 0);
    fprintf(jf, "  \"neighbors_array_mb\": %.1f,\n",
            (double)ix->hnsw.neighbors.size() * 4 / 1048576.0);
    fprintf(jf, "  \"stored_vectors_mb\": %.1f,\n",
            (double)ix->storage->ntotal * d * 4 / 1048576.0);
    fprintf(jf, "  \"level_hist\": {");
    bool first = true;
    for (size_t i = 0; i < lvl_hist.size(); ++i) {
        if (!lvl_hist[i]) continue;
        fprintf(jf, "%s\"%zu\": %zu", first ? "" : ", ", i, lvl_hist[i]);
        first = false;
    }
    fprintf(jf, "}\n}\n");
    fclose(jf);
    printf("build: n=%lld pilot=%d build_s=%.2f write_s=%.2f peak_rss=%.2fGB\n",
           n, (int)pilot, build_s, write_s, rss_final);
    delete ix;
    return 0;
}

// ----------------------------------------------------------------- export ---
static int mode_export(const Args& a) {
    const std::string idx_path = a.get("--index");
    const std::string prefix = a.get("--out-prefix");
    const std::string json_path = a.get("--json");

    auto t0 = Clock::now();
    faiss::Index* base = faiss::read_index(idx_path.c_str());
    const double load_s = secs_since(t0);
    faiss::IndexHNSW* h = dynamic_cast<faiss::IndexHNSW*>(base);
    if (!h) { fprintf(stderr, "not an IndexHNSW\n"); return 1; }
    faiss::HNSW& g = h->hnsw;
    const long long n = (long long)h->ntotal;

    std::vector<uint64_t> indptr((size_t)n + 1, 0);
    std::vector<int32_t> indices;
    indices.reserve((size_t)n * 8);
    std::vector<uint64_t> deg_hist(64, 0);
    double deg_sum = 0;

    auto t1 = Clock::now();
    for (long long i = 0; i < n; ++i) {
        size_t b, e;
        g.neighbor_range(i, 0, &b, &e);
        uint32_t dg = 0;
        for (size_t s = b; s < e; ++s) {
            int32_t t = g.neighbors[s];
            if (t < 0 || t == (int32_t)i) continue;
            indices.push_back(t);
            ++dg;
        }
        deg_hist[std::min<size_t>(dg, deg_hist.size() - 1)]++;
        deg_sum += dg;
        indptr[i + 1] = indices.size();
    }
    const double scan_s = secs_since(t1);

    FILE* f1 = fopen((prefix + "_l0.indptr").c_str(), "wb");
    fwrite(indptr.data(), 8, indptr.size(), f1);
    fclose(f1);
    FILE* f2 = fopen((prefix + "_l0.indices").c_str(), "wb");
    fwrite(indices.data(), 4, indices.size(), f2);
    fclose(f2);

    FILE* jf = fopen(json_path.c_str(), "w");
    fprintf(jf, "{\n  \"mode\": \"export\",\n  \"n\": %lld,\n  \"M\": %d,\n",
            n, g.nb_neighbors(0) / 2);
    fprintf(jf, "  \"directed_edges\": %zu,\n", indices.size());
    fprintf(jf, "  \"mean_out_degree\": %.4f,\n", deg_sum / (double)n);
    fprintf(jf, "  \"nodes_with_zero_out\": %llu,\n", (unsigned long long)deg_hist[0]);
    fprintf(jf, "  \"load_s\": %.2f,\n  \"scan_s\": %.2f,\n", load_s, scan_s);
    fprintf(jf, "  \"deg_hist\": {");
    bool first = true;
    for (size_t i = 0; i < deg_hist.size(); ++i) {
        if (!deg_hist[i]) continue;
        fprintf(jf, "%s\"%zu\": %llu", first ? "" : ", ", i,
                (unsigned long long)deg_hist[i]);
        first = false;
    }
    fprintf(jf, "}\n}\n");
    fclose(jf);
    printf("export: n=%lld directed=%zu mean_out=%.2f load_s=%.1f scan_s=%.1f\n",
           n, indices.size(), deg_sum / (double)n, load_s, scan_s);
    delete base;
    return 0;
}

// ----------------------------------------------------------------- analyze ---
struct Tree {
    int n = 0;
    std::vector<int> parent, depth, tin, tout, preorder, sub_size;
    std::vector<std::vector<int>> children;

    void init(std::vector<int> p) {
        parent = std::move(p);
        n = (int)parent.size();
        children.assign(n, {});
        for (int v = 0; v < n; ++v)
            if (parent[v] >= 0 && parent[v] < n) children[parent[v]].push_back(v);
        depth.assign(n, 0);
        tin.assign(n, 0);
        tout.assign(n, 0);
        sub_size.assign(n, 0);
        preorder.clear();
        preorder.reserve(n);
        std::vector<std::pair<int, int>> st;
        std::vector<int> roots;
        for (int v = 0; v < n; ++v)
            if (parent[v] < 0) roots.push_back(v);
        for (int r : roots) {
            depth[r] = 0;
            st.push_back({r, 0});
            while (!st.empty()) {
                auto& fr = st.back();
                int x = fr.first;
                if (fr.second == 0) {
                    tin[x] = (int)preorder.size();
                    preorder.push_back(x);
                }
                if (fr.second < (int)children[x].size()) {
                    int c = children[x][fr.second++];
                    depth[c] = depth[x] + 1;
                    st.push_back({c, 0});
                } else {
                    tout[x] = (int)preorder.size();
                    st.pop_back();
                }
            }
        }
        for (int i = n - 1; i >= 0; --i) {
            int x = preorder[i];
            sub_size[x] += 1;
            if (parent[x] >= 0) sub_size[parent[x]] += sub_size[x];
        }
    }
    // node v inside subtree(u)
    bool inside(int u, int v) const { return tin[u] <= tin[v] && tin[v] < tout[u]; }
    int n_ancestors(int u) const {
        int c = 0;
        for (int x = parent[u]; x >= 0; x = parent[x]) ++c;
        return c;
    }
};

struct Detail {
    int op_idx, u, bucket, depth, n_anc, sampled;
    long long sub_nodes, sub_pts, sum_deg, internal, cross_undir, cross_out;
    long long boundary_pts, isolated_pts, n_comp, largest_comp;
    double cross_frac;
    // augmentation analysis (only when --augment-index is given)
    bool has_aug = false;
    long long frag_count = 0, frag_points = 0, aug_queries = 0;
    double aug_d2_p50 = 0, aug_d2_p90 = 0, aug_d2_max = 0, aug_d2_mean = 0;
    double int_d2_p50 = 0, int_d2_p90 = 0, int_d2_p95 = 0, int_d2_mean = 0;
    double cross_d2_p50 = 0, cross_d2_mean = 0;
    double frac_aug_le_int_p95 = 0, aug_over_int_p50 = 0;
    long long n_int_dist = 0, n_cross_dist = 0;
};

static std::vector<double> parse_edges(const std::string& s) {
    std::vector<double> v;
    size_t i = 0;
    while (i < s.size()) {
        size_t j = s.find(',', i);
        if (j == std::string::npos) j = s.size();
        if (j > i) v.push_back(atof(s.substr(i, j - i).c_str()));
        i = j + 1;
    }
    std::sort(v.begin(), v.end());
    return v;
}

static std::string bucket_name(const std::vector<double>& e, int b) {
    char buf[80];
    if (b == 0) snprintf(buf, sizeof(buf), "lt_%g", e.empty() ? 0.0 : e[0]);
    else if ((size_t)b >= e.size()) snprintf(buf, sizeof(buf), "ge_%g", e.back());
    else snprintf(buf, sizeof(buf), "%g_%g", e[b - 1], e[b]);
    return buf;
}

static int mode_analyze(const Args& a) {
    const std::string csr_pref = a.get("--csr-prefix");
    const std::string tree_path = a.get("--tree");
    const std::string bind_path = a.get("--bindings");
    const std::string ops_path = a.get("--ops");
    const std::string out_csv = a.get("--out-csv");
    const std::string out_full_csv = a.get("--out-full-csv");
    const std::string out_json = a.get("--out-json");
    const std::string pilot_map = a.get("--pilot-map");
    const std::string aug_index = a.get("--augment-index");   // optional
    const int per_bucket = (int)a.geti("--per-bucket", 12);
    const std::vector<double> bedges = parse_edges(a.get("--bucket-edges", "100,1000,10000"));
    const int NB = (int)bedges.size() + 1;
    const double aug_max_pairs = a.getd("--augment-max-pairs", 5e8);
    const long long dist_sample = a.geti("--dist-sample", 20000);

    auto t0 = Clock::now();
    // ---- tree
    std::vector<int> parent;
    {
        FILE* f = fopen(tree_path.c_str(), "r");
        if (!f) { fprintf(stderr, "no tree %s\n", tree_path.c_str()); return 1; }
        int nn = 0;
        if (fscanf(f, "%d", &nn) != 1) return 1;
        parent.assign(nn, -1);
        int u, p;
        while (fscanf(f, "%d %d", &u, &p) == 2)
            if (u >= 0 && u < nn) parent[u] = p;
        fclose(f);
    }
    Tree tree;
    tree.init(std::move(parent));

    // ---- bindings (point -> node); if pilot, keep only mapped rows in order
    std::vector<int64_t> keep;
    if (!pilot_map.empty()) {
        FILE* f = fopen(pilot_map.c_str(), "r");
        if (!f) { fprintf(stderr, "no pilot map\n"); return 1; }
        long long x;
        while (fscanf(f, "%lld", &x) == 1) keep.push_back(x);
        fclose(f);
    }
    std::vector<int> pnode;
    pnode.reserve(keep.empty() ? 2000000u : keep.size());
    {
        FILE* f = fopen(bind_path.c_str(), "r");
        if (!f) { fprintf(stderr, "no bindings\n"); return 1; }
        long long i = 0, k = 0;
        int v;
        while (fscanf(f, "%d", &v) == 1) {
            if (keep.empty()) {
                pnode.push_back(v);
            } else if (k < (long long)keep.size() && i == keep[k]) {
                pnode.push_back(v);
                ++k;
            }
            ++i;
        }
        fclose(f);
    }
    const long long P = (long long)pnode.size();
    if (!keep.empty() && (long long)keep.size() != P) {
        fprintf(stderr, "pilot map size %zu != loaded %lld\n", keep.size(), P);
        return 1;
    }

    // ---- CSR adjacency
    std::vector<uint64_t> indptr;
    std::vector<int32_t> indices;
    {
        FILE* f = fopen((csr_pref + "_l0.indptr").c_str(), "rb");
        fseeko(f, 0, SEEK_END);
        long long b = (long long)ftello(f);
        indptr.resize((size_t)(b / 8));
        fseeko(f, 0, SEEK_SET);
        if (fread(indptr.data(), 8, indptr.size(), f) != indptr.size()) abort();
        fclose(f);
        FILE* g = fopen((csr_pref + "_l0.indices").c_str(), "rb");
        fseeko(g, 0, SEEK_END);
        long long b2 = (long long)ftello(g);
        indices.resize((size_t)(b2 / 4));
        fseeko(g, 0, SEEK_SET);
        if (fread(indices.data(), 4, indices.size(), g) != indices.size()) abort();
        fclose(g);
    }
    const long long Gn = (long long)indptr.size() - 1;
    if (Gn != P) {
        fprintf(stderr, "graph vertices %lld != points %lld\n", Gn, P);
        return 1;
    }
    const double load_s = secs_since(t0);
    printf("loaded: points=%lld tree_nodes=%d directed=%zu\n", P, tree.n, indices.size());

    // ---- undirected unique edge set + undirected degree
    auto t1 = Clock::now();
    std::vector<uint64_t> keys;
    keys.reserve(indices.size());
    for (long long i = 0; i < P; ++i)
        for (uint64_t s = indptr[i]; s < indptr[i + 1]; ++s) {
            int64_t j = indices[s];
            if (j < 0 || j >= P || j == i) continue;
            uint64_t x = (uint64_t)i, y = (uint64_t)j;
            keys.push_back(x < y ? ((x << 32) | y) : ((y << 32) | x));
        }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    const long long E = (long long)keys.size();
    std::vector<int64_t> deg(P, 0);
    for (long long e = 0; e < E; ++e) {
        deg[keys[e] >> 32]++;
        deg[keys[e] & 0xffffffffu]++;
    }
    const double dedup_s = secs_since(t1);
    printf("undirected edges=%lld dedup_s=%.1f\n", E, dedup_s);

    // ---- per-node / subtree aggregates
    std::vector<int64_t> pts_per_node(tree.n, 0), deg_per_node(tree.n, 0);
    for (long long i = 0; i < P; ++i) { pts_per_node[pnode[i]]++; deg_per_node[pnode[i]] += deg[i]; }
    std::vector<int64_t> sub_pts(tree.n, 0), sub_deg(tree.n, 0);
    // children appear after parents in preorder, so accumulate in REVERSE
    // preorder (children finished before parent).
    for (int i = tree.n - 1; i >= 0; --i) {
        int x = tree.preorder[i];
        sub_pts[x] += pts_per_node[x];
        sub_deg[x] += deg_per_node[x];
        if (tree.parent[x] >= 0) { sub_pts[tree.parent[x]] += sub_pts[x]; sub_deg[tree.parent[x]] += sub_deg[x]; }
    }
    long long same_node_edges = 0;
    for (long long e = 0; e < E; ++e)
        if (pnode[keys[e] >> 32] == pnode[keys[e] & 0xffffffffu]) ++same_node_edges;

    // ---- ops
    std::vector<std::pair<std::string, std::pair<int, int>>> ops;
    {
        FILE* f = fopen(ops_path.c_str(), "r");
        if (!f) { fprintf(stderr, "no ops\n"); return 1; }
        int m = 0;
        if (fscanf(f, "%d", &m) != 1) return 1;
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            auto tok = split_ws(line);
            if (tok.size() != 3) continue;
            ops.push_back({tok[0], {atoi(tok[1].c_str()), atoi(tok[2].c_str())}});
        }
        fclose(f);
    }
    std::vector<std::pair<int, int>> move_ops;
    for (int i = 0; i < (int)ops.size(); ++i)
        if (ops[i].first == "MOVE") move_ops.push_back({i, ops[i].second.first});

    // ---- internal-edge count for EVERY distinct MOVE root.
    // Edge (x,y) lies inside subtree(u) <=> u is ancestor-or-self of both
    // endpoints' binding nodes. Walk both ancestor chains (depth <= ~32).
    auto t2 = Clock::now();
    std::vector<int> move_roots;
    {
        std::vector<char> seen(tree.n, 0);
        for (auto& mo : move_ops)
            if (!seen[mo.second]) { seen[mo.second] = 1; move_roots.push_back(mo.second); }
    }
    std::vector<int> root_idx(tree.n, -1);
    for (int i = 0; i < (int)move_roots.size(); ++i) root_idx[move_roots[i]] = i;
    std::vector<long long> internal_edges(move_roots.size(), 0);
    {
        std::vector<int32_t> mark(tree.n, -1);
        long long touched = 0;
        for (long long e = 0; e < E; ++e) {
            int nx = pnode[keys[e] >> 32], ny = pnode[keys[e] & 0xffffffffu];
            for (int z = nx; z >= 0; z = tree.parent[z]) {
                int k = root_idx[z];
                if (k >= 0) { mark[z] = k; touched++; }
            }
            for (int z = ny; z >= 0; z = tree.parent[z]) {
                int k = root_idx[z];
                if (k >= 0 && mark[z] == k) internal_edges[k]++;
            }
            for (int z = nx; z >= 0; z = tree.parent[z])
                if (root_idx[z] >= 0) mark[z] = -1;
        }
        (void)touched;
    }
    const double edge_scan_s = secs_since(t2);

    // ---- full MOVE table (all ops, cheap closed-form metrics)
    struct FullRow { int op_idx, u, depth, n_anc; long long sub_nodes, sub_pts, sum_deg, internal, cross, related; };
    std::vector<FullRow> full;
    full.reserve(move_ops.size());
    for (auto& mo : move_ops) {
        int u = mo.second;
        int k = root_idx[u];
        FullRow r;
        r.op_idx = mo.first; r.u = u;
        r.depth = tree.depth[u]; r.n_anc = tree.n_ancestors(u);
        r.sub_nodes = tree.sub_size[u]; r.sub_pts = sub_pts[u]; r.sum_deg = sub_deg[u];
        r.internal = internal_edges[k];
        r.cross = r.sum_deg - 2 * r.internal;
        r.related = r.internal + r.cross;
        full.push_back(r);
    }

    // ---- bucket of a root by subtree node count
    auto bucket_of = [&](int u) {
        double sz = (double)tree.sub_size[u];
        int b = 0;
        while (b < (int)bedges.size() && sz > bedges[b]) ++b;
        return b;
    };
    // ---- stratified sample of distinct roots per size bucket (first N distinct
    //      roots in DSM op order within each bucket)
    std::vector<std::vector<int>> buckets(NB);
    std::vector<char> sampled_flag(tree.n, 0);
    {
        std::vector<char> used(tree.n, 0);
        for (auto& mo : move_ops) {
            int u = mo.second;
            if (used[u]) continue;
            used[u] = 1;
            std::vector<int>& b = buckets[bucket_of(u)];
            if ((int)b.size() < per_bucket) { b.push_back(u); sampled_flag[u] = 1; }
        }
    }

    // node -> points CSR (built once)
    std::vector<uint64_t> nptr(tree.n + 1, 0);
    for (long long i = 0; i < P; ++i) nptr[pnode[i] + 1]++;
    for (int i = 0; i < tree.n; ++i) nptr[i + 1] += nptr[i];
    std::vector<int32_t> npts((size_t)P);
    {
        std::vector<uint64_t> cur(nptr.begin(), nptr.end() - 1);
        for (long long i = 0; i < P; ++i) npts[cur[pnode[i]]++] = (int32_t)i;
    }

    auto t3 = Clock::now();
    std::vector<Detail> details;
    std::vector<int64_t> gather;
    std::vector<int32_t> comp(P, -1), stack;

    // ---- optional vector access for augmentation-edge distance analysis
    faiss::Index* aux = nullptr;
    const float* vec = nullptr;
    int dv = 0;
    if (!aug_index.empty()) {
        aux = faiss::read_index(aug_index.c_str());
        auto* hx = dynamic_cast<faiss::IndexHNSW*>(aux);
        auto* fc = hx ? dynamic_cast<faiss::IndexFlatCodes*>(hx->storage) : nullptr;
        if (!fc) { fprintf(stderr, "augment index storage is not flat codes\n"); return 1; }
        if ((long long)fc->ntotal != P) {
            fprintf(stderr, "augment index ntotal %lld != points %lld\n", (long long)fc->ntotal, P);
            return 1;
        }
        if ((int)fc->code_size != fc->d * 4) {
            fprintf(stderr, "augment index is compressed (code_size %zu)\n", fc->code_size);
            return 1;
        }
        vec = reinterpret_cast<const float*>(fc->codes.data());
        dv = (int)fc->d;
        printf("augment vectors loaded: %lld x %d (%.1f GB)\n", (long long)fc->ntotal, dv,
               (double)fc->codes.size() / 1073741824.0);
    }

    // detail pass runs over ALL distinct MOVE roots (census); the stratified
    // sample is flagged so the required per-bucket table can be filtered out.
    for (int u : move_roots) {
        {
            Detail d{};
            d.u = u;
            d.bucket = bucket_of(u);
            d.sampled = sampled_flag[u];
            d.depth = tree.depth[u];
            d.n_anc = tree.n_ancestors(u);
            d.sub_nodes = tree.sub_size[u];
            const int tin = tree.tin[u], tout = tree.tout[u];
            gather.clear();
            for (int i = tin; i < tout; ++i) {
                int node = tree.preorder[i];
                for (uint64_t s = nptr[node]; s < nptr[node + 1]; ++s) gather.push_back(npts[s]);
            }
            d.sub_pts = (long long)gather.size();
            long long cross_out = 0, sum_deg_pt = 0, boundary_pts = 0, iso = 0;
            for (int64_t p : gather) {
                sum_deg_pt += deg[p];               // undirected degree
                if (deg[p] == 0) iso++;
                long long dout = 0;
                for (uint64_t s = indptr[p]; s < indptr[p + 1]; ++s) {
                    int64_t q = indices[s];
                    if (q < 0 || q >= P || q == p) continue;
                    if (!tree.inside(u, pnode[q])) dout++;
                }
                cross_out += dout;
                if (dout > 0) boundary_pts++;
            }
            d.cross_out = cross_out;
            d.sum_deg = sum_deg_pt;
            // internal edge count comes from the exact undirected pass above,
            // so full and detail tables agree by construction
            d.internal = internal_edges[root_idx[u]];
            d.cross_undir = d.sum_deg - 2 * d.internal;
            d.boundary_pts = boundary_pts;
            d.isolated_pts = iso;
            d.cross_frac = (d.internal + d.cross_undir) > 0
                           ? (double)d.cross_undir / (double)(d.internal + d.cross_undir) : 0.0;
            // induced-subgraph components via iterative BFS
            std::vector<std::pair<int32_t, int64_t>> comps;  // (label, size)
            for (int64_t p : gather) comp[p] = -1;
            for (int64_t p : gather) {
                if (comp[p] != -1) continue;
                int32_t cid = (int32_t)p;
                comp[p] = cid;
                stack.clear();
                stack.push_back((int32_t)p);
                int64_t cnt = 0;
                while (!stack.empty()) {
                    int32_t x = stack.back();
                    stack.pop_back();
                    cnt++;
                    for (uint64_t s = indptr[x]; s < indptr[x + 1]; ++s) {
                        int32_t q = indices[s];
                        if (q < 0 || q >= P || comp[q] != -1) continue;
                        if (!tree.inside(u, pnode[q])) continue;
                        comp[q] = cid;
                        stack.push_back(q);
                    }
                }
                comps.push_back({cid, cnt});
            }
            d.n_comp = (long long)comps.size();
            int32_t giant_cid = -1;
            int64_t largest = 0;
            for (auto& cs : comps)
                if (cs.second > largest) { largest = cs.second; giant_cid = cs.first; }
            d.largest_comp = largest;

            // ---- augmentation-edge distance analysis (needs the raw vectors);
            //      run on the stratified sample only (cost ~ |frag| x |giant|)
            if (vec && giant_cid >= 0 && sampled_flag[u]) {
                d.has_aug = true;
                d.frag_count = (long long)comps.size() - 1;
                std::vector<int32_t> giant_pts, frag_pts;
                giant_pts.reserve((size_t)largest);
                for (int64_t p : gather) {
                    if (comp[p] == giant_cid) giant_pts.push_back((int32_t)p);
                    else frag_pts.push_back((int32_t)p);
                }
                d.frag_points = (long long)frag_pts.size();
                // exact nearest point of the giant component per fragment point,
                // with a work cap (queries truncated deterministically)
                long long qn = (long long)frag_pts.size();
                if (giant_pts.empty() || frag_pts.empty()) qn = 0;
                else {
                    double cap = aug_max_pairs / (double)giant_pts.size();
                    if (cap < 1.0) cap = 1.0;
                    if ((double)qn > cap) qn = (long long)cap;
                }
                d.aug_queries = qn;
                std::vector<double> augd;
                augd.reserve((size_t)qn);
                for (long long i = 0; i < qn; ++i) {
                    const float* xp = vec + (size_t)frag_pts[i] * dv;
                    float best = INFINITY;
                    for (int32_t q : giant_pts) {
                        float dd = faiss::fvec_L2sqr(xp, vec + (size_t)q * dv, dv);
                        if (dd < best) best = dd;
                    }
                    augd.push_back((double)best);
                }
                // ---- distance of existing intra-subtree (internal) edges
                std::vector<double> intd, crossd;
                if (d.internal > 0) {
                    double stride = std::max(1.0, (double)d.internal / (double)dist_sample);
                    long long k = 0, taken = 0;
                    double next = 0;
                    for (int64_t p : gather) {
                        for (uint64_t s = indptr[p]; s < indptr[p + 1]; ++s) {
                            int64_t q = indices[s];
                            if (q < 0 || q >= P || q <= p) continue;   // p<q -> each pair once
                            if (!tree.inside(u, pnode[q])) continue;
                            if ((double)k >= next && taken < dist_sample) {
                                intd.push_back((double)faiss::fvec_L2sqr(
                                        vec + (size_t)p * dv, vec + (size_t)q * dv, dv));
                                ++taken;
                                next += stride;
                            }
                            ++k;
                        }
                    }
                }
                if (d.cross_out > 0) {
                    double stride = std::max(1.0, (double)d.cross_out / (double)dist_sample);
                    long long k = 0, taken = 0;
                    double next = 0;
                    for (int64_t p : gather) {
                        for (uint64_t s = indptr[p]; s < indptr[p + 1]; ++s) {
                            int64_t q = indices[s];
                            if (q < 0 || q >= P || q == p) continue;
                            if (tree.inside(u, pnode[q])) continue;
                            if ((double)k >= next && taken < dist_sample) {
                                crossd.push_back((double)faiss::fvec_L2sqr(
                                        vec + (size_t)p * dv, vec + (size_t)q * dv, dv));
                                ++taken;
                                next += stride;
                            }
                            ++k;
                        }
                    }
                }
                d.n_int_dist = (long long)intd.size();
                d.n_cross_dist = (long long)crossd.size();
                if (!intd.empty()) {
                    d.int_d2_p50 = quantile(intd, 0.5);
                    d.int_d2_p90 = quantile(intd, 0.9);
                    d.int_d2_p95 = quantile(intd, 0.95);
                    d.int_d2_mean = mean_of(intd);
                }
                if (!crossd.empty()) {
                    d.cross_d2_p50 = quantile(crossd, 0.5);
                    d.cross_d2_mean = mean_of(crossd);
                }
                if (!augd.empty()) {
                    d.aug_d2_p50 = quantile(augd, 0.5);
                    d.aug_d2_p90 = quantile(augd, 0.9);
                    d.aug_d2_max = *std::max_element(augd.begin(), augd.end());
                    d.aug_d2_mean = mean_of(augd);
                    if (!intd.empty() && d.int_d2_p95 > 0) {
                        long long ok = 0;
                        for (double x : augd) ok += (x <= d.int_d2_p95);
                        d.frac_aug_le_int_p95 = (double)ok / (double)augd.size();
                        d.aug_over_int_p50 = d.aug_d2_p50 / std::max(1e-12, d.int_d2_p50);
                    }
                }
            }
            for (int64_t p : gather) comp[p] = -1;
            details.push_back(d);
        }
    }
    const double detail_s = secs_since(t3);

    // ---- per-op CSV (census over distinct roots, sampled flag marks the
    //      stratified subset)
    {
        FILE* f = fopen(out_csv.c_str(), "w");
        fprintf(f, "u,bucket,bucket_name,sampled,depth,n_ancestors,subtree_nodes,subtree_points,"
                   "internal_edges_undirected,cross_edges_undirected,cross_edges_out_directed,"
                   "boundary_points,isolated_points,sum_undirected_degree,n_components,"
                   "largest_component,cross_over_related,min_new_edges_for_connectivity,"
                   "fragments,fragment_points,aug_queries,"
                   "aug_d2_p50,aug_d2_p90,aug_d2_max,aug_d2_mean,"
                   "internal_d2_p50,internal_d2_p90,internal_d2_p95,internal_d2_mean,"
                   "cross_d2_p50,cross_d2_mean,"
                   "frac_aug_le_internal_p95,aug_over_internal_p50,"
                   "n_internal_dist,n_cross_dist\n");
        for (auto& d : details) {
            std::string bn = bucket_name(bedges, d.bucket);
            fprintf(f, "%d,%d,%s,%d,%d,%d,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,"
                       "%.6f,%lld,%lld,%lld,%lld,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,"
                       "%.6g,%.6g,%.6g,%lld,%lld\n",
                    d.u, d.bucket, bn.c_str(), d.sampled, d.depth, d.n_anc, d.sub_nodes,
                    d.sub_pts, d.internal, d.cross_undir, d.cross_out, d.boundary_pts,
                    d.isolated_pts, d.sum_deg, d.n_comp, d.largest_comp, d.cross_frac,
                    d.sub_pts > 0 ? (d.n_comp - 1) + 1 : 0LL,
                    d.frag_count, d.frag_points, d.aug_queries,
                    d.aug_d2_p50, d.aug_d2_p90, d.aug_d2_max, d.aug_d2_mean,
                    d.int_d2_p50, d.int_d2_p90, d.int_d2_p95, d.int_d2_mean,
                    d.cross_d2_p50, d.cross_d2_mean,
                    d.frac_aug_le_int_p95, d.aug_over_int_p50,
                    d.n_int_dist, d.n_cross_dist);
        }
        fclose(f);
    }
    // ---- full CSV (all MOVE ops, cheap metrics)
    {
        FILE* f = fopen(out_full_csv.c_str(), "w");
        fprintf(f, "op_idx,u,depth,n_ancestors,subtree_nodes,subtree_points,"
                   "internal_edges_undirected,cross_edges_undirected,sum_undirected_degree,"
                   "cross_over_related\n");
        for (auto& r : full)
            fprintf(f, "%d,%d,%d,%d,%lld,%lld,%lld,%lld,%lld,%.6f\n", r.op_idx, r.u, r.depth,
                    r.n_anc, r.sub_nodes, r.sub_pts, r.internal, r.cross, r.sum_deg,
                    r.related > 0 ? (double)r.cross / (double)r.related : 0.0);
        fclose(f);
    }

    // ---- JSON summary
    auto qstats = [&](const std::vector<double>& v) {
        return std::vector<double>{quantile(v, 0.0), quantile(v, 0.25), quantile(v, 0.5),
                                   quantile(v, 0.75), quantile(v, 0.9), quantile(v, 1.0),
                                   mean_of(v)};
    };
    auto emit = [&](FILE* f, const std::string& name, const std::vector<double>& v) {
        auto s = qstats(v);
        fprintf(f, "    \"%s\": {\"n\": %zu, \"min\": %.6g, \"p25\": %.6g, \"p50\": %.6g, "
                   "\"p75\": %.6g, \"p90\": %.6g, \"max\": %.6g, \"mean\": %.6g}",
                name.c_str(), v.size(), s[0], s[1], s[2], s[3], s[4], s[5], s[6]);
    };

    FILE* jf = fopen(out_json.c_str(), "w");
    fprintf(jf, "{\n");
    fprintf(jf, "  \"pilot\": %s,\n", keep.empty() ? "false" : "true");
    fprintf(jf, "  \"graph_vertices\": %lld,\n  \"tree_nodes\": %d,\n", P, tree.n);
    fprintf(jf, "  \"directed_edges\": %zu,\n  \"undirected_edges\": %lld,\n",
            indices.size(), E);
    fprintf(jf, "  \"same_tree_node_edges\": %lld,\n", same_node_edges);
    fprintf(jf, "  \"same_node_edge_frac\": %.8f,\n", (double)same_node_edges / (double)E);
    fprintf(jf, "  \"moves_total\": %zu,\n  \"move_roots_distinct\": %zu,\n",
            move_ops.size(), move_roots.size());
    fprintf(jf, "  \"bucket_edges\": [");
    for (size_t i = 0; i < bedges.size(); ++i)
        fprintf(jf, "%s%g", i ? ", " : "", bedges[i]);
    fprintf(jf, "],\n");
    fprintf(jf, "  \"augment_vectors\": %s,\n", vec ? "true" : "false");
    fprintf(jf, "  \"timing_s\": {\"load\": %.2f, \"dedup\": %.2f, \"edge_ancestor_scan\": %.2f, "
                "\"detail\": %.2f},\n", load_s, dedup_s, edge_scan_s, detail_s);
    // detail bucket stats: "stratified" = required >=per_bucket distinct roots
    // per bucket; "census" = every distinct MOVE root
    for (int pass = 0; pass < 2; ++pass) {
        const bool census = pass == 1;
        fprintf(jf, "  \"detail_per_bucket%s\": {\n", census ? "_census" : "_stratified");
        for (int b = 0; b < NB; ++b) {
            std::vector<double> subn, subp, inter, cross, cfrac, ncomp, bnd, iso, lanc, mintot;
            std::vector<double> degsum, crossout, lancshare;
            std::vector<double> aug50, aug90, augmax, intd50, intd95, crossd50, augfrac, augratio;
            int disc = 0, nonzero = 0;
            for (auto& d : details) {
                if (d.bucket != b) continue;
                if (!census && !d.sampled) continue;
                subn.push_back((double)d.sub_nodes);
                if (d.sub_pts <= 0) continue;
                nonzero++;
                subp.push_back((double)d.sub_pts);
                inter.push_back((double)d.internal);
                cross.push_back((double)d.cross_undir);
                crossout.push_back((double)d.cross_out);
                cfrac.push_back(d.cross_frac);
                ncomp.push_back((double)d.n_comp);
                bnd.push_back((double)d.boundary_pts);
                iso.push_back((double)d.isolated_pts);
                lanc.push_back((double)d.largest_comp);
                lancshare.push_back((double)d.largest_comp / (double)d.sub_pts);
                degsum.push_back((double)d.sum_deg);
                mintot.push_back((double)d.n_comp);  // (C-1) internal + 1 entry
                if (d.n_comp > 1) disc++;
                if (d.has_aug && d.aug_queries > 0) {
                    aug50.push_back(d.aug_d2_p50);
                    aug90.push_back(d.aug_d2_p90);
                    augmax.push_back(d.aug_d2_max);
                    augfrac.push_back(d.frac_aug_le_int_p95);
                    augratio.push_back(d.aug_over_int_p50);
                    intd50.push_back(d.int_d2_p50);
                    intd95.push_back(d.int_d2_p95);
                    crossd50.push_back(d.cross_d2_p50);
                }
            }
            fprintf(jf, "    \"%s\": {\n", bucket_name(bedges, b).c_str());
            fprintf(jf, "      \"ops\": %zu,\n      \"ops_with_points\": %d,\n",
                    subn.size(), nonzero);
            fprintf(jf, "      \"disconnected_frac\": %.6f,\n",
                    nonzero ? (double)disc / (double)nonzero : 0.0);
            emit(jf, "subtree_nodes", subn); fprintf(jf, ",\n");
            emit(jf, "subtree_points", subp); fprintf(jf, ",\n");
            emit(jf, "internal_edges", inter); fprintf(jf, ",\n");
            emit(jf, "cross_edges_undirected", cross); fprintf(jf, ",\n");
            emit(jf, "cross_edges_out_directed", crossout); fprintf(jf, ",\n");
            emit(jf, "sum_undirected_degree", degsum); fprintf(jf, ",\n");
            emit(jf, "cross_over_related", cfrac); fprintf(jf, ",\n");
            emit(jf, "boundary_points", bnd); fprintf(jf, ",\n");
            emit(jf, "isolated_points", iso); fprintf(jf, ",\n");
            emit(jf, "n_components", ncomp); fprintf(jf, ",\n");
            emit(jf, "largest_component", lanc); fprintf(jf, ",\n");
            emit(jf, "largest_component_share", lancshare); fprintf(jf, ",\n");
            emit(jf, "min_new_edges_for_connectivity", mintot);
            if (!aug50.empty()) {
                fprintf(jf, ",\n");
                emit(jf, "aug_d2_p50", aug50); fprintf(jf, ",\n");
                emit(jf, "aug_d2_p90", aug90); fprintf(jf, ",\n");
                emit(jf, "aug_d2_max", augmax); fprintf(jf, ",\n");
                emit(jf, "internal_d2_p50", intd50); fprintf(jf, ",\n");
                emit(jf, "internal_d2_p95", intd95); fprintf(jf, ",\n");
                emit(jf, "cross_d2_p50", crossd50); fprintf(jf, ",\n");
                emit(jf, "frac_aug_le_internal_p95", augfrac); fprintf(jf, ",\n");
                emit(jf, "aug_over_internal_p50", augratio);
            }
            fprintf(jf, "\n");
            fprintf(jf, "    }%s\n", b == NB - 1 ? "" : ",");
        }
        fprintf(jf, "  },\n");
    }
    // all-MOVE aggregate
    {
        std::vector<double> subn, subp, inter, cross, cfrac;
        int nonzero = 0;
        for (auto& r : full) {
            subn.push_back((double)r.sub_nodes);
            if (r.sub_pts <= 0) continue;
            nonzero++;
            subp.push_back((double)r.sub_pts);
            inter.push_back((double)r.internal);
            cross.push_back((double)r.cross);
            cfrac.push_back(r.related > 0 ? (double)r.cross / (double)r.related : 0.0);
        }
        fprintf(jf, "  \"all_moves\": {\n      \"ops\": %zu,\n      \"ops_with_points\": %d,\n"
                    "      \"distinct_roots\": %zu,\n",
                full.size(), nonzero, move_roots.size());
        emit(jf, "subtree_nodes", subn); fprintf(jf, ",\n");
        emit(jf, "subtree_points", subp); fprintf(jf, ",\n");
        emit(jf, "internal_edges", inter); fprintf(jf, ",\n");
        emit(jf, "cross_edges_undirected", cross); fprintf(jf, ",\n");
        emit(jf, "cross_over_related", cfrac); fprintf(jf, "\n  }\n");
    }
    fprintf(jf, "}\n");
    fclose(jf);

    printf("analyze done: details=%zu full=%zu detail_s=%.1f edge_scan_s=%.1f\n",
           details.size(), full.size(), detail_s, edge_scan_s);
    delete aux;
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s build|export|analyze [--k v ...]\n", argv[0]);
        return 2;
    }
    std::string mode = argv[1];
    Args a = parse_args(argc, argv, 2);
    if (mode == "build") return mode_build(a);
    if (mode == "export") return mode_export(a);
    if (mode == "analyze") return mode_analyze(a);
    fprintf(stderr, "unknown mode %s\n", mode.c_str());
    return 2;
}
