// Unified episode-retrieval benchmark: exact baseline, order baselines, ANN baselines.
//
// Data layout (produced by prep.py):
//   points.bin   int32 N, int32 D, then N * (int32 user, int64 ts, float lat,
//                float lon, int32 cat, float emb[D])
//   users.bin    int32 U, then U * (int32 offset, int32 len)
//   queries.bin  int32 Q, int32 D, int32 MAXM, then Q * (int32 m, int32 source,
//                int64 ws, int64 we, int64 dur, int64 tol, float bbox[4],
//                int64 tau[m], float emb[m][D])
//
// Query semantics follow problem-definition v4:
//   episode [a,b] feasible iff ws <= ts[a], ts[b] <= we,
//   |(ts[b]-ts[a]) - dur| <= tol, every point in bbox, b-a+1 >= m;
//   score = DTW with steps {(1,1),(0,1)} (no query skip) / m;
//   results exclude the query's source user; top-k by (score, user).
//
// Modes:
//   dump         write queries.csv
//   gt           exact time-first = ground truth (also E1 timing)
//   space        exact space-first via grid (B2)
//   ann-post     global HNSW then filter (B3)
//   ann-bucket   month-partitioned HNSW then filter (B4)
//
// Build:
//   g++ -O3 -march=native -std=c++17 -pthread -I deps/hnswlib \
//       src/bench.cpp -o work/bench

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "hnswlib/hnswlib.h"

static constexpr int D = 128;
static constexpr int MAXM = 6;
static constexpr int PD = 64;  // projection dim for catalog ranking
static constexpr float INF = 1e30f;

struct Point {
    int32_t user;
    int64_t ts;
    float lat, lon;
    int32_t cat;
    std::array<float, D> emb;
};

struct Query {
    int m = 0;
    int source = -1;
    int64_t ws = 0, we = 0, dur = 0, tol = 0;
    std::array<float, 4> bbox{};  // min_lat, max_lat, min_lon, max_lon
    std::array<int64_t, MAXM> tau{};
    std::array<std::array<float, D>, MAXM> emb{};
};

struct Hit {
    float score = INF;
    int user = -1;
    int64_t start_ts = 0, end_ts = 0;
    int a = -1, b = -1;
};

struct RunStats {
    double ms = 0.0;
    long long cand = 0, verified = 0, dtw = 0;
};

static inline float l2(const std::array<float, D>& x, const std::array<float, D>& y) {
    float s = 0.0f;
    for (int i = 0; i < D; ++i) {
        float d = x[i] - y[i];
        s += d * d;
    }
    return std::sqrt(s);
}

static inline bool in_bbox(const Query& q, const Point& p) {
    return p.lat >= q.bbox[0] && p.lat <= q.bbox[1] &&
           p.lon >= q.bbox[2] && p.lon <= q.bbox[3];
}

static double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------- data loading

static bool load_points(const std::string& dir, std::vector<Point>& pts) {
    std::ifstream f(dir + "/points.bin", std::ios::binary);
    if (!f) return false;
    int32_t n = 0, d = 0;
    f.read((char*)&n, 4);
    f.read((char*)&d, 4);
    if (d != D) {
        std::cerr << "dim mismatch: " << d << "\n";
        return false;
    }
    pts.resize(n);
    for (auto& p : pts) {
        f.read((char*)&p.user, 4);
        f.read((char*)&p.ts, 8);
        f.read((char*)&p.lat, 4);
        f.read((char*)&p.lon, 4);
        f.read((char*)&p.cat, 4);
        f.read((char*)p.emb.data(), D * 4);
    }
    return true;
}

static bool load_users(const std::string& dir, std::vector<int32_t>& off,
                       std::vector<int32_t>& len) {
    std::ifstream f(dir + "/users.bin", std::ios::binary);
    if (!f) return false;
    int32_t u = 0;
    f.read((char*)&u, 4);
    off.resize(u);
    len.resize(u);
    for (int i = 0; i < u; ++i) {
        f.read((char*)&off[i], 4);
        f.read((char*)&len[i], 4);
    }
    return true;
}

static bool load_queries(const std::string& dir, std::vector<Query>& qs) {
    std::ifstream f(dir + "/queries.bin", std::ios::binary);
    if (!f) return false;
    int32_t q = 0, d = 0, mm = 0;
    f.read((char*)&q, 4);
    f.read((char*)&d, 4);
    f.read((char*)&mm, 4);
    if (d != D || mm > MAXM) {
        std::cerr << "query layout mismatch\n";
        return false;
    }
    qs.resize(q);
    for (auto& x : qs) {
        f.read((char*)&x.m, 4);
        f.read((char*)&x.source, 4);
        f.read((char*)&x.ws, 8);
        f.read((char*)&x.we, 8);
        f.read((char*)&x.dur, 8);
        f.read((char*)&x.tol, 8);
        f.read((char*)x.bbox.data(), 4 * 4);
        for (int j = 0; j < x.m; ++j) f.read((char*)&x.tau[j], 8);
        for (int j = 0; j < x.m; ++j) f.read((char*)x.emb[j].data(), D * 4);
    }
    return true;
}

// ---------------------------------------------------------------- episode search

// DTW over the segment sequence of [a,b]; steps (1,1) and (0,1) only, so every
// query unit must match at least one segment (no free skipping).
static float episode_dtw(const Query& q, const Point* pts, int a, int n, float lambda) {
    const int m = q.m;
    static thread_local std::vector<float> prev, cur;
    prev.assign(n + 1, INF);
    cur.assign(n + 1, INF);
    prev[0] = 0.0f;
    for (int i = 1; i <= m; ++i) {
        std::fill(cur.begin(), cur.end(), INF);
        cur[0] = INF;
        for (int j = 1; j <= n; ++j) {
            float cost = l2(q.emb[i - 1], pts[a + j - 1].emb);
            if (lambda > 0.0f) {
                double dt = std::fabs((double)q.tau[i - 1] -
                                      (double)(pts[a + j - 1].ts - pts[a].ts)) / 3600.0;
                cost += lambda * (float)dt;
            }
            float best = std::min(prev[j - 1], cur[j - 1]);
            cur[j] = (best >= INF) ? INF : cost + best;
        }
        std::swap(prev, cur);
    }
    return prev[n] / (float)m;
}

