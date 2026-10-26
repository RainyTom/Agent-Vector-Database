// Reproduction of "Directory-Aware Query and Maintenance in Vector
// Databases" (arXiv:2606.16903v1) on SIFT100k with synthetic
// hierarchical nested attributes.
//
// Implements the paper's three directory designs over Roaring bitmaps:
//   * PE-Online   : only Local postings per directory; recursive DSQ
//                   expands descendant directories at query time.
//   * PE-Offline  : materialized ancestor memberships (inclusive sets);
//                   recursive DSQ is one lookup; non-recursive is
//                   Inc(v) - union Inc(children).
//   * TrieHI      : trie nodes with inclusive entry-ID sets; same query
//                   semantics as PE-Offline but subtree relinking for DSM
//                   instead of path-key rewriting.
//
// ANN executors mirror the paper's PG and IVF backends with FAISS:
//   HNSW (graph, "PG") and IndexIVFFlat ("IVF"), filtered through an
//   IDSelector that tests the resolved Roaring scope bitmap.
//
// Metrics follow §V-A: Recall@k, directory-only latency (scope
// resolution to a candidate entry-ID set), end-to-end latency, DSM
// wall-clock latency, construction time and index size.

#include <faiss/IndexHNSW.h>
#include <faiss/IndexIVFFlat.h>
#include <faiss/impl/IDSelector.h>
#include <faiss/index_io.h>

#include <roaring/roaring.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <queue>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

using Clock = std::chrono::steady_clock;
static double us_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}

// ------------------------------------------------------------------ data ----
struct Tree {
    int n = 0;
    std::vector<int> parent;  // -1 root, -2 removed (DSM MERGE)
    std::vector<std::vector<int>> children;
    std::vector<int> depth;

    explicit Tree(std::vector<int> p) : n((int)p.size()), parent(std::move(p)) {
        rebuild();
    }
    void rebuild() {
        children.assign(n, {});
        depth.assign(n, 0);
        for (int v = 1; v < n; ++v)
            if (parent[v] >= 0) {
                children[parent[v]].push_back(v);
                depth[v] = depth[parent[v]] + 1;
            }
    }
    std::vector<int> subtree_nodes(int u) const {
        std::vector<int> out, st(1, u);
        while (!st.empty()) {
            int x = st.back();
            st.pop_back();
            out.push_back(x);
            for (int c : children[x]) st.push_back(c);
        }
        return out;
    }
    bool is_under(int u, int w) const {  // is w strictly inside subtree(u)?
        for (int a = parent[w]; a >= 0; a = parent[a])
            if (a == u) return true;
        return false;
    }
    void relink(int u, int w) {  // detach u from its parent, attach under w
        int p = parent[u];
        auto& sib = children[p];
        sib.erase(std::remove(sib.begin(), sib.end(), u), sib.end());
        children[w].push_back(u);
        parent[u] = w;
        for (int x : subtree_nodes(u)) depth[x] = depth[parent[x]] + 1;
    }
};

static Tree load_tree(const std::string& path) {
    std::ifstream f(path);
    int n;
    f >> n;
    std::vector<int> p(n);
    for (int i = 0; i < n; ++i) {
        int v, pa;
        f >> v >> pa;
        p[v] = pa;
    }
    return Tree(std::move(p));
}

static std::vector<int> load_ints(const std::string& path) {
    std::ifstream f(path);
    std::vector<int> out;
    long long x;
    while (f >> x) out.push_back((int)x);
    return out;
}

struct Vectors {
    int n = 0, d = 0;
    std::vector<float> x;
    static Vectors load(const std::string& path, int d) {
        Vectors v;
        v.d = d;
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        size_t bytes = f.tellg();
        f.seekg(0);
        v.n = (int)(bytes / (sizeof(float) * d));
        v.x.resize((size_t)v.n * d);
        f.read(reinterpret_cast<char*>(v.x.data()), bytes);
        return v;
    }
};

