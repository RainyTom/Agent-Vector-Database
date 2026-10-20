// LES prototype builder (Subtree level-0 sharing).
//
// Reads a hierarchy, object labels and a set of per-scope graphs (compact
// inline format c_*.bin), and writes:
//   base_u8.bin                shared uint8 vectors (one copy per object)
//   tin.bin / tout.bin         DFS preorder intervals
//   seed.bin                   one feasible member per node
//   buckets/offsets.bin        per-object level-0 bucket offsets (u32)
//   buckets/nbr.bin            neighbour ids (u32)
//   buckets/act.bin            activation preorder (u16)
//   upper/up_<v>.bin           per-scope upper-level structure
//
// A directed edge (i,j) is stored at its minimal activation node(s):
// min(A_i & A_j). Query at u scans the bucket slice tin(u)..tout(u).
//
// This shares level-0 edges across scopes. Upper levels are per scope.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct Args {
  std::string fixtures, index_dir, out;
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

template <typename T>
bool read_exact(std::ifstream& in, std::vector<T>& v) {
  in.read(reinterpret_cast<char*>(v.data()), v.size() * sizeof(T));
  return bool(in);
}

template <typename T>
void write_bin(const std::string& path, const std::vector<T>& v) {
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(T));
  if (!out) { std::cerr << "write failed " << path << "\n"; std::exit(2); }
}

struct Mask {
  std::vector<uint64_t> w;
  explicit Mask(size_t words) : w(words, 0) {}
  void set(int i) { w[i >> 6] |= 1ull << (i & 63); }
  bool test(int i) const { return (w[i >> 6] >> (i & 63)) & 1ull; }
};

struct Entry {
  uint32_t i;
  uint16_t act;
  uint32_t j;
  bool operator<(const Entry& o) const {
    return i != o.i ? i < o.i : act != o.act ? act < o.act : j < o.j;
  }
  bool operator==(const Entry& o) const {
    return i == o.i && act == o.act && j == o.j;
  }
};

inline uint8_t to_u8(float v) {
  if (v <= 0.f) return 0;
  if (v >= 255.f) return 255;
  return static_cast<uint8_t>(v + 0.5f);
}

}  // namespace