static Hit best_episode_user(const Query& q, const std::vector<Point>& pts, int off,
                             int len, float lambda, long long* dtw_count) {
    Hit best;
    if (len <= 0) return best;
    const Point* p = pts.data() + off;
    int lo = 0, hi = len;
    while (lo < hi) {  // first point with ts >= ws
        int mid = (lo + hi) / 2;
        if (p[mid].ts < q.ws) lo = mid + 1; else hi = mid;
    }
    int start = lo;
    hi = len;
    while (lo < hi) {  // first point with ts > we
        int mid = (lo + hi) / 2;
        if (p[mid].ts <= q.we) lo = mid + 1; else hi = mid;
    }
    int end = lo;
    for (int a = start; a < end; ++a) {
        if (!in_bbox(q, p[a])) continue;  // any episode starting here is invalid
        for (int b = a; b < end; ++b) {
            if (!in_bbox(q, p[b])) break;  // longer episodes also invalid
            int64_t dur = p[b].ts - p[a].ts;
            if (dur > q.dur + q.tol) break;
            int cnt = b - a + 1;
            if (cnt < q.m) continue;
            if (dur < q.dur - q.tol) continue;
            float s = episode_dtw(q, p, a, cnt, lambda);
            ++*dtw_count;
            if (s < best.score) best = {s, p[a].user, p[a].ts, p[b].ts, a, b};
        }
    }
    return best;
}

static std::vector<Hit> topk_from_users(const Query& q, const std::vector<Point>& pts,
                                        const std::vector<int32_t>& off,
                                        const std::vector<int32_t>& len,
                                        const std::vector<int>& users, float lambda,
                                        int k, RunStats* st) {
    std::vector<Hit> hits;
    hits.reserve(users.size());
    for (int u : users) {
        if (u == q.source) continue;
        Hit h = best_episode_user(q, pts, off[u], len[u], lambda, &st->dtw);
        if (h.user >= 0) hits.push_back(h);
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& x, const Hit& y) {
        if (x.score != y.score) return x.score < y.score;
        return x.user < y.user;
    });
    if ((int)hits.size() > k) hits.resize(k);
    return hits;
}

// ---------------------------------------------------------------- exact orders

static std::vector<Hit> exact_time_first(const Query& q, const std::vector<Point>& pts,
                                         const std::vector<int32_t>& off,
                                         const std::vector<int32_t>& len, float lambda,
                                         int k, RunStats* st) {
    std::vector<int> users;
    users.reserve(off.size());
    for (int u = 0; u < (int)off.size(); ++u) users.push_back(u);
    st->cand = (long long)users.size();
    auto hits = topk_from_users(q, pts, off, len, users, lambda, k, st);
    st->verified = st->cand;
    return hits;
}

static std::vector<Hit> exact_space_first(const Query& q, const std::vector<Point>& pts,
                                          const std::vector<int32_t>& off,
                                          const std::vector<int32_t>& len, float lambda,
                                          int k, RunStats* st, double cell_deg = 0.05) {
    // Grid of users: a user enters every cell containing at least one point.
    static std::unordered_map<int64_t, std::vector<int>> grid;
    static bool built = false;
    if (!built) {
        for (int u = 0; u < (int)off.size(); ++u) {
            for (int i = 0; i < len[u]; ++i) {
                const Point& p = pts[off[u] + i];
                int iy = (int)std::floor(p.lat / cell_deg);
                int ix = (int)std::floor(p.lon / cell_deg);
                int64_t key = ((int64_t)iy << 32) ^ (uint32_t)ix;
                auto& v = grid[key];
                if (v.empty() || v.back() != u) v.push_back(u);
            }
        }
        built = true;
    }
    int y0 = (int)std::floor(q.bbox[0] / cell_deg);
    int y1 = (int)std::floor(q.bbox[1] / cell_deg);
    int x0 = (int)std::floor(q.bbox[2] / cell_deg);
    int x1 = (int)std::floor(q.bbox[3] / cell_deg);
    std::vector<int> users;
    std::vector<uint8_t> seen(off.size(), 0);
    for (int iy = y0; iy <= y1; ++iy) {
        for (int ix = x0; ix <= x1; ++ix) {
            int64_t key = ((int64_t)iy << 32) ^ (uint32_t)ix;
            auto it = grid.find(key);
            if (it == grid.end()) continue;
            for (int u : it->second) {
                if (!seen[u]) {
                    seen[u] = 1;
                    users.push_back(u);
                }
            }
        }
    }
    st->cand = (long long)users.size();
    auto hits = topk_from_users(q, pts, off, len, users, lambda, k, st);
    st->verified = st->cand;
    return hits;
}

// ---------------------------------------------------------------- ANN baselines

struct Indexed {
    hnswlib::HierarchicalNSW<float>* idx = nullptr;
    std::vector<int> label_to_pt;  // local label -> global point id
};

