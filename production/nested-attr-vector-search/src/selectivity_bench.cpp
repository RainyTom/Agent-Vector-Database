// Selectivity sweep for attribute-filtered vector search on SIFT100k.
//
// Predicate modes:
//   random : per-query uniform feasible set of exactly round(s*n) ids. This
//            isolates filter selectivity s=|feasible|/n from attribute semantics.
//   sub    : recursive directory scope (Inc(v)) from the CCS-shaped tree.
//   exact  : non-recursive directory scope (Local(v)) from the same tree.
//
// Methods:
//   exact  : enumerate feasible bits, compute squared L2, keep top-k. This is
//            both the ground-truth routine and the low-selectivity baseline.
//   hnsw   : FAISS IndexHNSWFlat + IDSelector bitmap. In this fork the selector
//            filters which nodes enter the result heap; traversal is unchanged,
//            so recall at fixed ef degrades as selectivity drops.
//   post   : unfiltered HNSW top-ef, then keep feasible ids; ef expands x4
//            until k feasible ids are found (or max_ef).
//
// Directory variants for hierarchy modes:
//   precomputed : bitmap already materialized (no resolution cost)
//   triehi      : copy the node's pre-materialized inclusive/local bitmap
//   peonline    : recursively OR Local(child) bitmaps at query time
//   local       : copy the node's pre-materialized Local bitmap
//
// Outputs JSONL/CSV rows and a summary JSON. The primary number is the
// geometric mean over random-selectivity buckets of the best QPS that reaches
// Recall@10 >= 0.95 (exact scan included, so the frontier is always defined).

#include <faiss/IndexFlat.h>
#include <faiss/IndexHNSW.h>
#include <faiss/impl/HNSW.h>
#include <faiss/impl/IDSelector.h>
#include <faiss/index_io.h>

#include <immintrin.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

constexpr int K = 10;
constexpr double TARGET_RECALL = 0.95;
constexpr const char* BUCKET_NAMES[7] = {
    "0.001", "0.003", "0.01", "0.03", "0.1", "0.3", "1.0"};
constexpr double BUCKET_LO[7] = {0.0, 0.0015, 0.005, 0.015, 0.05, 0.15, 0.5};
constexpr double BUCKET_HI[7] = {0.0015, 0.005, 0.015, 0.05, 0.15, 0.5, 1.01};

struct Args {
    std::string fixtures;
    std::string out_dir;
    int nq_per_bucket = 200;
    uint64_t seed = 20261026;
    int M = 32;
    int efc = 200;
    int max_ef = 16384;
    bool rebuild_index = false;
};

Args parse(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string f = argv[i];
        auto next = [&]() {
            if (++i >= argc) throw std::runtime_error("missing value for " + f);
            return std::string(argv[i]);
        };
        if (f == "--fixtures") a.fixtures = next();
        else if (f == "--out-dir") a.out_dir = next();
        else if (f == "--nq-per-bucket") a.nq_per_bucket = std::stoi(next());
        else if (f == "--seed") a.seed = std::stoull(next());
        else if (f == "--M") a.M = std::stoi(next());
        else if (f == "--efc") a.efc = std::stoi(next());
        else if (f == "--max-ef") a.max_ef = std::stoi(next());
        else if (f == "--rebuild-index") a.rebuild_index = true;
        else throw std::runtime_error("unknown arg " + f);
    }
    if (a.fixtures.empty() || a.out_dir.empty()) {
        throw std::runtime_error("--fixtures and --out-dir are required");
    }
    if (a.nq_per_bucket < 2 || a.nq_per_bucket % 2) {
        throw std::runtime_error("--nq-per-bucket must be even and >= 2");
    }
    if (a.max_ef < 16) throw std::runtime_error("--max-ef must be >= 16");
    return a;
}

double now_us() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::micro>(
               clock::now().time_since_epoch())
        .count();
}

double now_s() {
    return now_us() / 1e6;
}

std::vector<float> read_f32(const std::string& path, size_t expect = 0) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("cannot open " + path);
    size_t bytes = size_t(in.tellg());
    if (expect && bytes != expect) {
        throw std::runtime_error("size mismatch for " + path);
    }
    in.seekg(0);
    std::vector<float> v(bytes / sizeof(float));
    in.read(reinterpret_cast<char*>(v.data()), bytes);
    if (!in) throw std::runtime_error("short read " + path);
    return v;
}

std::vector<int32_t> read_i32(const std::string& path, size_t expect = 0) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("cannot open " + path);
    size_t bytes = size_t(in.tellg());
    if (expect && bytes != expect) {
        throw std::runtime_error("size mismatch for " + path);
    }
    in.seekg(0);
    std::vector<int32_t> v(bytes / sizeof(int32_t));
    in.read(reinterpret_cast<char*>(v.data()), bytes);
    if (!in) throw std::runtime_error("short read " + path);
    return v;
}

