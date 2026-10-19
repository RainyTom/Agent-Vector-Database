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

// ---------------------------------------------------------------------------
// Inline compact graph: uint16/uint32 adjacency followed by the uint8 vector in
// the same per-element block. Keeps hnswlib's vector/link co-location while
// removing the 4-byte neighbour ids and the size_t label.
// ---------------------------------------------------------------------------
template <typename T>
struct InlineGraph {
  uint32_t m = 0, maxlevel = 0, enter = 0, M = 0, dim = 0;
  size_t stride = 0;
  std::vector<uint32_t> gid;
  std::vector<uint8_t> lv;
  std::vector<uint32_t> upper_off;
  std::vector<uint8_t> blocks;
  std::vector<T> up_adj;
  std::vector<uint32_t> visited;
  uint32_t gen = 0;
};

template <typename T>
size_t inline_vec_off(uint32_t M) { return sizeof(T) + size_t(2 * M) * sizeof(T); }

template <typename T>
void write_inline_graph(const hnswlib::HierarchicalNSW<float>& idx, uint32_t m,
                        uint32_t M, uint32_t dim, const std::string& path) {
  InlineGraph<T> g;
  g.m = m; g.M = M; g.dim = dim;
  g.maxlevel = uint32_t(idx.maxlevel_ < 0 ? 0 : idx.maxlevel_);
  g.enter = uint32_t(idx.enterpoint_node_ < 0 ? 0 : idx.enterpoint_node_);
  g.stride = inline_vec_off<T>(M) + dim;
  g.gid.resize(m);
  g.lv.resize(m);
  g.upper_off.assign(m, 0);
  g.blocks.assign(size_t(m) * g.stride, 0);
  size_t voff = inline_vec_off<T>(M);
  for (uint32_t t = 0; t < m; ++t) {
    g.gid[t] = uint32_t(idx.getExternalLabel(t));
    g.lv[t] = uint8_t(idx.element_levels_[t]);
    uint8_t* blk = &g.blocks[size_t(t) * g.stride];
    auto* lk = idx.get_linklist_at_level(t, 0);
    uint32_t cnt = *lk;
    auto* ids = reinterpret_cast<uint32_t*>(lk + 1);
    T* slot = reinterpret_cast<T*>(blk);
    slot[0] = T(cnt);
    for (uint32_t j = 0; j < cnt; ++j) slot[1 + j] = T(ids[j]);
    std::memcpy(blk + voff, idx.getDataByInternalId(t), dim);
    if (g.lv[t] > 0) {
      g.upper_off[t] = uint32_t(g.up_adj.size());
      for (int level = 1; level <= int(g.lv[t]); ++level) {
        auto* ul = idx.get_linklist_at_level(t, level);
        uint32_t c = *ul;
        auto* uids = reinterpret_cast<uint32_t*>(ul + 1);
        g.up_adj.push_back(T(c));
        for (uint32_t j = 0; j < c; ++j) g.up_adj.push_back(T(uids[j]));
      }
    }
  }
  std::ofstream out(path, std::ios::binary);
  uint32_t hdr[7] = {m, g.maxlevel, g.enter, M, dim, uint32_t(g.stride), uint32_t(sizeof(T))};
  out.write(reinterpret_cast<char*>(hdr), sizeof(hdr));
  out.write(reinterpret_cast<char*>(g.gid.data()), g.gid.size() * 4);
  out.write(reinterpret_cast<char*>(g.lv.data()), g.lv.size());
  out.write(reinterpret_cast<char*>(g.upper_off.data()), g.upper_off.size() * 4);
  out.write(reinterpret_cast<char*>(g.blocks.data()), g.blocks.size());
  uint32_t upn = uint32_t(g.up_adj.size());
  out.write(reinterpret_cast<char*>(&upn), 4);
  out.write(reinterpret_cast<char*>(g.up_adj.data()), g.up_adj.size() * sizeof(T));
}

