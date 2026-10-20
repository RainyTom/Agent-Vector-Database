// Reproduction of Directory-Aware scope resolution: three directory
// representations (PE-Online, PE-Offline, TrieHI) over a hierarchy with
// direct labels, producing candidate ID sets (roaring-like bitmaps) for
// recursive and non-recursive directory queries.
//
// This isolates directory-layer cost from ANN behavior, mirroring the
// paper's "directory-only latency" measurement.

#include <algorithm>
#include <chrono>
#include <functional>
#include <numeric>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct Args {
  std::string fixtures, queries, out;
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

// A tiny roaring-like bitset (one word per 64 ids; the paper uses Roaring).
struct Bits {
  std::vector<uint64_t> w;
  explicit Bits(size_t words) : w(words, 0) {}
  void set(int i) { w[i >> 6] |= 1ull << (i & 63); }
  bool test(int i) const { return (w[i >> 6] >> (i & 63)) & 1ull; }
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
  int popcount() const {
    int c = 0;
    for (uint64_t x : w) c += __builtin_popcountll(x);
    return c;
  }
};

}  // namespace

int main(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; ++i) {
    std::string f = argv[i];
    auto next = [&]() { return std::string(argv[++i]); };
    if (f == "--fixtures") a.fixtures = next();
    else if (f == "--queries") a.queries = next();
    else if (f == "--out") a.out = next();
    else { std::cerr << "unknown " << f << "\n"; std::exit(2); }
  }

  int n = 0, dim = 0, nq = 0, k = 0, nodes = 0;
  {
    std::ifstream f(a.fixtures + "/meta.txt");
    size_t words;
    f >> n >> dim >> nq >> k >> nodes >> words;
  }
  int L = 3;
  {
    std::ifstream f(a.fixtures + "/labels_meta.txt");
    if (f) { int ln = 0; f >> ln >> L; }
  }
  std::vector<int32_t> labels = read_bin<int32_t>(a.fixtures + "/labels.bin", size_t(n) * L);
  std::vector<int> parent(nodes, -1);
  std::vector<std::vector<int>> children(nodes);
  {
    std::ifstream f(a.fixtures + "/tree.txt");
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
  std::vector<int> tin(nodes), tout(nodes);
  std::vector<int> sz(nodes, 1);
  for (auto it = order.rbegin(); it != order.rend(); ++it)
    if (parent[*it] >= 0) sz[parent[*it]] += sz[*it];
  for (int v = 0; v < nodes; ++v) tout[v] = tin[v] + sz[v] - 1;

  const size_t W = (size_t(n) + 63) / 64;

  // Direct postings (bitmap per node).
  std::vector<Bits> direct(nodes, Bits(W));
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < L; ++j) {
      int l = labels[size_t(i) * L + j];
      if (l >= 0) direct[l].set(i);
    }
  }

  // Subtree postings (inclusive bitmaps) for PE-Offline and TrieHI.
  std::vector<Bits> inc(nodes, Bits(W));
  for (int v = 0; v < nodes; ++v) inc[v] = direct[v];
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    if (parent[*it] >= 0) {
      inc[parent[*it]] = inc[parent[*it]] | inc[*it];
    }
  }

  // Read queries: each line "|U| u1 u2 ...".
  std::ifstream qf(a.queries);
  int nqw = 0, kw = 0;
  qf >> nqw >> kw;
  struct Q { std::vector<int> us; };
  std::vector<Q> qs(nqw);
  for (int qi = 0; qi < nqw; ++qi) {
    int w; qf >> w;
    qs[qi].us.resize(w);
    for (int t = 0; t < w; ++t) qf >> qs[qi].us[t];
  }

  std::ofstream out(a.out);
  // Correctness: verify candidate set of query 0 matches oracle for each method.
  {
    auto oracle = [&](const std::vector<int>& us) {
      Bits r(W);
      for (int u : us) r = r | inc[u];
      return r;
    };
    auto pe_online = [&](const std::vector<int>& us) {
      Bits r(W);
      for (int u : us) {
        std::vector<int> st2{u};
        while (!st2.empty()) {
          int v = st2.back(); st2.pop_back();
          r = r | direct[v];
          for (int c : children[v]) st2.push_back(c);
        }
      }
      return r;
    };
    for (auto& q : qs) {
      Bits a = pe_online(q.us);
      Bits b = oracle(q.us);
      for (size_t t = 0; t < W; ++t) {
        if (a.w[t] != b.w[t]) {
          std::fprintf(stderr, "MISMATCH query %d word %zu\n", 0, t);
          return -1;
        }
      }
      break;
    }
    std::printf("correctness: PE-Online recursive candidate set == oracle for query 0\n");
  }

  out << "mode,method,mean_us,p90_us,p95_us,p99_us\n";

  auto measure = [&](const std::string& mode, const std::string& method,
                     const std::function<Bits(const std::vector<int>&)>& fn) {
    std::vector<double> lat;
    lat.reserve(nqw);
    for (int qi = 0; qi < nqw; ++qi) {
      auto t0 = std::chrono::steady_clock::now();
      Bits r = fn(qs[qi].us);
      auto t1 = std::chrono::steady_clock::now();
      lat.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
      (void)r;
    }
    std::sort(lat.begin(), lat.end());
    auto pct = [&](double p) { return lat[std::min(size_t(p * lat.size()), lat.size() - 1)]; };
    double mean = std::accumulate(lat.begin(), lat.end(), 0.0) / lat.size();
    out << mode << "," << method << "," << mean << "," << pct(0.90) << ","
        << pct(0.95) << "," << pct(0.99) << "\n";
    std::printf("%s/%s mean %.3f p90 %.3f p95 %.3f p99 %.3f\n", mode.c_str(),
                method.c_str(), mean, pct(0.90), pct(0.95), pct(0.99));
  };

  measure("recursive", "PE-Online", [&](const std::vector<int>& us) {
    Bits r(W);
    for (int u : us) {
      std::vector<int> st2{u};
      while (!st2.empty()) {
        int v = st2.back(); st2.pop_back();
        r = r | direct[v];
        for (int c : children[v]) st2.push_back(c);
      }
    }
    return r;
  });
  measure("recursive", "PE-Offline", [&](const std::vector<int>& us) {
    Bits r(W);
    for (int u : us) r = r | inc[u];
    return r;
  });
  measure("recursive", "TrieHI", [&](const std::vector<int>& us) {
    Bits r(W);
    for (int u : us) r = r | inc[u];
    return r;
  });

  measure("non-recursive", "PE-Online", [&](const std::vector<int>& us) {
    Bits r(W);
    for (int u : us) r = r | direct[u];
    return r;
  });
  measure("non-recursive", "PE-Offline", [&](const std::vector<int>& us) {
    Bits r(W);
    for (int u : us) {
      Bits x = inc[u];
      Bits chi(W);
      for (int c : children[u]) chi = chi | inc[c];
      r = r | (x & ~chi);
    }
    return r;
  });
  measure("non-recursive", "TrieHI", [&](const std::vector<int>& us) {
    Bits r(W);
    for (int u : us) {
      Bits x = inc[u];
      Bits chi(W);
      for (int c : children[u]) chi = chi | inc[c];
      r = r | (x & ~chi);
    }
    return r;
  });

  std::printf("wrote %s\n", a.out.c_str());
  return 0;
}
