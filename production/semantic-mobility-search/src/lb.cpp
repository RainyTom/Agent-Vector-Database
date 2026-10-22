// Exact episode retrieval with a per-user DTW lower bound.
//
// For each user, compute LB_u = sum_j min_i ||q_j - p_i|| / (m + MAX_LEN_DELTA)
// over points p_i inside the query time window and spatial bbox. This is a
// valid lower bound on normalized DTW for any feasible episode of that user.
// Users are verified in ascending LB order and the scan stops once
// LB_u >= current k-th best score. Top-k is identical to the full scan.
//
// Build: g++ -O3 -march=native -std=c++17 lb.cpp -o lb

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <queue>
#include <string>
#include <unordered_set>
#include <vector>

static constexpr int D = 128;
static constexpr int MAXM = 6;
static constexpr int MAX_LEN_DELTA = 2;

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
    std::array<std::array<float, D>, MAXM> emb;
};

struct Hit {
    float score;
    int user;
    int64_t start_ts, end_ts;
    int a, b;
    bool operator<(const Hit& o) const {  // max-heap: worst (score, id) on top
        if (score != o.score) return score < o.score;
        return user < o.user;
    }
};

static inline float cosine(const std::array<float, D>& x, const std::array<float, D>& y) {
    float s = 0.0f;
    for (int i = 0; i < D; ++i) s += x[i] * y[i];
    return s;
}

