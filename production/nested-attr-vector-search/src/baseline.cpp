// Baseline candidate for the hierarchical filtered top-k benchmark.
//
// Two modes, selected per query:
//   * scope popcount <= SCAN_THRESHOLD: exact brute-force scan over the
//     feasible set (guaranteed exact, bounded work).
//   * larger scope: global HNSW with an id-allowance filter; ef is sized from
//     the scope selectivity and doubled until k results are found.
//
// CLI:
//   baseline build --base B --tree T --labels L --index-dir DIR
//   baseline query --base B --tree T --index-dir DIR --queries Q \
//                  --workload W --out IDS --timings T --repeats R
//
// Query mode prints "QUERY_SECONDS <median>" for the timed repeat.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <numeric>
#include <queue>
#include <string>
#include <vector>

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
  std::vector<char> in_desc;

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
    in_desc.assign(nodes, 0);
  }

  void mark_descendants(int u, std::vector<char>& mark) const {
    std::vector<int> stack{u};
    while (!stack.empty()) {
      int v = stack.back(); stack.pop_back();
      mark[v] = 1;
      for (int c : children[v]) stack.push_back(c);
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

inline int popcount_bits(const uint64_t* bits, int words) {
  int c = 0;
  for (int w = 0; w < words; ++w) c += __builtin_popcountll(bits[w]);
  return c;
}

struct BitsFilter : hnswlib::BaseFilterFunctor {
  const uint64_t* bits;
  explicit BitsFilter(const uint64_t* b) : bits(b) {}
  bool operator()(hnswlib::labeltype id) override {
    return (bits[id >> 6] >> (id & 63)) & 1ULL;
  }
};

struct Cand {
  float d;
  int id;
};
struct Worse {
  bool operator()(const Cand& a, const Cand& b) const {
    return a.d < b.d || (a.d == b.d && a.id < b.id);
  }
};

void topk_exact(const float* q, const float* base, int dim, const uint64_t* bits,
                int words, int k, std::vector<std::pair<float, int>>& out) {
  std::priority_queue<Cand, std::vector<Cand>, Worse> heap;
  for (int w = 0; w < words; ++w) {
    uint64_t x = bits[w];
    while (x) {
      int b = __builtin_ctzll(x);
      x &= x - 1;
      int id = w * 64 + b;
      float d = l2sq(q, base + size_t(id) * dim, dim);
      if (int(heap.size()) < k) {
        heap.push({d, id});
      } else {
        Cand worst = heap.top();
        if (d < worst.d || (d == worst.d && id < worst.id)) {
          heap.pop();
          heap.push({d, id});
        }
      }
    }
  }
  out.clear();
  while (!heap.empty()) { out.push_back({heap.top().d, heap.top().id}); heap.pop(); }
  std::reverse(out.begin(), out.end());
}

}  // namespace

int main(int argc, char** argv) {
  Args args = parse(argc, argv);
  int SCAN_THRESHOLD = 16000;
  if (const char* e = std::getenv("FILTERED_SCAN_THRESHOLD")) SCAN_THRESHOLD = std::atoi(e);

  if (args.mode == "build") {
    Meta m = read_meta(args.meta);
    TreeData tree;
    tree.load(args.tree);
    std::vector<int32_t> labels = read_bin<int32_t>(args.labels, size_t(m.n) * 3);
    std::vector<float> base = read_bin<float>(args.base, size_t(m.n) * m.dim);

    // Scope masks: bit i set iff object i has a direct label inside the
    // subtree rooted at this node.
    std::vector<uint64_t> masks(size_t(m.nodes) * m.words, 0);
    for (int i = 0; i < m.n; ++i) {
      for (int j = 0; j < 3; ++j) {
        int l = labels[size_t(i) * 3 + j];
        if (l >= 0) masks[size_t(l) * m.words + (i >> 6)] |= 1ULL << (i & 63);
      }
    }
    for (int v = m.nodes - 1; v > 0; --v) {
      int p = tree.parent[v];
      uint64_t* dst = &masks[size_t(p) * m.words];
      const uint64_t* src = &masks[size_t(v) * m.words];
      for (size_t w = 0; w < m.words; ++w) dst[w] |= src[w];
    }
    write_bin(args.index_dir + "/masks.bin", masks);

    hnswlib::L2Space space(m.dim);
    hnswlib::HierarchicalNSW<float> index(&space, m.n, 16, 200, 42);
    for (int i = 0; i < m.n; ++i) index.addPoint(&base[size_t(i) * m.dim], hnswlib::labeltype(i));
    index.saveIndex(args.index_dir + "/hnsw.bin");
    std::cout << "BUILD_OK\n";
    return 0;
  }

  if (args.mode == "query") {
    Meta m = read_meta(args.meta);
    TreeData tree;
    tree.load(args.tree);
    std::vector<float> base = read_bin<float>(args.base, size_t(m.n) * m.dim);
    std::vector<float> queries = read_bin<float>(args.queries, size_t(m.nq) * m.dim);
    std::vector<uint64_t> masks = read_bin<uint64_t>(args.index_dir + "/masks.bin",
                                                     size_t(m.nodes) * m.words);

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

    hnswlib::L2Space space(m.dim);
    auto* index = new hnswlib::HierarchicalNSW<float>(&space, args.index_dir + "/hnsw.bin", false, m.n);

    std::vector<uint64_t> bits(m.words);
    std::vector<int32_t> ids(size_t(nq) * k, -1);
    std::vector<double> timings(nq, 0.0);
    std::vector<std::pair<float, int>> cands;

    double best_per_repeat = 1e300;
    for (int rep = 0; rep < args.repeats; ++rep) {
      double total_s = 0.0;
      for (int qi = 0; qi < nq; ++qi) {
        auto t0 = std::chrono::steady_clock::now();
        std::fill(bits.begin(), bits.end(), 0);
        for (int u : qnodes[qi]) {
          const uint64_t* src = &masks[size_t(u) * m.words];
          for (size_t w = 0; w < m.words; ++w) bits[w] |= src[w];
        }
        int cnt = popcount_bits(bits.data(), int(m.words));
        const float* q = &queries[size_t(qi) * m.dim];
        cands.clear();
        if (cnt <= SCAN_THRESHOLD) {
          topk_exact(q, base.data(), m.dim, bits.data(), int(m.words), k, cands);
        } else {
          double sel = double(cnt) / double(m.n);
          size_t ef = std::max<size_t>(64, size_t(std::ceil(1.5 * k / sel)));
          if (ef > size_t(m.n)) ef = m.n;
          BitsFilter filter(bits.data());
          hnswlib::AlgorithmInterface<float>::DistanceLabelPriorityQueue res;
          while (true) {
            index->setEf(ef);
            res = index->searchKnn(q, k, &filter);
            if (res.size() >= size_t(k) || ef >= size_t(m.n)) break;
            ef *= 2;
          }
          while (!res.empty()) {
            cands.push_back({res.top().first, int(res.top().second)});
            res.pop();
          }
          std::sort(cands.begin(), cands.end(), [](const auto& a, const auto& b) {
            return a.first < b.first || (a.first == b.first && a.second < b.second);
          });
        }
        std::fill(ids.begin() + size_t(qi) * k, ids.begin() + size_t(qi + 1) * k, -1);
        for (int j = 0; j < k && j < int(cands.size()); ++j)
          ids[size_t(qi) * k + j] = cands[j].second;
        auto t1 = std::chrono::steady_clock::now();
        timings[qi] = std::chrono::duration<double, std::milli>(t1 - t0).count();
        total_s += timings[qi] / 1000.0;
      }
      best_per_repeat = std::min(best_per_repeat, total_s);
    }
    write_bin(args.out, ids);
    write_bin(args.timings, timings);
    std::printf("QUERY_SECONDS %.9f\n", best_per_repeat);
    return 0;
  }

  std::cerr << "unknown mode " << args.mode << "\n";
  return 2;
}
