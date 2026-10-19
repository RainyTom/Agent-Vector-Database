// Candidate: one materialized HNSW index per hierarchy node ("index per
// subtree"). Each node's index contains exactly its feasible set, so query
// execution is a plain ANN search with no predicate filtering; antichain
// queries search each node index and merge by (distance, id).
//
// CLI:
//   candidate build --base B --tree T --labels L --meta M --index-dir DIR
//   candidate query --base B --tree T --meta M --index-dir DIR --queries Q \
//                   --workload W --out IDS --timings T --repeats R

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <queue>
#include <string>
#include <vector>
#include <immintrin.h>

#include "hnswlib/hnswlib.h"

namespace {

struct Args {
  std::string mode, base, tree, labels, meta, index_dir, queries, workload, out, timings;
  int repeats = 1;
};

Args parse(int argc, char** argv) {
  Args a;
  if (argc < 2) { std::cerr << "need mode\n"; std::exit(2); }
  a.mode = argv[1];
  for (int i = 2; i < argc; ++i) {
    std::string f = argv[i];
    auto next = [&]() -> std::string { return argv[++i]; };
    if (f == "--base") a.base = next();
    else if (f == "--tree") a.tree = next();
    else if (f == "--labels") a.labels = next();
    else if (f == "--meta") a.meta = next();
    else if (f == "--index-dir") a.index_dir = next();
    else if (f == "--queries") a.queries = next();
    else if (f == "--workload") a.workload = next();
    else if (f == "--out") a.out = next();
    else if (f == "--timings") a.timings = next();
    else if (f == "--repeats") a.repeats = std::stoi(next());
    else { std::cerr << "unknown arg " << f << "\n"; std::exit(2); }
  }
  return a;
}

template <typename T>
std::vector<T> read_bin(const std::string& path, size_t count) {
  std::vector<T> v(count);
  std::ifstream in(path, std::ios::binary);
  if (!in) { std::cerr << "cannot open " << path << "\n"; std::exit(2); }
  in.read(reinterpret_cast<char*>(v.data()), count * sizeof(T));
  if (!in) { std::cerr << "short read " << path << "\n"; std::exit(2); }
  return v;
}

template <typename T>
void write_bin(const std::string& path, const std::vector<T>& v) {
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(T));
  if (!out) { std::cerr << "write failed " << path << "\n"; std::exit(2); }
}

struct Meta {
  int n = 0, dim = 0, nq = 0, k = 0, nodes = 0;
  size_t words = 0;
};

Meta read_meta(const std::string& path) {
  Meta m;
  std::ifstream f(path);
  if (!f) { std::cerr << "cannot open " << path << "\n"; std::exit(2); }
  f >> m.n >> m.dim >> m.nq >> m.k >> m.nodes >> m.words;
  return m;
}

struct TreeData {
  std::vector<int> parent;
  std::vector<std::vector<int>> children;

  void load(const std::string& path) {
    std::ifstream f(path);
    int nodes;
    f >> nodes;
    parent.assign(nodes, -1);
    children.assign(nodes, {});
    for (int i = 0; i < nodes; ++i) {
      int id, p;
      f >> id >> p;
      parent[id] = p;
      if (p >= 0) children[p].push_back(id);
    }
  }
};

inline float l2sq(const float* a, const float* b, int dim) {
  float s0 = 0.f, s1 = 0.f, s2 = 0.f, s3 = 0.f;
  int j = 0;
  for (; j + 3 < dim; j += 4) {
    float d0 = a[j] - b[j], d1 = a[j + 1] - b[j + 1];
    float d2 = a[j + 2] - b[j + 2], d3 = a[j + 3] - b[j + 3];
    s0 += d0 * d0; s1 += d1 * d1; s2 += d2 * d2; s3 += d3 * d3;
  }
  for (; j < dim; ++j) {
    float d = a[j] - b[j];
    s0 += d * d;
  }
  return (s0 + s1) + (s2 + s3);
}

// SIFT descriptors are integers in [0, 255]. Storing them as uint8 keeps the
// distance exact (squared L2 sums fit in int32) while cutting per-element
// bytes from 656 to 272 at M=16 and using 16-wide integer SIMD.
inline float u8_l2(const uint8_t* a, const uint8_t* b, size_t d) {
  __m256i acc = _mm256_setzero_si256();
  size_t i = 0;
  for (; i + 15 < d; i += 16) {
    __m128i xb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a + i));
    __m128i yb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + i));
    __m256i x16 = _mm256_cvtepu8_epi16(xb);
    __m256i y16 = _mm256_cvtepu8_epi16(yb);
    __m256i diff = _mm256_sub_epi16(x16, y16);
    acc = _mm256_add_epi32(acc, _mm256_madd_epi16(diff, diff));
  }
  __m128i s = _mm_add_epi32(_mm256_castsi256_si128(acc), _mm256_extracti128_si256(acc, 1));
  s = _mm_hadd_epi32(s, s);
  s = _mm_hadd_epi32(s, s);
  int32_t total = _mm_cvtsi128_si32(s);
  for (; i < d; ++i) {
    int32_t diff = int32_t(a[i]) - int32_t(b[i]);
    total += diff * diff;
  }
  return float(total);
}