template <typename T>
bool load_inline_graph(const std::string& path, InlineGraph<T>& g) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  uint32_t hdr[7];
  in.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
  if (!in) return false;
  g.m = hdr[0]; g.maxlevel = hdr[1]; g.enter = hdr[2];
  g.M = hdr[3]; g.dim = hdr[4]; g.stride = hdr[5];
  g.gid.resize(g.m);
  g.lv.resize(g.m);
  g.upper_off.resize(g.m);
  g.blocks.resize(size_t(g.m) * g.stride);
  in.read(reinterpret_cast<char*>(g.gid.data()), g.gid.size() * 4);
  in.read(reinterpret_cast<char*>(g.lv.data()), g.lv.size());
  in.read(reinterpret_cast<char*>(g.upper_off.data()), g.upper_off.size() * 4);
  in.read(reinterpret_cast<char*>(g.blocks.data()), g.blocks.size());
  uint32_t upn = 0;
  in.read(reinterpret_cast<char*>(&upn), 4);
  g.up_adj.resize(upn);
  in.read(reinterpret_cast<char*>(g.up_adj.data()), g.up_adj.size() * sizeof(T));
  g.visited.assign(g.m, 0);
  return bool(in);
}

template <typename T>
inline const T* inline_upper_block(const InlineGraph<T>& g, uint32_t id, int level) {
  uint32_t off = g.upper_off[id];
  for (int l = 1; l < level; ++l) {
    uint32_t c = g.up_adj[off];
    off += 1 + c;
  }
  return &g.up_adj[off];
}

template <typename T>
void search_inline(InlineGraph<T>& g, const uint8_t* q, size_t ef, int k,
                   std::vector<Cand>& out) {
  if (g.m == 0) return;
  if (++g.gen == 0) {
    std::fill(g.visited.begin(), g.visited.end(), 0);
    g.gen = 1;
  }
  const uint32_t gen = g.gen;
  // Prefetch the vector+adjacency block of every neighbour before the
  // distance loop; random block access is the dominant cold-graph cost.
  constexpr int prefetch = 3;
  const bool descent_prefetch = std::getenv("GRAPH_DESCENT_PREFETCH") != nullptr;
  const size_t voff = inline_vec_off<T>(g.M);
  auto dist = [&](uint32_t id) {
    return u8_l2(q, &g.blocks[size_t(id) * g.stride + voff], g.dim);
  };

  uint32_t ep = g.enter;
  float d = dist(ep);
  for (int level = int(g.maxlevel); level >= 1; --level) {
    bool changed = true;
    while (changed) {
      changed = false;
      const T* p = inline_upper_block(g, ep, level);
      uint32_t c = p[0];
      if (descent_prefetch) {
        for (uint32_t j = 0; j < c; ++j) {
          const uint8_t* bp = &g.blocks[size_t(p[1 + j]) * g.stride];
          __builtin_prefetch(bp, 0, 1);
          __builtin_prefetch(bp + 64, 0, 1);
          __builtin_prefetch(bp + 128, 0, 1);
        }
      }
      for (uint32_t j = 0; j < c; ++j) {
        uint32_t nb = p[1 + j];
        float dn = dist(nb);
        if (dn < d) { d = dn; ep = nb; changed = true; }
      }
    }
  }

  static thread_local std::vector<Cand> top;
  static thread_local std::vector<std::pair<float, uint32_t>> cand;
  top.clear(); cand.clear();
  auto top_push = [&](Cand c) {
    top.push_back(c); std::push_heap(top.begin(), top.end(), Worse());
  };
  auto top_pop = [&]() {
    std::pop_heap(top.begin(), top.end(), Worse()); top.pop_back();
  };
  auto cand_push = [&](std::pair<float, uint32_t> c) {
    cand.push_back(c); std::push_heap(cand.begin(), cand.end(), std::greater<>());
  };
  auto cand_pop = [&]() {
    std::pop_heap(cand.begin(), cand.end(), std::greater<>());
    auto c = cand.back(); cand.pop_back(); return c;
  };

  g.visited[ep] = gen;
  top_push({d, int(ep)});
  cand_push({d, ep});
  while (!cand.empty()) {
    auto cd = cand.front();
    if (top.size() >= ef && cd.first > top.front().d) break;
    cand_pop();
    uint32_t c = cd.second;
    const T* slot = reinterpret_cast<const T*>(&g.blocks[size_t(c) * g.stride]);
    uint32_t cnt = slot[0];
    if (prefetch) {
      for (uint32_t j = 0; j < cnt; ++j) {
        uint32_t nb = slot[1 + j];
        const uint8_t* bp = &g.blocks[size_t(nb) * g.stride];
        __builtin_prefetch(bp, 0, 1);
        __builtin_prefetch(bp + 64, 0, 1);
        __builtin_prefetch(bp + 128, 0, 1);
      }
    }
    for (uint32_t j = 0; j < cnt; ++j) {
      uint32_t nb = slot[1 + j];
      if (g.visited[nb] == gen) continue;
      g.visited[nb] = gen;
      float dn = dist(nb);
      if (top.size() < ef || dn < top.front().d) {
        cand_push({dn, nb});
        top_push({dn, int(nb)});
        if (top.size() > ef) top_pop();
      }
    }
  }

  out.clear();
  for (const Cand& c : top) out.push_back({c.d, int(g.gid[c.id])});
  std::sort(out.begin(), out.end(), [](const Cand& a, const Cand& b) {
    return a.d < b.d || (a.d == b.d && a.id < b.id);
  });
  if (int(out.size()) > k) out.resize(k);
}



}  // namespace