inline void set_bit(uint8_t* bm, int id) {
    bm[id >> 3] |= uint8_t(1u << (id & 7));
}

inline bool test_bit(const uint8_t* bm, int id) {
    return (bm[id >> 3] >> (id & 7)) & 1u;
}

int popcount_bm(const uint8_t* bm, int n) {
    int c = 0;
    size_t full = size_t(n) / 64;
    const uint64_t* w = reinterpret_cast<const uint64_t*>(bm);
    for (size_t i = 0; i < full; ++i) c += __builtin_popcountll(w[i]);
    for (size_t id = full * 64; id < size_t(n); ++id) c += test_bit(bm, int(id));
    return c;
}

struct BitmapSel : faiss::IDSelector {
    const uint8_t* b;
    size_t nbytes;
    BitmapSel(size_t nb, const uint8_t* p) : b(p), nbytes(nb) {}
    bool is_member(faiss::idx_t id) const override {
        if (id < 0 || size_t(id >> 3) >= nbytes) return false;
        return (b[id >> 3] >> (id & 7)) & 1;
    }
    bool is_member(faiss::idx_t id, int) const override {
        return is_member(id);
    }
};

struct Cand {
    float d;
    int id;
};

inline bool better(const Cand& a, const Cand& b) {
    return a.d < b.d || (a.d == b.d && a.id < b.id);
}

float l2sq(const float* x, const float* y, int d) {
    float s = 0;
    for (int j = 0; j < d; ++j) {
        float t = x[j] - y[j];
        s += t * t;
    }
    return s;
}

void exact_topk(const uint8_t* bm, const float* q, const float* base, int n,
                int d, int* out) {
    std::array<Cand, K> heap{};
    int hn = 0;
    auto consider = [&](int id) {
        float dist = l2sq(q, base + size_t(id) * d, d);
        Cand c{dist, id};
        if (hn < K) {
            heap[hn++] = c;
            std::push_heap(heap.begin(), heap.begin() + hn, better);
        } else if (better(c, heap[0])) {
            std::pop_heap(heap.begin(), heap.begin() + hn, better);
            heap[hn - 1] = c;
            std::push_heap(heap.begin(), heap.begin() + hn, better);
        }
    };
    size_t full = size_t(n) / 64;
    const uint64_t* w = reinterpret_cast<const uint64_t*>(bm);
    for (size_t i = 0; i < full; ++i) {
        uint64_t word = w[i];
        while (word) {
            int b = __builtin_ctzll(word);
            consider(int(i * 64 + size_t(b)));
            word &= word - 1;
        }
    }
    for (size_t id = full * 64; id < size_t(n); ++id) {
        if (test_bit(bm, int(id))) consider(int(id));
    }
    std::sort(heap.begin(), heap.begin() + hn, better);
    for (int i = 0; i < K; ++i) out[i] = i < hn ? heap[i].id : -1;
}

std::vector<int> bitmap_to_ids(const uint8_t* bm, int n) {
    std::vector<int> ids;
    ids.reserve(size_t(n) * 0.05);
    size_t full = size_t(n) / 64;
    const uint64_t* w = reinterpret_cast<const uint64_t*>(bm);
    for (size_t i = 0; i < full; ++i) {
        uint64_t word = w[i];
        while (word) {
            int b = __builtin_ctzll(word);
            ids.push_back(int(i * 64 + size_t(b)));
            word &= word - 1;
        }
    }
    for (size_t id = full * 64; id < size_t(n); ++id) {
        if (test_bit(bm, int(id))) ids.push_back(int(id));
    }
    return ids;
}

// Posting-list exact scan. Prefetching the next candidate's first cache line
// overlaps the sparse random gathers that dominate low-selectivity scans.
void exact_topk_ids(const int* ids, int m, const float* q, const float* base,
                    int d, int* out, int pf = 8) {
    std::array<Cand, K> heap{};
    int hn = 0;
    for (int i = 0; i < m; ++i) {
        if (pf > 0 && i + pf < m) {
            _mm_prefetch(reinterpret_cast<const char*>(base + size_t(ids[i + pf]) * d),
                         _MM_HINT_T0);
        }
        float dist = l2sq(q, base + size_t(ids[i]) * d, d);
        Cand c{dist, ids[i]};
        if (hn < K) {
            heap[hn++] = c;
            std::push_heap(heap.begin(), heap.begin() + hn, better);
        } else if (better(c, heap[0])) {
            std::pop_heap(heap.begin(), heap.begin() + hn, better);
            heap[hn - 1] = c;
            std::push_heap(heap.begin(), heap.begin() + hn, better);
        }
    }
    std::sort(heap.begin(), heap.begin() + hn, better);
    for (int i = 0; i < K; ++i) out[i] = i < hn ? heap[i].id : -1;
}

struct Tree {
    int nodes = 0;
    std::vector<int> parent;
    std::vector<std::vector<int>> children;
    std::vector<std::vector<uint8_t>> local;
    std::vector<std::vector<uint8_t>> inc;