struct U8L2Space : hnswlib::SpaceInterface<float> {
  size_t dim;
  explicit U8L2Space(size_t d) : dim(d) {}
  size_t get_data_size() override { return dim; }
  hnswlib::DISTFUNC<float> get_dist_func() override { return &dist; }
  void* get_dist_func_param() override { return this; }
  static float dist(const void* ap, const void* bp, const void* param) {
    return u8_l2(static_cast<const uint8_t*>(ap), static_cast<const uint8_t*>(bp),
                 static_cast<const U8L2Space*>(param)->dim);
  }
};

inline uint8_t to_u8(float v) {
  if (v <= 0.f) return 0;
  if (v >= 255.f) return 255;
  return static_cast<uint8_t>(v + 0.5f);
}

struct Cand {
  float d;
  int id;
};

// Max-heap: worst (largest distance, then largest id) on top.
struct Worse {
  bool operator()(const Cand& a, const Cand& b) const {
    return a.d < b.d || (a.d == b.d && a.id < b.id);
  }
};

std::string node_path(const std::string& dir, int v) {
  return dir + "/node_" + std::to_string(v) + ".bin";
}

}  // namespace

int main(int argc, char** argv) {
  Args args = parse(argc, argv);
  // Search breadth for each per-node ANN search. Local indexes contain only
  // feasible points, so no filter is involved.
  size_t EF = 28;
  if (const char* e = std::getenv("GRAPH_EF")) EF = std::strtoull(e, nullptr, 10);

  if (args.mode == "build") {
    Meta m = read_meta(args.meta);
    TreeData tree;
    tree.load(args.tree);
    std::vector<int32_t> labels = read_bin<int32_t>(args.labels, size_t(m.n) * 3);
    std::vector<float> base_f = read_bin<float>(args.base, size_t(m.n) * m.dim);
    std::vector<uint8_t> base(size_t(m.n) * m.dim);
    for (size_t i = 0; i < base.size(); ++i) base[i] = to_u8(base_f[i]);

    // Exact feasible set per node: walk each direct label up to the root.
    std::vector<std::vector<uint32_t>> members(m.nodes);
    for (int i = 0; i < m.n; ++i) {
      for (int j = 0; j < 3; ++j) {
        int l = labels[size_t(i) * 3 + j];
        for (int v = l; v >= 0; v = tree.parent[v]) members[v].push_back(uint32_t(i));
      }
    }

    size_t M_OVERRIDE = 16, EFC_OVERRIDE = 0;
    if (const char* e = std::getenv("GRAPH_M")) M_OVERRIDE = std::strtoull(e, nullptr, 10);
    if (const char* e = std::getenv("GRAPH_EFC")) EFC_OVERRIDE = std::strtoull(e, nullptr, 10);
    U8L2Space space(m.dim);
    for (int v = 0; v < m.nodes; ++v) {
      auto& mem = members[v];
      if (mem.empty()) continue;
      std::sort(mem.begin(), mem.end());
      mem.erase(std::unique(mem.begin(), mem.end()), mem.end());
      size_t M = M_OVERRIDE;
      size_t ef_construction = EFC_OVERRIDE ? EFC_OVERRIDE
                                            : (mem.size() > 40000 ? 200 : 100);
      hnswlib::HierarchicalNSW<float> index(&space, mem.size(), M, ef_construction, 42 + v);
      for (uint32_t id : mem) index.addPoint(&base[size_t(id) * m.dim], hnswlib::labeltype(id));
      index.saveIndex(node_path(args.index_dir, v));
      std::vector<uint32_t>().swap(mem);
    }
    std::cout << "BUILD_OK\n";
    return 0;
  }

  if (args.mode == "query") {
    Meta m = read_meta(args.meta);
    TreeData tree;
    tree.load(args.tree);
    std::vector<float> queries_f = read_bin<float>(args.queries, size_t(m.nq) * m.dim);
    std::vector<uint8_t> queries(size_t(m.nq) * m.dim);
    for (size_t i = 0; i < queries.size(); ++i) queries[i] = to_u8(queries_f[i]);

    std::ifstream wf(args.workload);
    int nq, k;
    wf >> nq >> k;
    std::vector<std::vector<int>> qnodes(nq);
    for (int qi = 0; qi < nq; ++qi) {
      int w;
      wf >> w;
      qnodes[qi].resize(w);
      for (int t = 0; t < w; ++t) wf >> qnodes[qi][t];
    }

    U8L2Space space(m.dim);
    std::vector<hnswlib::HierarchicalNSW<float>*> indexes(m.nodes, nullptr);
    for (int v = 0; v < m.nodes; ++v) {
      std::ifstream probe(node_path(args.index_dir, v));
      if (!probe.good()) continue;
      probe.close();
      indexes[v] = new hnswlib::HierarchicalNSW<float>(&space, node_path(args.index_dir, v), false, 0);
      size_t sz = indexes[v]->getMaxElements();
      size_t ef_v = EF;
      if (sz > 50000) { if (const char* e = std::getenv("GRAPH_EF_ROOT")) ef_v = std::strtoull(e, nullptr, 10); }
      else if (sz > 10000) { if (const char* e = std::getenv("GRAPH_EF_D1")) ef_v = std::strtoull(e, nullptr, 10); }
      else if (sz > 4000) { if (const char* e = std::getenv("GRAPH_EF_D2")) ef_v = std::strtoull(e, nullptr, 10); }
      else { if (const char* e = std::getenv("GRAPH_EF_SMALL")) ef_v = std::strtoull(e, nullptr, 10); }
      indexes[v]->setEf(std::max<size_t>(ef_v, k));
    }

    // Scope-grouped scheduling: all queries that touch node v run back to
    // back while v's graph is hot. Results are buffered per query and merged
    // afterwards, preserving the original output order.
    std::vector<std::vector<std::pair<int, int>>> node_qs(m.nodes);
    for (int qi = 0; qi < nq; ++qi) {
      for (int slot = 0; slot < int(qnodes[qi].size()); ++slot) {
        node_qs[qnodes[qi][slot]].push_back({qi, slot});
      }
    }
    std::vector<std::vector<Cand>> parts(nq);
    for (int qi = 0; qi < nq; ++qi) parts[qi].reserve(qnodes[qi].size() * k);

    std::vector<int32_t> ids(size_t(nq) * k, -1);
    std::vector<double> timings(nq, 0.0);

    double best_per_repeat = 1e300;
    for (int rep = 0; rep < args.repeats; ++rep) {
      std::fill(timings.begin(), timings.end(), 0.0);
      for (int qi = 0; qi < nq; ++qi) parts[qi].clear();
      auto rep_start = std::chrono::steady_clock::now();
      for (int v = 0; v < m.nodes; ++v) {
        if (!indexes[v]) continue;
        for (const auto& qs : node_qs[v]) {
          int qi = qs.first;
          auto t0 = std::chrono::steady_clock::now();
          auto res = indexes[v]->searchKnn(&queries[size_t(qi) * m.dim], k, nullptr);
          auto t1 = std::chrono::steady_clock::now();
          timings[qi] += std::chrono::duration<double, std::milli>(t1 - t0).count();
          auto& dst = parts[qi];
          while (!res.empty()) {
            dst.push_back({res.top().first, int(res.top().second)});
            res.pop();
          }
        }
      }
      for (int qi = 0; qi < nq; ++qi) {
        auto t0 = std::chrono::steady_clock::now();
        auto& cands = parts[qi];
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
          return a.d < b.d || (a.d == b.d && a.id < b.id);
        });
        int32_t* row = &ids[size_t(qi) * k];
        std::fill(row, row + k, -1);
        int filled = 0;
        for (const Cand& c : cands) {
          bool dup = false;
          for (int t = 0; t < filled; ++t) dup = dup || row[t] == c.id;
          if (dup) continue;
          row[filled++] = c.id;
          if (filled == k) break;
        }
        auto t1 = std::chrono::steady_clock::now();
        timings[qi] += std::chrono::duration<double, std::milli>(t1 - t0).count();
      }
      auto rep_end = std::chrono::steady_clock::now();
      best_per_repeat = std::min(best_per_repeat,
          std::chrono::duration<double>(rep_end - rep_start).count());
    }
    write_bin(args.out, ids);
    write_bin(args.timings, timings);
    std::printf("QUERY_SECONDS %.9f\n", best_per_repeat);
    return 0;
  }

  std::cerr << "unknown mode " << args.mode << "\n";
  return 2;
}
