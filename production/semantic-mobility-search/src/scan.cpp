// Episode retrieval pilot: exact time-windowed scan and two baselines.
//
// Inputs are produced by prep.py:
//   points.bin      : int32 N, int32 D, then N records
//   users.bin       : int32 U, then U * (int32 offset, int32 len)
//   queries.bin     : int32 Q, int32 D, int32 MAXM, then Q queries
//   queries_cat.bin : int32 Q, int32 MAXM, then Q * MAXM category ids
//
// Methods per query:
//   exact    : time + space filtered scan over all users, DTW top-k
//   inverted : users sharing >=1 query category, same verification
//   meanknn  : top-N users by mean-embedding cosine, same verification
//
// Build: g++ -O3 -march=native -std=c++17 scan.cpp -o scan

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

static constexpr int D = 128;
static constexpr int MAXM = 6;
static constexpr int MAX_LEN_DELTA = 2;  // pilot: episode length in [m-2, m+2]

struct Point {
    int32_t user;
    int64_t ts;
    float lat, lon;
    int32_t cat;
    std::array<float, D> emb;
};

struct Query {
    int m;
    int source;
    int64_t ws, we, dur, tol;
    float min_lat, max_lat, min_lon, max_lon;
    std::array<int64_t, MAXM> tau;
    std::array<int32_t, MAXM> cats;
    std::array<std::array<float, D>, MAXM> emb;
};

struct Hit {
    float score;
    int user;
    int64_t start_ts, end_ts;
    int a, b;
};

static inline float sq_l2(const std::array<float, D>& x, const std::array<float, D>& y) {
    float s = 0.0f;
    for (int i = 0; i < D; ++i) {
        float d = x[i] - y[i];
        s += d * d;
    }
    return s;
}

static float dtw(const Query& q, const Point* pts, int a, int b, float lambda) {
    const int m = q.m;
    const int n = b - a + 1;
    const float INF = std::numeric_limits<float>::infinity();
    std::array<float, 256> prev{}, cur{};
    std::fill(prev.begin(), prev.begin() + n + 1, INF);
    std::fill(cur.begin(), cur.begin() + n + 1, INF);
    prev[0] = 0.0f;
    for (int i = 1; i <= m; ++i) {
        cur[0] = INF;
        for (int j = 1; j <= n; ++j) {
            float cost = std::sqrt(sq_l2(q.emb[i - 1], pts[a + j - 1].emb));
            if (lambda > 0.0f) {
                float dt = std::fabs((float)(q.tau[i - 1] - q.tau[0]) -
                                     (float)(pts[a + j - 1].ts - pts[a].ts)) / 3600.0f;
                cost += lambda * dt;
            }
            float best = std::min(prev[j], std::min(cur[j - 1], prev[j - 1]));
            cur[j] = cost + best;
        }
        std::swap(prev, cur);
    }
    return prev[n] / (float)std::max(m, n);
}

static inline bool in_bbox(const Query& q, const Point& p) {
    return p.lat >= q.min_lat && p.lat <= q.max_lat &&
           p.lon >= q.min_lon && p.lon <= q.max_lon;
}

static Hit best_episode(const Query& q, const std::vector<Point>& pts,
                        int off, int len, float lambda,
                        long long* dtw_count, bool* has_window_points) {
    Hit best{std::numeric_limits<float>::infinity(), -1, 0, 0, -1, -1};
    *has_window_points = false;
    if (len <= 0) return best;

    int lo = off, hi = off + len;
    int l = lo, r = hi;
    while (l < r) {
        int mid = (l + r) / 2;
        if (pts[mid].ts < q.ws) l = mid + 1; else r = mid;
    }
    int start = l;
    l = start; r = hi;
    while (l < r) {
        int mid = (l + r) / 2;
        if (pts[mid].ts <= q.we) l = mid + 1; else r = mid;
    }
    int end = l;  // exclusive
    if (start >= end) return best;
    *has_window_points = true;

    for (int a = start; a < end; ++a) {
        for (int b = a; b < end; ++b) {
            int64_t dur = pts[b].ts - pts[a].ts;
            if (dur > q.dur + q.tol) break;
            int len_ep = b - a + 1;
            if (len_ep > q.m + MAX_LEN_DELTA) break;
            if (len_ep < std::max(1, q.m - MAX_LEN_DELTA)) continue;
            if (dur < q.dur - q.tol) continue;
            bool ok = true;
            for (int k = a; k <= b; ++k) {
                if (!in_bbox(q, pts[k])) { ok = false; break; }
            }
            if (!ok) continue;
            float s = dtw(q, pts.data(), a, b, lambda);
            ++*dtw_count;
            if (s < best.score) best = {s, pts[a].user, pts[a].ts, pts[b].ts, a, b};
        }
    }
    return best;
}