    size_t meta_bytes() const {
        size_t s = parent.size() * sizeof(int);
        for (const auto& b : local) s += b.size();
        for (const auto& b : inc) s += b.size();
        return s;
    }
};

struct Query {
    std::vector<uint8_t> bm;
    std::vector<int> ids;  // sorted feasible ids (posting-list representation)
    int node = -1;
    int qvec = 0;
};

struct Group {
    std::string mode;
    std::string bucket;
    double s_mean = 0;
    std::vector<Query> qs;
};

struct Row {
    std::string mode, bucket, method, variant;
    int ef = 0;
    int nq = 0;
    double s = 0;
    double qps = 0;
    double recall = 0;
    double recall_tune = 0;
    double p50_us = 0;
    double p95_us = 0;
    double resolve_us = 0;
    bool ok = false;
};

struct EvalOut {
    long long hits = 0;
    long long violations = 0;
    double seconds = 0;
    double resolve_us = 0;
    std::vector<double> times;
};

int count_hits(const int* pred, const int* gt) {
    int hits = 0;
    for (int i = 0; i < K; ++i) {
        if (pred[i] < 0) continue;
        for (int j = 0; j < K; ++j) {
            if (pred[i] == gt[j]) {
                ++hits;
                break;
            }
        }
    }
    return hits;
}

int count_duplicates(const int* pred) {
    int dup = 0;
    for (int i = 0; i < K; ++i) {
        if (pred[i] < 0) continue;
        for (int j = 0; j < i; ++j) {
            if (pred[j] == pred[i]) {
                ++dup;
                break;
            }
        }
    }
    return dup;
}

const uint8_t* prepare_bitmap(const Query& q, const Tree& tree,
                              const std::string& variant,
                              std::vector<uint8_t>& scratch,
                              std::vector<int>& stack, double& resolve_us) {
    if (variant == "precomputed") return q.bm.data();
    const int nb = int(q.bm.size());
    const double t0 = now_us();
    if (variant == "triehi") {
        std::memcpy(scratch.data(), tree.inc[q.node].data(), nb);
        resolve_us += now_us() - t0;
        return scratch.data();
    }
    if (variant == "local") {
        std::memcpy(scratch.data(), tree.local[q.node].data(), nb);
        resolve_us += now_us() - t0;
        return scratch.data();
    }
    if (variant == "peonline") {
        std::memset(scratch.data(), 0, nb);
        stack.clear();
        stack.push_back(q.node);
        while (!stack.empty()) {
            int v = stack.back();
            stack.pop_back();
            const uint8_t* src = tree.local[v].data();
            for (int t = 0; t < nb; ++t) scratch[t] |= src[t];
            for (int c : tree.children[v]) stack.push_back(c);
        }
        resolve_us += now_us() - t0;
        return scratch.data();
    }
    throw std::runtime_error("unknown bitmap variant " + variant);
}

EvalOut eval_exact(const Group& g, const Tree& tree, const float* queries,
                   int nq_data, const float* base, int n, int d,
                   const std::vector<int>& gt, const std::string& variant,
                   bool use_ids, int begin, int end) {
    EvalOut out;
    const int nb = int(g.qs[0].bm.size());
    std::vector<uint8_t> scratch(nb);
    std::vector<int> stack;
    std::vector<double> times;
    times.reserve(end - begin);
    for (int qi = begin; qi < end; ++qi) {
        const double t0 = now_us();
        const uint8_t* bm = prepare_bitmap(g.qs[qi], tree, variant, scratch,
                                            stack, out.resolve_us);
        int pred[K];
        if (use_ids) {
            exact_topk_ids(g.qs[qi].ids.data(), int(g.qs[qi].ids.size()),
                           queries + size_t(g.qs[qi].qvec) * d, base, d, pred);
        } else {
            exact_topk(bm, queries + size_t(g.qs[qi].qvec) * d, base, n, d, pred);
        }
        times.push_back(now_us() - t0);
        out.hits += count_hits(pred, gt.data() + size_t(qi) * K);
        out.violations += count_duplicates(pred);
        for (int t = 0; t < K; ++t) {
            if (pred[t] >= 0 && !test_bit(bm, pred[t])) ++out.violations;
        }
    }
    out.times = std::move(times);
    return out;
}

