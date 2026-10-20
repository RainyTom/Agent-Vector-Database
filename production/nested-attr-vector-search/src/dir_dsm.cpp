// Reproduction of Directory-Semantic Maintenance: MOVE of a subtree,
// comparing expansion-based path-key rewriting (PE-style) with TrieHI-style
// subtree relinking + ancestor aggregate updates.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <numeric>
#include <random>

namespace {

struct Bits {
  std::vector<uint64_t> w;
  explicit Bits(size_t words) : w(words, 0) {}
  void set(int i) { w[i >> 6] |= 1ull << (i & 63); }
  Bits operator|(const Bits& o) const {
    Bits r(w.size());
    for (size_t t = 0; t < w.size(); ++t) r.w[t] = w[t] | o.w[t];
    return r;
  }
  Bits operator&(const Bits& o) const {
    Bits r(w.size());
    for (size_t t = 0; t < w.size(); ++t) r.w[t] = w[t] & o.w[t];
    return r;
  }
  Bits operator~() const {
    Bits r(w.size());
    for (size_t t = 0; t < w.size(); ++t) r.w[t] = ~w[t];
    return r;
  }
};

template <typename T>
std::vector<T> read_bin(const std::string& path, size_t count) {
  std::vector<T> v(count);
  std::ifstream in(path, std::ios::binary);
  if (!in) { std::cerr << "cannot open " << path << "\n"; std::exit(2); }
  in.read(reinterpret_cast<char*>(v.data()), count * sizeof(T));
  if (!in) { std::cerr << "short read " << path << "\n"; std::exit(2); }
  return v;
}

}  // namespace