struct Workload {
    struct Q {
        int qidx, rec, anchor;
    };
    std::vector<Q> qs;
    std::vector<int> ns_flag;
    static Workload load(const std::string& path, bool has_ns) {
        std::ifstream f(path);
        Workload w;
        int nq, _;
        f >> nq >> _;
        for (int i = 0; i < nq; ++i) {
            Q q;
            if (has_ns) {
                int s;
                f >> q.qidx >> s >> q.rec >> q.anchor;
                w.ns_flag.push_back(s);
            } else {
                f >> q.qidx >> q.rec >> q.anchor;
            }
            w.qs.push_back(q);
        }
        return w;
    }
};

// --------------------------------------------------------------- designs ----
struct RoaringSel : faiss::IDSelector {
    const roaring_bitmap_t* r;
    explicit RoaringSel(const roaring_bitmap_t* b) : r(b) {}
    bool is_member(faiss::idx_t id) const override {
        return roaring_bitmap_contains(r, (uint32_t)id);
    }
    bool is_member(faiss::idx_t id, int) const override { return is_member(id); }
};

struct Design {
    enum Kind { PE_ONLINE, PE_OFFLINE, TRIEHI };
    Kind kind;
    Tree tree;
    int n_entries;
    std::vector<roaring_bitmap_t*> local;  // direct bindings
    std::vector<roaring_bitmap_t*> inc;    // inclusive sets (mat. designs)
    std::vector<std::string> path_key;     // expansion-based designs only
    std::vector<std::string> entry_key;    // PE-Online: per-entry path keys
    double build_us = 0;
    size_t bytes = 0;

    Design(Kind k, Tree t, const std::vector<int>& entry_node)
        : kind(k), tree(std::move(t)), n_entries((int)entry_node.size()) {
        auto t0 = Clock::now();
        int n = tree.n;
        local.assign(n, nullptr);
        inc.assign(n, nullptr);
        for (int v = 0; v < n; ++v) {
            local[v] = roaring_bitmap_create();
            inc[v] = roaring_bitmap_create();
        }
        for (int i = 0; i < n_entries; ++i)
            roaring_bitmap_add(local[entry_node[i]], (uint32_t)i);
        if (kind != PE_ONLINE) {
            // generator guarantees parent index < child index: reverse
            // order processes children before parents
            for (int v = n - 1; v >= 0; --v) {
                roaring_bitmap_or_inplace(inc[v], local[v]);
                if (v > 0) roaring_bitmap_or_inplace(inc[tree.parent[v]], inc[v]);
            }
        }
        if (kind != TRIEHI) {
            path_key.assign(n, "");
            path_key[0] = "/";
            for (int v = 1; v < n; ++v)
                path_key[v] = path_key[tree.parent[v]] + std::to_string(v) + "/";
        }
        if (kind == PE_ONLINE) entry_key.assign(n_entries, "0");
        build_us = us_since(t0);
        refresh_size();
    }
    Design(const Design&) = delete;
    Design& operator=(const Design&) = delete;
    ~Design() {
        for (auto* b : local) roaring_bitmap_free(b);
        for (auto* b : inc) roaring_bitmap_free(b);
    }

    void refresh_size() {
        bytes = 0;
        for (auto* b : local) bytes += roaring_bitmap_portable_size_in_bytes(b);
        if (kind != PE_ONLINE)
            for (auto* b : inc) bytes += roaring_bitmap_portable_size_in_bytes(b);
        for (auto& p : path_key) bytes += p.size() + 1;
        for (auto& e : entry_key) bytes += e.size() + 1;
    }

