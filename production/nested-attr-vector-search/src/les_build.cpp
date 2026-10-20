// LES prototype builder (Subtree level-0 sharing).
//
// Reads a hierarchy, object labels and a set of per-scope graphs (compact
// inline format c_*.bin), and writes:
//   base_u8.bin                shared uint8 vectors (one copy per object)
//   tin.bin / tout.bin         DFS preorder intervals
//   seed.bin                   one feasible member per node
//   offsets.bin                per-object level-0 bucket offsets (u32)
//   nbr.bin                    neighbour ids (u32)
//   run_offsets.bin            per-object activation-run offsets (u32)
//   run_start.bin              first global edge position of each run (u32)
//   run_act.bin                activation preorder of each run (u16)
//
// A directed edge (i,j) is stored at its minimal activation node(s):
// min(A_i & A_j). Query at u scans the bucket slice tin(u)..tout(u).
//
// This shares level-0 edges across scopes. Upper levels are per scope.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <numeric>
#include <immintrin.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct Args {
  std::string fixtures, index_dir, out;
  size_t max_degree = 0;  // 0 = keep every deduped edge (default)
  size_t max_bucket_degree = 0;  // cap within each (source, activation)
  bool drop_aux = false;  // omit dist16/dflag (measured 0% pruning, diagnostics only)
  bool varint_layout = false;  // delta-varint adjacency per activation run
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
  uint8_t flag;
  bool operator<(const Entry& o) const {
    return i != o.i ? i < o.i : act != o.act ? act < o.act : j < o.j;
  }
  bool operator==(const Entry& o) const {
    return i == o.i && act == o.act && j == o.j && flag == o.flag;
  }
};

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
  for (; i < d; ++i) { int32_t diff = int32_t(a[i]) - int32_t(b[i]); total += diff * diff; }
  return float(total);
}

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
    else if (f == "--max-degree") a.max_degree = std::stoull(next());
    else if (f == "--max-bucket-degree") a.max_bucket_degree = std::stoull(next());
    else if (f == "--drop-aux") a.drop_aux = true;
    else if (f == "--varint") a.varint_layout = true;
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
  std::vector<uint32_t> feasible_count(nodes, 0);
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < L; ++j) {
      int l = labels[size_t(i) * L + j];
      if (l < 0) continue;
      for (int v = l; v >= 0; v = parent[v]) {
        anc[size_t(i) * W + (v >> 6)] |= 1ull << (v & 63);
        if (seed[v] < 0) seed[v] = i;
      }
    }
    for (size_t w = 0; w < W; ++w) {
      uint64_t m = anc[size_t(i) * W + w];
      while (m) {
        int b = __builtin_ctzll(m);
        m &= m - 1;
        ++feasible_count[size_t(w * 64 + b)];
      }
    }
  }

  std::vector<float> base_f = read_bin<float>(a.fixtures + "/base.f32", size_t(n) * dim);
  std::vector<uint8_t> base8(base_f.size());
  for (size_t t = 0; t < base_f.size(); ++t) base8[t] = to_u8(base_f[t]);

  std::string outdir = a.out;
  std::string udir = outdir + "/upper";
  std::system(("mkdir -p " + outdir + " " + udir).c_str());

  auto is_direct = [&](int i, int u) {
    for (int j = 0; j < L; ++j)
      if (labels[size_t(i) * L + j] == u) return true;
    return false;
  };

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
            if (minimal) {
              uint8_t f = (is_direct(int(i), u) && is_direct(int(j), u)) ? 1 : 0;
              entries.push_back({i, uint16_t(tin[u]), j, f});
            }
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

  // Optional diagnostic: cap each object's shared out-degree by keeping its
  // nearest neighbours across activation buckets. This is NOT part of the
  // default LES index; it tests whether degree inflation explains the
  // throughput gap. Slices remain exact w.r.t. the retained edge subset.
  if (a.max_degree > 0) {
    std::vector<Entry> kept;
    kept.reserve(entries.size());
    size_t dropped = 0;
    for (size_t s = 0; s < entries.size();) {
      size_t e = s + 1;
      while (e < entries.size() && entries[e].i == entries[s].i) ++e;
      const size_t count = e - s;
      if (count <= a.max_degree) {
        kept.insert(kept.end(), entries.begin() + s, entries.begin() + e);
      } else {
        std::vector<std::pair<float, Entry>> scored;
        scored.reserve(count);
        for (size_t t = s; t < e; ++t) {
          float d = u8_l2(&base8[size_t(entries[t].i) * dim],
                          &base8[size_t(entries[t].j) * dim], dim);
          scored.push_back({d, entries[t]});
        }
        std::partial_sort(scored.begin(), scored.begin() + a.max_degree,
                          scored.end(), [](const auto& x, const auto& y) {
                            if (x.first != y.first) return x.first < y.first;
                            if (x.second.act != y.second.act)
                              return x.second.act < y.second.act;
                            return x.second.j < y.second.j;
                          });
        std::vector<Entry> chosen;
        chosen.reserve(a.max_degree);
        for (size_t t = 0; t < a.max_degree; ++t) chosen.push_back(scored[t].second);
        std::sort(chosen.begin(), chosen.end());
        kept.insert(kept.end(), chosen.begin(), chosen.end());
        dropped += count - a.max_degree;
      }
      s = e;
    }
    entries.swap(kept);
    std::printf("degree cap %zu: entries %zu, dropped %zu\n",
                a.max_degree, entries.size(), dropped);
  }

  // Cap each (source, activation) bucket independently. Unlike the global cap,
  // this keeps every scope's own local edges and only trims buckets that
  // received more than the per-scope build degree through dedup.
  if (a.max_bucket_degree > 0) {
    std::vector<Entry> kept;
    kept.reserve(entries.size());
    size_t dropped = 0;
    for (size_t s = 0; s < entries.size();) {
      size_t e = s + 1;
      while (e < entries.size() && entries[e].i == entries[s].i &&
             entries[e].act == entries[s].act)
        ++e;
      const size_t count = e - s;
      if (count <= a.max_bucket_degree) {
        kept.insert(kept.end(), entries.begin() + s, entries.begin() + e);
      } else {
        std::vector<std::pair<float, Entry>> scored;
        scored.reserve(count);
        for (size_t t = s; t < e; ++t) {
          float d = u8_l2(&base8[size_t(entries[t].i) * dim],
                          &base8[size_t(entries[t].j) * dim], dim);
          scored.push_back({d, entries[t]});
        }
        std::partial_sort(scored.begin(), scored.begin() + a.max_bucket_degree,
                          scored.end(), [](const auto& x, const auto& y) {
                            if (x.first != y.first) return x.first < y.first;
                            return x.second.j < y.second.j;
                          });
        std::vector<Entry> chosen;
        chosen.reserve(a.max_bucket_degree);
        for (size_t t = 0; t < a.max_bucket_degree; ++t)
          chosen.push_back(scored[t].second);
        std::sort(chosen.begin(), chosen.end());
        kept.insert(kept.end(), chosen.begin(), chosen.end());
        dropped += count - a.max_bucket_degree;
      }
      s = e;
    }
    entries.swap(kept);
    std::printf("bucket cap %zu: entries %zu, dropped %zu\n",
                a.max_bucket_degree, entries.size(), dropped);
  }

  if (a.varint_layout) a.drop_aux = true;  // aux arrays index raw entries
  std::vector<uint32_t> nbr;
  std::vector<uint8_t> nbr_varint;
  std::vector<uint32_t> run_start;
  std::vector<uint16_t> run_act;
  std::vector<uint32_t> run_offsets(size_t(n) + 1, 0);
  std::vector<uint16_t> dist16;
  std::vector<uint8_t> dflag;
  std::vector<uint32_t> offs(size_t(n) + 1, 0);
  for (const Entry& e : entries) ++offs[size_t(e.i) + 1];
  for (size_t i = 0; i < size_t(n); ++i) offs[i + 1] += offs[i];
  if (a.varint_layout) nbr_varint.reserve(entries.size() * 2);
  else nbr.reserve(entries.size());
  for (size_t i = 0; i < size_t(n); ++i) {
    run_offsets[i] = uint32_t(run_start.size());
    uint32_t prev = 0;
    for (size_t t = offs[i]; t < offs[i + 1]; ++t) {
      if (t == offs[i] || entries[t].act != entries[t - 1].act) {
        run_start.push_back(a.varint_layout ? uint32_t(nbr_varint.size())
                                            : uint32_t(nbr.size()));
        run_act.push_back(entries[t].act);
        prev = 0;
      }
      if (a.varint_layout) {
        // Entries are sorted by (source, activation, target), so targets inside
        // one run are strictly increasing: delta-varint is lossless.
        uint32_t delta = entries[t].j - prev;
        prev = entries[t].j;
        while (delta >= 0x80) {
          nbr_varint.push_back(uint8_t(delta | 0x80));
          delta >>= 7;
        }
        nbr_varint.push_back(uint8_t(delta));
      } else {
        nbr.push_back(entries[t].j);
      }
    }
  }
  run_offsets[n] = uint32_t(run_start.size());
  // Sentinel: byte/edge offset just past the last run.
  run_start.push_back(a.varint_layout ? uint32_t(nbr_varint.size())
                                      : uint32_t(nbr.size()));
  if (!a.drop_aux) {
    dist16.resize(entries.size());
    dflag.resize(entries.size());
    for (size_t t = 0; t < entries.size(); ++t) dflag[t] = entries[t].flag;
  }

  // Per-entry quantized distance to the source vector (u16, floor of sqrt of
  // squared L2). Entries stay in (act, ...) order for slicing; the distance is
  // used as an exact triangle-inequality bound at query time.
  {
    for (size_t i = 0; i < size_t(n) && !a.drop_aux; ++i) {
      for (size_t t = offs[i]; t < offs[size_t(i) + 1]; ++t) {
        float d = u8_l2(&base8[i * dim], &base8[size_t(nbr[t]) * dim], dim);
        dist16[t] = uint16_t(std::sqrt(d));
      }
    }
  }

  write_bin(outdir + "/base_u8.bin", base8);
  write_bin(outdir + "/tin.bin", tin);
  write_bin(outdir + "/tout.bin", tout);
  write_bin(outdir + "/seed.bin", seed);
  write_bin(outdir + "/counts.bin", feasible_count);
  write_bin(outdir + "/offsets.bin", offs);
  if (a.varint_layout) {
    write_bin(outdir + "/nbr_varint.bin", nbr_varint);
    std::ofstream flag(outdir + "/layout_varint");
    flag << "1\n";
  } else {
    write_bin(outdir + "/nbr.bin", nbr);
  }
  write_bin(outdir + "/run_offsets.bin", run_offsets);
  write_bin(outdir + "/run_start.bin", run_start);
  write_bin(outdir + "/run_act.bin", run_act);
  if (!a.drop_aux) {
    write_bin(outdir + "/dist16.bin", dist16);
    write_bin(outdir + "/dflag.bin", dflag);
  }

  std::ofstream f(outdir + "/meta.txt");
  f << n << " " << dim << " " << nodes << " " << entries.size() << "\n";

  std::cout << "LES index written to " << outdir << " entries=" << entries.size()
            << " upper_nodes=" << udir << "\n";
  return 0;
}