EvalOut eval_hnsw(faiss::IndexHNSWFlat& index, const Group& g, const Tree& tree,
                  const float* queries, int nq_data,
                  const std::vector<int>& gt, const std::string& variant,
                  int ef, int begin, int end) {
    EvalOut out;
    const int nb = int(g.qs[0].bm.size());
    std::vector<uint8_t> scratch(nb);
    std::vector<int> stack;
    std::vector<float> D(K);
    std::vector<faiss::idx_t> I(K);
    out.times.reserve(end - begin);
    for (int qi = begin; qi < end; ++qi) {
        const Query& q = g.qs[qi];
        const uint8_t* bm = prepare_bitmap(q, tree, variant, scratch, stack,
                                            out.resolve_us);
        const double t0 = now_us();
        std::fill(I.begin(), I.end(), -1);
        BitmapSel sel(nb, bm);
        faiss::SearchParametersHNSW p;
        p.efSearch = ef;
        p.sel = &sel;
        index.search(1, queries + size_t(q.qvec) * index.d, K, D.data(),
                     I.data(), &p);
        out.times.push_back(now_us() - t0);
        int pred[K];
        for (int t = 0; t < K; ++t) pred[t] = int(I[t]);
        out.hits += count_hits(pred, gt.data() + size_t(qi) * K);
        out.violations += count_duplicates(pred);
        for (int t = 0; t < K; ++t) {
            if (pred[t] >= 0 && !test_bit(bm, pred[t])) ++out.violations;
        }
    }
    return out;
}

EvalOut eval_post(faiss::IndexHNSWFlat& index, const Group& g,
                  const float* queries, int nq_data, const std::vector<int>& gt,
                  int ef0, int max_ef, int begin, int end) {
    EvalOut out;
    const int nb = int(g.qs[0].bm.size());
    const int cap = std::min(max_ef, int(index.ntotal));
    std::vector<float> D(cap);
    std::vector<faiss::idx_t> I(cap);
    out.times.reserve(end - begin);
    for (int qi = begin; qi < end; ++qi) {
        const Query& q = g.qs[qi];
        const uint8_t* bm = q.bm.data();
        const float* qv = queries + size_t(q.qvec) * index.d;
        const double t0 = now_us();
        int pred[K];
        for (int t = 0; t < K; ++t) pred[t] = -1;
        int got = 0;
        int ef = std::min(std::max(ef0, K), cap);
        for (;;) {
            faiss::SearchParametersHNSW p;
            p.efSearch = ef;
            index.search(1, qv, ef, D.data(), I.data(), &p);
            got = 0;
            for (int i = 0; i < ef && got < K; ++i) {
                int id = int(I[i]);
                if (id >= 0 && test_bit(bm, id)) pred[got++] = id;
            }
            if (got >= K || ef >= cap) break;
            ef = std::min(ef * 4, cap);
        }
        out.times.push_back(now_us() - t0);
        out.hits += count_hits(pred, gt.data() + size_t(qi) * K);
        out.violations += count_duplicates(pred);
    }
    return out;
}

void fill_percentiles(EvalOut& e) {
    std::sort(e.times.begin(), e.times.end());
    (void)e;
}

Row make_row(const std::string& mode, const std::string& bucket, double s,
             const std::string& method, const std::string& variant, int ef,
             const EvalOut& e, int nq, const EvalOut* tune) {
    Row r;
    r.mode = mode;
    r.bucket = bucket;
    r.s = s;
    r.method = method;
    r.variant = variant;
    r.ef = ef;
    r.nq = nq;
    r.qps = double(nq) / e.seconds;
    r.recall = double(e.hits) / double(nq * K);
    r.recall_tune = 0;
    r.ok = (e.violations == 0 && r.recall >= TARGET_RECALL);
    r.resolve_us = e.resolve_us / double(nq);
    std::vector<double> t = e.times;
    std::sort(t.begin(), t.end());
    if (!t.empty()) {
        r.p50_us = t[t.size() / 2];
        r.p95_us = t[std::min(t.size() - 1, size_t(t.size() * 95 / 100))];
    }
    return r;
}

void self_check(const std::vector<float>& base, const std::vector<float>& queries,
                int n, int d, const Group& g, const std::vector<int>& gt) {
    faiss::IndexFlat flat(d, faiss::METRIC_L2);
    flat.add(n, base.data());
    const int nb = int(g.qs[0].bm.size());
    for (int qi = 0; qi < std::min<int>(2, int(g.qs.size())); ++qi) {
        BitmapSel sel(nb, g.qs[qi].bm.data());
        faiss::SearchParameters p;
        p.sel = &sel;
        float D[K];
        faiss::idx_t I[K];
        flat.search(1, queries.data() + size_t(g.qs[qi].qvec) * d, K, D, I, &p);
        std::vector<int> a, b;
        for (int t = 0; t < K; ++t) {
            if (I[t] >= 0) a.push_back(int(I[t]));
            if (gt[size_t(qi) * K + t] >= 0) b.push_back(gt[size_t(qi) * K + t]);
        }
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        if (a != b) {
            throw std::runtime_error("self-check mismatch vs IndexFlat");
        }
    }
}

int classify(double s) {
    for (int i = 0; i < 7; ++i) {
        if (s >= BUCKET_LO[i] && s < BUCKET_HI[i]) return i;
    }
    return -1;
}