static std::vector<Hit> ann_candidates(const Query& q, const std::vector<Point>& pts,
                                       const std::vector<int32_t>& off,
                                       const std::vector<int32_t>& len,
                                       const std::vector<Indexed*>& idxs,
                                       float lambda, int k, int topn, int cand_cap,
                                       RunStats* st) {
    const int nu = (int)off.size();
    std::vector<int> cnt(nu, 0);
    std::vector<float> sumdist(nu, 0.0f);
    std::vector<float> bestd(nu, INF);
    std::vector<int> touched;
    touched.reserve(4096);
    std::vector<int> unit_touched;
    unit_touched.reserve(1024);
    for (auto* ix : idxs) ix->idx->setEf(std::max(topn, 64));
    for (int i = 0; i < q.m; ++i) {
        unit_touched.clear();
        for (auto* ix : idxs) {
            auto res = ix->idx->searchKnn(q.emb[i].data(), topn);
            while (!res.empty()) {
                float d = res.top().first;
                int label = (int)res.top().second;
                res.pop();
                int u = pts[ix->label_to_pt[label]].user;
                if (u == q.source) continue;
                if (bestd[u] >= INF) unit_touched.push_back(u);
                if (d < bestd[u]) bestd[u] = d;
            }
        }
        for (int u : unit_touched) {
            if (cnt[u] == 0) touched.push_back(u);
            ++cnt[u];
            sumdist[u] += bestd[u];
            bestd[u] = INF;
        }
    }
    std::sort(touched.begin(), touched.end(), [&](int a, int b) {
        if (cnt[a] != cnt[b]) return cnt[a] > cnt[b];
        if (sumdist[a] != sumdist[b]) return sumdist[a] < sumdist[b];
        return a < b;
    });
    if ((int)touched.size() > cand_cap) touched.resize(cand_cap);
    st->cand = (long long)touched.size();
    auto hits = topk_from_users(q, pts, off, len, touched, lambda, k, st);
    st->verified = st->cand;
    return hits;
}

static Indexed* build_index(const std::vector<Point>& pts,
                            const std::vector<int>& ids,
                            hnswlib::L2Space* space, int m,
                            int efc, int threads) {
    auto* idx = new hnswlib::HierarchicalNSW<float>(space, ids.size(), m, efc);
    std::atomic<size_t> next{0};
    auto worker = [&]() {
        for (;;) {
            size_t i = next.fetch_add(1);
            if (i >= ids.size()) break;
            idx->addPoint(pts[ids[i]].emb.data(), i, false);
        }
    };
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) pool.emplace_back(worker);
    for (auto& th : pool) th.join();
    return new Indexed{idx, ids};
}

// ---------------------------------------------------------------- output

static void write_queries_csv(const std::string& path, const std::vector<Query>& qs) {
    std::ofstream f(path);
    f << "qid,source,m,ws,we,dur_s,tol_s,min_lat,max_lat,min_lon,max_lon\n";
    for (size_t i = 0; i < qs.size(); ++i) {
        const auto& q = qs[i];
        f << i << ',' << q.source << ',' << q.m << ',' << q.ws << ',' << q.we << ','
          << q.dur << ',' << q.tol << ',' << q.bbox[0] << ',' << q.bbox[1] << ','
          << q.bbox[2] << ',' << q.bbox[3] << '\n';
    }
}

static void write_gt_csv(const std::string& path, const std::vector<std::vector<Hit>>& gt) {
    std::ofstream f(path);
    f << "qid,rank,user,score,a,b,start_ts,end_ts\n";
    for (size_t qi = 0; qi < gt.size(); ++qi) {
        for (size_t r = 0; r < gt[qi].size(); ++r) {
            const auto& h = gt[qi][r];
            f << qi << ',' << (r + 1) << ',' << h.user << ',' << h.score << ',' << h.a
              << ',' << h.b << ',' << h.start_ts << ',' << h.end_ts << '\n';
        }
    }
}

static void write_method_csv(const std::string& path, const std::vector<RunStats>& st,
                             const std::vector<double>& recall,
                             const std::vector<size_t>& hits) {
    std::ofstream f(path);
    f << "qid,time_us,cand_users,verified,dtw,hits,recall\n";
    for (size_t i = 0; i < st.size(); ++i) {
        f << i << ',' << st[i].ms * 1000.0 << ',' << st[i].cand << ',' << st[i].verified
          << ',' << st[i].dtw << ',' << hits[i] << ',' << recall[i] << '\n';
    }
}

static double mean(const std::vector<double>& v) {
    double s = 0;
    for (double x : v) s += x;
    return v.empty() ? 0.0 : s / (double)v.size();
}

static double pct(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t i = std::min(v.size() - 1, (size_t)(p * (double)v.size()));
    return v[i];
}

static void summary_line(const std::string& method, const std::vector<RunStats>& st,
                         const std::vector<double>& recall, const std::vector<size_t>& hits) {
    std::vector<double> us, rec;
    long long cand = 0, dtw = 0;
    size_t empty = 0;
    for (size_t i = 0; i < st.size(); ++i) {
        us.push_back(st[i].ms * 1000.0);
        rec.push_back(recall[i]);
        cand += st[i].cand;
        dtw += st[i].dtw;
        if (hits[i] == 0) ++empty;
    }
    std::cout << "SUMMARY " << method << " n=" << st.size()
              << " recall=" << mean(rec) << " mean_us=" << mean(us)
              << " p95_us=" << pct(us, 0.95) << " qps=" << (1000.0 / mean(us))
              << " mean_cand=" << (double)cand / std::max<size_t>(1, st.size())
              << " mean_dtw=" << (double)dtw / std::max<size_t>(1, st.size())
              << " empty=" << empty << "\n";
}


// ---------------------------------------------------------------- episode catalog

struct Span {
    int32_t traj = -1, u = -1, v = -1, n = 0;
    int64_t ts = 0;
    float dur = 0.0f;
    float bbox[4] = {0, 0, 0, 0};  // min_lat, max_lat, min_lon, max_lon
    float maxnorm = 0.0f;
};
static_assert(sizeof(Span) == 48, "Span should stay compact");