    // DSQ resolution -> owned candidate entry-ID set (paper §V-A)
    roaring_bitmap_t* resolve(int anchor, bool recursive) {
        if (kind == PE_ONLINE) {
            if (!recursive) return roaring_bitmap_copy(local[anchor]);
            roaring_bitmap_t* out = roaring_bitmap_create();
            std::vector<int> st(1, anchor);
            while (!st.empty()) {
                int v = st.back();
                st.pop_back();
                roaring_bitmap_or_inplace(out, local[v]);
                for (int c : tree.children[v]) st.push_back(c);
            }
            return out;
        }
        if (recursive) return roaring_bitmap_copy(inc[anchor]);
        roaring_bitmap_t* out = roaring_bitmap_copy(inc[anchor]);
        for (int c : tree.children[anchor]) roaring_bitmap_andnot_inplace(out, inc[c]);
        return out;
    }

    // union of Local sets over the subtree of u (PE-Online scope assembly)
    roaring_bitmap_t* subtree_union(int u) {
        roaring_bitmap_t* out = roaring_bitmap_create();
        std::vector<int> st(1, u);
        while (!st.empty()) {
            int v = st.back();
            st.pop_back();
            roaring_bitmap_or_inplace(out, local[v]);
            for (int c : tree.children[v]) st.push_back(c);
        }
        return out;
    }

    // PE-Online DSM: postings are keyed by path key, so every entry in the
    // moved subtree must be re-keyed (paper §V-C: expansion-based designs
    // rewrite affected descendants)
    void rewrite_subtree_entry_keys(int u, const std::string& new_prefix) {
        auto* keys = subtree_union(u);
        roaring_uint32_iterator_t it;
        roaring_init_iterator(keys, &it);
        while (it.has_value) {
            entry_key[it.current_value] = new_prefix;
            roaring_advance_uint32_iterator(&it);
        }
        roaring_bitmap_free(keys);
    }

    void rewrite_subtree_paths(int u, const std::string& new_prefix) {
        const std::string old = path_key[u];  // copy: path_key[u] is rewritten below
        for (int d : tree.subtree_nodes(u)) {
            if (path_key[d].compare(0, old.size(), old) != 0) {
                std::cerr << "PREFIX-BREAK at d=" << d << " u=" << u
                          << " old=\"" << old << "\" key=\"" << path_key[d]
                          << "\" parent[d]=" << tree.parent[d] << "\n";
                std::abort();
            }
            path_key[d] = (d == u) ? new_prefix : new_prefix + path_key[d].substr(old.size());
        }
    }

    // DSM MOVE: relink subtree u under w (w not inside subtree(u))
    void move(int u, int w) {
        if (u == w || tree.is_under(u, w)) return;
        int p = tree.parent[u];
        if (kind == TRIEHI) {
            tree.relink(u, w);
            for (int a = p; a >= 0; a = tree.parent[a])
                roaring_bitmap_andnot_inplace(inc[a], inc[u]);
            for (int a = w; a >= 0; a = tree.parent[a])
                roaring_bitmap_or_inplace(inc[a], inc[u]);
            return;
        }
        // expansion-based designs: subtree-wide path-key rewriting
        rewrite_subtree_paths(u, path_key[w] + std::to_string(u) + "/");
        tree.relink(u, w);
        if (kind == PE_ONLINE)
            rewrite_subtree_entry_keys(u, path_key[u]);
        if (kind == PE_OFFLINE) {
            for (int a = p; a >= 0; a = tree.parent[a])
                roaring_bitmap_andnot_inplace(inc[a], inc[u]);
            for (int a = w; a >= 0; a = tree.parent[a])
                roaring_bitmap_or_inplace(inc[a], inc[u]);
        }
    }