uint64_t fnv1a(const uint8_t* p, size_t n, uint64_t seed) {
    uint64_t h = 1469598103934665603ull ^ seed;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

Tree load_tree(const std::string& fix, int n, int nb) {
    Tree t;
    {
        std::ifstream in(fix + "/labels_meta.txt");
        int ln = 0, L = 0;
        if (!(in >> ln >> L) || ln != n || L <= 0) {
            throw std::runtime_error("bad labels_meta");
        }
        (void)L;
    }
    {
        std::ifstream in(fix + "/tree.txt");
        int count = 0;
        if (!(in >> count) || count <= 0) {
            throw std::runtime_error("bad tree");
        }
        t.nodes = count;
        t.parent.assign(t.nodes, -1);
        t.children.assign(t.nodes, {});
        for (int v = 0; v < t.nodes; ++v) {
            int id = 0, p = -1;
            if (!(in >> id >> p) || id != v) {
                throw std::runtime_error("bad tree row");
            }
            if (v == 0) {
                if (p != -1) throw std::runtime_error("root has parent");
            } else {
                if (p < 0 || p >= v) throw std::runtime_error("bad parent");
                t.parent[v] = p;
                t.children[p].push_back(v);
            }
        }
    }
    int ln = 0, L = 0;
    {
        std::ifstream in(fix + "/labels_meta.txt");
        in >> ln >> L;
    }
    const auto labels = read_i32(fix + "/labels.bin",
                                 size_t(n) * size_t(L) * sizeof(int32_t));
    t.local.assign(t.nodes, std::vector<uint8_t>(nb, 0));
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < L; ++j) {
            int lab = labels[size_t(i) * L + j];
            if (lab < 0) continue;
            if (lab >= t.nodes) throw std::runtime_error("label out of range");
            set_bit(t.local[lab].data(), i);
        }
    }
    t.inc = t.local;
    for (int v = t.nodes - 1; v >= 1; --v) {
        int p = t.parent[v];
        for (int w = 0; w < nb; ++w) t.inc[p][w] |= t.inc[v][w];
    }
    return t;
}

std::vector<Group> make_random_groups(int n, int nb, int nq_data,
                                      int nq_per_bucket, uint64_t seed) {
    static const double targets[7] = {0.001, 0.003, 0.01, 0.03,
                                      0.1,   0.3,   1.0};
    std::mt19937_64 rng(seed ^ 0x9e3779b97f4a7c15ull);
    std::vector<Group> groups;
    for (int bi = 0; bi < 7; ++bi) {
        Group g;
        g.mode = "random";
        g.bucket = BUCKET_NAMES[bi];
        size_t m = size_t(std::llround(targets[bi] * double(n)));
        m = std::max<size_t>(1, std::min<size_t>(size_t(n), m));
        g.s_mean = double(m) / double(n);
        g.qs.reserve(nq_per_bucket);
        std::vector<int> pool(n);
        for (int qi = 0; qi < nq_per_bucket; ++qi) {
            Query q;
            q.bm.assign(nb, 0);
            q.qvec = qi % nq_data;
            q.ids.resize(m);
            if (m == size_t(n)) {
                for (int id = 0; id < n; ++id) {
                    set_bit(q.bm.data(), id);
                    q.ids[id] = id;
                }
            } else {
                std::iota(pool.begin(), pool.end(), 0);
                for (size_t i = 0; i < m; ++i) {
                    size_t j = i + size_t(rng() % (size_t(n) - i));
                    std::swap(pool[i], pool[j]);
                    set_bit(q.bm.data(), pool[i]);
                    q.ids[i] = pool[i];
                }
                std::sort(q.ids.begin(), q.ids.end());
            }
            g.qs.push_back(std::move(q));
        }
        groups.push_back(std::move(g));
    }
    return groups;
}

std::vector<Group> make_tree_groups(const Tree& tree, int n, int nb,
                                    int nq_data, int nq_per_bucket,
                                    uint64_t seed) {
    std::vector<Group> groups;
    for (const std::string scope : {"sub", "exact"}) {
        std::vector<std::vector<std::pair<int, double>>> buckets(7);
        std::unordered_set<uint64_t> seen;
        for (int v = 0; v < tree.nodes; ++v) {
            const auto& bm = scope == "sub" ? tree.inc[v] : tree.local[v];
            int m = popcount_bm(bm.data(), n);
            if (m < 16 || m > n - 16) continue;
            double s = double(m) / double(n);
            int bi = classify(s);
            if (bi < 0) continue;
            uint64_t h = fnv1a(bm.data(), nb, strcmp(scope.c_str(), "sub") == 0 ? 1 : 2);
            if (!seen.insert(h).second) continue;
            buckets[bi].push_back({v, s});
        }
        for (int bi = 0; bi < 7; ++bi) {
            auto& cand = buckets[bi];
            if (cand.size() < 2) continue;
            std::mt19937_64 rng(seed + uint64_t(bi) * 131 + (scope == "sub" ? 7 : 11));
            std::shuffle(cand.begin(), cand.end(), rng);
            size_t take = std::min<size_t>(size_t(nq_per_bucket), cand.size());
            take -= take % 2;
            if (take < 2) continue;
            Group g;
            g.mode = scope;
            g.bucket = BUCKET_NAMES[bi];
            double ssum = 0;
            g.qs.reserve(take);
            for (size_t i = 0; i < take; ++i) {
                int v = cand[i].first;
                Query q;
                q.bm = scope == "sub" ? tree.inc[v] : tree.local[v];
                q.ids = bitmap_to_ids(q.bm.data(), n);
                q.node = v;
                q.qvec = int(i) % nq_data;
                ssum += cand[i].second;
                g.qs.push_back(std::move(q));
            }
            g.s_mean = ssum / double(take);
            groups.push_back(std::move(g));
        }
    }
    return groups;
}

