// Offline analysis for Laminar Edge Sharing (LES).
//
// Reads a hierarchy, object labels and a set of per-scope graphs (compact
// format), and computes:
//   * observed directed edges (sum of per-scope degrees) vs unique edges;
//   * the activation mask of each unique edge (nodes where both endpoints are
//     feasible and the edge appears);
//   * for every tree node, the mean slice degree if edges were stored once and
//     queries scanned the union of buckets in subtree(u).
//
// This quantifies the space/work tradeoff of sharing edges across ancestor and
// descendant scopes instead of materializing one graph per scope.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

struct Args {
  std::string meta, tree, labels, index_dir;
};

Args parse(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; ++i) {
    std::string f = argv[i];
    auto next = [&]() { return std::string(argv[++i]); };
    if (f == "--meta") a.meta = next();
    else if (f == "--tree") a.tree = next();
    else if (f == "--labels") a.labels = next();
    else if (f == "--index-dir") a.index_dir = next();
    else { std::cerr << "unknown " << f << "\n"; std::exit(2); }
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
bool read_exact(std::ifstream& in, std::vector<T>& v) {
  in.read(reinterpret_cast<char*>(v.data()), v.size() * sizeof(T));
  return bool(in);
}

// Two-word bitmask over tree nodes.
struct Mask {
  uint64_t w[2] = {0, 0};
  void set(int i) { w[i >> 6] |= 1ull << (i & 63); }
  bool test(int i) const { return (w[i >> 6] >> (i & 63)) & 1ull; }
  Mask operator&(const Mask& o) const {
    Mask m; m.w[0] = w[0] & o.w[0]; m.w[1] = w[1] & o.w[1]; return m;
  }
  int popcount() const {
    return __builtin_popcountll(w[0]) + __builtin_popcountll(w[1]);
  }
};

}  // namespace

int main(int argc, char** argv) {
  Args a = parse(argc, argv);

  // meta: n dim nq k nodes words
  int n = 0, dim = 0, nq = 0, k = 0, nodes = 0;
  size_t words = 0;
  {
    std::ifstream f(a.meta);
    f >> n >> dim >> nq >> k >> nodes >> words;
  }
  int labels_n = 0, L = 0;
  {
    std::ifstream f(a.labels);  // labels_meta.txt
    std::string path = a.labels;
    auto slash = path.find_last_of('/');
    std::string meta2 = path.substr(0, slash + 1) + "labels_meta.txt";
    std::ifstream g(meta2);
    g >> labels_n >> L;
  }
  std::vector<int> parent(nodes, -1);
  std::vector<std::vector<int>> children(nodes);
  {
    std::ifstream f(a.tree);
    int m;
    f >> m;
    for (int i = 0; i < m; ++i) {
      int id, p; f >> id >> p;
      parent[id] = p;
      if (p >= 0) children[p].push_back(id);
    }
  }
  std::vector<int32_t> labels = read_bin<int32_t>(a.labels, size_t(n) * L);

  // Per-object ancestor closure (upward-closed feasible-node set).
  std::vector<Mask> anc(n);
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < L; ++j) {
      int v = labels[size_t(i) * L + j];
      for (; v >= 0; v = parent[v]) anc[i].set(v);
    }
  }

  // Per-node feasibility counts and slice-degree matrix.
  std::vector<uint32_t> feasible(nodes, 0);
  for (int i = 0; i < n; ++i)
    for (int u = 0; u < nodes; ++u)
      if (anc[i].test(u)) ++feasible[u];

  std::vector<uint16_t> deg(size_t(nodes) * n, 0);

  std::unordered_set<uint64_t> unique_edges;
  unique_edges.reserve(1 << 22);
  uint64_t observed = 0, bad_edges = 0, dup = 0, bucket_entries = 0;
  std::vector<uint64_t> per_scope_deg_sum(nodes, 0);
  std::vector<uint32_t> per_scope_elems(nodes, 0);

  for (int v = 0; v < nodes; ++v) {
    std::string path = a.index_dir + "/c_" + std::to_string(v) + ".bin";
    std::ifstream in(path, std::ios::binary);
    if (!in) continue;
    uint32_t hdr[7];
    in.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
    uint32_t m = hdr[0], cap = hdr[3];
    size_t stride = hdr[5];
    uint32_t tsize = hdr[6];
    if (m == 0) continue;
    std::vector<uint32_t> gid(m);
    std::vector<uint8_t> lv(m), blocks(size_t(m) * stride);
    std::vector<uint32_t> upper_off(m), off;  // off unused in fixed-stride
    read_exact(in, gid);
    read_exact(in, lv);
    read_exact(in, upper_off);
    read_exact(in, blocks);
    per_scope_elems[v] = m;
    for (uint32_t t = 0; t < m; ++t) {
      uint32_t i = gid[t];
      const uint8_t* blk = &blocks[size_t(t) * stride];
      uint32_t cnt;
      std::vector<uint32_t> nbrs;
      if (tsize == 2) {
        const uint16_t* slot = reinterpret_cast<const uint16_t*>(blk + dim);
        cnt = slot[0];
        for (uint32_t j = 0; j < cnt; ++j) nbrs.push_back(slot[1 + j]);
      } else {
        const uint32_t* slot = reinterpret_cast<const uint32_t*>(blk + dim);
        cnt = slot[0];
        for (uint32_t j = 0; j < cnt; ++j) nbrs.push_back(slot[1 + j]);
      }
      per_scope_deg_sum[v] += cnt;
      for (uint32_t nb : nbrs) {
        uint32_t j = gid[nb];
        ++observed;
        Mask inter = anc[i] & anc[j];
        if (inter.popcount() == 0) { ++bad_edges; continue; }
        uint64_t key = (uint64_t(i) << 32) | j;
        if (!unique_edges.insert(key).second) { ++dup; continue; }
        for (int u = 0; u < nodes; ++u) {
          if (!inter.test(u)) continue;
          ++deg[size_t(u) * n + i];
          bool minimal = true;
          for (int c : children[u]) {
            if (inter.test(c)) { minimal = false; break; }
          }
          if (minimal) ++bucket_entries;
        }
      }
    }
  }

  std::printf("objects %d, nodes %d, dim %d\n", n, nodes, dim);
  std::printf("observed_directed_edges %llu\n", (unsigned long long)observed);
  std::printf("unique_directed_edges %zu\n", unique_edges.size());
  std::printf("duplicate_fraction %.3f\n",
              observed ? double(dup) / double(observed) : 0.0);
  std::printf("inconsistent_edges %llu\n", (unsigned long long)bad_edges);
  std::printf("bucket_entries %llu (multiplicity %.3f)\n",
              (unsigned long long)bucket_entries,
              unique_edges.empty() ? 0.0
                                   : double(bucket_entries) / unique_edges.size());
  std::printf("per_scope_mean_degree:");
  for (int v = 0; v < nodes; ++v) {
    if (!per_scope_elems[v]) continue;
    std::printf(" %.1f", double(per_scope_deg_sum[v]) / per_scope_elems[v]);
    if (v > 6) break;
  }
  std::printf("\nslice_mean_degree (node: mean over feasible objects):\n");
  int shown = 0;
  for (int v = 0; v < nodes && shown < 12; ++v) {
    if (!feasible[v]) continue;
    uint64_t sum = 0;
    for (int i = 0; i < n; ++i)
      if (anc[i].test(v)) sum += deg[size_t(v) * n + i];
    std::printf("  node %d feasible %u slice_mean_deg %.2f\n", v, feasible[v],
                double(sum) / feasible[v]);
    ++shown;
  }
  return 0;
}
