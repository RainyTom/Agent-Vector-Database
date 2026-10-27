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
#include "scope_run_index.h"

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
#include <sstream>
#include <stdexcept>
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

// multi-binding fixture: pairs of "entry node" (entry may bind several nodes)
static std::vector<std::pair<int, int>> load_pairs(const std::string& path) {
    std::ifstream f(path);
    std::vector<std::pair<int, int>> out;
    long long e, v;
    while (f >> e >> v) out.push_back({(int)e, (int)v});
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
    // Fig 12 directory-only latency decomposition (set by resolve()):
    //   t_enum_us   : sub-path / child-path enumeration (PE-Online subtree
    //                 walk, non-recursive child enumeration)
    //   t_bitmap_us : bitmap fetch / compute (posting reads + set ops)
    double t_enum_us = 0;
    double t_bitmap_us = 0;

    Design(Kind k, Tree t, const std::vector<int>& entry_node)
        : kind(k), tree(std::move(t)), n_entries((int)entry_node.size()) {
        int n = tree.n;
        local.assign(n, nullptr);
        inc.assign(n, nullptr);
        for (int v = 0; v < n; ++v) {
            local[v] = roaring_bitmap_create();
            inc[v] = roaring_bitmap_create();
        }
        for (int i = 0; i < n_entries; ++i)
            roaring_bitmap_add(local[entry_node[i]], (uint32_t)i);
        init_rest();
    }

    Design(Kind k, Tree t, std::vector<std::pair<int, int>> bindings, int n_ent)
        : kind(k), tree(std::move(t)), n_entries(n_ent) {
        auto t0 = Clock::now();
        int n = tree.n;
        local.assign(n, nullptr);
        inc.assign(n, nullptr);
        for (int v = 0; v < n; ++v) {
            local[v] = roaring_bitmap_create();
            inc[v] = roaring_bitmap_create();
        }
        for (auto& [e, v] : bindings)
            roaring_bitmap_add(local[v], (uint32_t)e);
        init_rest();
    }

    void init_rest() {
        auto t0 = Clock::now();
        int n = tree.n;
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

    // DSQ resolution -> owned candidate entry-ID set (paper §V-A).
    // Sets t_enum_us / t_bitmap_us for the Fig 12 latency decomposition.
    roaring_bitmap_t* resolve(int anchor, bool recursive) {
        t_enum_us = t_bitmap_us = 0;
        auto t0 = Clock::now();
        if (kind == PE_ONLINE) {
            if (!recursive) {
                t_bitmap_us = us_since(t0);
                return roaring_bitmap_copy(local[anchor]);
            }
            roaring_bitmap_t* out = roaring_bitmap_create();
            // stage 1: descendant path enumeration (auxiliary dir index walk)
            std::vector<int> nodes;
            {
                std::vector<int> st(1, anchor);
                while (!st.empty()) {
                    int v = st.back();
                    st.pop_back();
                    nodes.push_back(v);
                    for (int c : tree.children[v]) st.push_back(c);
                }
            }
            t_enum_us = us_since(t0);
            auto t1 = Clock::now();
            // stage 2: posting-list fetch + union
            for (int v : nodes) roaring_bitmap_or_inplace(out, local[v]);
            t_bitmap_us = us_since(t1);
            return out;
        }
        if (recursive) {
            // one aggregate-set access (materialized designs)
            t_bitmap_us = us_since(t0);
            return roaring_bitmap_copy(inc[anchor]);
        }
        // non-recursive materialized designs: Inc(anchor) minus child subtrees
        std::vector<int> kids(tree.children[anchor]);
        t_enum_us = us_since(t0);
        auto t1 = Clock::now();
        roaring_bitmap_t* out = roaring_bitmap_copy(inc[anchor]);
        for (int c : kids) roaring_bitmap_andnot_inplace(out, inc[c]);
        t_bitmap_us = us_since(t1);
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

// ------------------------------------------------------------------ DSM -----
static void run_dsm(std::ofstream& csv_dsm, std::ofstream& csv_perop,
                    const std::string& ops_path,
                    const std::string& tree_path, const std::string& bindings_path) {
    std::ifstream f(ops_path);
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
    Tree orig = load_tree(tree_path);
    std::vector<int> entry_node = load_ints(bindings_path);
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
            double op_ms = us_since(t0) / 1000.0;
            lat[op.kind == "MOVE" ? 0 : 1].add(op_ms);
            csv_perop << kind_name(e) << "," << oi << "," << op.kind << "," << op_ms
                      << "\n";
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
                  << " merge_mean_ms=" << lat[1].mean() << " audit=" << (ok ? "ok" : "FAIL")
                  << "\n";
    }
}

// ------------------------------------------------------------------ main ----
int main(int argc, char** argv) {
    std::unordered_map<std::string, std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--", 0) == 0 && i + 1 < argc) args[a.substr(2)] = argv[++i];
    }
    auto arg_or = [&](const char* k, const char* def) {
        auto it = args.find(k);
        return it == args.end() ? std::string(def) : it->second;
    };
    std::string data = arg_or("data", "data/sift100k");
    std::string csv = arg_or("csv", "experiments/results");
    std::string mode = arg_or("mode", "all");
    int ef_search = std::atoi(arg_or("ef", "2048").c_str());
    int nprobe = std::atoi(arg_or("nprobe", "256").c_str());
    int dim = std::atoi(arg_or("dim", "128").c_str());
    std::string which_exec = arg_or("executor", "both");

    Vectors base, test;
    struct Exec {
        std::string name;
        std::unique_ptr<faiss::Index> ix;
        std::vector<std::unique_ptr<faiss::Index>> node_ix;        // NODE exec
        std::vector<uint8_t> node_is_hnsw;                          // per-node type
        int node_ef = 64;                                           // HNSW per-node efSearch
        std::vector<std::vector<int>> node_ids;                     // local -> global
        std::vector<std::vector<float>> node_centroid;              // per-node mean
        std::unique_ptr<ScopeRunIndex> scope_run;
        int scope_mode = 0;
        int node_topn = 10;                                         // scan knob
        int node_limit = 0;                                         // 0 = all candidates
        double build_us;
        size_t bytes = 0;
    };
    std::vector<Exec> execs;
    if (mode == "all" || mode == "dsq" || mode == "dsq_scan") {
        base = Vectors::load(data + "/base.f32", dim);
        test = Vectors::load(data + "/test.f32", dim);
        std::cerr << "base " << base.n << "x" << base.d << ", test " << test.n << "x"
                  << test.d << "\n";
        if (which_exec == "both" || which_exec == "hnsw" || which_exec == "all3") {
            auto t0 = Clock::now();
            auto ix = std::make_unique<faiss::IndexHNSWFlat>(base.d, 16);
            ix->hnsw.efConstruction = 200;
            ix->add(base.n, base.x.data());
            Exec ex; ex.name = "PG-hnsw-M16"; ex.ix = std::move(ix);
            ex.build_us = us_since(t0); execs.push_back(std::move(ex));
        }
        if (which_exec == "both" || which_exec == "ivf" || which_exec == "all3") {
            auto t0 = Clock::now();
            auto ix = std::make_unique<faiss::IndexIVFFlat>(
                new faiss::IndexFlatL2(base.d), base.d, 1024, faiss::METRIC_L2);
            ix->train(base.n, base.x.data());
            ix->add(base.n, base.x.data());
            Exec ex; ex.name = "IVF-nlist1024"; ex.ix = std::move(ix);
            ex.build_us = us_since(t0); execs.push_back(std::move(ex));
        }
        for (auto& ex : execs) {
            if (ex.name.rfind("NODE", 0) == 0) continue;  // per-node indexes, not serializable as one
            std::string tmp = csv + "_faiss_index.bin";
            faiss::write_index(ex.ix.get(), tmp.c_str());
            std::ifstream f(tmp, std::ios::binary | std::ios::ate);
            ex.bytes = f.tellg();
            std::remove(tmp.c_str());
        }
        for (auto& ex : execs)
            std::cout << "executor " << ex.name << " build_s=" << ex.build_us / 1e6
                      << " size_MB=" << ex.bytes / 1e6 << "\n";
    }

    std::ofstream csv_dsq, csv_dsm, csv_size, csv_perop, csv_perquery, csv_scan;
    bool want_dsq = mode == "all" || mode == "dsq";
    bool want_dsm = mode == "all" || mode == "dsm" ||
                    (mode == "scope" && args.count("dsm-spec") > 0);
    bool want_scan = mode == "dsq_scan";
    bool want_size = want_dsq || mode == "scope";
    if (want_dsq) {
        csv_dsq.open(csv + "_dsq.csv");
        csv_perquery.open(csv + "_dsq_perquery.csv");
        csv_dsq << "dataset,design,executor,query_type,scope_bucket,ndcg10,recall10,dir_mean_us,"
                   "dir_p90_us,dir_p95_us,dir_p99_us,dir_p999_us,e2e_mean_ms\n";
        csv_perquery << "dataset,design,executor,query_type,anchor_depth,scope_card,dir_us,"
                        "enum_us,bitmap_us,e2e_ms,recall10,ndcg10,"
                        "gt_count,hit_count,valid_return_count,query_index,search_us,result_ids\n";
    }
    if (want_dsm) {
        csv_dsm.open(csv + "_dsm.csv");
        csv_perop.open(csv + "_dsm_perop.csv");
        csv_dsm << "design,op,mean_ms,p95_ms,p99_ms\n";
        csv_perop << "design,op_index,op_kind,latency_ms\n";
    }
    if (want_size) {
        csv_size.open(csv + "_size.csv");
        csv_size << "component,design,build_s,size_mb\n";
    }
    if (want_scan) {
        csv_scan.open(csv + "_dsq_scan.csv");
        csv_scan << "dataset,design,executor,query_type,param,value,ndcg10,recall10,"
                      "dir_mean_us,e2e_mean_ms\n";
    }
    auto parse_int_list = [](const std::string& s) {
        std::vector<int> out;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, ',')) out.push_back(std::atoi(item.c_str()));
        return out;
    };
    std::vector<int> ef_list = parse_int_list(arg_or("ef-list", "2048"));
    std::vector<int> nprobe_list = parse_int_list(arg_or("nprobe-list", "256"));
    std::vector<int> noden_list = parse_int_list(arg_or("noden-list", "10,20,40,80"));
    std::vector<int> nodelim_list = parse_int_list(arg_or("node-limit-list", "0"));

    // -------------------------------------------- scope (no vectors) --------
    if (mode == "scope") {
        std::string ds_name = arg_or("ds", "ds"), wl_path = arg_or("wl", "");
        bool has_ns = arg_or("has-ns", "0") == "1";
        struct NS {
            std::string name, tree_path, bind_path;
        };
        std::vector<NS> nss;
        std::string spec = arg_or("nsspec", "");
        std::stringstream ss(spec);
        std::string item;
        while (std::getline(ss, item, ';')) {
            if (item.empty()) continue;
            auto p1 = item.find(':'), p2 = item.find(':', p1 + 1);
            nss.push_back({item.substr(0, p1), item.substr(p1 + 1, p2 - p1 - 1),
                           item.substr(p2 + 1)});
        }
        if (nss.empty() || wl_path.empty()) {
            std::cerr << "scope mode needs --nsspec name:tree:bindings[;...] and --wl\n";
            return 1;
        }
        Workload wl = Workload::load(wl_path, has_ns);
        for (int e = 0; e < 3; ++e) {
            std::vector<std::unique_ptr<Design>> designs;
            double dir_build_s = 0;
            size_t dir_bytes = 0;
            for (auto& nsd : nss) {
                Tree t = load_tree(nsd.tree_path);
                bool pairs = nsd.bind_path.size() >= 6 &&
                             nsd.bind_path.compare(nsd.bind_path.size() - 6, 6, ".pairs") == 0;
                std::unique_ptr<Design> d;
                if (pairs) {
                    auto bp = load_pairs(nsd.bind_path);
                    int ne = 0;
                    for (auto& [en, _] : bp) ne = std::max(ne, en + 1);
                    d = std::make_unique<Design>((Design::Kind)e, std::move(t),
                                                 std::move(bp), ne);
                } else {
                    d = std::make_unique<Design>((Design::Kind)e, std::move(t),
                                                 load_ints(nsd.bind_path));
                }
                dir_build_s += d->build_us;
                dir_bytes += d->bytes;
                designs.push_back(std::move(d));
            }
            csv_size << "directory_module," << kind_name(e) << "," << dir_build_s / 1e6
                     << "," << dir_bytes / 1e6 << "\n";
            Stats dir[2];
            std::vector<int> cards[2];
            for (size_t qi = 0; qi < wl.qs.size(); ++qi) {
                auto& q = wl.qs[qi];
                int nsi = has_ns ? wl.ns_flag[qi] : 0;
                auto t0 = Clock::now();
                auto* scope = designs[nsi]->resolve(q.anchor, q.rec == 1);
                double d_us = us_since(t0);
                int r = q.rec == 1 ? 0 : 1;
                dir[r].add(d_us);
                cards[r].push_back((int)roaring_bitmap_get_cardinality(scope));
                roaring_bitmap_free(scope);
            }
            for (int r = 0; r < 2; ++r) {
                const char* qt = r == 0 ? "recursive" : "non-recursive";
                csv_dsq << ds_name << "," << kind_name(e) << ",none," << qt << ",all,,,"
                        << dir[r].mean() << "," << dir[r].pct(0.90) << "," << dir[r].pct(0.95)
                        << "," << dir[r].pct(0.99) << "," << dir[r].pct(0.999) << ",\n";
            }
            double m0 = dir[0].v.empty() ? 0 : dir[0].mean();
            double m1 = dir[1].v.empty() ? 0 : dir[1].mean();
            std::cout << ds_name << " " << kind_name(e)
                      << " rec_n=" << dir[0].v.size() << " rec_mean_us=" << m0
                      << " nonrec_n=" << dir[1].v.size() << " nonrec_mean_us=" << m1
                      << "\n";
        }
        // optional DSM on real trees: --dsm-spec name:ops:tree:bindings[;...]
        std::string dsm_spec = arg_or("dsm-spec", "");
        std::stringstream ds2(dsm_spec);
        while (std::getline(ds2, item, ';')) {
            if (item.empty()) continue;
            auto p1 = item.find(':'), p2 = item.find(':', p1 + 1), p3 = item.find(':', p2 + 1);
            run_dsm(csv_dsm, csv_perop, item.substr(p1 + 1, p2 - p1 - 1),
                    item.substr(p2 + 1, p3 - p2 - 1), item.substr(p3 + 1));
        }
        return 0;
    }

    // ------------------------------------------------------------ DSQ -------
    if (mode == "all" || mode == "dsq" || mode == "dsq_scan") {
        // auto-detect binding format: "<name>_bindings.txt" holds one node
        // per entry; "<name>_bindings.pairs" holds "entry node" pairs
        struct NSDataAny {
            std::string name;
            Tree tree;
            std::vector<std::pair<int, int>> bindings;
            int n_entries;
        };
        auto load_ns = [&](const char* name, const char* tree_f) {
            std::string pairs_p = std::string(data) + "/" + name + "_bindings.pairs";
            std::string ints_p = std::string(data) + "/" + name + "_bindings.txt";
            std::string legacy_p = std::string(data) + "/" + name + "_entry_node.txt";
            std::ifstream probe(pairs_p);
            std::ifstream probe2(ints_p);
            NSDataAny nsd{name, load_tree(std::string(data) + "/" + tree_f), {}, 0};
            if (probe.good()) {
                nsd.bindings = load_pairs(pairs_p);
                for (auto& [e, _] : nsd.bindings) nsd.n_entries = std::max(nsd.n_entries, e + 1);
            } else {
                auto en = load_ints(probe2.good() ? ints_p : legacy_p);
                nsd.n_entries = (int)en.size();
                nsd.bindings.reserve(en.size());
                for (int i = 0; i < (int)en.size(); ++i)
                    nsd.bindings.push_back({i, en[i]});
            }
            return nsd;
        };
        auto file_ok0 = [&](const char* p) {
            std::ifstream f(std::string(data) + "/" + p);
            return f.good();
        };
        std::vector<NSDataAny> wiki, arxiv;
        if (file_ok0("wiki_tree.txt"))
            wiki = {load_ns("wiki", "wiki_tree.txt")};
        if (file_ok0("subject_tree.txt"))
            arxiv = {load_ns("subject", "subject_tree.txt"),
                     load_ns("temporal", "temporal_tree.txt")};

        // baseline D: per-node ANN indexes (only for single-namespace datasets)
        bool want_node = which_exec == "node" || which_exec == "all3";
        if (want_node && !wiki.empty() && execs.end() ==
                std::find_if(execs.begin(), execs.end(), [](const Exec& e) {
                    return e.name.rfind("NODE", 0) == 0;
                })) {
            Exec ex;
            int thr = std::atoi(arg_or("node-hnsw-threshold", "0").c_str());
            auto& nsd = wiki[0];
            const std::string node_algorithm = arg_or(
                "node-algorithm", mode == "dsq_scan" || which_exec == "all3" || thr > 0
                                      ? "hnsw" : "scope-run");
            if (node_algorithm == "scope-run") {
                if (mode == "dsq_scan")
                    throw std::invalid_argument("scope-run uses exact scope search, not nodeTopN/limit scans");
                auto t0 = Clock::now();
                ex.scope_mode = std::atoi(arg_or("scope-mode", "4").c_str());
                ex.scope_run = std::make_unique<ScopeRunIndex>(
                    base.x, base.d, nsd.tree.children, nsd.bindings, ex.scope_mode >= 3,
                    arg_or("scope-local-order", "geometry") == "geometry");
                ex.name = "NODE-scope-run";
                ex.build_us = us_since(t0);
                ex.bytes = ex.scope_run->size_bytes();
                std::cout << "executor " << ex.name << " build_s=" << ex.build_us / 1e6
                          << " size_MB=" << ex.bytes / 1e6 << "\n";
                execs.push_back(std::move(ex));
            } else {
            if (node_algorithm != "hnsw")
                throw std::invalid_argument("unknown node-algorithm: " + node_algorithm);
            ex.name = thr > 0 ? "NODE-hnsw-thr" : "NODE-hnsw";
            ex.node_ef = std::atoi(arg_or("node-ef", "64").c_str());
            int nodes = nsd.tree.n;
            ex.node_ix.resize(nodes);
            ex.node_ids.resize(nodes);
            ex.node_is_hnsw.assign(nodes, 0);
            ex.node_centroid.assign(nodes, {});
            std::vector<std::vector<int>> ids(nodes);
            for (auto& [e, v] : nsd.bindings) ids[v].push_back(e);
            auto t0 = Clock::now();
            size_t n_hnsw = 0, n_flat = 0;
            for (int v = 0; v < nodes; ++v) {
                if (ids[v].empty()) continue;
                bool use_hnsw = (int)ids[v].size() > thr;
                std::unique_ptr<faiss::Index> ix;
                if (use_hnsw) {
                    auto h = std::make_unique<faiss::IndexHNSWFlat>(base.d, 16);
                    h->hnsw.efConstruction = 200;
                    h->hnsw.efSearch = ex.node_ef;
                    ix = std::move(h);
                    ex.node_is_hnsw[v] = 1;
                    ++n_hnsw;
                } else {
                    ix = std::make_unique<faiss::IndexFlatL2>(base.d);
                    ++n_flat;
                }
                std::vector<float> buf((size_t)ids[v].size() * base.d);
                std::vector<float> cen(base.d, 0.0f);
                for (size_t j = 0; j < ids[v].size(); ++j) {
                    const float* x = &base.x[(size_t)ids[v][j] * base.d];
                    std::copy_n(x, base.d, &buf[j * base.d]);
                    for (int t = 0; t < base.d; ++t) cen[t] += x[t];
                }
                for (int t = 0; t < base.d; ++t) cen[t] /= (float)ids[v].size();
                ex.node_centroid[v] = std::move(cen);
                ix->add((faiss::idx_t)ids[v].size(), buf.data());
                ex.node_ix[v] = std::move(ix);
                ex.node_ids[v] = std::move(ids[v]);
            }
            ex.build_us = us_since(t0);
            ex.bytes = (size_t)base.n * base.d * 4;  // one vector copy total
            std::cout << "executor " << ex.name << " nodes_with_items="
                      << std::count_if(ex.node_ix.begin(), ex.node_ix.end(),
                                       [](const auto& p) { return (bool)p; })
                      << " hnsw=" << n_hnsw << " flat=" << n_flat
                      << " thr=" << thr << " ef=" << ex.node_ef
                      << " build_s=" << ex.build_us / 1e6
                      << " size_MB=" << ex.bytes / 1e6 << "\n";
            execs.push_back(std::move(ex));
            }
        }

        const int K = 10;
        auto make_design = [](Design::Kind k, NSDataAny& nsd) {
            return std::make_unique<Design>(k, Tree(nsd.tree.parent), nsd.bindings,
                                            nsd.n_entries);
        };
        auto validate_queries = [&](const Workload& wl, const std::string& path) {
            for (size_t qi = 0; qi < wl.qs.size(); ++qi)
                if (wl.qs[qi].qidx < 0 || wl.qs[qi].qidx >= test.n)
                    throw std::runtime_error(
                        path + ": query_index=" + std::to_string(qi) +
                        " has out-of-range qidx=" + std::to_string(wl.qs[qi].qidx) +
                        " (test.n=" + std::to_string(test.n) + ")");
        };
        // NODE resolves candidate nodes, never an entry-ID bitmap. Both DSQ
        // paths use the same enumeration and test nonemptiness on the index.
        auto candidate_nodes = [](const Exec& ex, const Tree& tree,
                                  const Workload::Q& q) {
            std::vector<int> cand;
            if (q.rec == 1) {
                std::vector<int> st(1, q.anchor);
                while (!st.empty()) {
                    int v = st.back();
                    st.pop_back();
                    if (ex.node_ix[v]) cand.push_back(v);
                    for (int c : tree.children[v]) st.push_back(c);
                }
            } else if (ex.node_ix[q.anchor]) {
                cand.push_back(q.anchor);
            }
            return cand;
        };
        struct GroundTruth {
            std::vector<std::unique_ptr<Design>> namespaces;
            std::vector<std::vector<int>> ids;
            std::vector<int> cards, depths;
        };
        auto prepare_gt = [&](std::vector<NSDataAny>& nss, const Workload& wl,
                              bool has_ns) {
            GroundTruth gt;
            // One reference design per namespace, not one per query. Retain
            // it for untimed result-membership checks without building scopes.
            for (auto& nsd : nss)
                gt.namespaces.push_back(make_design(Design::TRIEHI, nsd));
            gt.ids.resize(wl.qs.size());
            gt.cards.resize(wl.qs.size());
            gt.depths.resize(wl.qs.size());
            for (size_t qi = 0; qi < wl.qs.size(); ++qi) {
                const auto& q = wl.qs[qi];
                int nsi = has_ns ? wl.ns_flag[qi] : 0;
                auto& d = *gt.namespaces[nsi];
                auto* sc = d.resolve(q.anchor, q.rec == 1);
                gt.cards[qi] = (int)roaring_bitmap_get_cardinality(sc);
                gt.depths[qi] = d.tree.depth[q.anchor];
                gt.ids[qi] = brute_force_topk(
                    base, &test.x[(size_t)q.qidx * test.d], sc, K);
                roaring_bitmap_free(sc);
            }
            return gt;
        };
        struct QueryQuality {
            int hit = 0, valid = 0;
            double dcg = 0;
        };
        auto query_quality = [&](const std::vector<faiss::idx_t>& ids,
                                 const GroundTruth& gt, size_t qi, int nsi,
                                 const Workload::Q& q) {
            QueryQuality quality;
            const auto& d = *gt.namespaces[nsi];
            int duplicate = 0, outside = 0;
            for (int t = 0; t < K; ++t) {
                faiss::idx_t id = ids[t];
                if (id < 0) continue;  // FAISS missing-result sentinel
                if (std::find(ids.begin(), ids.begin() + t, id) != ids.begin() + t) {
                    ++duplicate;
                    continue;
                }
                bool in_scope = id < base.n &&
                    roaring_bitmap_contains(d.inc[q.anchor], (uint32_t)id);
                if (in_scope && q.rec != 1)
                    for (int c : d.tree.children[q.anchor])
                        if (roaring_bitmap_contains(d.inc[c], (uint32_t)id)) {
                            in_scope = false;
                            break;
                        }
                if (!in_scope) {
                    ++outside;
                    continue;
                }
                ++quality.valid;
                if (std::find(gt.ids[qi].begin(), gt.ids[qi].end(), (int)id) !=
                    gt.ids[qi].end()) {
                    ++quality.hit;
                    quality.dcg += 1.0 / std::log2(2.0 + t);
                }
            }
            // Do not silently filter or change ANN output. Multi-bindings can
            // expose duplicate NODE IDs or Local-vs-materialized scope gaps.
            if (duplicate || outside)
                std::cerr << "RESULT_SCOPE_WARNING qi=" << qi << " ns=" << nsi
                          << " duplicates=" << duplicate << " out_of_scope=" << outside
                          << "\n";
            return quality;
        };
        auto run_group = [&](const std::string& dataset_name, std::vector<NSDataAny>& nss,
                             const std::string& wl_path, bool has_ns) {
            Workload wl = Workload::load(wl_path, has_ns);
            validate_queries(wl, wl_path);
            // Fixed reference scopes and evaluation metadata are prepared
            // before any timed query, independently of the executor.
            auto gt = prepare_gt(nss, wl, has_ns);
            const auto& gts = gt.ids;
            bool dbg = getenv("RECALL_DEBUG") != nullptr;
            if (dbg)
                for (size_t qi = 0; qi < wl.qs.size() && qi < 10; ++qi)
                    std::cerr << "GT qi=" << qi
                              << " ns=" << (has_ns ? wl.ns_flag[qi] : 0)
                              << " anchor=" << wl.qs[qi].anchor
                              << " rec=" << wl.qs[qi].rec
                              << " scope_card=" << gt.cards[qi]
                              << " gt_size=" << gts[qi].size()
                              << " gt0=" << (gts[qi].empty() ? -1 : gts[qi][0])
                              << "\n";
            for (int e = 0; e < 3; ++e) {
                std::vector<std::unique_ptr<Design>> designs;
                for (auto& nsd : nss) designs.push_back(make_design((Design::Kind)e, nsd));
                double dir_build_s = 0;
                size_t dir_bytes = 0;
                for (auto& d : designs) {
                    dir_build_s += d->build_us;
                    dir_bytes += d->bytes;
                }
                csv_size << "directory_module," << kind_name(e) << ","
                         << dir_build_s / 1e6 << "," << dir_bytes / 1e6 << "\n";
                for (auto& ex : execs) {
                    bool is_node = ex.name.rfind("NODE", 0) == 0;
                    if (is_node && e != 0) continue;  // design-independent, run once
                    if (is_node && has_ns) continue;  // v1: single-namespace only
                    std::string dlabel = is_node ? "NODE" : kind_name(e);
                    Stats dir_all, e2e_all, rec_all;
                    Stats dir[2], e2e[2], rec[2], ndc[2];  // [recursive, non-recursive]
                    Stats ndc_b[2][3];                   // x [card<10, 10..1000, >=1000]
                    for (size_t qi = 0; qi < wl.qs.size(); ++qi) {
                        auto& q = wl.qs[qi];
                        int nsi = has_ns ? wl.ns_flag[qi] : 0;
                        roaring_bitmap_t* scope = nullptr;
                        double d_us = 0, search_us = 0;
                        std::vector<faiss::idx_t> I(K, -1);
                        std::vector<float> D(K);
                        if (is_node && ex.scope_run) {
                            auto t0 = Clock::now();
                            auto range = ex.scope_run->range(q.anchor, q.rec == 1);
                            d_us = us_since(t0);
                            auto t1 = Clock::now();
                            ex.scope_run->search(&test.x[(size_t)q.qidx * test.d],
                                                 range, K, D.data(), I.data(), ex.scope_mode);
                            search_us = us_since(t1);
                        } else if (is_node) {
                            auto t0 = Clock::now();
                            auto cand = candidate_nodes(ex, nss[nsi].tree, q);
                            d_us = us_since(t0);
                            auto t1 = Clock::now();
                            const float* qv = &test.x[(size_t)q.qidx * test.d];
                            if (ex.node_limit > 0 && (int)cand.size() > ex.node_limit) {
                                // route to the closest centroids (P4-lite)
                                std::vector<std::pair<float, int>> cd;
                                cd.reserve(cand.size());
                                for (int v : cand) {
                                    const auto& cen = ex.node_centroid[v];
                                    float s2 = 0;
                                    for (int t = 0; t < base.d; ++t) {
                                        float df = qv[t] - cen[t];
                                        s2 += df * df;
                                    }
                                    cd.push_back({s2, v});
                                }
                                std::nth_element(cd.begin(), cd.begin() + ex.node_limit,
                                                 cd.end());
                                cd.resize(ex.node_limit);
                                cand.clear();
                                for (auto& [d2, v] : cd) cand.push_back(v);
                            }
                            std::vector<float> Dn(ex.node_topn);
                            std::vector<faiss::idx_t> In(ex.node_topn);
                            std::vector<std::pair<float, int>> heap_storage;
                            heap_storage.reserve(K);
                            std::priority_queue<std::pair<float, int>> heap(
                                std::less<std::pair<float, int>>(), std::move(heap_storage));
                            for (int v : cand) {
                                if (!ex.node_ix[v]) continue;
                                int nn = std::min(ex.node_topn, (int)ex.node_ids[v].size());
                                if (ex.node_is_hnsw[v]) {
                                    faiss::SearchParametersHNSW sp;
                                    sp.efSearch = std::max(ex.node_ef, nn);
                                    ex.node_ix[v]->search(1, qv, nn, Dn.data(),
                                                         In.data(), &sp);
                                } else {
                                    ex.node_ix[v]->search(1, qv, nn, Dn.data(), In.data());
                                }
                                for (int j = 0; j < nn; ++j) {
                                    if (In[j] < 0) continue;
                                    float dist = Dn[j];
                                    int gid = ex.node_ids[v][(size_t)In[j]];
                                    if ((int)heap.size() < K) heap.push({dist, gid});
                                    else if (dist < heap.top().first) {
                                        heap.pop();
                                        heap.push({dist, gid});
                                    }
                                }
                            }
                            for (int t = (int)heap.size() - 1; t >= 0; --t) {
                                I[t] = heap.top().second;
                                D[t] = heap.top().first;
                                heap.pop();
                            }
                            search_us = us_since(t1);
                        } else {
                            auto t0 = Clock::now();
                            scope = designs[nsi]->resolve(q.anchor, q.rec == 1);
                            d_us = us_since(t0);
                            auto t1 = Clock::now();
                            RoaringSel sel(scope);
                            if (ex.name.rfind("PG", 0) == 0) {
                                faiss::SearchParametersHNSW sp;
                                sp.sel = &sel;
                                sp.efSearch = ef_search;
                                ex.ix->search(1, &test.x[(size_t)q.qidx * test.d], K,
                                              D.data(), I.data(), &sp);
                            } else {
                                faiss::SearchParametersIVF sp;
                                sp.sel = &sel;
                                sp.nprobe = nprobe;
                                ex.ix->search(1, &test.x[(size_t)q.qidx * test.d], K,
                                              D.data(), I.data(), &sp);
                            }
                            search_us = us_since(t1);
                        }
                        double e2e_ms = (search_us + d_us) / 1000.0;
                        if (dbg && qi < 3 && e == 0)
                            std::cerr << "SRCH qi=" << qi << " d=" << kind_name(e)
                                      << " ex=" << ex.name << " I0=" << (long)I[0]
                                      << " I1=" << (long)I[1]
                                      << " D0=" << D[0] << "\n";
                        auto quality = query_quality(I, gt, qi, nsi, q);
                        int hit = quality.hit;
                        double dcg = quality.dcg;
                        double idcg = 0;
                        for (size_t t = 0; t < gts[qi].size() && t < K; ++t)
                            idcg += 1.0 / std::log2(2.0 + t);
                        double ndcg = idcg > 0 ? dcg / idcg : 1.0;
                        int r = q.rec == 1 ? 0 : 1;
                        int card = gt.cards[qi];
                        int b = card < 10 ? 0 : card < 1000 ? 1 : 2;
                        csv_perquery << dataset_name << "," << dlabel << "," << ex.name
                                     << "," << (r == 0 ? "recursive" : "non-recursive")
                                     << "," << gt.depths[qi] << ","
                                     << card << "," << d_us << ","
                                     << (is_node ? d_us : designs[nsi]->t_enum_us) << ","
                                     << (is_node ? 0.0 : designs[nsi]->t_bitmap_us)
                                     << "," << e2e_ms << "," << (double)hit / K << ","
                                     << ndcg << "," << gts[qi].size() << "," << hit
                                     << "," << quality.valid << "," << qi << ","
                                     << search_us << ",";
                        for (size_t j = 0; j < I.size(); ++j) {
                            if (j) csv_perquery << " ";
                            csv_perquery << I[j];
                        }
                        csv_perquery << "\n";
                        dir[r].add(d_us);
                        e2e[r].add(e2e_ms);
                        rec[r].add((double)hit / K);
                        ndc[r].add(ndcg);
                        ndc_b[r][b].add(ndcg);
                        dir_all.add(d_us);
                        e2e_all.add(e2e_ms);
                        rec_all.add((double)hit / K);
                        if (scope) roaring_bitmap_free(scope);
                    }
                    const char* buckets[3] = {"card<10", "card10-1000", "card>=1000"};
                    for (int r = 0; r < 2; ++r) {
                        Stats& d = dir[r];
                        const char* qt = r == 0 ? "recursive" : "non-recursive";
                        csv_dsq << dataset_name << "," << dlabel << "," << ex.name
                                << "," << qt << ",all," << ndc[r].mean() << ","
                                << rec[r].mean() << "," << d.mean() << "," << d.pct(0.90)
                                << "," << d.pct(0.95) << "," << d.pct(0.99) << ","
                                << d.pct(0.999) << "," << e2e[r].mean() << "\n";
                        for (int b = 0; b < 3; ++b)
                            csv_dsq << dataset_name << "," << dlabel << "," << ex.name
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
        auto file_ok = [](const std::string& p) {
            std::ifstream f(p);
            return f.good();
        };
        if (mode != "dsq_scan") {
            if (file_ok(data + "/workload_wiki.txt") && file_ok(data + "/wiki_tree.txt"))
                run_group("wiki", wiki, data + "/workload_wiki.txt", false);
            if (file_ok(data + "/workload_arxiv.txt") && file_ok(data + "/subject_tree.txt"))
                run_group("arxiv", arxiv, data + "/workload_arxiv.txt", true);
        }

        // ---- Fig 7: quality-latency trade-off sweep over ANN parameters ----
        if (mode == "dsq_scan") {
            auto run_scan = [&](const std::string& dataset_name, std::vector<NSDataAny>& nss,
                                const std::string& wl_path, bool has_ns) {
                Workload wl = Workload::load(wl_path, has_ns);
                validate_queries(wl, wl_path);
                auto gt = prepare_gt(nss, wl, has_ns);
                const auto& gts = gt.ids;
                for (int e = 0; e < 3; ++e) {
                    std::vector<std::unique_ptr<Design>> designs;
                    for (auto& nsd : nss) designs.push_back(make_design((Design::Kind)e, nsd));
                    for (auto& ex : execs) {
                        bool is_node = ex.name.rfind("NODE", 0) == 0;
                        if (is_node && e != 0) continue;  // design-independent, run once
                        if (is_node && has_ns) continue;  // v1: single-namespace only
                        std::string dlabel = is_node ? "NODE" : kind_name(e);
                        bool is_pg = ex.name.rfind("PG", 0) == 0;
                        const char* pname = is_node ? "nodeTopN" : is_pg ? "efSearch" : "nprobe";
                        const std::vector<int>& params =
                            is_node ? noden_list : is_pg ? ef_list : nprobe_list;
                        for (int pv : params) {
                            if (is_node) ex.node_topn = pv;
                            const std::vector<int> lims =
                                is_node ? nodelim_list : std::vector<int>{0};
                            for (int lim : lims) {
                            if (is_node) ex.node_limit = lim;
                            Stats ndc[2], rec[2], dir[2], e2e[2];
                            for (size_t qi = 0; qi < wl.qs.size(); ++qi) {
                                auto& q = wl.qs[qi];
                                int nsi = has_ns ? wl.ns_flag[qi] : 0;
                                std::vector<faiss::idx_t> I(K, -1);
                                std::vector<float> D(K);
                                double d_us = 0, search_us = 0;
                                roaring_bitmap_t* scope = nullptr;
                                if (is_node) {
                                    auto t0 = Clock::now();
                                    auto cand = candidate_nodes(ex, nss[nsi].tree, q);
                                    d_us = us_since(t0);
                                    auto t1 = Clock::now();
                                    const float* qv = &test.x[(size_t)q.qidx * test.d];
                                    if (ex.node_limit > 0 && (int)cand.size() > ex.node_limit) {
                                        std::vector<std::pair<float, int>> cd;
                                        cd.reserve(cand.size());
                                        for (int v : cand) {
                                            const auto& cen = ex.node_centroid[v];
                                            float s2 = 0;
                                            for (int t = 0; t < base.d; ++t) {
                                                float df = qv[t] - cen[t];
                                                s2 += df * df;
                                            }
                                            cd.push_back({s2, v});
                                        }
                                        std::nth_element(cd.begin(),
                                                         cd.begin() + ex.node_limit, cd.end());
                                        cd.resize(ex.node_limit);
                                        cand.clear();
                                        for (auto& [d2, v] : cd) cand.push_back(v);
                                    }
                                    std::priority_queue<std::pair<float, int>> heap;
                                    for (int v : cand) {
                                        if (!ex.node_ix[v]) continue;
                                        int nn = std::min(pv, (int)ex.node_ids[v].size());
                                        std::vector<float> Dn(nn);
                                        std::vector<faiss::idx_t> In(nn);
                                        if (ex.node_is_hnsw[v]) {
                                    faiss::SearchParametersHNSW sp;
                                    sp.efSearch = std::max(ex.node_ef, nn);
                                    ex.node_ix[v]->search(1, qv, nn, Dn.data(),
                                                         In.data(), &sp);
                                } else {
                                    ex.node_ix[v]->search(1, qv, nn, Dn.data(), In.data());
                                }
                                        for (int j = 0; j < nn; ++j) {
                                            if (In[j] < 0) continue;
                                            float dist = Dn[j];
                                            int gid = ex.node_ids[v][(size_t)In[j]];
                                            if ((int)heap.size() < K) heap.push({dist, gid});
                                            else if (dist < heap.top().first) {
                                                heap.pop();
                                                heap.push({dist, gid});
                                            }
                                        }
                                    }
                                    for (int t = (int)heap.size() - 1; t >= 0; --t) {
                                        I[t] = heap.top().second;
                                        heap.pop();
                                    }
                                    search_us = us_since(t1);
                                } else {
                                    auto t0 = Clock::now();
                                    scope = designs[nsi]->resolve(q.anchor, q.rec == 1);
                                    d_us = us_since(t0);
                                    auto t1 = Clock::now();
                                    RoaringSel sel(scope);
                                    if (is_pg) {
                                        faiss::SearchParametersHNSW sp;
                                        sp.sel = &sel;
                                        sp.efSearch = pv;
                                        ex.ix->search(1, &test.x[(size_t)q.qidx * test.d], K,
                                                      D.data(), I.data(), &sp);
                                    } else {
                                        faiss::SearchParametersIVF sp;
                                        sp.sel = &sel;
                                        sp.nprobe = pv;
                                        ex.ix->search(1, &test.x[(size_t)q.qidx * test.d], K,
                                                      D.data(), I.data(), &sp);
                                    }
                                    search_us = us_since(t1);
                                }
                                double e2e_ms = (search_us + d_us) / 1000.0;

                                auto quality = query_quality(I, gt, qi, nsi, q);
                                int hit = quality.hit;
                                double dcg = quality.dcg;
                                double idcg = 0;
                                for (size_t t = 0; t < gts[qi].size() && t < K; ++t)
                                    idcg += 1.0 / std::log2(2.0 + t);
                                double ndcg = idcg > 0 ? dcg / idcg : 1.0;
                                int r = q.rec == 1 ? 0 : 1;
                                dir[r].add(d_us);
                                e2e[r].add(e2e_ms);
                                rec[r].add((double)hit / K);
                                ndc[r].add(ndcg);
                                if (scope) roaring_bitmap_free(scope);
                            }
                            for (int r = 0; r < 2; ++r) {
                                csv_scan << dataset_name << "," << dlabel << ","
                                         << ex.name << ","
                                         << (r == 0 ? "recursive" : "non-recursive") << ","
                                         << pname << "," << pv << "," << ndc[r].mean() << ","
                                         << rec[r].mean() << "," << dir[r].mean() << ","
                                         << e2e[r].mean();
                                if (is_node) csv_scan << ",nodeLimit," << lim;
                                csv_scan << "\n";
                            }
                            }
                        }
                    }
                }
            };
            if (file_ok(data + "/workload_wiki.txt") && file_ok(data + "/wiki_tree.txt"))
                run_scan("wiki", wiki, data + "/workload_wiki.txt", false);
            if (file_ok(data + "/workload_arxiv.txt") && file_ok(data + "/subject_tree.txt"))
                run_scan("arxiv", arxiv, data + "/workload_arxiv.txt", true);
        }
    }

    // ------------------------------------------------------------ DSM -------
    if (mode == "all" || mode == "dsm")
        run_dsm(csv_dsm, csv_perop, data + "/dsm_wiki.txt", data + "/wiki_tree.txt",
                data + "/wiki_entry_node.txt");
    return 0;
}