int main(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; ++i) {
    std::string f = argv[i];
    auto next = [&]() { return std::string(argv[++i]); };
    if (f == "--fixtures") a.fixtures = next();
    else if (f == "--index-dir") a.index_dir = next();
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
  // Proper preorder DFS: tin is a pre-order timestamp; a node's subtree is the
  // contiguous interval [tin(v), tout(v)].
  std::vector<int> tin(nodes), tout(nodes);
  {
    int timer = 0;
    std::vector<std::pair<int, int>> stack;
    stack.push_back({0, 0});
    while (!stack.empty()) {
      auto& [v, idx] = stack.back();
      if (idx == 0) tin[v] = timer++;
      if (idx < int(children[v].size())) {
        int c = children[v][idx++];
        stack.push_back({c, 0});
      } else {
        tout[v] = timer - 1;
        stack.pop_back();
      }
    }
  }

  const size_t W = (size_t(nodes) + 63) / 64;
  std::vector<uint64_t> anc(size_t(n) * W, 0);
  auto Atest = [&](int i, int u) { return (anc[size_t(i) * W + (u >> 6)] >> (u & 63)) & 1ull; };
  std::vector<int32_t> seed(nodes, -1);
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < L; ++j) {
      int l = labels[size_t(i) * L + j];
      if (l < 0) continue;
      for (int v = l; v >= 0; v = parent[v]) {
        anc[size_t(i) * W + (v >> 6)] |= 1ull << (v & 63);
        if (seed[v] < 0) seed[v] = i;
      }
    }
  }

  std::vector<float> base_f = read_bin<float>(a.fixtures + "/base.f32", size_t(n) * dim);
  std::vector<uint8_t> base8(base_f.size());
  for (size_t t = 0; t < base_f.size(); ++t) base8[t] = to_u8(base_f[t]);

  std::string outdir = a.out;
  std::string udir = outdir + "/upper";
  std::system(("mkdir -p " + outdir + " " + udir).c_str());

  std::vector<Entry> entries;
  entries.reserve(1 << 22);
  uint64_t observed = 0, dup = 0;

  for (int v = 0; v < nodes; ++v) {
    std::string path = a.index_dir + "/c_" + std::to_string(v) + ".bin";
    std::ifstream in(path, std::ios::binary);
    if (!in) continue;
    uint32_t hdr[7];
    in.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
    uint32_t m = hdr[0], maxlevel = hdr[1], enter = hdr[2];
    size_t stride = hdr[5];
    uint32_t tsize = hdr[6];
    if (m == 0) continue;
    std::vector<uint32_t> gid(m), upper_off(m);
    std::vector<uint8_t> lv(m), blocks(size_t(m) * stride);
    read_exact(in, gid);
    read_exact(in, lv);
    read_exact(in, upper_off);
    read_exact(in, blocks);
    uint32_t upn = 0;
    in.read(reinterpret_cast<char*>(&upn), 4);

    // write per-scope upper structure
    {
      std::ofstream up(udir + "/up_" + std::to_string(v) + ".bin", std::ios::binary);
      uint32_t h[5] = {m, maxlevel, enter, tsize, upn};
      up.write(reinterpret_cast<char*>(h), sizeof(h));
      up.write(reinterpret_cast<char*>(gid.data()), gid.size() * 4);
      up.write(reinterpret_cast<char*>(lv.data()), lv.size());
      up.write(reinterpret_cast<char*>(upper_off.data()), upper_off.size() * 4);
    }
    // upper adjacency is also needed; re-read from the same file after blocks
    std::vector<uint8_t> upbytes(upn * tsize);
    in.read(reinterpret_cast<char*>(upbytes.data()), upbytes.size());
    {
      std::ofstream up(udir + "/up_" + std::to_string(v) + ".bin",
                       std::ios::binary | std::ios::app);
      up.write(reinterpret_cast<char*>(upbytes.data()), upbytes.size());
    }

    // level-0 edges -> activation buckets
    for (uint32_t t = 0; t < m; ++t) {
      uint32_t i = gid[t];
      const uint8_t* blk = &blocks[size_t(t) * stride];
      uint32_t cnt = 0;
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
      for (uint32_t nb : nbrs) {
        uint32_t j = gid[nb];
        ++observed;
        // Intersection mask and minimal-activation nodes, iterating only set
        // bits (each edge has at most ~depth minimal candidates).
        for (size_t w = 0; w < W; ++w) {
          uint64_t mi = anc[size_t(i) * W + w] & anc[size_t(j) * W + w];
          while (mi) {
            int b = __builtin_ctzll(mi);
            mi &= mi - 1;
            int u = int(w * 64) + b;
            bool minimal = true;
            for (int c : children[u]) {
              if (Atest(i, c) && Atest(j, c)) { minimal = false; break; }
            }
            if (minimal) entries.push_back({i, uint16_t(tin[u]), j});
          }
        }
      }
    }
  }
  std::printf("observed level-0 edges %llu, entries before dedup %zu\n",
              (unsigned long long)observed, entries.size());
  std::sort(entries.begin(), entries.end());
  auto uit = std::unique(entries.begin(), entries.end());
  dup = entries.end() - uit;
  entries.erase(uit, entries.end());
  std::printf("unique bucket entries %zu (dup %llu)\n",
              entries.size(), (unsigned long long)dup);

  // offsets per object
  std::vector<uint32_t> nbr;
  std::vector<uint16_t> act;
  std::vector<uint32_t> offs(size_t(n) + 1, 0);
  for (const Entry& e : entries) ++offs[size_t(e.i) + 1];
  for (size_t i = 0; i < size_t(n); ++i) offs[i + 1] += offs[i];
  nbr.reserve(entries.size());
  act.reserve(entries.size());
  for (const Entry& e : entries) { nbr.push_back(e.j); act.push_back(e.act); }

  write_bin(outdir + "/base_u8.bin", base8);
  write_bin(outdir + "/tin.bin", tin);
  write_bin(outdir + "/tout.bin", tout);
  write_bin(outdir + "/seed.bin", seed);
  write_bin(outdir + "/offsets.bin", offs);
  write_bin(outdir + "/nbr.bin", nbr);
  write_bin(outdir + "/act.bin", act);

  std::ofstream f(outdir + "/meta.txt");
  f << n << " " << dim << " " << nodes << " " << entries.size() << "\n";

  std::cout << "LES index written to " << outdir << " entries=" << entries.size()
            << " upper_nodes=" << udir << "\n";
  return 0;
}
