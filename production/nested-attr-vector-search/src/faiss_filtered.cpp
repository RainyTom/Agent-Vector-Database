// Filtered-ANN baselines on the PathSeer/FAISS layout.
//
// Methods:
//   hnsw     : faiss::IndexHNSWFlat + bitmap selector (BF-HNSW).
//   acorn    : faiss::IndexACORNFlat; optional primary-direct-label metadata
//              is a lossy single-label encoding of our multitag workload.
//   pathseer : faiss::IndexPathSeerFlat from deps/PathSeer, with fixed expanding
//              degree at build/search (not its automatically profiled variant).
//   triehi-hnsw : Directory-Aware-style inclusive node bitmaps resolved online,
//              then the EXACT SAME persisted BF-HNSW graph and selector.
//              Static Subtree DSQ only; no path-string lookup or DSM.
// Loads load/load.bin, query/query.bin, query/query_<mode>.bv,
// golden/golden_<mode>_l2_10.bin. Raw vectors, METRIC_L2.
//
// Self-test (--self-test) builds a tiny two-cluster index and verifies that
// the bitmap prunes traversal (finds the nearest FEASIBLE point) rather than
// filtering a global top-k after the fact.

#include <faiss/IndexACORN.h>
#include <faiss/IndexHNSW.h>
#include <faiss/IndexPathSeer.h>
#include <faiss/impl/IDSelector.h>
#include <faiss/index_io.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <set>
#include <string>
#include <vector>

namespace {

// This fork's HNSW filtered path calls is_member(id, pos); the base class
// default returns false, so a selector must override BOTH signatures.
struct BitmapSel : faiss::IDSelector {
  const uint8_t* b;
  size_t nbytes;
  BitmapSel(size_t nb, const uint8_t* p) : b(p), nbytes(nb) {}
  bool is_member(faiss::idx_t id) const override {
    if (id < 0 || size_t(id >> 3) >= nbytes) return false;
    return (b[id >> 3] >> (id & 7)) & 1;
  }
  bool is_member(faiss::idx_t id, int) const override { return is_member(id); }
};

struct Args {
  std::string dataset, mode = "sub", method = "hnsw", out, ids_out, timings_out;
  std::string scope_fixtures, index_in, index_out;
  int M = 32, efc = 200, ef = 64, k = 10, repeats = 2;
  int M_exp = 64, M_exp_search = 16;
  int gamma = 1, Mbeta = 4;
  bool self_test = false;
};

Args parse(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; ++i) {
    std::string f = argv[i];
    auto next = [&]() { return std::string(argv[++i]); };
    if (f == "--dataset") a.dataset = next();
    else if (f == "--mode") a.mode = next();
    else if (f == "--method") a.method = next();
    else if (f == "--out") a.out = next();
    else if (f == "--ids") a.ids_out = next();
    else if (f == "--timings") a.timings_out = next();
    else if (f == "--scope-fixtures") a.scope_fixtures = next();
    else if (f == "--index-in") a.index_in = next();
    else if (f == "--index-out") a.index_out = next();
    else if (f == "--M") a.M = std::stoi(next());
    else if (f == "--efc") a.efc = std::stoi(next());
    else if (f == "--ef") a.ef = std::stoi(next());
    else if (f == "--k") a.k = std::stoi(next());
    else if (f == "--repeats") a.repeats = std::stoi(next());
    else if (f == "--M-exp") a.M_exp = std::stoi(next());
    else if (f == "--M-exp-search") a.M_exp_search = std::stoi(next());
    else if (f == "--gamma") a.gamma = std::stoi(next());
    else if (f == "--Mbeta") a.Mbeta = std::stoi(next());
    else if (f == "--self-test") a.self_test = true;
    else { std::cerr << "unknown arg " << f << "\n"; std::exit(2); }
  }
  return a;
}

template <typename T>
std::vector<T> read_vec(const std::string& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) { std::cerr << "cannot open " << path << "\n"; std::exit(2); }
  size_t bytes = size_t(in.tellg());
  in.seekg(0);
  std::vector<T> v(bytes / sizeof(T));
  in.read(reinterpret_cast<char*>(v.data()), bytes);
  return v;
}

struct Data {
  int n = 0, d = 0, nq = 0, k = 0;
  size_t nbytes = 0;
  std::vector<float> base, queries;
  std::vector<uint8_t> bitmaps;
  std::vector<int32_t> golden;
};