static inline float sq_l2(const std::array<float, D>& x, const std::array<float, D>& y) {
    float s = 0.0f;
    for (int i = 0; i < D; ++i) { float d = x[i] - y[i]; s += d * d; }
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

static void window_range(const Query& q, const std::vector<Point>& pts,
                         int off, int len, int* start, int* end) {
    int l = off, r = off + len;
    while (l < r) { int mid = (l + r) / 2; if (pts[mid].ts < q.ws) l = mid + 1; else r = mid; }
    *start = l;
    l = *start; r = off + len;
    while (l < r) { int mid = (l + r) / 2; if (pts[mid].ts <= q.we) l = mid + 1; else r = mid; }
    *end = l;
}

static float user_lb(const Query& q, const std::vector<Point>& pts,
                     int off, int len) {
    int a, b;
    window_range(q, pts, off, len, &a, &b);
    if (a >= b) return std::numeric_limits<float>::infinity();
    std::array<float, MAXM> mins;
    mins.fill(std::numeric_limits<float>::infinity());
    for (int i = a; i < b; ++i) {
        const Point& p = pts[i];
        if (!in_bbox(q, p)) continue;
        for (int j = 0; j < q.m; ++j) {
            float d = sq_l2(q.emb[j], p.emb);
            if (d < mins[j]) mins[j] = d;
        }
    }
    float sum = 0.0f;
    for (int j = 0; j < q.m; ++j) {
        if (!std::isfinite(mins[j])) return std::numeric_limits<float>::infinity();
        sum += std::sqrt(mins[j]);
    }
    return sum / (float)(q.m + MAX_LEN_DELTA);
}

static Hit best_episode(const Query& q, const std::vector<Point>& pts,
                        int off, int len, float lambda, long long* dtw_count) {
    Hit best{std::numeric_limits<float>::infinity(), -1, 0, 0, -1, -1};
    int start, end;
    window_range(q, pts, off, len, &start, &end);
    for (int a = start; a < end; ++a) {
        for (int b = a; b < end; ++b) {
            int64_t dur = pts[b].ts - pts[a].ts;
            if (dur > q.dur + q.tol) break;
            int len_ep = b - a + 1;
            if (len_ep > q.m + MAX_LEN_DELTA) break;
            if (len_ep < std::max(1, q.m - MAX_LEN_DELTA)) continue;
            if (dur < q.dur - q.tol) continue;
            bool ok = true;
            for (int k = a; k <= b; ++k) if (!in_bbox(q, pts[k])) { ok = false; break; }
            if (!ok) continue;
            float s = dtw(q, pts.data(), a, b, lambda);
            ++*dtw_count;
            if (s < best.score) best = {s, pts[a].user, pts[a].ts, pts[b].ts, a, b};
        }
    }
    return best;
}

static void update_heap(std::priority_queue<Hit>& heap, const Hit& h, int k) {
    if (h.user < 0) return;
    if ((int)heap.size() < k) heap.push(h);
    else if (h.score < heap.top().score) { heap.pop(); heap.push(h); }
}

static double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

int main(int argc, char** argv) {
    std::string dir = ".";
    int k = 10, init_n = 100;
    float lambda = 0.0f;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--dir" && i + 1 < argc) dir = argv[++i];
        else if (a == "--k" && i + 1 < argc) k = std::stoi(argv[++i]);
        else if (a == "--init" && i + 1 < argc) init_n = std::stoi(argv[++i]);
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
    for (int i = 0; i < n_users; ++i) { uf.read((char*)&off[i], 4); uf.read((char*)&len[i], 4); }

    std::ifstream qf(dir + "/queries.bin", std::ios::binary);
    int32_t n_queries = 0, qdim = 0, maxm = 0;
    qf.read((char*)&n_queries, 4); qf.read((char*)&qdim, 4); qf.read((char*)&maxm, 4);
    if (qdim != D || maxm > MAXM) { std::cerr << "query dim mismatch\n"; return 2; }
    std::vector<Query> queries(n_queries);
    for (int i = 0; i < n_queries; ++i) {
        Query& q = queries[i];
        qf.read((char*)&q.m, 4); qf.read((char*)&q.source, 4);
        qf.read((char*)&q.ws, 8); qf.read((char*)&q.we, 8); qf.read((char*)&q.dur, 8); qf.read((char*)&q.tol, 8);
        qf.read((char*)&q.min_lat, 4); qf.read((char*)&q.max_lat, 4);
        qf.read((char*)&q.min_lon, 4); qf.read((char*)&q.max_lon, 4);
        for (int j = 0; j < q.m; ++j) qf.read((char*)&q.tau[j], 8);
        for (int j = 0; j < q.m; ++j) qf.read((char*)q.emb[j].data(), D * 4);
    }

    // User mean embeddings for the initial threshold.
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

    std::ofstream out(dir + "/lb_results.csv");
    out << "qid,m,exact_us,exact_dtw,lb_us,lb_dtw,lb_verified,lb_lb_users,same_topk,"
        << "init_us,init_dtw\n";

    double sum_exact = 0, sum_lb = 0, sum_init = 0;
    long long sum_dtw_exact = 0, sum_dtw_lb = 0, sum_init_dtw = 0;
    int valid = 0, same = 0;

    for (int qi = 0; qi < n_queries; ++qi) {
        const Query& q = queries[qi];
        std::vector<int> all_users;
        for (int u = 0; u < n_users; ++u) if (u != q.source) all_users.push_back(u);

        // Baseline: exhaustive exact scan.
        std::priority_queue<Hit> base_heap;
        long long base_dtw = 0;
        double e0 = now_ms();
        for (int u : all_users) {
            Hit h = best_episode(q, pts, off[u], len[u], lambda, &base_dtw);
            update_heap(base_heap, h, k);
        }
        double e1 = now_ms();
        if ((int)base_heap.size() < k) continue;
        ++valid;
        sum_exact += e1 - e0;
        sum_dtw_exact += base_dtw;

        // LB method: initial mean-kNN verification for a threshold, then
        // branch-and-bound over all users.
        double i0 = now_ms();
        std::priority_queue<Hit> heap;
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
        int ninit = std::min(init_n, (int)ranked.size());
        long long init_dtw = 0;
        std::unordered_set<int> init_users;
        for (int i = 0; i < ninit; ++i) {
            int u = ranked[i].second;
            init_users.insert(u);
            Hit h = best_episode(q, pts, off[u], len[u], lambda, &init_dtw);
            update_heap(heap, h, k);
        }
        double i1 = now_ms();
        sum_init += i1 - i0;
        sum_init_dtw += init_dtw;

        // Compute LB for all users.
        std::vector<std::pair<float, int>> lbs;
        lbs.reserve(all_users.size());
        double l0 = now_ms();
        for (int u : all_users) lbs.push_back({user_lb(q, pts, off[u], len[u]), u});
        std::sort(lbs.begin(), lbs.end());
        double l1 = now_ms();
        float thresh = (int)heap.size() >= k ? heap.top().score : std::numeric_limits<float>::infinity();
        long long lb_dtw = 0;
        int verified = 0;
        for (auto& [lb, u] : lbs) {
            if ((int)heap.size() >= k && lb >= thresh) break;
            if (init_users.count(u)) continue;
            Hit h = best_episode(q, pts, off[u], len[u], lambda, &lb_dtw);
            ++verified;
            update_heap(heap, h, k);
            if ((int)heap.size() >= k) thresh = heap.top().score;
        }
        double l2 = now_ms();

        // Compare top-k user sets.
        std::vector<int> a, b;
        auto bh = base_heap;
        while (!bh.empty()) { a.push_back(bh.top().user); bh.pop(); }
        auto lh = heap;
        while (!lh.empty()) { b.push_back(lh.top().user); lh.pop(); }
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        bool same_set = (a == b);
        if (same_set) ++same;

        sum_lb += (l2 - l0) + (i1 - i0);
        sum_dtw_lb += lb_dtw + init_dtw;

        out << qi << "," << q.m << ","
            << (e1 - e0) << "," << base_dtw << ","
            << ((l2 - l0) + (i1 - i0)) << "," << (lb_dtw + init_dtw) << ","
            << verified << "," << lbs.size() << "," << same_set << ","
            << (i1 - i0) << "," << init_dtw << "\n";
    }
    out.close();
    std::cerr << "valid " << valid << "/" << n_queries << " same_topk " << same << "\n";
    if (valid) {
        std::cerr << "exact_ms " << sum_exact / valid
                  << " lb_ms " << sum_lb / valid
                  << " init_ms " << sum_init / valid << "\n";
        std::cerr << "exact_dtw " << (double)sum_dtw_exact / valid
                  << " lb_dtw " << (double)sum_dtw_lb / valid
                  << " init_dtw " << (double)sum_init_dtw / valid << "\n";
    }
    return 0;
}