int main(int argc, char** argv) {
  Args args = parse(argc, argv);
  // Search breadth for each per-node ANN search. Local indexes contain only
  // feasible points, so no filter is involved.
  size_t EF = 20;
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

    size_t M_OVERRIDE = 24, EFC_OVERRIDE = 200;
    if (const char* e = std::getenv("GRAPH_M")) M_OVERRIDE = std::strtoull(e, nullptr, 10);
    if (const char* e = std::getenv("GRAPH_EFC")) EFC_OVERRIDE = std::strtoull(e, nullptr, 10);
    // Optional locality ordering: project all objects on the first principal
    // component once, then insert each node's members in projection order so
    // graph neighbours occupy nearby addresses.
    // Locality ordering: project objects on the first principal component
    // (power iteration on the centred uint8 data), then insert each node's
    // members in projection order so graph neighbours sit in nearby addresses.
    std::vector<uint32_t> order_key(m.n, 0);
    {
      std::vector<double> mean(m.dim, 0.0);
      for (int i = 0; i < m.n; ++i)
        for (int j = 0; j < m.dim; ++j) mean[j] += base[size_t(i) * m.dim + j];
      for (int j = 0; j < m.dim; ++j) mean[j] /= m.n;
      std::vector<double> v(m.dim, 1.0 / std::sqrt(double(m.dim)));
      for (int it = 0; it < 12; ++it) {
        std::vector<double> w(m.dim, 0.0);
        for (int i = 0; i < m.n; ++i) {
          double dot = 0.0;
          for (int j = 0; j < m.dim; ++j)
            dot += (base[size_t(i) * m.dim + j] - mean[j]) * v[j];
          for (int j = 0; j < m.dim; ++j)
            w[j] += dot * (base[size_t(i) * m.dim + j] - mean[j]);
        }
        double nrm = 0.0;
        for (int j = 0; j < m.dim; ++j) nrm += w[j] * w[j];
        nrm = std::sqrt(nrm);
        if (nrm == 0.0) break;
        for (int j = 0; j < m.dim; ++j) v[j] = w[j] / nrm;
      }
      std::vector<float> p1(m.n, 0.f);
      float lo = 1e30f, hi = -1e30f;
      for (int i = 0; i < m.n; ++i) {
        double dot = 0.0;
        for (int j = 0; j < m.dim; ++j)
          dot += (base[size_t(i) * m.dim + j] - mean[j]) * v[j];
        p1[i] = float(dot);
        lo = std::min(lo, p1[i]);
        hi = std::max(hi, p1[i]);
      }
      float span = std::max(1e-9f, hi - lo);
      for (int i = 0; i < m.n; ++i)
        order_key[i] = uint32_t((p1[i] - lo) / span * 1e6f);
    }

    U8L2Space space(m.dim);
    for (int v = 0; v < m.nodes; ++v) {
      auto& mem = members[v];
      if (mem.empty()) continue;
      std::sort(mem.begin(), mem.end());
      mem.erase(std::unique(mem.begin(), mem.end()), mem.end());
      std::sort(mem.begin(), mem.end(), [&](uint32_t a, uint32_t b) {
        if (order_key[a] != order_key[b]) return order_key[a] < order_key[b];
        return a < b;
      });
      size_t M = M_OVERRIDE;
      size_t ef_construction = EFC_OVERRIDE;
      hnswlib::HierarchicalNSW<float> index(&space, mem.size(), M, ef_construction, 42 + v);
      for (uint32_t id : mem) index.addPoint(&base[size_t(id) * m.dim], hnswlib::labeltype(id));
      std::string cpath = args.index_dir + "/c_" + std::to_string(v) + ".bin";
      if (mem.size() <= 65535)
        write_inline_graph<uint16_t>(index, uint32_t(mem.size()), uint32_t(M), uint32_t(m.dim), cpath);
      else
        write_inline_graph<uint32_t>(index, uint32_t(mem.size()), uint32_t(M), uint32_t(m.dim), cpath);
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

    std::vector<InlineGraph<uint16_t>> ci16(m.nodes);
    std::vector<InlineGraph<uint32_t>> ci32(m.nodes);
    std::vector<size_t> node_ef(m.nodes, EF);
    for (int v = 0; v < m.nodes; ++v) {
      std::string cp = args.index_dir + "/c_" + std::to_string(v) + ".bin";
      std::ifstream probe(cp, std::ios::binary);
      if (!probe.good()) continue;
      uint32_t hdr[7];
      probe.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
      probe.close();
      size_t sz = hdr[0];
      if (hdr[6] == 2) load_inline_graph<uint16_t>(cp, ci16[v]);
      else load_inline_graph<uint32_t>(cp, ci32[v]);
      size_t ef_v = EF;
      if (sz > 50000) { if (const char* e = std::getenv("GRAPH_EF_ROOT")) ef_v = std::strtoull(e, nullptr, 10); }
      else if (sz > 10000) { if (const char* e = std::getenv("GRAPH_EF_D1")) ef_v = std::strtoull(e, nullptr, 10); }
      else if (sz > 4000) { if (const char* e = std::getenv("GRAPH_EF_D2")) ef_v = std::strtoull(e, nullptr, 10); }
      else { if (const char* e = std::getenv("GRAPH_EF_SMALL")) ef_v = std::strtoull(e, nullptr, 10); }
      node_ef[v] = std::max<size_t>(ef_v, k);
    }

    int loaded = 0;
    for (int v = 0; v < m.nodes; ++v) loaded += (ci16[v].m != 0 || ci32[v].m != 0);
    if (loaded == 0) {
      std::cerr << "no node indexes found in " << args.index_dir << "\n";
      return 2;
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
      std::vector<Cand> res;
      for (int v = 0; v < m.nodes; ++v) {
        const bool narrow = (ci16[v].m != 0);
        if (!narrow && ci32[v].m == 0) continue;
        for (const auto& qs : node_qs[v]) {
          int qi = qs.first;
          const uint8_t* q = &queries[size_t(qi) * m.dim];
          auto t0 = std::chrono::steady_clock::now();
          if (narrow) search_inline<uint16_t>(ci16[v], q, node_ef[v], k, res);
          else search_inline<uint32_t>(ci32[v], q, node_ef[v], k, res);
          auto t1 = std::chrono::steady_clock::now();
          timings[qi] += std::chrono::duration<double, std::milli>(t1 - t0).count();
          auto& dst = parts[qi];
          for (const Cand& c : res) dst.push_back(c);
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