static inline int64_t cell_key(float lat, float lon, float cell) {
    int iy = (int)std::floor(lat / cell);
    int ix = (int)std::floor(lon / cell);
    return ((int64_t)iy << 32) ^ (uint32_t)ix;
}

// Banded DTW. f[1][off] = prefix cost; f[i][off] = cost + min(f[i-1][off-1], f[i][off-1]).
static float banded_dtw(const Query& q, const Point* p, int u, int v, float lambda) {
    const int m = q.m;
    const int n = v - u + 1;
    const int g = n - m;
    if (g < 0) return INF;
    static thread_local std::vector<float> prev, cur;
    prev.assign(n, INF);
    cur.assign(n, INF);
    auto cost_at = [&](int i, int off) -> float {
        float c = l2(q.emb[i - 1], p[u + off].emb);
        if (lambda > 0.0f) {
            double dt = std::fabs((double)q.tau[i - 1] -
                                  (double)(p[u + off].ts - p[u].ts)) / 3600.0;
            c += lambda * (float)dt;
        }
        return c;
    };
    // i = 1: q1 covers segments 0..off.
    float run = 0.0f;
    for (int off = 0; off <= g; ++off) {  // band for i=1: off in [0, g]
        run += cost_at(1, off);
        prev[off] = run;
    }
    for (int i = 2; i <= m; ++i) {
        std::fill(cur.begin(), cur.end(), INF);
        int lo = i - 1, hi = g + i - 1;
        for (int off = lo; off <= hi; ++off) {
            float best = INF;
            if (off - 1 >= (i - 2) && off - 1 <= g + (i - 2)) best = prev[off - 1];
            if (off - 1 >= lo) best = std::min(best, cur[off - 1]);
            if (best < INF) cur[off] = cost_at(i, off) + best;
        }
        std::swap(prev, cur);
    }
    return prev[n - 1] / (float)m;
}

static float banded_dtw_proj(const float qp[MAXM][PD], int m, const float* base,
                             int n, int skip) {
    const int g = n - m;
    if (g < 0) return INF;
    static thread_local std::vector<float> prev, cur;
    prev.assign(n, INF);
    cur.assign(n, INF);
    float run = 0.0f;
    for (int off = 0; off <= g; ++off) {
        const float* v = base + (size_t)(off + skip) * PD;
        float d2 = 0.0f;
        for (int c = 0; c < PD; ++c) { float t = qp[0][c] - v[c]; d2 += t * t; }
        run += std::sqrt(d2);
        prev[off] = run;
    }
    for (int i = 2; i <= m; ++i) {
        std::fill(cur.begin(), cur.end(), INF);
        int lo = i - 1, hi = g + i - 1;
        for (int off = lo; off <= hi; ++off) {
            float best = INF;
            if (off - 1 >= (i - 2) && off - 1 <= g + (i - 2)) best = prev[off - 1];
            if (off - 1 >= lo) best = std::min(best, cur[off - 1]);
            if (best >= INF) continue;
            const float* v = base + (size_t)(off + skip) * PD;
            float d2 = 0.0f;
            for (int c = 0; c < PD; ++c) { float t = qp[i - 1][c] - v[c]; d2 += t * t; }
            cur[off] = best + std::sqrt(d2);
        }
        std::swap(prev, cur);
    }
    return prev[n - 1] / (float)m;
}

// Banded DP over a symbolic sequence: cost(i, symbol) comes from a precomputed table.
static float banded_dtw_cat(const float* ctab, int stride, int m, const Point* p,
                            int u, int n) {
    const int g = n - m;
    if (g < 0) return INF;
    static thread_local std::vector<float> prev, cur;
    prev.assign(n, INF);
    cur.assign(n, INF);
    float run = 0.0f;
    for (int off = 0; off <= g; ++off) {
        run += ctab[(size_t)0 * stride + p[u + off].cat];
        prev[off] = run;
    }
    for (int i = 2; i <= m; ++i) {
        std::fill(cur.begin(), cur.end(), INF);
        int lo = i - 1, hi = g + i - 1;
        for (int off = lo; off <= hi; ++off) {
            float best = INF;
            if (off - 1 >= (i - 2) && off - 1 <= g + (i - 2)) best = prev[off - 1];
            if (off - 1 >= lo) best = std::min(best, cur[off - 1]);
            if (best >= INF) continue;
            cur[off] = best + ctab[(size_t)(i - 1) * stride + p[u + off].cat];
        }
        std::swap(prev, cur);
    }
    return prev[n - 1] / (float)m;
}