Data load(const Args& a) {
  Data x;
  {
    std::ifstream in(a.dataset + "/load/load.bin", std::ios::binary);
    uint32_t n, d;
    in.read(reinterpret_cast<char*>(&n), 4);
    in.read(reinterpret_cast<char*>(&d), 4);
    x.n = n; x.d = d;
    x.base.resize(size_t(n) * d);
    in.read(reinterpret_cast<char*>(x.base.data()), x.base.size() * 4);
  }
  {
    std::ifstream in(a.dataset + "/query/query.bin", std::ios::binary);
    uint32_t nq, d;
    in.read(reinterpret_cast<char*>(&nq), 4);
    in.read(reinterpret_cast<char*>(&d), 4);
    x.nq = nq;
    x.queries.resize(size_t(nq) * d);
    in.read(reinterpret_cast<char*>(x.queries.data()), x.queries.size() * 4);
  }
  x.nbytes = (size_t(x.n) + 7) / 8;
  x.bitmaps = read_vec<uint8_t>(a.dataset + "/query/query_" + a.mode + ".bv");
  if (x.bitmaps.size() != size_t(x.nq) * x.nbytes) {
    std::cerr << "bitmap size mismatch\n"; std::exit(2);
  }
  auto g = read_vec<int32_t>(a.dataset + "/golden/golden_" + a.mode + "_l2_10.bin");
  x.k = g[1];
  x.golden.assign(g.begin() + 2, g.end());
  return x;
}

// Static TRIEHI-style scope layer: Local(u) is direct multitag membership,
// Inc(u) is the union along ancestors. Queries carry node ids, so no path
// parsing is charged to either this method or LES.
struct ScopeIndex {
  size_t stride = 0, metadata_bytes = 0;
  double validation_seconds = 0;
  std::vector<uint8_t> inclusive;
  std::vector<std::vector<int>> query_nodes;

  void resolve(int qi, std::vector<uint8_t>& scratch) const {
    const auto& us = query_nodes[qi];
    std::copy_n(inclusive.data() + size_t(us[0]) * stride, stride, scratch.data());
    for (size_t z = 1; z < us.size(); ++z) {
      const uint8_t* src = inclusive.data() + size_t(us[z]) * stride;
      for (size_t t = 0; t < stride; ++t) scratch[t] |= src[t];
    }
  }
};

ScopeIndex load_scope(const Args& a, const Data& x) {
  if (a.scope_fixtures.empty() || a.mode != "sub") {
    std::cerr << "triehi-hnsw needs --scope-fixtures and --mode sub\n"; std::exit(2);
  }
  const std::string root = a.scope_fixtures + "/";
  int n, d, nq, k, nodes;
  size_t words;
  std::ifstream mf(root + "meta.txt");
  if (!(mf >> n >> d >> nq >> k >> nodes >> words) ||
      n != x.n || d != x.d || nq != x.nq || k != x.k || nodes <= 0) {
    std::cerr << "directory fixture metadata mismatch\n"; std::exit(2);
  }
  std::vector<int> parent(nodes, -1);
  std::ifstream tree(root + "tree.txt");
  int count;
  if (!(tree >> count) || count != nodes) {
    std::cerr << "invalid tree\n"; std::exit(2);
  }
  for (int v = 0; v < nodes; ++v) {
    int id, p;
    if (!(tree >> id >> p) || id != v || (v && (p < 0 || p >= v)) ||
        (!v && p != -1)) {
      std::cerr << "invalid preorder tree\n"; std::exit(2);
    }
    parent[v] = p;
  }
  int ln, L;
  std::ifstream lm(root + "labels_meta.txt");
  if (!(lm >> ln >> L) || ln != n || L <= 0) {
    std::cerr << "invalid label metadata\n"; std::exit(2);
  }
  const auto labels = read_vec<int32_t>(root + "labels.bin");
  if (labels.size() != size_t(n) * L) {
    std::cerr << "label count mismatch\n"; std::exit(2);
  }
  ScopeIndex idx;
  idx.stride = x.nbytes;
  idx.inclusive.resize(size_t(nodes) * idx.stride, 0);
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < L; ++j) {
      const int lab = labels[size_t(i) * L + j];
      if (lab < -1 || lab >= nodes) {
        std::cerr << "invalid label\n"; std::exit(2);
      }
      for (int v = lab; v >= 0; v = parent[v]) {
        idx.inclusive[size_t(v) * idx.stride + (i >> 3)] |= uint8_t(1u << (i & 7));
      }
    }
  }
  std::ifstream wf(root + "workload_sub.txt");
  int qn, qk;
  if (!(wf >> qn >> qk) || qn != nq || qk != k) {
    std::cerr << "directory workload mismatch\n"; std::exit(2);
  }
  idx.query_nodes.resize(nq);
  for (int qi = 0; qi < nq; ++qi) {
    int width;
    if (!(wf >> width) || width <= 0) {
      std::cerr << "empty directory query\n"; std::exit(2);
    }
    idx.query_nodes[qi].resize(width);
    for (int& u : idx.query_nodes[qi]) {
      if (!(wf >> u) || u < 0 || u >= nodes) {
        std::cerr << "invalid directory query node\n"; std::exit(2);
      }
    }
  }
  const auto validation_start = std::chrono::steady_clock::now();
  std::vector<uint8_t> scratch(idx.stride);
  for (int qi = 0; qi < nq; ++qi) {
    idx.resolve(qi, scratch);
    if (std::memcmp(scratch.data(), x.bitmaps.data() + size_t(qi) * idx.stride,
                    idx.stride) != 0) {
      std::cerr << "directory candidates differ from filter oracle, qi=" << qi << "\n";
      std::exit(2);
    }
  }
  idx.validation_seconds = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - validation_start).count();
  idx.metadata_bytes = idx.inclusive.size() + parent.size() * sizeof(parent[0]);
  return idx;
}

