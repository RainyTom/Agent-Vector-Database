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

// ---------------------------------------------------------------- main

int main(int argc, char** argv) {
    std::string dir = ".";
    std::string mode = "gt";
    std::string out = ".";
    int k = 10, topn = 300, cand_cap = 1250, threads = 8, limit = -1;
    int m_hnsw = 16, efc = 200;
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
        else { std::cerr << "unknown arg " << a << "\n"; return 2; }
    }

    std::vector<Point> pts;
    std::vector<int32_t> off, len;
    std::vector<Query> qs;
    if (!load_points(dir, pts) || !load_users(dir, off, len) || !load_queries(dir, qs))
        return 2;
    if (limit > 0 && limit < (int)qs.size()) qs.resize(limit);

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