static void run_catalog(const std::string& dir, const std::string& out,
                        const std::vector<Point>& pts, const std::vector<int32_t>& off,
                        const std::vector<int32_t>& len, const std::vector<Query>& qs,
                        const std::vector<std::vector<Hit>>& gt, int k, int topc,
                        float cap_hours, float cell_deg, int bucket_hours,
                        bool rank_cat) {
    const float CAP = cap_hours * 3600.0f;
    const int64_t BUCKET = (int64_t)bucket_hours * 3600;

    // Fixed random projection 128 -> 8 (deterministic).
    static float P[D][PD];
    {
        std::mt19937 rng(20261024);
        std::normal_distribution<float> nd(0.0f, 1.0f / std::sqrt((float)PD));
        for (int i = 0; i < D; ++i)
            for (int j = 0; j < PD; ++j) P[i][j] = nd(rng);
    }
    auto project = [&](const std::array<float, D>& e, float* dst) {
        for (int j = 0; j < PD; ++j) dst[j] = 0.0f;
        for (int i = 0; i < D; ++i) {
            float v = e[i];
            for (int j = 0; j < PD; ++j) dst[j] += v * P[i][j];
        }
    };

    int max_cat = 0;
    for (const auto& p : pts) max_cat = std::max(max_cat, (int)p.cat);
    std::vector<std::array<float, D>> centroids((size_t)max_cat + 1);
    std::vector<int> ccount((size_t)max_cat + 1, 0);
    if (rank_cat) {
        for (const auto& p : pts) {
            auto& c = centroids[(size_t)p.cat];
            for (int i = 0; i < D; ++i) c[i] += p.emb[i];
            ccount[(size_t)p.cat]++;
        }
        for (size_t c = 0; c < centroids.size(); ++c)
            if (ccount[c] > 0)
                for (int i = 0; i < D; ++i) centroids[c][i] /= (float)ccount[c];
    }

    std::vector<Span> cat;
    cat.reserve(7000000);
    std::vector<float> proj_pts((size_t)pts.size() * PD);
    std::vector<float> pref;  // (s+1)*8 projection prefix per trajectory
    double tb = now_ms();
    for (int uidx = 0; uidx < (int)off.size(); ++uidx) {
        const int s = len[uidx];
        const Point* p = pts.data() + off[uidx];
        pref.assign((size_t)(s + 1) * PD, 0.0f);
        for (int j = 0; j < s; ++j) {
            float v[PD];
            project(p[j].emb, v);
            for (int c = 0; c < PD; ++c) {
                proj_pts[(size_t)(off[uidx] + j) * PD + c] = v[c];
                pref[(size_t)(j + 1) * PD + c] = pref[(size_t)j * PD + c] + v[c];
            }
        }
        for (int u = 0; u < s; ++u) {
            float mnla = 1e9f, mxla = -1e9f, mnlo = 1e9f, mxlo = -1e9f, mxnorm = 0.0f;
            for (int v = u; v < s; ++v) {
                float dur = (float)(p[v].ts - p[u].ts);
                if (dur > CAP) break;
                mnla = std::min(mnla, p[v].lat);
                mxla = std::max(mxla, p[v].lat);
                mnlo = std::min(mnlo, p[v].lon);
                mxlo = std::max(mxlo, p[v].lon);
                mxnorm = std::max(mxnorm, std::sqrt(std::inner_product(
                    p[v].emb.begin(), p[v].emb.end(), p[v].emb.begin(), 0.0f)));
                Span sp;
                sp.traj = uidx; sp.u = u; sp.v = v; sp.n = v - u + 1;
                sp.ts = p[u].ts; sp.dur = dur;
                sp.bbox[0] = mnla; sp.bbox[1] = mxla; sp.bbox[2] = mnlo; sp.bbox[3] = mxlo;
                sp.maxnorm = mxnorm;
                cat.push_back(sp);
            }
        }
    }
    double build_s = (now_ms() - tb) / 1000.0;

    // Duration-sorted order.
    std::vector<uint32_t> dur_order(cat.size());
    for (size_t i = 0; i < cat.size(); ++i) dur_order[i] = (uint32_t)i;
    std::sort(dur_order.begin(), dur_order.end(),
              [&](uint32_t a, uint32_t b) { return cat[a].dur < cat[b].dur; });
    std::vector<float> dur_sorted(cat.size());
    for (size_t i = 0; i < cat.size(); ++i) dur_sorted[i] = cat[dur_order[i]].dur;

    std::unordered_map<int64_t, std::vector<uint32_t>> buckets;
    for (size_t i = 0; i < cat.size(); ++i)
        buckets[cat[i].ts / BUCKET].push_back((uint32_t)i);

    std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
    const int64_t MONTH32 = 30LL * 86400;
    const float DUR_BUCKET = 900.0f;  // 15 min
    auto grid_key = [&](int64_t ck, int64_t month, int db) -> uint64_t {
        uint64_t h = (uint64_t)ck * 1000003ULL ^ (uint64_t)month;
        return h * 1009ULL + (uint64_t)(db + 1);
    };
    for (size_t i = 0; i < cat.size(); ++i) {
        float clat = 0.5f * (cat[i].bbox[0] + cat[i].bbox[1]);
        float clon = 0.5f * (cat[i].bbox[2] + cat[i].bbox[3]);
        int db = (int)(cat[i].dur / DUR_BUCKET);
        grid[grid_key(cell_key(clat, clon, cell_deg), cat[i].ts / MONTH32, db)]
            .push_back((uint32_t)i);
    }

    // City bucket per span (from user_city.bin), with per-city bbox.
    std::vector<int32_t> user_city(off.size(), 0);
    int32_t ncity = 1;
    {
        std::ifstream ucf(dir + "/user_city.bin", std::ios::binary);
        if (ucf) {
            int32_t nu = 0, nc = 0;
            ucf.read((char*)&nu, 4); ucf.read((char*)&nc, 4);
            if (nu == (int32_t)off.size() && nc > 0) {
                ncity = nc;
                for (int i = 0; i < nu; ++i) ucf.read((char*)&user_city[i], 4);
            }
        }
    }
    std::vector<float> cb(4 * (size_t)ncity, 0.0f);
    std::vector<char> cinit(ncity, 0);
    std::vector<std::vector<uint32_t>> by_city(ncity);
    for (size_t i = 0; i < cat.size(); ++i) {
        int c = user_city[cat[i].traj];
        by_city[c].push_back((uint32_t)i);
        if (!cinit[c]) {
            cinit[c] = 1;
            cb[4 * (size_t)c + 0] = cat[i].bbox[0]; cb[4 * (size_t)c + 1] = cat[i].bbox[1];
            cb[4 * (size_t)c + 2] = cat[i].bbox[2]; cb[4 * (size_t)c + 3] = cat[i].bbox[3];
        } else {
            cb[4 * (size_t)c + 0] = std::min(cb[4 * (size_t)c + 0], cat[i].bbox[0]);
            cb[4 * (size_t)c + 1] = std::max(cb[4 * (size_t)c + 1], cat[i].bbox[1]);
            cb[4 * (size_t)c + 2] = std::min(cb[4 * (size_t)c + 2], cat[i].bbox[2]);
            cb[4 * (size_t)c + 3] = std::max(cb[4 * (size_t)c + 3], cat[i].bbox[3]);
        }
    }

    std::vector<RunStats> st(qs.size());
    std::vector<double> recall(qs.size(), 0.0);
    std::vector<size_t> hits(qs.size(), 0);
    long long tot_slice = 0, tot_filter = 0, tot_verify = 0;

    for (size_t qi = 0; qi < qs.size(); ++qi) {
        const Query& q = qs[qi];
        double t0 = now_ms();
        // Stage 1: duration slice.
        float lo = std::max(0.0f, (float)q.dur - (float)q.tol);
        float hi = (float)q.dur + (float)q.tol;
        auto b = std::lower_bound(dur_sorted.begin(), dur_sorted.end(), lo);
        auto e = std::upper_bound(dur_sorted.begin(), dur_sorted.end(), hi);

        // Stage 2: time buckets overlapping [ws, we].
        int64_t b0 = q.ws / BUCKET, b1 = q.we / BUCKET;
        long long slice_n = 0, filter_n = 0;

        std::vector<uint32_t> cand;
        cand.reserve((size_t)(e - b));
        // Use the smaller of duration slice and time-bucket union as the driver.
        size_t dur_n = (size_t)(e - b);
        size_t time_n = 0;
        for (int64_t bt = b0; bt <= b1; ++bt) {
            auto it = buckets.find(bt);
            if (it != buckets.end()) time_n += it->second.size();
        }
        int y0 = (int)std::floor(q.bbox[0] / cell_deg), y1 = (int)std::floor(q.bbox[1] / cell_deg);
        int x0 = (int)std::floor(q.bbox[2] / cell_deg), x1 = (int)std::floor(q.bbox[3] / cell_deg);
        size_t city_n = 0;
        for (int c = 0; c < ncity; ++c) {
            if (!cinit[c]) continue;
            bool inter = !(cb[4*(size_t)c+1] < q.bbox[0] || cb[4*(size_t)c+0] > q.bbox[1] ||
                           cb[4*(size_t)c+3] < q.bbox[2] || cb[4*(size_t)c+2] > q.bbox[3]);
            if (inter) city_n += by_city[c].size();
        }
        int64_t m0 = q.ws / MONTH32, m1 = q.we / MONTH32;
        int d0 = (int)(lo / DUR_BUCKET), d1 = (int)(hi / DUR_BUCKET);
        auto grid_collect = [&](std::vector<uint32_t>* dst, size_t* cnt) {
            for (int iy = y0; iy <= y1; ++iy)
                for (int ix = x0; ix <= x1; ++ix) {
                    int64_t ck = ((int64_t)iy << 32) ^ (uint32_t)ix;
                    for (int64_t mm = m0; mm <= m1; ++mm)
                        for (int db = d0; db <= d1; ++db) {
                            auto it = grid.find(grid_key(ck, mm, db));
                            if (it == grid.end()) continue;
                            if (cnt) *cnt += it->second.size();
                            if (dst) dst->insert(dst->end(), it->second.begin(), it->second.end());
                        }
                }
        };
        size_t space_n = 0;
        grid_collect(nullptr, &space_n);
        if (city_n <= dur_n && city_n <= time_n && city_n <= space_n) {
            for (int c = 0; c < ncity; ++c) {
                if (!cinit[c]) continue;
                bool inter = !(cb[4*(size_t)c+1] < q.bbox[0] || cb[4*(size_t)c+0] > q.bbox[1] ||
                               cb[4*(size_t)c+3] < q.bbox[2] || cb[4*(size_t)c+2] > q.bbox[3]);
                if (inter) cand.insert(cand.end(), by_city[c].begin(), by_city[c].end());
            }
        } else if (dur_n <= time_n && dur_n <= space_n) {
            for (auto it = b; it != e; ++it) cand.push_back(dur_order[(size_t)(it - dur_sorted.begin())]);
        } else if (time_n <= space_n) {
            for (int64_t bt = b0; bt <= b1; ++bt) {
                auto it = buckets.find(bt);
                if (it != buckets.end()) cand.insert(cand.end(), it->second.begin(), it->second.end());
            }
        } else {
            grid_collect(&cand, nullptr);
        }
        slice_n = (long long)cand.size();

        // Stage 3: exact hard constraints + sketch distance.
        float qproj[MAXM][PD];
        for (int i = 0; i < q.m; ++i) project(q.emb[i], qproj[i]);
        std::vector<float> ctab;
        if (rank_cat) {
            ctab.resize((size_t)MAXM * (max_cat + 1), 0.0f);
            for (int i = 0; i < q.m; ++i)
                for (int c = 0; c <= max_cat; ++c) {
                    if (ccount[c] == 0) { ctab[(size_t)i * (max_cat + 1) + c] = INF; continue; }
                    float d2 = 0.0f;
                    for (int k = 0; k < D; ++k) {
                        float t = q.emb[i][k] - centroids[(size_t)c][k];
                        d2 += t * t;
                    }
                    ctab[(size_t)i * (max_cat + 1) + c] = std::sqrt(d2);
                }
        }

        struct CScored { float d; uint32_t id; };
        std::vector<CScored> scored;
        scored.reserve(cand.size());
        for (uint32_t id : cand) {
            const Span& s = cat[id];
            if (s.n < q.m) continue;
            if (s.dur < lo || s.dur > hi) continue;
            if (s.ts < q.ws || (double)s.ts + s.dur > (double)q.we) continue;
            if (s.bbox[0] < q.bbox[0] || s.bbox[1] > q.bbox[1] ||
                s.bbox[2] < q.bbox[2] || s.bbox[3] > q.bbox[3]) continue;
            float approx;
            if (rank_cat) {
                approx = banded_dtw_cat(ctab.data(), max_cat + 1, q.m,
                                        pts.data() + off[s.traj], s.u, s.n);
            } else {
                const float* base = proj_pts.data() + (size_t)(off[s.traj] + s.u) * PD;
                approx = banded_dtw_proj(qproj, q.m, base, s.n, 0);
            }
            scored.push_back({approx, id});
        }
        filter_n = (long long)scored.size();
        size_t keep = std::min(scored.size(), (size_t)topc);
        if (keep < scored.size())
            std::nth_element(scored.begin(), scored.begin() + keep, scored.end(),
                             [](const CScored& a, const CScored& b) { return a.d < b.d; });
        scored.resize(keep);

        std::vector<Hit> all;
        all.reserve(keep);
        for (auto& c : scored) {
            const Span& s = cat[c.id];
            float sc = banded_dtw(q, pts.data() + off[s.traj], s.u, s.v, 0.0f);
            if (sc < INF) all.push_back(Hit{sc, s.traj, pts[off[s.traj] + s.u].ts,
                                            pts[off[s.traj] + s.v].ts, s.u, s.v});
        }
        std::sort(all.begin(), all.end(), [](const Hit& x, const Hit& y) {
            if (x.score != y.score) return x.score < y.score;
            return x.user < y.user;
        });
        std::vector<Hit> top;
        for (auto& h : all) {
            if (h.user == q.source) continue;
            bool dup = false;
            for (auto& t : top) if (t.user == h.user) { dup = true; break; }
            if (!dup) top.push_back(h);
            if ((int)top.size() >= k) break;
        }
        st[qi].ms = now_ms() - t0;
        st[qi].cand = slice_n;
        st[qi].verified = (long long)keep;
        st[qi].dtw = (long long)keep;
        hits[qi] = top.size();
        std::vector<int> got, want;
        for (auto& x : top) got.push_back(x.user);
        for (auto& x : gt[qi]) want.push_back(x.user);
        std::sort(got.begin(), got.end());
        std::sort(want.begin(), want.end());
        std::vector<int> inter;
        std::set_intersection(got.begin(), got.end(), want.begin(), want.end(),
                              std::back_inserter(inter));
        recall[qi] = want.empty() ? 1.0 : (double)inter.size() / want.size();
        tot_slice += slice_n; tot_filter += filter_n; tot_verify += keep;
    }
    write_method_csv(out + "/catalog.csv", st, recall, hits);
    summary_line("catalog", st, recall, hits);
    std::cout << "CATALOG spans=" << cat.size() << " build_s=" << build_s
              << " bytes=" << (double)cat.size() * sizeof(Span)
              << " mean_slice=" << (double)tot_slice / qs.size()
              << " mean_filter=" << (double)tot_filter / qs.size()
              << " mean_verify=" << (double)tot_verify / qs.size()
              << " topc=" << topc << " cap_h=" << cap_hours << "\n";
}

