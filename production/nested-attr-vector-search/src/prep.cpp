// Deterministic fixture generator for hierarchical (nested attribute) filtered
// vector search.
//
// Produces:
//   base.f32        n x dim float32 (copied subset)
//   test.f32        nq x dim float32 query vectors
//   tree.txt        node count, then "id parent" lines (BFS order, root 0)
//   labels.bin      n x L int32 direct node labels, -1 padded
//   labels_meta.txt "n L"
//   workload.txt    "nq k", then per query "|U| u1 u2 ..."
//   gt.bin          nq x k int32 exact top-k ids (distance asc, id asc)
//   meta.txt        "n dim nq k n_nodes words"
//   stats.txt       per-query scope sizes
//
// Hierarchy: arity 4, depth 3 (85 nodes, 64 leaves). Each object gets a
// primary leaf label chosen by nearest of 64 seed centroids (attributes
// correlate with vector geometry), plus sparse extra leaf/depth-2 labels.
// Queries cover root, depth-1, depth-2, leaf, and antichain (2-3 nodes)
// scopes. Distances are squared L2, tie-break by ascending id.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <numeric>
#include <queue>
#include <random>
#include <string>
#include <vector>

namespace {

struct Args {
  std::string base, test, out;
  int base_n = 100000, dim = 128, test_n = 10000, nq = 1000, k = 10;
  uint64_t seed = 20261020;
};

Args parse(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; ++i) {
    std::string f = argv[i];
    auto next = [&]() -> std::string { return argv[++i]; };
    if (f == "--base") a.base = next();
    else if (f == "--test") a.test = next();
    else if (f == "--out") a.out = next();
    else if (f == "--base-n") a.base_n = std::stoi(next());
    else if (f == "--dim") a.dim = std::stoi(next());
    else if (f == "--test-n") a.test_n = std::stoi(next());
    else if (f == "--nq") a.nq = std::stoi(next());
    else if (f == "--k") a.k = std::stoi(next());
    else if (f == "--seed") a.seed = std::stoull(next());
    else { std::cerr << "unknown arg " << f << "\n"; std::exit(2); }
  }
  if (a.base.empty() || a.test.empty() || a.out.empty()) {
    std::cerr << "usage: prep --base base.f32 --test test.f32 --out DIR "
                 "[--base-n N] [--dim D] [--nq Q] [--k K]\n";
    std::exit(2);
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

// Worst candidate on top: larger distance, then larger id.
struct Worse {
  bool operator()(const Cand& a, const Cand& b) const {
    return a.d < b.d || (a.d == b.d && a.id < b.id);
  }
};

struct Tree {
  int arity = 4, depth = 3;
  std::vector<int> parent;
  std::vector<std::vector<int>> children;
  std::vector<int> level_start, level_size;
  int leaf_start = 0, n_leaves = 0, n_nodes = 0;

  void build() {
    level_start.clear(); level_size.clear();
    int sz = 1, off = 0;
    for (int d = 0; d <= depth; ++d) {
      level_start.push_back(off);
      level_size.push_back(sz);
      off += sz;
      sz *= arity;
    }
    n_nodes = off;
    n_leaves = level_size[depth];
    leaf_start = level_start[depth];
    parent.assign(n_nodes, -1);
    children.assign(n_nodes, {});
    for (int d = 1; d <= depth; ++d) {
      int poff = level_start[d - 1], coff = level_start[d];
      for (int i = 0; i < level_size[d]; ++i) {
        int node = coff + i;
        int p = poff + i / arity;
        parent[node] = p;
        children[p].push_back(node);
      }
    }
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

int count_feasible(const std::vector<int32_t>& labels, int n, int L,
                   const std::vector<char>& in_desc) {
  int cnt = 0;
  for (int i = 0; i < n; ++i) {
    bool ok = false;
    for (int j = 0; j < L; ++j) {
      int l = labels[size_t(i) * L + j];
      if (l >= 0 && in_desc[l]) { ok = true; break; }
    }
    cnt += ok;
  }
  return cnt;
}

}  // namespace

int main(int argc, char** argv) {
  Args args = parse(argc, argv);
  const int n = args.base_n, dim = args.dim, nq = args.nq, k = args.k;
  std::mt19937_64 rng(args.seed);

  std::vector<float> base = read_bin<float>(args.base, size_t(n) * dim);
  std::vector<float> test_all = read_bin<float>(args.test, size_t(args.test_n) * dim);

  Tree tree;
  tree.build();
  const int L = 3;

  // Seed centroids: 64 distinct base rows, one per leaf category.
  std::vector<int> perm(n);
  std::iota(perm.begin(), perm.end(), 0);
  std::shuffle(perm.begin(), perm.end(), rng);
  std::vector<int> centroid_ids(tree.n_leaves);
  for (int c = 0; c < tree.n_leaves; ++c) centroid_ids[c] = perm[c];

  // Direct labels: primary nearest-centroid leaf; sparse extra leaf or depth-2
  // label. Labels are only ever attached to leaf or depth-2 nodes.
  std::vector<int32_t> labels(size_t(n) * L, -1);
  std::uniform_int_distribution<int> leaf_pick(0, tree.n_leaves - 1);
  std::uniform_int_distribution<int> mid_pick(0, tree.level_size[2] - 1);
  const int depth2_start = tree.level_start[2];
  for (int i = 0; i < n; ++i) {
    const float* x = &base[size_t(i) * dim];
    int best = 0;
    float bestd = l2sq(x, &base[size_t(centroid_ids[0]) * dim], dim);
    for (int c = 1; c < tree.n_leaves; ++c) {
      float d = l2sq(x, &base[size_t(centroid_ids[c]) * dim], dim);
      if (d < bestd) { bestd = d; best = c; }
    }
    labels[size_t(i) * L + 0] = tree.leaf_start + best;
    uint64_t roll = rng() % 1000;
    int pos = 1;
    if (roll < 120) {
      labels[size_t(i) * L + pos++] = tree.leaf_start + leaf_pick(rng);
    }
    if (roll < 40 && pos < L) {
      labels[size_t(i) * L + pos++] = depth2_start + mid_pick(rng);
    }
  }

  // Query vectors: distinct test rows.
  std::vector<int> qperm(args.test_n);
  std::iota(qperm.begin(), qperm.end(), 0);
  std::shuffle(qperm.begin(), qperm.end(), rng);
  std::vector<float> queries(size_t(nq) * dim);
  for (int i = 0; i < nq; ++i) {
    std::memcpy(&queries[size_t(i) * dim],
                &test_all[size_t(qperm[i]) * dim], sizeof(float) * dim);
  }

  // Workload: root / depth-1 / depth-2 / leaf / antichain(2) / antichain(3).
  std::vector<std::vector<int>> qnodes(nq);
  std::vector<char> in_desc(tree.n_nodes);
  std::vector<int> leaves_walk(tree.n_leaves);
  std::iota(leaves_walk.begin(), leaves_walk.end(), 0);
  std::vector<int> mids(tree.level_size[2]);
  std::iota(mids.begin(), mids.end(), 0);

  std::uniform_int_distribution<int> uni(0, 999);
  auto pick_leaf = [&](void) { return tree.leaf_start + leaf_pick(rng); };
  auto pick_mid = [&](void) { return depth2_start + mid_pick(rng); };

  for (int qi = 0; qi < nq; ++qi) {
    std::vector<int> U;
    for (int attempt = 0; attempt < 200; ++attempt) {
      U.clear();
      int r = uni(rng);
      if (r < 80) {
        U.push_back(0);
      } else if (r < 280) {
        U.push_back(tree.level_start[1] + (rng() % tree.level_size[1]));
      } else if (r < 660) {
        U.push_back(pick_mid());
      } else if (r < 820) {
        U.push_back(pick_leaf());
      } else if (r < 970) {
        // Two leaves in distinct depth-1 subtrees: antichain, overlapping only
        // through multi-label objects.
        int per = tree.level_size[3] / tree.level_size[1];
        int s1 = rng() % tree.level_size[1];
        int s2 = (s1 + 1 + rng() % (tree.level_size[1] - 1)) % tree.level_size[1];
        U.push_back(tree.level_start[3] + s1 * per + (rng() % per));
        U.push_back(tree.level_start[3] + s2 * per + (rng() % per));
      } else {
        int s[4];
        for (int t = 0; t < 4; ++t) s[t] = t;
        std::shuffle(s, s + 4, rng);
        int per = tree.level_size[3] / tree.level_size[1];
        for (int t = 0; t < 3; ++t) {
          U.push_back(tree.level_start[3] + s[t] * per + (rng() % per));
        }
      }
      std::fill(in_desc.begin(), in_desc.end(), 0);
      for (int u : U) tree.mark_descendants(u, in_desc);
      if (count_feasible(labels, n, L, in_desc) >= 64) break;
      U.clear();
    }
    if (U.empty()) U.push_back(0);
    qnodes[qi] = U;
  }

  // Exact ground truth.
  std::vector<int32_t> gt(size_t(nq) * k, -1);
  std::vector<int> scope_sizes(nq);
  for (int qi = 0; qi < nq; ++qi) {
    std::fill(in_desc.begin(), in_desc.end(), 0);
    for (int u : qnodes[qi]) tree.mark_descendants(u, in_desc);
    const float* q = &queries[size_t(qi) * dim];
    std::priority_queue<Cand, std::vector<Cand>, Worse> heap;
    int cnt = 0;
    for (int i = 0; i < n; ++i) {
      bool ok = false;
      for (int j = 0; j < L; ++j) {
        int l = labels[size_t(i) * L + j];
        if (l >= 0 && in_desc[l]) { ok = true; break; }
      }
      if (!ok) continue;
      ++cnt;
      float d = l2sq(q, &base[size_t(i) * dim], dim);
      if (int(heap.size()) < k) {
        heap.push({d, i});
      } else {
        Cand worst = heap.top();
        if (d < worst.d || (d == worst.d && i < worst.id)) {
          heap.pop();
          heap.push({d, i});
        }
      }
    }
    scope_sizes[qi] = cnt;
    std::vector<Cand> out;
    while (!heap.empty()) { out.push_back(heap.top()); heap.pop(); }
    std::reverse(out.begin(), out.end());
    for (int j = 0; j < k && j < int(out.size()); ++j)
      gt[size_t(qi) * k + j] = out[j].id;
  }

  // Write fixtures.
  std::string outdir = args.out;
  write_bin(outdir + "/base.f32", base);
  write_bin(outdir + "/test.f32", queries);
  write_bin(outdir + "/labels.bin", labels);
  write_bin(outdir + "/gt.bin", gt);

  {
    std::ofstream f(outdir + "/tree.txt");
    f << tree.n_nodes << "\n";
    for (int v = 0; v < tree.n_nodes; ++v) f << v << " " << tree.parent[v] << "\n";
  }
  {
    std::ofstream f(outdir + "/labels_meta.txt");
    f << n << " " << L << "\n";
  }
  {
    std::ofstream f(outdir + "/workload.txt");
    f << nq << " " << k << "\n";
    for (int qi = 0; qi < nq; ++qi) {
      f << qnodes[qi].size();
      for (int u : qnodes[qi]) f << " " << u;
      f << "\n";
    }
  }
  {
    std::ofstream f(outdir + "/meta.txt");
    size_t words = (size_t(n) + 63) / 64;
    f << n << " " << dim << " " << nq << " " << k << " " << tree.n_nodes << " " << words << "\n";
  }
  {
    std::ofstream f(outdir + "/stats.txt");
    f << "query\twidth\tscope\n";
    for (int qi = 0; qi < nq; ++qi)
      f << qi << "\t" << qnodes[qi].size() << "\t" << scope_sizes[qi] << "\n";
  }

  std::cout << "prep n=" << n << " nq=" << nq << " nodes=" << tree.n_nodes
            << " leaves=" << tree.n_leaves << "\n";
  return 0;
}
