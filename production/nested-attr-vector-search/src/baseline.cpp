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

struct Cand {
  float d;
  int id;
};

std::string node_path(const std::string& dir, int v) {
  return dir + "/node_" + std::to_string(v) + ".bin";
}

}  // namespace

int main(int argc, char** argv) {
  Args args = parse(argc, argv);
  // Search breadth for each per-node ANN search. Local indexes contain only
  // feasible points, so no filter is involved.
  const size_t EF = 64;

  if (args.mode == "build") {
    Meta m = read_meta(args.meta);
    TreeData tree;
    tree.load(args.tree);
    std::vector<int32_t> labels = read_bin<int32_t>(args.labels, size_t(m.n) * 3);
    std::vector<float> base = read_bin<float>(args.base, size_t(m.n) * m.dim);

    // Exact feasible set per node: walk each direct label up to the root.
    std::vector<std::vector<uint32_t>> members(m.nodes);
    for (int i = 0; i < m.n; ++i) {
      for (int j = 0; j < 3; ++j) {
        int l = labels[size_t(i) * 3 + j];
        for (int v = l; v >= 0; v = tree.parent[v]) members[v].push_back(uint32_t(i));
      }
    }

    hnswlib::L2Space space(m.dim);
    for (int v = 0; v < m.nodes; ++v) {
      auto& mem = members[v];
      if (mem.empty()) continue;
      std::sort(mem.begin(), mem.end());
      mem.erase(std::unique(mem.begin(), mem.end()), mem.end());
      size_t M = 16;
      size_t ef_construction = mem.size() > 40000 ? 200 : 100;
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
    std::vector<float> queries = read_bin<float>(args.queries, size_t(m.nq) * m.dim);

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
    std::vector<hnswlib::HierarchicalNSW<float>*> indexes(m.nodes, nullptr);
    for (int v = 0; v < m.nodes; ++v) {
      std::ifstream probe(node_path(args.index_dir, v));
      if (!probe.good()) continue;
      probe.close();
      indexes[v] = new hnswlib::HierarchicalNSW<float>(&space, node_path(args.index_dir, v), false, 0);
      indexes[v]->setEf(std::max<size_t>(EF, k));
    }

    std::vector<int32_t> ids(size_t(nq) * k, -1);
    std::vector<double> timings(nq, 0.0);
    std::vector<Cand> cands;

    double best_per_repeat = 1e300;
    for (int rep = 0; rep < args.repeats; ++rep) {
      double total_s = 0.0;
      for (int qi = 0; qi < nq; ++qi) {
        auto t0 = std::chrono::steady_clock::now();
        const float* q = &queries[size_t(qi) * m.dim];
        cands.clear();
        for (int u : qnodes[qi]) {
          if (!indexes[u]) continue;
          auto res = indexes[u]->searchKnn(q, k, nullptr);
          while (!res.empty()) {
            cands.push_back({res.top().first, int(res.top().second)});
            res.pop();
          }
        }
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
          return a.d < b.d || (a.d == b.d && a.id < b.id);
        });
        std::fill(ids.begin() + size_t(qi) * k, ids.begin() + size_t(qi + 1) * k, -1);
        int filled = 0;
        int32_t* row = &ids[size_t(qi) * k];
        for (const Cand& c : cands) {
          bool dup = false;
          for (int t = 0; t < filled; ++t) dup = dup || row[t] == c.id;
          if (dup) continue;
          row[filled++] = c.id;
          if (filled == k) break;
        }
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