    // DSM MERGE: fold directory u into w; entries migrate to w,
    // children of u are reparented to w, u is removed
    void merge(int u, int w) {
        if (u == w || tree.is_under(u, w)) return;
        int p = tree.parent[u];
        if (kind == PE_ONLINE) {
            roaring_bitmap_or_inplace(local[w], local[u]);
            rewrite_subtree_entry_keys(u, path_key[w]);
        } else {
            // child subtrees transfer as units (paper §V-C): only the
            // directly bound entries of u re-bind at w
            for (int a = p; a >= 0; a = tree.parent[a])
                roaring_bitmap_andnot_inplace(inc[a], inc[u]);
            roaring_bitmap_or_inplace(local[w], local[u]);
            for (int a = w; a >= 0; a = tree.parent[a])
                roaring_bitmap_or_inplace(inc[a], inc[u]);
        }
        roaring_bitmap_clear(local[u]);
        auto& sib = tree.children[p];
        sib.erase(std::remove(sib.begin(), sib.end(), u), sib.end());
        if (kind != TRIEHI) {
            // reparented children get path keys under w so later prefix
            // rewrites stay consistent
            for (int c : tree.children[u])
                rewrite_subtree_paths(c, path_key[w] + path_key[c].substr(path_key[u].size()));
        }
        for (int c : tree.children[u]) {
            tree.parent[c] = w;
            tree.children[w].push_back(c);
        }
        tree.children[u].clear();
        tree.parent[u] = -2;
    }

    // structural invariant: parent[]/children[]/path_key[] agree
    bool validate() const {
        if (path_key.empty()) return true;
        for (int d = 1; d < tree.n; ++d) {
            if (tree.parent[d] == -2) continue;
            const auto& sib = tree.children[tree.parent[d]];
            bool found = false;
            for (int c : sib)
                if (c == d) found = true;
            if (!found) {
                std::cerr << "STRUCT d=" << d << " parent=" << tree.parent[d]
                          << " missing in children\n";
                return false;
            }
            const std::string& pk = path_key[tree.parent[d]];
            if (path_key[d].compare(0, pk.size(), pk) != 0) {
                std::cerr << "STRUCT d=" << d << " pkey=\"" << path_key[d]
                          << "\" parent_key=\"" << pk << "\"\n";
                return false;
            }
        }
        return true;
    }

    // audit: recompute inclusive sets from local bindings, compare
    bool audit() {
        if (kind == PE_ONLINE) return true;
        int n = tree.n;
        std::vector<roaring_bitmap_t*> ref(n, nullptr);
        std::vector<int> order(1, 0);
        for (size_t i = 0; i < order.size(); ++i) {
            int x = order[i];
            for (int c : tree.children[x])
                if (tree.parent[c] == x) order.push_back(c);
        }
        std::reverse(order.begin(), order.end());
        for (int v : order) {
            ref[v] = roaring_bitmap_copy(local[v]);
            for (int c : tree.children[v])
                if (tree.parent[c] == v) roaring_bitmap_or_inplace(ref[v], ref[c]);
        }
        bool ok = true;
        for (int v = 0; v < n; ++v) {
            if (tree.parent[v] == -2) continue;
            if (!roaring_bitmap_equals(ref[v], inc[v])) {
                if (ok)
                    std::cerr << "mismatch v=" << v << " parent=" << tree.parent[v]
                              << " ref_card=" << roaring_bitmap_get_cardinality(ref[v])
                              << " inc_card=" << roaring_bitmap_get_cardinality(inc[v])
                              << "\n";
                ok = false;
            }
            roaring_bitmap_free(ref[v]);
        }
        return ok;
    }
};

// ------------------------------------------------------------------ GT ------
static std::vector<int> brute_force_topk(const Vectors& base, const float* q,
                                         const roaring_bitmap_t* scope, int k) {
    std::priority_queue<std::pair<float, int>> heap;
    roaring_uint32_iterator_t it;
    roaring_init_iterator(scope, &it);
    while (it.has_value) {
        uint32_t i = it.current_value;
        const float* x = &base.x[(size_t)i * base.d];
        float s = 0;
        for (int j = 0; j < base.d; ++j) {
            float t = x[j] - q[j];
            s += t * t;
        }
        if ((int)heap.size() < k) heap.push({s, (int)i});
        else if (s < heap.top().first) {
            heap.pop();
            heap.push({s, (int)i});
        }
        roaring_advance_uint32_iterator(&it);
    }
    std::vector<int> out;
    while (!heap.empty()) {
        out.push_back(heap.top().second);
        heap.pop();
    }
    std::reverse(out.begin(), out.end());
    return out;
}

