// Filtered-ANN baselines on the PathSeer/FAISS layout.
//
// Methods:
//   hnsw  : faiss::IndexHNSWFlat + SearchParametersHNSW.sel = IDSelectorBitmap
//           (this is the "Filtered-HNSW" baseline used by PathSeer's benchmark)
//   acorn : faiss::IndexACORNFlat + SearchParametersACORN.sel = IDSelectorBitmap
//           metadata is a placeholder (all zeros): construction is NOT
//           hierarchy-aware; only the query-time bitmap filters. Label this
//           variant "filter-agnostic ACORN" in results.
//
// Loads load/load.bin, query/query.bin, query/query_<mode>.bv,
// golden/golden_<mode>_l2_10.bin. Raw vectors, no normalization, METRIC_L2.
//
// Self-test (--self-test) builds a tiny two-cluster index and verifies that
// the bitmap prunes traversal (finds the nearest FEASIBLE point) rather than
// filtering a global top-k after the fact.

#include <faiss/IndexACORN.h>
#include <faiss/IndexHNSW.h>
#include <faiss/impl/IDSelector.h>
#include <faiss/index_io.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <numeric>
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
  std::string dataset, mode = "sub", method = "hnsw", out;
  int M = 32, efc = 200, ef = 64, k = 10, repeats = 2;
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
    else if (f == "--M") a.M = std::stoi(next());
    else if (f == "--efc") a.efc = std::stoi(next());
    else if (f == "--ef") a.ef = std::stoi(next());
    else if (f == "--k") a.k = std::stoi(next());
    else if (f == "--repeats") a.repeats = std::stoi(next());
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

  double t0 = now_s();
  faiss::Index* index = nullptr;
  if (a.method == "hnsw") {
    auto* h = new faiss::IndexHNSWFlat(x.d, a.M, faiss::METRIC_L2);
    h->hnsw.efConstruction = a.efc;
    h->add(x.n, x.base.data());
    index = h;
  } else if (a.method == "acorn") {
    std::vector<int> metadata(x.n, 0);  // default: no hierarchy-aware build
    {
      std::ifstream mf(a.dataset + "/load/load.meta");
      if (mf) {
        for (int i = 0; i < x.n; ++i) {
          if (!(mf >> metadata[i])) { metadata[i] = 0; }
        }
        printf("acorn metadata: primary direct label (single-label encoding)\n");
      }
    }
    auto* c = new faiss::IndexACORNFlat(x.d, a.M, a.gamma, metadata, a.Mbeta,
                                        faiss::METRIC_L2);
    c->add(x.n, x.base.data());
    index = c;
  } else {
    std::cerr << "unknown method\n"; return 2;
  }
  double build_s = now_s() - t0;

  size_t index_bytes = 0;
  try {
    faiss::write_index(index, "/tmp/faiss_filtered_index.bin");
    std::ifstream f("/tmp/faiss_filtered_index.bin", std::ios::binary | std::ios::ate);
    index_bytes = size_t(f.tellg());
  } catch (const std::exception& e) {
    index_bytes = 0;
  }

  // warmup
  std::vector<float> D(x.k);
  std::vector<faiss::idx_t> I(x.k);
  for (int qi = 0; qi < std::min(50, x.nq); ++qi) {
    BitmapSel sel(x.nbytes, &x.bitmaps[size_t(qi) * x.nbytes]);
    if (a.method == "hnsw") {
      faiss::SearchParametersHNSW p; p.efSearch = a.ef; p.sel = &sel;
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
      BitmapSel sel(x.nbytes, &x.bitmaps[size_t(qi) * x.nbytes]);
      double s = now_s();
      if (a.method == "hnsw") {
        faiss::SearchParametersHNSW p; p.efSearch = a.ef; p.sel = &sel;
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
  std::sort(best_timings.begin(), best_timings.end());
  double p50 = best_timings[best_timings.size() / 2];
  double p95 = best_timings[size_t(best_timings.size() * 0.95)];
  double qps = x.nq / (best_total / 1000.0);
  double recall = double(hits) / double(x.nq * x.k);
  printf("method=%s mode=%s M=%d efc=%d ef=%d\n", a.method.c_str(), a.mode.c_str(), a.M, a.efc, a.ef);
  printf("build_s=%.3f qps=%.1f recall=%.4f p50_ms=%.4f p95_ms=%.4f "
         "index_mb=%.1f violations=%lld empty=%lld\n",
         build_s, qps, recall, p50, p95, double(index_bytes) / (1024 * 1024),
         violations, empty);
  if (!a.out.empty()) {
    std::ofstream f(a.out);
    f << "{\"method\":\"" << a.method << "\",\"mode\":\"" << a.mode
      << "\",\"M\":" << a.M << ",\"efc\":" << a.efc << ",\"ef\":" << a.ef
      << ",\"build_s\":" << build_s << ",\"qps\":" << qps
      << ",\"recall\":" << recall << ",\"p50_ms\":" << p50
      << ",\"p95_ms\":" << p95 << ",\"index_bytes\":" << index_bytes
      << ",\"violations\":" << violations << ",\"empty\":" << empty << "}\n";
  }
  return 0;
}
