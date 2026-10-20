// LES prototype query engine (Subtree mode).
//
// Loads the LES index (shared level-0 buckets + per-scope upper levels) and
// runs the workload. Per query: descend the scope's own upper levels, then a
// beam search over the union level-0 graph, enumerating only the bucket slice
// [tin(u), tout(u)]. No label/predicate checks in the hot path.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <queue>
#include <string>
#include <vector>
#include <immintrin.h>

namespace {

struct Args {
  std::string outdir, queries, workload, out, timings;
  std::string mode = "subtree";
  int k = 10, repeats = 3;
  size_t ef = 64;
  bool adaptive = false;
};

Args parse(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; ++i) {
    std::string f = argv[i];
    auto next = [&]() { return std::string(argv[++i]); };
    if (f == "--out-dir") a.outdir = next();
    else if (f == "--queries") a.queries = next();
    else if (f == "--workload") a.workload = next();
    else if (f == "--out") a.out = next();
    else if (f == "--timings") a.timings = next();
    else if (f == "--mode") a.mode = next();
    else if (f == "--k") a.k = std::stoi(next());
    else if (f == "--ef") a.ef = std::stoull(next());
    else if (f == "--repeats") a.repeats = std::stoi(next());
    else if (f == "--adaptive") a.adaptive = true;
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

std::vector<uint8_t> read_bytes(const std::string& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) { std::cerr << "cannot open " << path << "\n"; std::exit(2); }
  size_t bytes = size_t(in.tellg());
  in.seekg(0);
  std::vector<uint8_t> v(bytes);
  in.read(reinterpret_cast<char*>(v.data()), bytes);
  return v;
}

template <typename T>
void write_bin(const std::string& path, const std::vector<T>& v) {
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(T));
  if (!out) { std::cerr << "write failed " << path << "\n"; std::exit(2); }
}

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

struct Cand { float d; int id; };
struct Worse {
  bool operator()(const Cand& a, const Cand& b) const {
    return a.d < b.d || (a.d == b.d && a.id < b.id);
  }
};

struct Upper {
  uint32_t m = 0, maxlevel = 0, enter = 0, tsize = 0, upn = 0;
  std::vector<uint32_t> gid, upper_off;
  std::vector<uint8_t> lv;
  std::vector<uint8_t> up_bytes;  // raw adjacency (tsize-sized ids)

  bool empty() const { return m == 0; }
};

// Level-l adjacency block of an element, in the per-scope upper structure.
template <typename T>
const T* upper_block(const Upper& U, uint32_t id, int level) {
  uint32_t off = U.upper_off[id];
  const T* p = reinterpret_cast<const T*>(U.up_bytes.data());
  for (int l = 1; l < level; ++l) {
    uint32_t c = p[off];
    off += 1 + c;
  }
  return p + off;
}

}  // namespace