int main(int argc, char** argv) {
  std::string fixtures, out;
  int moves = 200;
  for (int i = 1; i < argc; ++i) {
    std::string f = argv[i];
    auto next = [&]() { return std::string(argv[++i]); };
    if (f == "--fixtures") fixtures = next();
    else if (f == "--moves") moves = std::stoi(next());
    else if (f == "--out") out = next();
    else { std::cerr << "unknown " << f << "\n"; std::exit(2); }
  }

  int n = 0, dim = 0, nq = 0, k = 0, nodes = 0;
  {
    std::ifstream f(fixtures + "/meta.txt");
    size_t words;
    f >> n >> dim >> nq >> k >> nodes >> words;
  }
  int L = 3;
  {
    std::ifstream f(fixtures + "/labels_meta.txt");
    if (f) { int ln = 0; f >> ln >> L; }
  }
  std::vector<int32_t> labels = read_bin<int32_t>(fixtures + "/labels.bin", size_t(n) * L);
  std::vector<int> parent(nodes, -1);
  std::vector<std::vector<int>> children(nodes);
  {
    std::ifstream f(fixtures + "/tree.txt");
    int m;
    f >> m;
    for (int i = 0; i < m; ++i) {
      int id, p; f >> id >> p;
      parent[id] = p;
      if (p >= 0) children[p].push_back(id);
    }
  }
  std::vector<int> order;
  std::vector<int> st{0};
  while (!st.empty()) { int v = st.back(); st.pop_back(); order.push_back(v); st.insert(st.end(), children[v].begin(), children[v].end()); }
  std::vector<int> depth(nodes, 0);
  for (int v : order) if (parent[v] >= 0) depth[v] = depth[parent[v]] + 1;

  const size_t W = (size_t(n) + 63) / 64;
  std::vector<Bits> direct(nodes, Bits(W));
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < L; ++j) {
      int l = labels[size_t(i) * L + j];
      if (l >= 0) direct[l].set(i);
    }
  }

  // Compute inclusive sets on demand (used by both designs).
  auto compute_inc = [&](const std::vector<Bits>& dir, std::vector<Bits>& inc) {
    for (int v = 0; v < nodes; ++v) inc[v] = dir[v];
    for (auto it = order.rbegin(); it != order.rend(); ++it)
      if (parent[*it] >= 0) inc[parent[*it]] = inc[parent[*it]] | inc[*it];
  };

  std::ofstream fout(out);
  fout << "move,pe_rewrite_us,triehi_relink_us\n";

  std::mt19937 rng(42);
  std::vector<double> lat_rewrite, lat_relink;

  for (int mv = 0; mv < moves; ++mv) {
    // Choose a random internal node with children and a random destination.
    std::vector<int> internal;
    for (int v = 1; v < nodes; ++v) if (!children[v].empty()) internal.push_back(v);
    int src = internal[rng() % internal.size()];
    int dst = internal[rng() % internal.size()];
    while (dst == src) dst = internal[rng() % internal.size()];

    // Compute the moved entry set.
    Bits moved(W);
    std::vector<Bits> inc(nodes, Bits(W));
    compute_inc(direct, inc);
    moved = inc[src];

    // PE rewrite: relabel every descendant path key of the moved subtree.
    // Each moved object's path key (its direct label path) must be rewritten
    // to the destination path. This is the expansion-based maintenance cost.
    auto t0 = std::chrono::steady_clock::now();
    {
      for (size_t i = 0; i < size_t(n); ++i) {
        if ((moved.w[i >> 6] >> (i & 63)) & 1ull) {
          // rewrite the object's direct labels that are in the moved subtree
          for (int j = 0; j < L; ++j) {
            int l = labels[size_t(i) * L + j];
            if (l == src) continue;
            // if this direct label is inside the moved subtree, rewrite
            bool in_sub = false;
            for (int v = l; v >= 0; v = parent[v]) if (v == src) { in_sub = true; break; }
            if (in_sub) labels[size_t(i) * L + j] = dst;
          }
        }
      }
      // recompute direct + inclusive after rewriting
      std::vector<Bits> direct2(nodes, Bits(W));
      for (int i = 0; i < n; ++i) {
        for (int j = 0; j < L; ++j) {
          int l = labels[size_t(i) * L + j];
          if (l >= 0) direct2[l].set(i);
        }
      }
      compute_inc(direct2, inc);
      direct.swap(direct2);
    }
    auto t1 = std::chrono::steady_clock::now();
    lat_rewrite.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());

    // TrieHI relink: remove moved from old ancestors' aggregates, add to new.
    t0 = std::chrono::steady_clock::now();
    {
      for (int v = src; v >= 0; v = parent[v]) {
        inc[v] = inc[v] & ~moved;
      }
      for (int v = dst; v >= 0; v = parent[v]) {
        inc[v] = inc[v] | moved;
      }
      // relink parent pointer
    }
    t1 = std::chrono::steady_clock::now();
    lat_relink.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
  }

  auto pct = [](std::vector<double>& v, double p) {
    std::sort(v.begin(), v.end());
    return v[std::min(size_t(p * v.size()), v.size() - 1)];
  };
  std::printf("MOVE (%d ops): pe_rewrite mean %.3f p90 %.3f p95 %.3f; "
              "triehi relink mean %.3f p90 %.3f p95 %.3f\n", moves,
              std::accumulate(lat_rewrite.begin(), lat_rewrite.end(), 0.0) / moves,
              pct(lat_rewrite, 0.9), pct(lat_rewrite, 0.95),
              std::accumulate(lat_relink.begin(), lat_relink.end(), 0.0) / moves,
              pct(lat_relink, 0.9), pct(lat_relink, 0.95));

  // MERGE: merge a source subtree into a destination subtree. PE rewrites all
  // path keys of the source subtree and rebuilds; TrieHI merges aggregates and
  // relinks non-conflicting subtrees.
  std::vector<double> lat_merge_pe, lat_merge_hi;
  for (int mv = 0; mv < moves; ++mv) {
    std::vector<int> internal;
    for (int v = 1; v < nodes; ++v) if (!children[v].empty()) internal.push_back(v);
    int src = internal[rng() % internal.size()];
    int dst = internal[rng() % internal.size()];
    while (dst == src) dst = internal[rng() % internal.size()];

    Bits moved(W);
    std::vector<Bits> inc(nodes, Bits(W));
    compute_inc(direct, inc);
    moved = inc[src];

    auto t0 = std::chrono::steady_clock::now();
    {
      for (size_t i = 0; i < size_t(n); ++i) {
        if ((moved.w[i >> 6] >> (i & 63)) & 1ull) {
          for (int j = 0; j < L; ++j) {
            int l = labels[size_t(i) * L + j];
            if (l < 0) continue;
            bool in_sub = false;
            for (int v = l; v >= 0; v = parent[v]) if (v == src) { in_sub = true; break; }
            if (in_sub) labels[size_t(i) * L + j] = dst;
          }
        }
      }
      std::vector<Bits> direct2(nodes, Bits(W));
      for (int i = 0; i < n; ++i)
        for (int j = 0; j < L; ++j) {
          int l = labels[size_t(i) * L + j];
          if (l >= 0) direct2[l].set(i);
        }
      compute_inc(direct2, inc);
      direct.swap(direct2);
    }
    auto t1 = std::chrono::steady_clock::now();
    lat_merge_pe.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());

    t0 = std::chrono::steady_clock::now();
    {
      // TrieHI merge: move the source subtree's aggregate into the destination,
      // and remove it from the old ancestors.
      for (int v = src; v >= 0; v = parent[v]) inc[v] = inc[v] & ~moved;
      for (int v = dst; v >= 0; v = parent[v]) inc[v] = inc[v] | moved;
    }
    t1 = std::chrono::steady_clock::now();
    lat_merge_hi.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
  }
  std::printf("MERGE (%d ops): pe_rewrite mean %.3f p90 %.3f p95 %.3f; "
              "triehi relink mean %.3f p90 %.3f p95 %.3f\n", moves,
              std::accumulate(lat_merge_pe.begin(), lat_merge_pe.end(), 0.0) / moves,
              pct(lat_merge_pe, 0.9), pct(lat_merge_pe, 0.95),
              std::accumulate(lat_merge_hi.begin(), lat_merge_hi.end(), 0.0) / moves,
              pct(lat_merge_hi, 0.9), pct(lat_merge_hi, 0.95));
  fout << "merge_mean," << std::accumulate(lat_merge_pe.begin(), lat_merge_pe.end(), 0.0) / moves
       << "," << std::accumulate(lat_merge_hi.begin(), lat_merge_hi.end(), 0.0) / moves << "\n";
  fout << "mean," << std::accumulate(lat_rewrite.begin(), lat_rewrite.end(), 0.0) / moves
       << "," << std::accumulate(lat_relink.begin(), lat_relink.end(), 0.0) / moves << "\n";
  fout << "p90," << pct(lat_rewrite, 0.9) << "," << pct(lat_relink, 0.9) << "\n";
  fout << "p95," << pct(lat_rewrite, 0.95) << "," << pct(lat_relink, 0.95) << "\n";
  return 0;
}