// ------------------------------------------------------------- statistics ---
struct Stats {
    std::vector<double> v;
    void add(double x) { v.push_back(x); }
    double pct(double p) const {
        if (v.empty()) return 0;
        std::vector<double> s = v;
        size_t i = (size_t)((s.size() - 1) * p);
        std::nth_element(s.begin(), s.begin() + i, s.end());
        return s[i];
    }
    double mean() const {
        if (v.empty()) return 0;
        return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
    }
};

static std::string kind_name(int e) {
    return e == 0 ? "PE-Online" : e == 1 ? "PE-Offline" : "TrieHI";
}

// ------------------------------------------------------------------ main ----
int main(int argc, char** argv) {
    std::string data = "data/sift100k";
    std::string csv = "experiments/results";
    std::string mode = "all";
    int ef_search = 2048, nprobe = 256;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--data") data = argv[++i];
        else if (a == "--csv") csv = argv[++i];
        else if (a == "--mode") mode = argv[++i];
        else if (a == "--ef") ef_search = std::atoi(argv[++i]);
        else if (a == "--nprobe") nprobe = std::atoi(argv[++i]);
    }

    Vectors base = Vectors::load(data + "/base.f32", 128);
    Vectors test = Vectors::load(data + "/test.f32", 128);
    std::cerr << "base " << base.n << "x" << base.d << ", test " << test.n << "x"
              << test.d << "\n";

    struct Exec {
        std::string name;
        std::unique_ptr<faiss::Index> ix;
        double build_us;
        size_t bytes = 0;
    };
    std::vector<Exec> execs;
    {
        auto t0 = Clock::now();
        auto ix = std::make_unique<faiss::IndexHNSWFlat>(base.d, 16);
        ix->hnsw.efConstruction = 200;
        ix->add(base.n, base.x.data());
        execs.push_back({"PG-hnsw-M16", std::move(ix), us_since(t0)});
    }
    {
        auto t0 = Clock::now();
        auto ix = std::make_unique<faiss::IndexIVFFlat>(
            new faiss::IndexFlatL2(base.d), base.d, 1024, faiss::METRIC_L2);
        ix->train(base.n, base.x.data());
        ix->add(base.n, base.x.data());
        execs.push_back({"IVF-nlist1024", std::move(ix), us_since(t0)});
    }
    for (auto& ex : execs) {
        std::string tmp = csv + "_faiss_index.bin";
        faiss::write_index(ex.ix.get(), tmp.c_str());
        std::ifstream f(tmp, std::ios::binary | std::ios::ate);
        ex.bytes = f.tellg();
        std::remove(tmp.c_str());
        std::cout << "executor " << ex.name << " build_s=" << ex.build_us / 1e6
                  << " size_MB=" << ex.bytes / 1e6 << "\n";
    }

    std::ofstream csv_dsq(csv + "_dsq.csv"), csv_dsm(csv + "_dsm.csv"),
        csv_size(csv + "_size.csv");
    csv_dsq << "dataset,design,executor,query_type,scope_bucket,ndcg10,recall10,dir_mean_us,"
               "dir_p90_us,dir_p95_us,dir_p99_us,dir_p999_us,e2e_mean_ms\n";
    csv_dsm << "design,op,mean_ms,p95_ms,p99_ms\n";
    csv_size << "component,design,build_s,size_mb\n";

    // ------------------------------------------------------------ DSQ -------
    if (mode == "all" || mode == "dsq") {
        struct NSData {
            std::string name;
            Tree tree;
            std::vector<int> entry_node;
        };
        std::vector<NSData> wiki = {{"wiki", load_tree(data + "/wiki_tree.txt"),
                                     load_ints(data + "/wiki_entry_node.txt")}};
        std::vector<NSData> arxiv = {{"arxiv-subject", load_tree(data + "/subject_tree.txt"),
                                      load_ints(data + "/subject_entry_node.txt")},
                                     {"arxiv-temporal", load_tree(data + "/temporal_tree.txt"),
                                      load_ints(data + "/temporal_entry_node.txt")}};

        const int K = 10;
        auto run_group = [&](const std::string& dataset_name, std::vector<NSData>& nss,
                             const std::string& wl_path, bool has_ns) {
            Workload wl = Workload::load(wl_path, has_ns);
            // ground truth once per workload (scope semantics are design-
            // independent; verified by the DSM audit)
            std::vector<std::vector<int>> gts(wl.qs.size());
            bool dbg = getenv("RECALL_DEBUG") != nullptr;
            {
                Design scratch(Design::TRIEHI, nss[0].tree, nss[0].entry_node);
                for (size_t qi = 0; qi < wl.qs.size(); ++qi) {
                    int nsi = has_ns ? wl.ns_flag[qi] : 0;
                    Design* dp = &scratch;
                    Design d2(Design::TRIEHI, nss[nsi].tree, nss[nsi].entry_node);
                    if (nsi != 0) dp = &d2;
                    auto* sc = dp->resolve(wl.qs[qi].anchor, wl.qs[qi].rec == 1);
                    gts[qi] = brute_force_topk(
                        base, &test.x[(size_t)wl.qs[qi].qidx * test.d], sc, K);
                    if (dbg && qi < 10)
                        std::cerr << "GT qi=" << qi << " ns=" << nsi
                                  << " anchor=" << wl.qs[qi].anchor
                                  << " rec=" << wl.qs[qi].rec
                                  << " scope_card=" << roaring_bitmap_get_cardinality(sc)
                                  << " gt_size=" << gts[qi].size()
                                  << " gt0=" << (gts[qi].empty() ? -1 : gts[qi][0])
                                  << "\n";
                    roaring_bitmap_free(sc);
                }
            }
            for (int e = 0; e < 3; ++e) {
                std::vector<std::unique_ptr<Design>> designs;
                for (auto& nsd : nss)
                    designs.push_back(
                        std::make_unique<Design>((Design::Kind)e, nsd.tree, nsd.entry_node));
                double dir_build_s = 0;
                size_t dir_bytes = 0;
                for (auto& d : designs) {
                    dir_build_s += d->build_us;
                    dir_bytes += d->bytes;
                }
                csv_size << "directory_module," << kind_name(e) << ","
                         << dir_build_s / 1e6 << "," << dir_bytes / 1e6 << "\n";
                for (auto& ex : execs) {
                    Stats dir_all, e2e_all, rec_all;
                    Stats dir[2], e2e[2], rec[2], ndc[2];  // [recursive, non-recursive]
                    Stats ndc_b[2][3];                   // x [card<10, 10..1000, >=1000]
                    for (size_t qi = 0; qi < wl.qs.size(); ++qi) {
                        auto& q = wl.qs[qi];
                        int nsi = has_ns ? wl.ns_flag[qi] : 0;
                        auto t0 = Clock::now();
                        auto* scope = designs[nsi]->resolve(q.anchor, q.rec == 1);
                        double d_us = us_since(t0);
                        auto t1 = Clock::now();
                        RoaringSel sel(scope);
                        std::vector<faiss::idx_t> I(K, -1);
                        std::vector<float> D(K);
                        if (ex.name.rfind("PG", 0) == 0) {
                            faiss::SearchParametersHNSW sp;
                            sp.sel = &sel;
                            sp.efSearch = ef_search;
                            ex.ix->search(1, &test.x[(size_t)q.qidx * test.d], K, D.data(),
                                          I.data(), &sp);
                        } else {
                            faiss::SearchParametersIVF sp;
                            sp.sel = &sel;
                            sp.nprobe = nprobe;
                            ex.ix->search(1, &test.x[(size_t)q.qidx * test.d], K, D.data(),
                                          I.data(), &sp);
                        }
                        double e2e_ms = us_since(t1) / 1000.0 + d_us / 1000.0;
                        if (dbg && qi < 3 && e == 0)
                            std::cerr << "SRCH qi=" << qi << " d=" << kind_name(e)
                                      << " ex=" << ex.name << " I0=" << (long)I[0]
                                      << " I1=" << (long)I[1]
                                      << " D0=" << D[0] << "\n";
                        std::unordered_set<int> gs(gts[qi].begin(), gts[qi].end());
                        int hit = 0;
                        double dcg = 0;
                        for (int t = 0; t < K; ++t) {
                            if (I[t] >= 0 && gs.count((int)I[t])) {
                                ++hit;
                                dcg += 1.0 / std::log2(2.0 + t);
                            }
                        }
                        double idcg = 0;
                        for (size_t t = 0; t < gts[qi].size() && t < K; ++t)
                            idcg += 1.0 / std::log2(2.0 + t);
                        double ndcg = idcg > 0 ? dcg / idcg : 1.0;
                        int r = q.rec == 1 ? 0 : 1;
                        int card = (int)roaring_bitmap_get_cardinality(scope);
                        int b = card < 10 ? 0 : card < 1000 ? 1 : 2;
                        dir[r].add(d_us);
                        e2e[r].add(e2e_ms);
                        rec[r].add((double)hit / K);
                        ndc[r].add(ndcg);
                        ndc_b[r][b].add(ndcg);
                        dir_all.add(d_us);
                        e2e_all.add(e2e_ms);
                        rec_all.add((double)hit / K);
                        roaring_bitmap_free(scope);
                    }
                    const char* buckets[3] = {"card<10", "card10-1000", "card>=1000"};
                    for (int r = 0; r < 2; ++r) {
                        Stats& d = dir[r];
                        const char* qt = r == 0 ? "recursive" : "non-recursive";
                        csv_dsq << dataset_name << "," << kind_name(e) << "," << ex.name
                                << "," << qt << ",all," << ndc[r].mean() << ","
                                << rec[r].mean() << "," << d.mean() << "," << d.pct(0.90)
                                << "," << d.pct(0.95) << "," << d.pct(0.99) << ","
                                << d.pct(0.999) << "," << e2e[r].mean() << "\n";
                        for (int b = 0; b < 3; ++b)
                            csv_dsq << dataset_name << "," << kind_name(e) << "," << ex.name
                                    << "," << qt << "," << buckets[b] << ","
                                    << ndc_b[r][b].mean() << ",,,,,,,\n";
                    }
                    std::cout << dataset_name << " " << kind_name(e) << " " << ex.name
                              << " ndcg=" << (ndc[0].mean() + ndc[1].mean()) / 2
                              << " recall=" << rec_all.mean()
                              << " dir_mean_us=" << dir_all.mean()
                              << " e2e_mean_ms=" << e2e_all.mean() << "\n";
                }
            }
        };
        run_group("wiki", wiki, data + "/workload_wiki.txt", false);
        run_group("arxiv", arxiv, data + "/workload_arxiv.txt", true);
    }

    // ------------------------------------------------------------ DSM -------
    if (mode == "all" || mode == "dsm") {
        std::ifstream f(data + "/dsm_wiki.txt");
        int nop, _;
        f >> nop >> _;
        struct Op {
            std::string kind;
            int u, w;
        };
        std::vector<Op> ops;
        for (int i = 0; i < nop; ++i) {
            Op op;
            f >> op.kind >> op.u >> op.w;
            ops.push_back(op);
        }
        Tree orig = load_tree(data + "/wiki_tree.txt");
        std::vector<int> entry_node = load_ints(data + "/wiki_entry_node.txt");
        for (int e = 0; e < 3; ++e) {
            Design d((Design::Kind)e, Tree(orig.parent), entry_node);
            Stats lat[2];
            for (size_t oi = 0; oi < ops.size(); ++oi) {
                auto& op = ops[oi];
                // ops were drawn against the initial tree; MERGE removes
                // directories, so later ops may reference removed nodes.
                // Tree evolution is identical across designs, so skipping
                // keeps the compared op streams consistent.
                if (d.tree.parent[op.u] == -2 || d.tree.parent[op.w] == -2)
                    continue;
                if (op.kind == "MERGE" && (op.u == 0 || op.w == 0)) continue;
                auto t0 = Clock::now();
                if (op.kind == "MOVE") d.move(op.u, op.w);
                else d.merge(op.u, op.w);
                lat[op.kind == "MOVE" ? 0 : 1].add(us_since(t0) / 1000.0);
                if (!d.validate()) {
                    std::cerr << "at op " << oi << " " << op.kind << " " << op.u << " "
                              << op.w << "\n";
                    std::abort();
                }
                if (getenv("DSM_AUDIT_EACH") && !d.audit()) {
                    std::cerr << "AUDIT-FAIL at op " << oi << " " << op.kind << " " << op.u
                              << " " << op.w << "\n";
                    auto chain = [&](int x) {
                        std::ostringstream oss;
                        for (int a = x; a >= 0; a = d.tree.parent[a]) oss << a << " ";
                        return oss.str();
                    };
                    std::cerr << "u-chain: " << chain(op.u) << "\nw-chain: " << chain(op.w)
                              << "\n";
                    // rebuild ref for reporting
                    int n = d.tree.n;
                    std::vector<roaring_bitmap_t*> ref(n, nullptr);
                    std::vector<int> order(1, 0);
                    for (size_t i = 0; i < order.size(); ++i)
                        for (int c : d.tree.children[order[i]])
                            if (d.tree.parent[c] == order[i]) order.push_back(c);
                    std::reverse(order.begin(), order.end());
                    for (int v : order) {
                        ref[v] = roaring_bitmap_copy(d.local[v]);
                        for (int c : d.tree.children[v])
                            if (d.tree.parent[c] == v)
                                roaring_bitmap_or_inplace(ref[v], ref[c]);
                    }
                    for (int v = 0; v < n; ++v) {
                        if (d.tree.parent[v] == -2) continue;
                        if (roaring_bitmap_equals(ref[v], d.inc[v])) continue;
                        roaring_bitmap_t* diff = roaring_bitmap_andnot(ref[v], d.inc[v]);
                        roaring_uint32_iterator_t it;
                        roaring_init_iterator(diff, &it);
                        int shown = 0;
                        while (it.has_value && shown < 8) {
                            uint32_t e = it.current_value;
                            int owner = -1;
                            for (int x = 0; x < n; ++x) {
                                if (d.tree.parent[x] == -2) continue;
                                if (roaring_bitmap_contains(d.local[x], e)) owner = x;
                            }
                                std::cerr << "  v=" << v << " missing-entry=" << e
                                          << " owner=" << owner
                                          << " owner-chain: " << chain(owner) << "\n";
                            roaring_advance_uint32_iterator(&it);
                            ++shown;
                        }
                        roaring_bitmap_free(diff);
                        break;
                    }
                    std::abort();
                }
            }
            bool ok = d.audit();
            for (int i = 0; i < 2; ++i) {
                csv_dsm << kind_name(e) << "," << (i == 0 ? "MOVE" : "MERGE") << ","
                        << lat[i].mean() << "," << lat[i].pct(0.95) << "," << lat[i].pct(0.99)
                        << "\n";
            }
            std::cout << "DSM " << kind_name(e) << " move_mean_ms=" << lat[0].mean()
                      << " merge_mean_ms=" << lat[1].mean()
                      << " audit=" << (ok ? "ok" : "FAIL") << "\n";
        }
    }
    return 0;
}