// ---------------------------------------------------------------- main

int main(int argc, char** argv) {
    std::string dir = ".";
    std::string mode = "gt";
    std::string out = ".";
    int k = 10, topn = 300, cand_cap = 1250, threads = 8, limit = -1;
    int m_hnsw = 16, efc = 200;
    int topc = 200, bucket_h = 24;
    std::string rank_mode = "proj";
    float cap_hours = 48.0f, cell_deg = 0.05f;
    float lambda = 0.0f;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--dir") dir = next();
        else if (a == "--mode") mode = next();
        else if (a == "--out") out = next();
        else if (a == "--k") k = std::stoi(next());
        else if (a == "--topn") topn = std::stoi(next());
        else if (a == "--cand") cand_cap = std::stoi(next());
        else if (a == "--ef") efc = std::stoi(next());
        else if (a == "--threads") threads = std::stoi(next());
        else if (a == "--limit") limit = std::stoi(next());
        else if (a == "--lambda") lambda = std::stof(next());
        else if (a == "--topc") topc = std::stoi(next());
        else if (a == "--cap-hours") cap_hours = std::stof(next());
        else if (a == "--cell") cell_deg = std::stof(next());
        else if (a == "--bucket-hours") bucket_h = std::stoi(next());
        else if (a == "--rank") rank_mode = next();
        else { std::cerr << "unknown arg " << a << "\n"; return 2; }
    }

    std::vector<Point> pts;
    std::vector<int32_t> off, len;
    std::vector<Query> qs;
    if (!load_points(dir, pts) || !load_users(dir, off, len) || !load_queries(dir, qs))
        return 2;
    if (limit > 0 && limit < (int)qs.size()) qs.resize(limit);

    if (mode == "selftest") {
        std::mt19937 rng(7);
        int bad = 0, tests = 0;
        for (int it = 0; it < 5000; ++it) {
            const Query& q = qs[rng() % qs.size()];
            int uidx = (int)(rng() % off.size());
            int s = len[uidx];
            if (s < q.m + 1) continue;
            int u = (int)(rng() % (s - q.m));
            int maxn = std::min(s - u, q.m + 4);
            int n = q.m + (int)(rng() % (maxn - q.m + 1));
            int v = u + n - 1;
            const Point* base = pts.data() + off[uidx];
            float a = banded_dtw(q, base, u, v, 0.0f);
            float b = episode_dtw(q, base, u, n, 0.0f);
            ++tests;
            if (std::fabs(a - b) > 1e-3f * std::max(1.0f, std::fabs(b))) {
                if (bad++ < 5) std::printf("MISMATCH traj=%d u=%d v=%d band=%.6f full=%.6f\n",
                                           uidx, u, v, a, b);
            }
        }
        std::printf("selftest tests=%d bad=%d\n", tests, bad);
        return bad ? 2 : 0;
    }

    if (mode == "dump") {
        write_queries_csv(out + "/queries.csv", qs);
        std::cout << "wrote queries.csv n=" << qs.size() << "\n";
        return 0;
    }

    std::vector<std::vector<Hit>> gt;
    if (mode == "gt") {
        std::vector<RunStats> st(qs.size());
        gt.resize(qs.size());
        for (size_t i = 0; i < qs.size(); ++i) {
            double t0 = now_ms();
            gt[i] = exact_time_first(qs[i], pts, off, len, lambda, k, &st[i]);
            st[i].ms = now_ms() - t0;
        }
        write_gt_csv(out + "/groundtruth.csv", gt);
        write_method_csv(out + "/e1_time_first.csv", st,
                         std::vector<double>(qs.size(), 1.0),
                         [&] {
                             std::vector<size_t> h(qs.size());
                             for (size_t i = 0; i < qs.size(); ++i) h[i] = gt[i].size();
                             return h;
                         }());
        summary_line("e1_time_first", st, std::vector<double>(qs.size(), 1.0),
                     [&] {
                         std::vector<size_t> h(qs.size());
                         for (size_t i = 0; i < qs.size(); ++i) h[i] = gt[i].size();
                         return h;
                     }());
        return 0;
    }

    // Non-GT modes need ground truth for recall.
    {
        std::ifstream f(out + "/groundtruth.csv");
        if (!f) {
            std::cerr << "missing " << out << "/groundtruth.csv\n";
            return 2;
        }
        std::string line;
        std::getline(f, line);
        gt.resize(qs.size());
        while (std::getline(f, line)) {
            int qid, rank, user, a, b;
            long long s, e;
            float score;
            if (std::sscanf(line.c_str(), "%d,%d,%d,%f,%d,%d,%lld,%lld", &qid, &rank,
                            &user, &score, &a, &b, &s, &e) != 8)
                continue;
            gt[qid].push_back(Hit{score, user, s, e, a, b});
        }
    }

    std::vector<RunStats> st(qs.size());
    std::vector<double> recall(qs.size(), 0.0);
    std::vector<size_t> hits(qs.size(), 0);

    if (mode == "catalog") {
        run_catalog(dir, out, pts, off, len, qs, gt, k, topc, cap_hours, cell_deg, bucket_h,
                    rank_mode == "cat");
        return 0;
    }

    if (mode == "space") {
        for (size_t i = 0; i < qs.size(); ++i) {
            double t0 = now_ms();
            auto h = exact_space_first(qs[i], pts, off, len, lambda, k, &st[i]);
            st[i].ms = now_ms() - t0;
            hits[i] = h.size();
            std::vector<int> got, want;
            for (auto& x : h) got.push_back(x.user);
            for (auto& x : gt[i]) want.push_back(x.user);
            std::sort(got.begin(), got.end());
            std::sort(want.begin(), want.end());
            std::vector<int> inter;
            std::set_intersection(got.begin(), got.end(), want.begin(), want.end(),
                                  std::back_inserter(inter));
            recall[i] = want.empty() ? 1.0 : (double)inter.size() / want.size();
        }
        write_method_csv(out + "/b2_space_first.csv", st, recall, hits);
        summary_line("b2_space_first", st, recall, hits);
        return 0;
    }

    if (mode == "ann-post" || mode == "ann-bucket") {
        hnswlib::L2Space space(D);
        std::vector<Indexed*> all;
        std::map<int64_t, Indexed*> month_idx;
        const int64_t MONTH = 30LL * 86400;

        double tb = now_ms();
        if (mode == "ann-post") {
            std::vector<int> ids(pts.size());
            for (size_t i = 0; i < pts.size(); ++i) ids[i] = (int)i;
            all.push_back(build_index(pts, ids, &space, m_hnsw, efc, threads));
        } else {
            std::unordered_map<int64_t, std::vector<int>> buckets;
            for (size_t i = 0; i < pts.size(); ++i)
                buckets[pts[i].ts / MONTH].push_back((int)i);
            for (auto& [b, ids] : buckets) {
                auto* idx = build_index(pts, ids, &space, m_hnsw, efc, threads);
                month_idx[b] = idx;
                all.push_back(idx);
            }
        }
        double build_s = (now_ms() - tb) / 1000.0;

        for (size_t i = 0; i < qs.size(); ++i) {
            std::vector<Indexed*> use;
            if (mode == "ann-post") {
                use = all;
            } else {
                int64_t m0 = qs[i].ws / MONTH;
                int64_t m1 = qs[i].we / MONTH;
                for (auto it = month_idx.lower_bound(m0);
                     it != month_idx.end() && it->first <= m1; ++it)
                    use.push_back(it->second);
            }
            double t0 = now_ms();
            auto h = ann_candidates(qs[i], pts, off, len, use, lambda, k, topn, cand_cap,
                                    &st[i]);
            st[i].ms = now_ms() - t0;
            hits[i] = h.size();
            std::vector<int> got, want;
            for (auto& x : h) got.push_back(x.user);
            for (auto& x : gt[i]) want.push_back(x.user);
            std::sort(got.begin(), got.end());
            std::sort(want.begin(), want.end());
            std::vector<int> inter;
            std::set_intersection(got.begin(), got.end(), want.begin(), want.end(),
                                  std::back_inserter(inter));
            recall[i] = want.empty() ? 1.0 : (double)inter.size() / want.size();
        }
        std::string name = (mode == "ann-post") ? "b3_ann_postfilter" : "b4_ann_timebucket";
        write_method_csv(out + "/" + name + ".csv", st, recall, hits);
        summary_line(name, st, recall, hits);
        std::cout << "BUILD_S " << build_s << "\n";
        for (auto* p : all) { delete p->idx; delete p; }
        return 0;
    }

    std::cerr << "unknown mode " << mode << "\n";
    return 2;
}