int main(int argc, char** argv) {
  Args a = parse(argc, argv);

  // meta.txt written by les_build: n dim nodes nentries
  int n = 0, dim = 0, nodes = 0;
  size_t nentries = 0;
  {
    std::ifstream f(a.outdir + "/meta.txt");
    f >> n >> dim >> nodes >> nentries;
  }
  std::vector<int> tin = read_bin<int>(a.outdir + "/tin.bin", nodes);
  std::vector<int> tout = read_bin<int>(a.outdir + "/tout.bin", nodes);
  std::vector<int32_t> seed = read_bin<int32_t>(a.outdir + "/seed.bin", nodes);
  std::vector<uint32_t> offs = read_bin<uint32_t>(a.outdir + "/offsets.bin", size_t(n) + 1);
  const bool varint_layout = std::ifstream(a.outdir + "/layout_varint").good();
  std::vector<uint32_t> nbr;
  std::vector<uint8_t> nbr_varint;
  if (varint_layout) {
    nbr_varint = read_bytes(a.outdir + "/nbr_varint.bin");
  } else {
    nbr = read_bin<uint32_t>(a.outdir + "/nbr.bin", nentries);
  }
  std::vector<uint32_t> run_offsets = read_bin<uint32_t>(a.outdir + "/run_offsets.bin", size_t(n) + 1);
  const size_t nruns = run_offsets[n];
  std::vector<uint32_t> run_start = read_bin<uint32_t>(
      a.outdir + "/run_start.bin", nruns + (varint_layout ? 1 : 0));
  std::vector<uint16_t> run_act = read_bin<uint16_t>(a.outdir + "/run_act.bin", nruns);
  std::vector<uint8_t> dflag;
  {
    std::ifstream probe(a.outdir + "/dflag.bin", std::ios::binary);
    if (probe.good()) {
      probe.close();
      dflag = read_bin<uint8_t>(a.outdir + "/dflag.bin", nentries);
    }
  }
  // Labels are needed to verify exact membership for exact_strict mode.
  std::vector<int32_t> labels;
  int labL = 0;
  {
    std::string fix = a.outdir.substr(0, a.outdir.find("/les_ccs"));
    if (!fix.empty() && fix != a.outdir) {
      std::ifstream lm(fix + "/fixtures_ccs/labels_meta.txt");
      if (lm) { int ln = 0; lm >> ln >> labL; }
      labels = read_bin<int32_t>(fix + "/fixtures_ccs/labels.bin", size_t(n) * 5);
    }
  }
  auto is_direct_lab = [&](int i, int u) -> bool {
    if (labels.empty()) return true;
    for (int j = 0; j < labL; ++j)
      if (labels[size_t(i) * labL + j] == u) return true;
    return false;
  };
  std::vector<uint16_t> dist16;
  {
    std::ifstream probe(a.outdir + "/dist16.bin", std::ios::binary);
    if (probe.good()) {
      probe.close();
      dist16 = read_bin<uint16_t>(a.outdir + "/dist16.bin", nentries);
    }
  }
  std::vector<uint32_t> counts;
  {
    std::ifstream probe(a.outdir + "/counts.bin", std::ios::binary);
    if (probe.good()) {
      probe.close();
      counts = read_bin<uint32_t>(a.outdir + "/counts.bin", nodes);
    }
  }
  std::vector<uint8_t> base8 = read_bin<uint8_t>(a.outdir + "/base_u8.bin", size_t(n) * dim);

  std::ifstream qf(a.queries, std::ios::binary);
  uint32_t nq = 0, dq = 0;
  qf.read(reinterpret_cast<char*>(&nq), 4);
  qf.read(reinterpret_cast<char*>(&dq), 4);
  std::vector<float> queries_f(size_t(nq) * dq);
  qf.read(reinterpret_cast<char*>(queries_f.data()), queries_f.size() * 4);
  std::vector<uint8_t> queries(queries_f.size());
  for (size_t i = 0; i < queries.size(); ++i) {
    float v = queries_f[i];
    queries[i] = v <= 0.f ? 0 : v >= 255.f ? 255 : uint8_t(v + 0.5f);
  }

  std::ifstream wf(a.workload);
  int nqw, kw;
  wf >> nqw >> kw;
  std::vector<std::vector<int>> qnodes(nqw);
  for (int qi = 0; qi < nqw; ++qi) {
    int w; wf >> w;
    qnodes[qi].resize(w);
    for (int t = 0; t < w; ++t) wf >> qnodes[qi][t];
  }

  // Load per-scope upper structures.
  std::vector<Upper> ups(nodes);
  for (int v = 0; v < nodes; ++v) {
    std::ifstream in(a.outdir + "/upper/up_" + std::to_string(v) + ".bin", std::ios::binary);
    if (!in) continue;
    uint32_t h[5];
    in.read(reinterpret_cast<char*>(h), sizeof(h));
    Upper& U = ups[v];
    U.m = h[0]; U.maxlevel = h[1]; U.enter = h[2]; U.tsize = h[3]; U.upn = h[4];
    U.gid.resize(U.m);
    U.lv.resize(U.m);
    U.upper_off.resize(U.m);
    in.read(reinterpret_cast<char*>(U.gid.data()), U.gid.size() * 4);
    in.read(reinterpret_cast<char*>(U.lv.data()), U.lv.size());
    in.read(reinterpret_cast<char*>(U.upper_off.data()), U.upper_off.size() * 4);
    U.up_bytes.resize(size_t(U.upn) * U.tsize);
    in.read(reinterpret_cast<char*>(U.up_bytes.data()), U.up_bytes.size());
  }

  std::vector<uint32_t> visited(n, 0);
  uint32_t gen = 0;

  std::vector<int32_t> ids(size_t(nq) * a.k, -1);
  std::vector<double> timings(nq, 0.0);
  double best = 1e300;
  std::vector<Cand> cands;
  uint64_t pruned = 0, scanned = 0, distcomp = 0;

  for (int rep = 0; rep < a.repeats; ++rep) {
    double total_s = 0.0;
    for (int qi = 0; qi < nq; ++qi) {
      auto t0 = std::chrono::steady_clock::now();
      const uint8_t* q = &queries[size_t(qi) * dim];
      size_t ef_q = a.ef;
      if (a.adaptive && !counts.empty()) {
        size_t size = 0;
        for (int u : qnodes[qi]) size += counts[size_t(u)];
        if (size < 500) ef_q = std::max<size_t>(a.k, a.ef / 2);
        else if (size < 10000) ef_q = std::max<size_t>(a.k, (a.ef * 2) / 3);
        else ef_q = a.ef;
      }
      cands.clear();
      for (int u : qnodes[qi]) {
        if (seed[u] < 0) continue;
        const bool exact_strict = (a.mode == "exact_strict");
        const bool exact_edges = exact_strict || (a.mode == "exact_edges");
        if (exact_strict && !is_direct_lab(seed[u], u)) continue;
        const Upper& U = ups[u];
        if (U.empty()) continue;
        const int lo = tin[u], hi = tout[u];

        // Upper-level descent within the scope's own graph.
        uint32_t ep = U.enter;
        float d = u8_l2(q, &base8[size_t(U.gid[ep]) * dim], dim);
        for (int level = int(U.maxlevel); level >= 1; --level) {
          bool changed = true;
          while (changed) {
            changed = false;
            if (U.tsize == 4) {
              const uint32_t* p = upper_block<uint32_t>(U, ep, level);
              uint32_t c = p[0];
              for (uint32_t j = 0; j < c; ++j) {
                uint32_t nb = p[1 + j];
                float dn = u8_l2(q, &base8[size_t(U.gid[nb]) * dim], dim);
                if (dn < d) { d = dn; ep = nb; changed = true; }
              }
            } else {
              const uint16_t* p = upper_block<uint16_t>(U, ep, level);
              uint32_t c = p[0];
              for (uint32_t j = 0; j < c; ++j) {
                uint32_t nb = p[1 + j];
                float dn = u8_l2(q, &base8[size_t(U.gid[nb]) * dim], dim);
                if (dn < d) { d = dn; ep = nb; changed = true; }
              }
            }
          }
        }
        uint32_t start = U.gid[ep];

        // Level-0 beam over the union graph's bucket slice.
        if (++gen == 0) { std::fill(visited.begin(), visited.end(), 0); gen = 1; }
        const uint32_t genq = gen;
        std::priority_queue<Cand, std::vector<Cand>, Worse> top;
        std::priority_queue<std::pair<float, uint32_t>,
                            std::vector<std::pair<float, uint32_t>>,
                            std::greater<std::pair<float, uint32_t>>> cand;
        visited[start] = genq;
        float ds = u8_l2(q, &base8[size_t(start) * dim], dim);
        top.push({ds, int(start)});
        cand.push({ds, start});
        while (!cand.empty()) {
          auto cd = cand.top();
          if (top.size() >= ef_q && cd.first > top.top().d) break;
          cand.pop();
          uint32_t c = cd.second;
          const float dq_c = cd.first;  // distance from query to c
          const uint32_t r0 = run_offsets[c], r1 = run_offsets[size_t(c) + 1];
          auto lo_it = std::lower_bound(run_act.data() + r0, run_act.data() + r1, uint16_t(lo));
          auto hi_it = std::upper_bound(lo_it, run_act.data() + r1, uint16_t(hi));
          const uint32_t first = uint32_t(lo_it - run_act.data());
          const uint32_t last = uint32_t(hi_it - run_act.data());
          const size_t s0 = first < r1 ? run_start[first] : offs[size_t(c) + 1];
          const size_t s1 = last < r1 ? run_start[last] : offs[size_t(c) + 1];
          const bool full = (top.size() >= ef_q);
          const float tau = full ? top.top().d : 0.f;
          if (varint_layout) {
            for (uint32_t r = first; r < last; ++r) {
              const uint8_t* p = nbr_varint.data() + run_start[r];
              const uint8_t* pe = nbr_varint.data() + run_start[r + 1];
              uint32_t prev = 0;  // each run restarts its delta chain
              while (p < pe) {
                uint32_t delta = 0;
                int shift = 0;
                while (true) {
                  uint8_t b = *p++;
                  delta |= uint32_t(b & 0x7f) << shift;
                  if (!(b & 0x80)) break;
                  shift += 7;
                }
                prev += delta;
                ++scanned;
                uint32_t nb = prev;
                if (visited[nb] == genq) continue;
                ++distcomp;
                visited[nb] = genq;
                float dn = u8_l2(q, &base8[size_t(nb) * dim], dim);
                if (top.size() < ef_q || dn < top.top().d) {
                  cand.push({dn, nb});
                  top.push({dn, int(nb)});
                  if (top.size() > ef_q) top.pop();
                }
              }
            }
          } else {
            const size_t s0 = first < r1 ? run_start[first] : offs[size_t(c) + 1];
            const size_t s1 = last < r1 ? run_start[last] : offs[size_t(c) + 1];
            for (size_t t = s0; t < s1; ++t) {
              ++scanned;
              if (exact_edges && !dflag.empty() && dflag[t] == 0) continue;
              uint32_t nb = nbr[t];
              if (visited[nb] == genq) continue;
              if (full && !dist16.empty() &&
                  float(dist16[t]) - dq_c > tau) {
                ++pruned;
                continue;
              }
              ++distcomp;
              visited[nb] = genq;
              float dn = u8_l2(q, &base8[size_t(nb) * dim], dim);
              if (top.size() < ef_q || dn < top.top().d) {
                cand.push({dn, nb});
                top.push({dn, int(nb)});
                if (top.size() > ef_q) top.pop();
              }
            }
          }
        }
        while (!top.empty()) { cands.push_back(top.top()); top.pop(); }
      }
      std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        return a.d < b.d || (a.d == b.d && a.id < b.id);
      });
      int32_t* row = &ids[size_t(qi) * a.k];
      std::fill(row, row + a.k, -1);
      int filled = 0;
      for (const Cand& c : cands) {
        if (a.mode == "exact_strict") {
          bool ok = false;
          for (int u : qnodes[qi]) ok = ok || is_direct_lab(c.id, u);
          if (!ok) continue;
        }
        bool dup = false;
        for (int t = 0; t < filled; ++t) dup = dup || row[t] == c.id;
        if (dup) continue;
        row[filled++] = c.id;
        if (filled == a.k) break;
      }
      auto t1 = std::chrono::steady_clock::now();
      timings[qi] = std::chrono::duration<double, std::milli>(t1 - t0).count();
      total_s += timings[qi] / 1000.0;
    }
    best = std::min(best, total_s);
  }
  write_bin(a.out, ids);
  write_bin(a.timings, timings);
  std::printf("QUERY_SECONDS %.9f\n", best);
  std::printf("PRUNED %llu / SCANNED %llu (%.2f%%), distcomp %llu\n",
              (unsigned long long)pruned, (unsigned long long)scanned,
              scanned ? 100.0 * pruned / scanned : 0.0, (unsigned long long)distcomp);
  return 0;
}