int choose_ef(faiss::IndexHNSWFlat& index, const Group& g, const Tree& tree,
              const float* queries, int nq_data, const std::vector<int>& gt,
              const std::string& variant, int max_ef, int tune_end) {
    const int efs[] = {16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192,
                       16384, 32768};
    int last = 16;
    for (int ef : efs) {
        if (ef > max_ef) break;
        EvalOut e = eval_hnsw(index, g, tree, queries, nq_data, gt, variant,
                              ef, 0, tune_end);
        last = ef;
        double recall = double(e.hits) / double(tune_end * K);
        if (recall >= TARGET_RECALL && e.violations == 0) return ef;
    }
    return last;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Args args = parse(argc, argv);
        std::filesystem::create_directories(args.out_dir);
        const std::string fix = args.fixtures;
        std::cerr << "[selectivity] loading fixtures\n";

        std::vector<float> base_raw = read_f32(fix + "/base.f32");
        std::vector<float> query_raw = read_f32(fix + "/test.f32");
        // Dimensions are known from the fixture contract; keep them explicit.
        const int n = 100000;
        const int d = 128;
        const int nq_data = int(query_raw.size() / d);
        if (base_raw.size() != size_t(n) * d) {
            throw std::runtime_error("base.f32 is not 100000x128");
        }
        const int nb = (n + 7) / 8;

        Tree tree = load_tree(fix, n, nb);
        std::cerr << "[selectivity] tree_nodes=" << tree.nodes
                  << " tree_meta_mb=" << tree.meta_bytes() / (1024.0 * 1024.0)
                  << "\n";

        std::vector<Group> groups =
            make_random_groups(n, nb, nq_data, args.nq_per_bucket, args.seed);
        std::vector<Group> tree_groups =
            make_tree_groups(tree, n, nb, nq_data, args.nq_per_bucket, args.seed);
        groups.insert(groups.end(), tree_groups.begin(), tree_groups.end());

        double bitmap_bytes = 0;
        for (const auto& g : groups)
            for (const auto& q : g.qs) bitmap_bytes += q.bm.size();
        std::cerr << "[selectivity] groups=" << groups.size()
                  << " query_bitmap_mb=" << bitmap_bytes / (1024.0 * 1024.0)
                  << "\n";

        // Validate every hierarchy bitmap against its resolved representation.
        for (const auto& g : groups) {
            if (g.mode == "random") continue;
            std::vector<uint8_t> scratch(nb);
            std::vector<int> stack;
            for (const auto& q : g.qs) {
                const auto& src = g.mode == "sub" ? tree.inc[q.node]
                                                  : tree.local[q.node];
                if (std::memcmp(src.data(), q.bm.data(), nb) != 0) {
                    throw std::runtime_error("hierarchy bitmap mismatch");
                }
            }
        }

        // Ground truth for every query.
        std::cerr << "[selectivity] computing exact oracle\n";
        const double gt0 = now_s();
        std::vector<std::vector<int>> gts(groups.size());
        for (size_t gi = 0; gi < groups.size(); ++gi) {
            const Group& g = groups[gi];
            gts[gi].assign(g.qs.size() * K, -1);
            for (size_t qi = 0; qi < g.qs.size(); ++qi) {
                exact_topk(g.qs[qi].bm.data(),
                           query_raw.data() + size_t(g.qs[qi].qvec) * d,
                           base_raw.data(), n, d, gts[gi].data() + qi * K);
            }
        }
        std::cerr << "[selectivity] oracle_s=" << now_s() - gt0 << "\n";
        for (size_t gi = 0; gi < groups.size(); ++gi) {
            if (groups[gi].mode != "random") continue;
            if (groups[gi].bucket != "0.01" && groups[gi].bucket != "1.0") continue;
            self_check(base_raw, query_raw, n, d, groups[gi], gts[gi]);
        }
        std::cerr << "[selectivity] self_check=PASS\n";

        // Build or load one HNSW graph; all predicates share it.
        const std::string index_path =
            args.out_dir + "/hnsw_M" + std::to_string(args.M) + "_efc" +
            std::to_string(args.efc) + "_n" + std::to_string(n) + ".faiss";
        faiss::IndexHNSWFlat* index = nullptr;
        double build_s = 0, load_s = 0;
        size_t index_bytes = 0;
        if (!args.rebuild_index && std::filesystem::exists(index_path)) {
            const double t0 = now_s();
            faiss::Index* raw = faiss::read_index(index_path.c_str());
            index = dynamic_cast<faiss::IndexHNSWFlat*>(raw);
            if (!index || index->ntotal != n || index->d != d) {
                throw std::runtime_error("cached index mismatch");
            }
            load_s = now_s() - t0;
        } else {
            const double t0 = now_s();
            index = new faiss::IndexHNSWFlat(d, args.M, faiss::METRIC_L2);
            index->hnsw.efConstruction = args.efc;
            index->add(n, base_raw.data());
            build_s = now_s() - t0;
            faiss::write_index(index, index_path.c_str());
        }
        index_bytes = std::filesystem::file_size(index_path);
        std::cerr << "[selectivity] hnsw build_s=" << build_s
                  << " load_s=" << load_s
                  << " index_mb=" << index_bytes / (1024.0 * 1024.0) << "\n";

        std::vector<Row> rows;
        const std::string efs_variants[3] = {"precomputed", "triehi", "peonline"};
        for (size_t gi = 0; gi < groups.size(); ++gi) {
            Group& g = groups[gi];
            const int nq = int(g.qs.size());
            const int half = nq / 2;
            std::cerr << "[selectivity] " << g.mode << " bucket=" << g.bucket
                      << " s=" << std::fixed << std::setprecision(5) << g.s_mean
                      << " nq=" << nq << "\n";

            std::vector<std::string> variants;
            if (g.mode == "random") {
                variants = {"precomputed"};
            } else if (g.mode == "sub") {
                variants = {"precomputed", "triehi", "peonline"};
            } else {
                variants = {"precomputed", "local"};
            }

            for (const auto& variant : variants) {
                const int ef = choose_ef(*index, g, tree, query_raw.data(),
                                         nq_data, gts[gi], variant, args.max_ef,
                                         half);
                EvalOut tune = eval_hnsw(*index, g, tree, query_raw.data(),
                                         nq_data, gts[gi], variant, ef, 0, half);
                const double t0 = now_s();
                EvalOut e = eval_hnsw(*index, g, tree, query_raw.data(),
                                      nq_data, gts[gi], variant, ef, half, nq);
                e.seconds = now_s() - t0;
                Row r = make_row(g.mode, g.bucket, g.s_mean, "hnsw", variant,
                                 ef, e, nq - half, &tune);
                r.recall_tune = double(tune.hits) / double(half * K);
                rows.push_back(r);

                const double t1 = now_s();
                EvalOut ex = eval_exact(g, tree, query_raw.data(), nq_data,
                                        base_raw.data(), n, d, gts[gi], variant,
                                        false, half, nq);
                ex.seconds = now_s() - t1;
                Row re = make_row(g.mode, g.bucket, g.s_mean, "exact", variant,
                                  0, ex, nq - half, nullptr);
                rows.push_back(re);

                if (variant == "precomputed") {
                    const double t2 = now_s();
                    EvalOut el = eval_exact(g, tree, query_raw.data(), nq_data,
                                           base_raw.data(), n, d, gts[gi], variant,
                                           true, half, nq);
                    el.seconds = now_s() - t2;
                    Row rl = make_row(g.mode, g.bucket, g.s_mean, "exact_list",
                                      variant, 0, el, nq - half, nullptr);
                    rows.push_back(rl);
                }
            }

            if (g.mode == "random") {
                const int ef0 = std::min(
                    args.max_ef,
                    std::max(16, int(std::ceil(2.0 * K / g.s_mean))));
                const double t0 = now_s();
                EvalOut p = eval_post(*index, g, query_raw.data(), nq_data,
                                      gts[gi], ef0, args.max_ef, half, nq);
                p.seconds = now_s() - t0;
                Row r = make_row(g.mode, g.bucket, g.s_mean, "post", "precomputed",
                                 ef0, p, nq - half, nullptr);
                rows.push_back(r);
            }
        }

        // Persist rows.
        const std::string jsonl_path = args.out_dir + "/selectivity.jsonl";
        const std::string csv_path = args.out_dir + "/selectivity.csv";
        {
            std::ofstream j(jsonl_path);
            std::ofstream c(csv_path);
            c << "mode,bucket,method,variant,ef,s,nq,qps,recall,recall_tune,"
                 "p50_us,p95_us,resolve_us,ok\n";
            j << std::setprecision(10);
            c << std::setprecision(10);
            for (const auto& r : rows) {
                j << "{\"mode\":\"" << r.mode << "\",\"bucket\":\"" << r.bucket
                  << "\",\"method\":\"" << r.method << "\",\"variant\":\""
                  << r.variant << "\",\"ef\":" << r.ef << ",\"s\":" << r.s
                  << ",\"nq\":" << r.nq << ",\"qps\":" << r.qps
                  << ",\"recall\":" << r.recall
                  << ",\"recall_tune\":" << r.recall_tune
                  << ",\"p50_us\":" << r.p50_us << ",\"p95_us\":" << r.p95_us
                  << ",\"resolve_us\":" << r.resolve_us
                  << ",\"ok\":" << (r.ok ? "true" : "false") << "}\n";
                c << r.mode << "," << r.bucket << "," << r.method << ","
                  << r.variant << "," << r.ef << "," << r.s << "," << r.nq
                  << "," << r.qps << "," << r.recall << "," << r.recall_tune
                  << "," << r.p50_us << "," << r.p95_us << ","
                  << r.resolve_us << "," << (r.ok ? 1 : 0) << "\n";
            }
        }

        // Frontier over random buckets: best gated QPS per bucket.
        double logsum = 0;
        int nvalid = 0;
        double crossover_s = -1;
        std::vector<std::string> frontier_method;
        std::vector<double> frontier_qps;
        for (int bi = 0; bi < 7; ++bi) {
            const std::string b = BUCKET_NAMES[bi];
            double best_qps = 0;
            std::string best_m;
            double exact_qps = -1, hnsw_qps = -1;
            bool hnsw_ok = false;
            for (const auto& r : rows) {
                if (r.mode != "random" || r.bucket != b) continue;
                if (r.ok && r.qps > best_qps) {
                    best_qps = r.qps;
                    best_m = r.method;
                }
                if (r.method == "exact" && r.variant == "precomputed" && r.ok)
                    exact_qps = r.qps;
                if (r.method == "hnsw" && r.variant == "precomputed" && r.ok) {
                    hnsw_qps = r.qps;
                    hnsw_ok = true;
                }
            }
            if (best_qps > 0) {
                logsum += std::log(best_qps);
                ++nvalid;
            }
            if (crossover_s < 0 && hnsw_ok && exact_qps > 0 &&
                hnsw_qps > exact_qps) {
                crossover_s = BUCKET_LO[bi];
            }
            frontier_method.push_back(best_m);
            frontier_qps.push_back(best_qps);
            std::cerr << "[selectivity] frontier " << b << " -> " << best_m
                      << " " << best_qps << " qps\n";
        }
        const double gmean =
            nvalid == 7 ? std::exp(logsum / 7.0) : 0.0;

        // Fixed policy: exact scan below s=0.01, BF-HNSW at/above s=0.01.
        double pol_logsum = 0;
        int pol_valid = 0;
        for (int bi = 0; bi < 7; ++bi) {
            const std::string b = BUCKET_NAMES[bi];
            const char* want = (bi < 2) ? "exact_list" : "hnsw";
            double q = 0;
            for (const auto& r : rows) {
                if (r.mode == "random" && r.bucket == b && r.ok &&
                    r.method == want && r.variant == "precomputed") {
                    q = std::max(q, r.qps);
                }
            }
            if (q > 0) {
                pol_logsum += std::log(q);
                ++pol_valid;
            }
        }
        const double policy_gmean =
            pol_valid == 7 ? std::exp(pol_logsum / 7.0) : 0.0;

        bool valid = true;
        for (const auto& r : rows) {
            if ((r.method == "exact" || r.method == "exact_list") &&
                r.recall < 0.999)
                valid = false;
        }

        const std::string summary_path = args.out_dir + "/summary.json";
        {
            std::ofstream s(summary_path);
            s << std::setprecision(10);
            s << "{\"gmean_qps_r95\":" << gmean
              << ",\"gmean_policy_qps_r95\":" << policy_gmean
              << ",\"crossover_s\":" << crossover_s
              << ",\"build_s\":" << build_s
              << ",\"index_load_s\":" << load_s
              << ",\"index_mb\":" << index_bytes / (1024.0 * 1024.0)
              << ",\"tree_meta_mb\":" << tree.meta_bytes() / (1024.0 * 1024.0)
              << ",\"query_bitmap_mb\":" << bitmap_bytes / (1024.0 * 1024.0)
              << ",\"n\":" << n << ",\"d\":" << d << ",\"nq_data\":" << nq_data
              << ",\"k\":" << K << ",\"target_recall\":" << TARGET_RECALL
              << ",\"nq_per_bucket\":" << args.nq_per_bucket
              << ",\"seed\":" << args.seed << ",\"valid\":"
              << (valid ? "true" : "false") << ",\"frontier\":[";
            for (int bi = 0; bi < 7; ++bi) {
                if (bi) s << ",";
                s << "{\"bucket\":\"" << BUCKET_NAMES[bi] << "\",\"method\":\""
                  << frontier_method[bi] << "\",\"qps\":" << frontier_qps[bi]
                  << "}";
            }
            s << "]}";
        }
        std::cout << "summary=" << summary_path << "\n";
        if (!valid) {
            std::cerr << "validation failed: exact scan recall < 0.999\n";
            return 3;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
}