struct ScanStats {
    long long dtw = 0;
    long long candidate_users = 0;  // users with >=1 point in the time window
    long long feasible_users = 0;   // users with >=1 feasible episode
};

static std::vector<Hit> topk_from_users(const Query& q, const std::vector<Point>& pts,
                                        const std::vector<int32_t>& off,
                                        const std::vector<int32_t>& len,
                                        const std::vector<int>& users,
                                        float lambda, int k, ScanStats* stats) {
    std::vector<Hit> hits;
    hits.reserve(users.size());
    for (int u : users) {
        bool has_points = false;
        Hit h = best_episode(q, pts, off[u], len[u], lambda, &stats->dtw, &has_points);
        if (has_points) ++stats->candidate_users;
        if (h.user >= 0) {
            hits.push_back(h);
            ++stats->feasible_users;
        }
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& x, const Hit& y) {
        if (x.score != y.score) return x.score < y.score;
        return x.user < y.user;
    });
    if ((int)hits.size() > k) hits.resize(k);
    return hits;
}

static inline float cosine(const std::array<float, D>& x, const std::array<float, D>& y) {
    float s = 0.0f;
    for (int i = 0; i < D; ++i) s += x[i] * y[i];
    return s;
}

static double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

int main(int argc, char** argv) {
    std::string dir = ".";
    int k = 10;
    int mean_n = 200;
    float lambda = 0.0f;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--dir" && i + 1 < argc) dir = argv[++i];
        else if (a == "--k" && i + 1 < argc) k = std::stoi(argv[++i]);
        else if (a == "--mean-n" && i + 1 < argc) mean_n = std::stoi(argv[++i]);
        else if (a == "--lambda" && i + 1 < argc) lambda = std::stof(argv[++i]);
        else { std::cerr << "unknown arg " << a << "\n"; return 2; }
    }

    std::ifstream pf(dir + "/points.bin", std::ios::binary);
    int32_t n_points = 0, dim = 0;
    pf.read((char*)&n_points, 4); pf.read((char*)&dim, 4);
    if (dim != D) { std::cerr << "dim mismatch\n"; return 2; }
    std::vector<Point> pts(n_points);
    for (int i = 0; i < n_points; ++i) {
        pf.read((char*)&pts[i].user, 4);
        pf.read((char*)&pts[i].ts, 8);
        pf.read((char*)&pts[i].lat, 4);
        pf.read((char*)&pts[i].lon, 4);
        pf.read((char*)&pts[i].cat, 4);
        pf.read((char*)pts[i].emb.data(), D * 4);
    }

    std::ifstream uf(dir + "/users.bin", std::ios::binary);
    int32_t n_users = 0;
    uf.read((char*)&n_users, 4);
    std::vector<int32_t> off(n_users), len(n_users);
    for (int i = 0; i < n_users; ++i) {
        uf.read((char*)&off[i], 4);
        uf.read((char*)&len[i], 4);
    }

    std::ifstream qf(dir + "/queries.bin", std::ios::binary);
    int32_t n_queries = 0, qdim = 0, maxm = 0;
    qf.read((char*)&n_queries, 4); qf.read((char*)&qdim, 4); qf.read((char*)&maxm, 4);
    if (qdim != D || maxm > MAXM) { std::cerr << "query dim mismatch\n"; return 2; }
    std::vector<Query> queries(n_queries);
    for (int i = 0; i < n_queries; ++i) {
        Query& q = queries[i];
        qf.read((char*)&q.m, 4);
        qf.read((char*)&q.source, 4);
        qf.read((char*)&q.ws, 8);
        qf.read((char*)&q.we, 8);
        qf.read((char*)&q.dur, 8);
        qf.read((char*)&q.tol, 8);
        qf.read((char*)&q.min_lat, 4);
        qf.read((char*)&q.max_lat, 4);
        qf.read((char*)&q.min_lon, 4);
        qf.read((char*)&q.max_lon, 4);
        for (int j = 0; j < q.m; ++j) qf.read((char*)&q.tau[j], 8);
        for (int j = 0; j < q.m; ++j)
            qf.read((char*)q.emb[j].data(), D * 4);
    }

    std::ifstream cf(dir + "/queries_cat.bin", std::ios::binary);
    int32_t nq_cat = 0, mm_cat = 0;
    cf.read((char*)&nq_cat, 4); cf.read((char*)&mm_cat, 4);
    if (nq_cat != n_queries || mm_cat != MAXM) { std::cerr << "query cat mismatch\n"; return 2; }
    std::vector<std::array<int32_t, MAXM>> qcats(nq_cat);
    for (int i = 0; i < nq_cat; ++i)
        for (int j = 0; j < mm_cat; ++j) cf.read((char*)&qcats[i][j], 4);

    int max_cat = 0;
    for (const auto& p : pts) max_cat = std::max(max_cat, p.cat);
    std::vector<std::vector<int>> cat_users(max_cat + 1);
    {
        std::vector<std::unordered_set<int>> tmp(max_cat + 1);
        for (int u = 0; u < n_users; ++u)
            for (int i = off[u]; i < off[u] + len[u]; ++i) tmp[pts[i].cat].insert(u);
        for (int c = 0; c <= max_cat; ++c) {
            cat_users[c].assign(tmp[c].begin(), tmp[c].end());
            std::sort(cat_users[c].begin(), cat_users[c].end());
        }
    }

    std::vector<std::array<float, D>> mean_emb(n_users);
    for (int u = 0; u < n_users; ++u) {
        std::array<float, D> s{};
        for (int i = off[u]; i < off[u] + len[u]; ++i)
            for (int j = 0; j < D; ++j) s[j] += pts[i].emb[j];
        float norm = 0.0f;
        for (float x : s) norm += x * x;
        norm = std::sqrt(std::max(norm, 1e-12f));
        for (int j = 0; j < D; ++j) mean_emb[u][j] = s[j] / norm;
    }

    std::ofstream out(dir + "/results.csv");
    out << "qid,m,exact_us,exact_candidates,exact_feasible,exact_dtw,exact_hits,"
        << "inv_us,inv_candidates,inv_feasible,inv_dtw,inv_hits,recall_inv,"
        << "mean_us,mean_candidates,mean_feasible,mean_dtw,mean_hits,recall_mean\n";

    double sum_exact = 0, sum_inv = 0, sum_mean = 0;
    long long sum_dtw_exact = 0, sum_dtw_inv = 0, sum_dtw_mean = 0;
    double sum_recall_inv = 0, sum_recall_mean = 0;
    int valid_queries = 0;

    for (int qi = 0; qi < n_queries; ++qi) {
        const Query& q = queries[qi];
        std::vector<int> all_users;
        all_users.reserve(n_users);
        for (int u = 0; u < n_users; ++u) if (u != q.source) all_users.push_back(u);

        ScanStats es;
        double t0 = now_ms();
        auto exact = topk_from_users(q, pts, off, len, all_users, lambda, k, &es);
        double t1 = now_ms();
        if ((int)exact.size() < k) continue;
        ++valid_queries;
        sum_exact += t1 - t0;
        sum_dtw_exact += es.dtw;

        std::vector<int> cats;
        for (int j = 0; j < q.m; ++j) cats.push_back(qcats[qi][j]);
        std::sort(cats.begin(), cats.end());
        cats.erase(std::unique(cats.begin(), cats.end()), cats.end());

        ScanStats is;
        double t2 = now_ms();
        std::vector<int> inv_users;
        for (int c : cats)
            inv_users.insert(inv_users.end(), cat_users[c].begin(), cat_users[c].end());
        std::sort(inv_users.begin(), inv_users.end());
        inv_users.erase(std::unique(inv_users.begin(), inv_users.end()), inv_users.end());
        inv_users.erase(std::remove(inv_users.begin(), inv_users.end(), q.source), inv_users.end());
        auto inv = topk_from_users(q, pts, off, len, inv_users, lambda, k, &is);
        double t3 = now_ms();
        std::unordered_set<int> exact_set;
        for (const auto& h : exact) exact_set.insert(h.user);
        int hit = 0;
        for (const auto& h : inv) if (exact_set.count(h.user)) ++hit;
        double recall_inv = exact.empty() ? 0.0 : (double)hit / exact.size();
        sum_inv += t3 - t2;
        sum_dtw_inv += is.dtw;
        sum_recall_inv += recall_inv;

        ScanStats ms;
        double t4 = now_ms();
        std::array<float, D> qmean{};
        for (int j = 0; j < q.m; ++j)
            for (int d = 0; d < D; ++d) qmean[d] += q.emb[j][d];
        float qn = 0.0f;
        for (float x : qmean) qn += x * x;
        qn = std::sqrt(std::max(qn, 1e-12f));
        for (float& x : qmean) x /= qn;
        std::vector<std::pair<float, int>> ranked;
        ranked.reserve(n_users);
        for (int u = 0; u < n_users; ++u) if (u != q.source)
            ranked.push_back({cosine(qmean, mean_emb[u]), u});
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
            return a.first > b.first;
        });
        int nmean = std::min(mean_n, (int)ranked.size());
        std::vector<int> mean_users;
        for (int i = 0; i < nmean; ++i) mean_users.push_back(ranked[i].second);
        auto mean_hits = topk_from_users(q, pts, off, len, mean_users, lambda, k, &ms);
        double t5 = now_ms();
        int mean_hit = 0;
        for (const auto& h : mean_hits) if (exact_set.count(h.user)) ++mean_hit;
        double recall_mean = exact.empty() ? 0.0 : (double)mean_hit / exact.size();
        sum_mean += t5 - t4;
        sum_dtw_mean += ms.dtw;
        sum_recall_mean += recall_mean;

        out << qi << "," << q.m << ","
            << (t1 - t0) << "," << es.candidate_users << "," << es.feasible_users << ","
            << es.dtw << "," << exact.size() << ","
            << (t3 - t2) << "," << is.candidate_users << "," << is.feasible_users << ","
            << is.dtw << "," << inv.size() << "," << recall_inv << ","
            << (t5 - t4) << "," << ms.candidate_users << "," << ms.feasible_users << ","
            << ms.dtw << "," << mean_hits.size() << "," << recall_mean << "\n";
    }

    out.close();
    std::cerr << "valid_queries " << valid_queries << " / " << n_queries << "\n";
    if (valid_queries > 0) {
        std::cerr << "mean_ms exact " << (sum_exact / valid_queries)
                  << " inverted " << (sum_inv / valid_queries)
                  << " mean " << (sum_mean / valid_queries) << "\n";
        std::cerr << "mean_dtw exact " << (double)sum_dtw_exact / valid_queries
                  << " inverted " << (double)sum_dtw_inv / valid_queries
                  << " mean " << (double)sum_dtw_mean / valid_queries << "\n";
        std::cerr << "mean_recall inverted " << (sum_recall_inv / valid_queries)
                  << " mean " << (sum_recall_mean / valid_queries) << "\n";
    }
    return 0;
}