bool feasible(const Data& x, int qi, int id) {
  if (id < 0 || id >= x.n) return false;
  const uint8_t* b = &x.bitmaps[size_t(qi) * x.nbytes];
  return (b[id >> 3] >> (id & 7)) & 1;
}

double now_s() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

int self_test() {
  // 1-D chain x=0..399, feasible only x>=360. The query sits at 0, so a
  // post-filter of the global top-10 returns nothing, while a selector-aware
  // search must traverse the connected chain and return the nearest feasible
  // point. The chain (unlike two separated clusters) is connected for HNSW.
  const int n = 400, neg = 360, dim = 1;
  std::vector<float> base(n);
  for (int i = 0; i < n; ++i) base[i] = float(i);
  size_t nbytes = (n + 7) / 8;
  std::vector<uint8_t> bm(nbytes, 0);
  for (int i = neg; i < n; ++i) bm[i >> 3] |= uint8_t(1 << (i & 7));

  faiss::IndexHNSWFlat index(dim, 16, faiss::METRIC_L2);
  index.hnsw.efConstruction = 200;
  index.add(n, base.data());

  float q[1] = {0.0f};
  float D[10]; faiss::idx_t I[10];
  BitmapSel sel(nbytes, bm.data());
  faiss::SearchParametersHNSW p;
  p.efSearch = 500;  // > chain prefix: must traverse through infeasible points
  p.sel = &sel;
  index.search(1, q, 1, D, I, &p);
  long long filtered_id = (long long)I[0];
  bool filtered_ok = I[0] >= neg && I[0] < n;

  // Post-filter comparison: global top-10 (no selector), then filter.
  faiss::SearchParametersHNSW p2;
  p2.efSearch = 500;
  index.search(1, q, 10, D, I, &p2);
  int post_feasible = 0;
  for (int i = 0; i < 10; ++i)
    if (I[i] >= neg && I[i] < n) ++post_feasible;

  printf("self-test: filtered_top1=%lld feasible=%d | post_filter(candidates=10) feasible=%d\n",
         filtered_id, int(filtered_ok), post_feasible);
  if (!filtered_ok || post_feasible != 0) {
    printf("SELFTEST FAIL (bitmap may only be filtering results, not traversal)\n");
    return 1;
  }
  printf("SELFTEST PASS\n");
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  Args a = parse(argc, argv);
  if (a.self_test) return self_test();
  if (a.dataset.empty()) { std::cerr << "need --dataset\n"; return 2; }
  Data x = load(a);
  printf("loaded n=%d d=%d nq=%d k=%d nbytes=%zu\n", x.n, x.d, x.nq, x.k, x.nbytes);

  ScopeIndex scope;
  double metadata_build_seconds = 0;
  if (a.method == "triehi-hnsw") {
    double s = now_s();
    scope = load_scope(a, x);
    metadata_build_seconds = now_s() - s - scope.validation_seconds;
    printf("triehi: checked %d online scope bitmaps against oracle\n", x.nq);
  }

  double t0 = now_s();
  faiss::Index* index = nullptr;
  if (!a.index_in.empty()) {
    if (a.method != "triehi-hnsw" && a.method != "hnsw") {
      std::cerr << "only HNSW can reuse --index-in\n"; return 2;
    }
    index = faiss::read_index(a.index_in.c_str());
    if (!dynamic_cast<faiss::IndexHNSWFlat*>(index) ||
        index->ntotal != x.n || index->d != x.d) {
      std::cerr << "reused index must be BF-HNSW for the same base\n"; return 2;
    }
  } else if (a.method == "hnsw" || a.method == "triehi-hnsw") {
    auto* h = new faiss::IndexHNSWFlat(x.d, a.M, faiss::METRIC_L2);
    h->hnsw.efConstruction = a.efc;
    h->add(x.n, x.base.data());
    index = h;
  } else if (a.method == "pathseer") {
    auto* h = new faiss::IndexPathSeerFlat(x.d, a.M, a.M_exp, faiss::METRIC_L2);
    h->pathseer.efConstruction = a.efc;
    h->pathseer.efConstructionExp = a.efc;
    h->add(x.n, x.base.data());
    index = h;
  } else if (a.method == "acorn") {
    std::vector<int> metadata(x.n, 0);  // no multi-label-aware construction
    std::ifstream mf(a.dataset + "/load/load.meta");
    if (mf) {
      for (int i = 0; i < x.n; ++i) {
        if (!(mf >> metadata[i])) metadata[i] = 0;
      }
      printf("acorn metadata: primary direct label (single-label encoding)\n");
    }
    auto* c = new faiss::IndexACORNFlat(x.d, a.M, a.gamma, metadata, a.Mbeta,
                                        faiss::METRIC_L2);
    c->add(x.n, x.base.data());
    index = c;
  } else {
    std::cerr << "unknown method\n"; return 2;
  }
  const double graph_elapsed_s = now_s() - t0;
  const double build_s = a.index_in.empty() ? graph_elapsed_s : 0;
  const double index_load_seconds = a.index_in.empty() ? 0 : graph_elapsed_s;

  size_t index_bytes = 0;
  const std::string storage = a.index_out.empty() ?
      (a.index_in.empty() ? "/tmp/faiss_filtered_index.bin" : a.index_in) : a.index_out;
  try {
    if (a.index_in.empty() || !a.index_out.empty()) faiss::write_index(index, storage.c_str());
    std::ifstream f(storage, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("cannot read index file");
    index_bytes = size_t(f.tellg());
  } catch (const std::exception& e) {
    if (!a.index_out.empty() || !a.index_in.empty()) {
      std::cerr << "index serialization: " << e.what() << "\n"; return 2;
    }
    index_bytes = 0;
  }

  // warmup
  std::vector<float> D(x.k);
  std::vector<faiss::idx_t> I(x.k);
  std::vector<uint8_t> scope_bitmap(a.method == "triehi-hnsw" ? x.nbytes : 0);
  for (int qi = 0; qi < std::min(50, x.nq); ++qi) {
    if (a.method == "triehi-hnsw") scope.resolve(qi, scope_bitmap);
    const uint8_t* bitmap = a.method == "triehi-hnsw" ?
        scope_bitmap.data() : &x.bitmaps[size_t(qi) * x.nbytes];
    BitmapSel sel(x.nbytes, bitmap);
    if (a.method == "hnsw" || a.method == "triehi-hnsw") {
      faiss::SearchParametersHNSW p; p.efSearch = a.ef; p.sel = &sel;
      index->search(1, &x.queries[size_t(qi) * x.d], x.k, D.data(), I.data(), &p);
    } else if (a.method == "pathseer") {
      faiss::SearchParametersPathSeer p;
      p.efSearch = a.ef; p.M_exp_search = a.M_exp_search; p.sel = &sel;
      index->search(1, &x.queries[size_t(qi) * x.d], x.k, D.data(), I.data(), &p);
    } else {
      faiss::SearchParametersACORN p; p.efSearch = a.ef; p.sel = &sel;
      static_cast<faiss::IndexACORNFlat*>(index)->hybrid_search(
          1, &x.queries[size_t(qi) * x.d], x.k, D.data(), I.data(), &p);
    }
  }

  double best_total = 1e300;
  std::vector<double> best_timings;
  std::vector<int32_t> best_ids;
  for (int rep = 0; rep < a.repeats; ++rep) {
    std::vector<double> timings(x.nq);
    std::vector<int32_t> ids(size_t(x.nq) * x.k, -1);
    double total = 0;
    for (int qi = 0; qi < x.nq; ++qi) {
      double s = now_s();
      if (a.method == "triehi-hnsw") scope.resolve(qi, scope_bitmap);
      const uint8_t* bitmap = a.method == "triehi-hnsw" ?
          scope_bitmap.data() : &x.bitmaps[size_t(qi) * x.nbytes];
      BitmapSel sel(x.nbytes, bitmap);
      if (a.method == "hnsw" || a.method == "triehi-hnsw") {
        faiss::SearchParametersHNSW p; p.efSearch = a.ef; p.sel = &sel;
        index->search(1, &x.queries[size_t(qi) * x.d], x.k, D.data(), I.data(), &p);
      } else if (a.method == "pathseer") {
        faiss::SearchParametersPathSeer p;
        p.efSearch = a.ef; p.M_exp_search = a.M_exp_search; p.sel = &sel;
        index->search(1, &x.queries[size_t(qi) * x.d], x.k, D.data(), I.data(), &p);
      } else {
        faiss::SearchParametersACORN p; p.efSearch = a.ef; p.sel = &sel;
        static_cast<faiss::IndexACORNFlat*>(index)->hybrid_search(
            1, &x.queries[size_t(qi) * x.d], x.k, D.data(), I.data(), &p);
      }
      timings[qi] = (now_s() - s) * 1000.0;
      total += timings[qi];
      for (int t = 0; t < x.k; ++t) ids[size_t(qi) * x.k + t] = int32_t(I[t]);
    }
    if (total < best_total) {
      best_total = total;
      best_timings = timings;
      best_ids = ids;
    }
  }

  long long hits = 0, violations = 0, empty = 0;
  for (int qi = 0; qi < x.nq; ++qi) {
    std::set<int> gt, pred;
    for (int t = 0; t < x.k; ++t) {
      int id = best_ids[size_t(qi) * x.k + t];
      if (id >= 0) {
        pred.insert(id);
        if (!feasible(x, qi, id)) ++violations;
      }
      gt.insert(x.golden[size_t(qi) * x.k + t]);
    }
    if (pred.empty()) ++empty;
    for (int id : pred) if (gt.count(id)) ++hits;
  }
  if (!a.ids_out.empty()) {
    std::ofstream f(a.ids_out, std::ios::binary);
    f.write(reinterpret_cast<const char*>(best_ids.data()), best_ids.size() * sizeof(best_ids[0]));
    if (!f) { std::cerr << "cannot write ids\n"; return 2; }
  }
  if (!a.timings_out.empty()) {
    std::ofstream f(a.timings_out, std::ios::binary);
    f.write(reinterpret_cast<const char*>(best_timings.data()),
            best_timings.size() * sizeof(best_timings[0]));
    if (!f) { std::cerr << "cannot write timings\n"; return 2; }
  }
  std::sort(best_timings.begin(), best_timings.end());
  double p50 = best_timings[best_timings.size() / 2];
  double p95 = best_timings[size_t(best_timings.size() * 0.95)];
  double qps = x.nq / (best_total / 1000.0);
  double recall = double(hits) / double(x.nq * x.k);
  printf("method=%s mode=%s M=%d efc=%d ef=%d M_exp=%d M_exp_search=%d\n",
         a.method.c_str(), a.mode.c_str(), a.M, a.efc, a.ef, a.M_exp, a.M_exp_search);
  printf("build_s=%.3f qps=%.1f recall=%.4f p50_ms=%.4f p95_ms=%.4f "
         "index_mb=%.1f violations=%lld empty=%lld\n",
         build_s, qps, recall, p50, p95, double(index_bytes) / (1024 * 1024),
         violations, empty);
  if (!a.out.empty()) {
    std::ofstream f(a.out);
    f << std::setprecision(12);
    f << "{\"method\":\"" << a.method << "\",\"mode\":\"" << a.mode
      << "\",\"M\":" << a.M << ",\"efc\":" << a.efc << ",\"ef\":" << a.ef
      << ",\"M_exp\":" << a.M_exp << ",\"M_exp_search\":" << a.M_exp_search
      << ",\"build_s\":" << build_s
      << ",\"index_load_seconds\":" << index_load_seconds
      << ",\"metadata_build_seconds\":" << metadata_build_seconds
      << ",\"metadata_validation_seconds\":" << scope.validation_seconds
      << ",\"metadata_bytes\":" << scope.metadata_bytes
      << ",\"index_reused\":" << (!a.index_in.empty() ? "true" : "false")
      << ",\"query_seconds\":" << best_total / 1000.0
      << ",\"qps\":" << qps
      << ",\"recall\":" << recall << ",\"p50_ms\":" << p50
      << ",\"p95_ms\":" << p95 << ",\"index_bytes\":" << index_bytes
      << ",\"violations\":" << violations << ",\"empty\":" << empty << "}\n";
  }
  return 0;
}
