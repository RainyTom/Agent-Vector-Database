// Time-partitioned point-ANN candidate generation + exact episode verification.
//
// For each query point and each month overlapping the query window, retrieve
// the top-N nearest point embeddings from that month's HNSW index. Users are
// ranked by how many query points they match (bitmask), then verified with the
// same DTW episode enumeration as scan.cpp.
//
// This is a strong "multi-vector ANN + verify" baseline, not the final index.
//
// Build:
//   g++ -O3 -march=native -std=c++17 -pthread \
//     -I deps/hnswlib ann.cpp -o ann

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "hnswlib/hnswlib.h"

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

static inline float sq_l2(const std::array<float, D>& x, const std::array<float, D>& y) {
    float s = 0.0f;
    for (int i = 0; i < D; ++i) { float d = x[i] - y[i]; s += d * d; }
    return s;
}

static float dtw(const Query& q, const Point* pts, int a, int b, float lambda,
                 float cutoff = std::numeric_limits<float>::infinity()) {
    const int m = q.m;
    const int n = b - a + 1;
    const float INF = std::numeric_limits<float>::infinity();
    const int norm = std::max(m, n);
    const float cutoff_raw = std::isfinite(cutoff) ? cutoff * (float)norm : INF;
    std::array<float, 256> prev{}, cur{};
    std::fill(prev.begin(), prev.begin() + n + 1, INF);
    std::fill(cur.begin(), cur.begin() + n + 1, INF);
    prev[0] = 0.0f;
    for (int i = 1; i <= m; ++i) {
        cur[0] = INF;
        float row_min = INF;
        for (int j = 1; j <= n; ++j) {
            float cost = std::sqrt(sq_l2(q.emb[i - 1], pts[a + j - 1].emb));
            if (lambda > 0.0f) {
                float dt = std::fabs((float)(q.tau[i - 1] - q.tau[0]) -
                                     (float)(pts[a + j - 1].ts - pts[a].ts)) / 3600.0f;
                cost += lambda * dt;
            }
            float best = std::min(prev[j], std::min(cur[j - 1], prev[j - 1]));
            cur[j] = cost + best;
            if (cur[j] < row_min) row_min = cur[j];
        }
        if (row_min > cutoff_raw) return INF;  // all continuations exceed cutoff
        std::swap(prev, cur);
    }
    return prev[n] / (float)norm;
}

static inline bool in_bbox(const Query& q, const Point& p) {
    return p.lat >= q.min_lat && p.lat <= q.max_lat &&
           p.lon >= q.min_lon && p.lon <= q.max_lon;
}

static Hit best_episode(const Query& q, const std::vector<Point>& pts,
                        int off, int len, float lambda,
                        long long* dtw_count, bool* has_window_points,
                        float cutoff = std::numeric_limits<float>::infinity()) {
    Hit best{std::numeric_limits<float>::infinity(), -1, 0, 0, -1, -1};
    *has_window_points = false;
    if (len <= 0) return best;
    int lo = off, hi = off + len;
    int l = lo, r = hi;
    while (l < r) { int mid = (l + r) / 2; if (pts[mid].ts < q.ws) l = mid + 1; else r = mid; }
    int start = l;
    l = start; r = hi;
    while (l < r) { int mid = (l + r) / 2; if (pts[mid].ts <= q.we) l = mid + 1; else r = mid; }
    int end = l;
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
            for (int k = a; k <= b; ++k) if (!in_bbox(q, pts[k])) { ok = false; break; }
            if (!ok) continue;
            float s = dtw(q, pts.data(), a, b, lambda, cutoff);
            ++*dtw_count;
            if (s < best.score) best = {s, pts[a].user, pts[a].ts, pts[b].ts, a, b};
        }
    }
    return best;
}

struct ScanStats {
    long long dtw = 0;
    long long candidate_users = 0;
    long long feasible_users = 0;
};

struct HitInfo {
    int j;
    int64_t ts;
    float dist;
};

// Greedy earliest-timestamp matching: does the user have one ANN hit per query
// point with strictly increasing timestamps? This is a necessary condition for
// an order-preserving episode and is used only for candidate ranking.
static bool ordered_hits(int m, const std::vector<HitInfo>& hs) {
    int64_t prev = std::numeric_limits<int64_t>::min();
    for (int j = 0; j < m; ++j) {
        int64_t best = std::numeric_limits<int64_t>::max();
        for (const auto& h : hs) {
            if (h.j == j && h.ts > prev && h.ts < best) best = h.ts;
        }
        if (best == std::numeric_limits<int64_t>::max()) return false;
        prev = best;
    }
    return true;
}

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
        if (h.user >= 0) { hits.push_back(h); ++stats->feasible_users; }
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& x, const Hit& y) {
        if (x.score != y.score) return x.score < y.score;
        return x.user < y.user;
    });
    if ((int)hits.size() > k) hits.resize(k);
    return hits;
}

static void heap_update(std::priority_queue<Hit>& heap, const Hit& h, int k) {
    if (h.user < 0) return;
    if ((int)heap.size() < k) heap.push(h);
    else if (h.score < heap.top().score ||
             (h.score == heap.top().score && h.user < heap.top().user)) {
        heap.pop();
        heap.push(h);
    }
}

// Candidates must be sorted by their DTW lower bound. Once the heap is full,
// all remaining candidates have LB >= the heap's worst score and can be
// skipped without changing the top-k within the candidate set.
static std::vector<Hit> verify_sorted(const Query& q, const std::vector<Point>& pts,
                                      const std::vector<int32_t>& off,
                                      const std::vector<int32_t>& len,
                                      const std::vector<std::pair<float, int>>& cand,
                                      float lambda, int k, ScanStats* stats,
                                      long long* verified) {
    std::priority_queue<Hit> heap;
    for (auto& [lb, u] : cand) {
        (void)lb;  // ordering heuristic only; not a valid per-user DTW bound
        float cutoff = ((int)heap.size() >= k)
                           ? heap.top().score
                           : std::numeric_limits<float>::infinity();
        bool has_points = false;
        Hit h = best_episode(q, pts, off[u], len[u], lambda, &stats->dtw,
                             &has_points, cutoff);
        ++*verified;
        if (has_points) ++stats->candidate_users;
        if (h.user >= 0) {
            ++stats->feasible_users;
            heap_update(heap, h, k);
        }
    }
    std::vector<Hit> out;
    out.reserve(heap.size());
    while (!heap.empty()) { out.push_back(heap.top()); heap.pop(); }
    std::sort(out.begin(), out.end(), [](const Hit& x, const Hit& y) {
        if (x.score != y.score) return x.score < y.score;
        return x.user < y.user;
    });
    return out;
}

static double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

static int month_key(int64_t ts) {
    std::time_t t = (std::time_t)ts;
    std::tm tm{};
    gmtime_r(&t, &tm);
    // Three-month buckets: fewer HNSW searches per 1-year window.
    return tm.tm_year * 4 + tm.tm_mon / 3;
}

int main(int argc, char** argv) {
    std::string dir = ".";
    int k = 10, topn = 50, cand_cap = 1000, ef = 80;
    int build_threads = 4;
    int limit = -1;
    int only_month = -999999;
    float lambda = 0.0f;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--dir" && i + 1 < argc) dir = argv[++i];
        else if (a == "--k" && i + 1 < argc) k = std::stoi(argv[++i]);
        else if (a == "--topn" && i + 1 < argc) topn = std::stoi(argv[++i]);
        else if (a == "--cand" && i + 1 < argc) cand_cap = std::stoi(argv[++i]);
        else if (a == "--ef" && i + 1 < argc) ef = std::stoi(argv[++i]);
        else if (a == "--threads" && i + 1 < argc) build_threads = std::stoi(argv[++i]);
        else if (a == "--limit" && i + 1 < argc) limit = std::stoi(argv[++i]);
        else if (a == "--only-month" && i + 1 < argc) only_month = std::stoi(argv[++i]);
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

    // Month buckets.
    std::map<int, std::vector<int>> month_points;
    for (int i = 0; i < n_points; ++i) month_points[month_key(pts[i].ts)].push_back(i);
    std::cerr << "months " << month_points.size() << " points " << n_points << " users " << n_users << "\n";

    using Index = hnswlib::HierarchicalNSW<float>;
    std::map<int, std::unique_ptr<Index>> indexes;
    double build0 = now_ms();
    for (auto& [mkey, ids] : month_points) {
        if (only_month != -999999 && mkey != only_month) continue;
        auto* space = new hnswlib::L2Space(D);
        auto idx = std::make_unique<Index>(space, ids.size(), 16, 100);
        idx->setEf(ef);
        // addPoint is thread-safe in hnswlib; parallel insertion per month.
        std::vector<std::thread> workers;
        int nthreads = std::max(1, build_threads);
        int chunk = (ids.size() + nthreads - 1) / nthreads;
        for (int t = 0; t < nthreads; ++t) {
            int begin = t * chunk, end = std::min((int)ids.size(), begin + chunk);
            if (begin >= end) break;
            workers.emplace_back([&, begin, end]() {
                for (int j = begin; j < end; ++j) {
                    // This hnswlib revision corrupts its visited list when many
                    // points are exact duplicates (common for POI embeddings).
                    // A deterministic sub-1e-5 jitter makes points distinct
                    // without changing ranking at our precision.
                    std::array<float, D> v = pts[ids[j]].emb;
                    uint32_t h = (uint32_t)ids[j] * 2654435761u;
                    for (int d = 0; d < 8; ++d) {
                        float u = (float)((h >> ((d % 4) * 8)) & 255) / 255.0f - 0.5f;
                        v[d] += 0.01f * u;
                    }
                    idx->addPoint(v.data(), ids[j]);
                }
            });
        }
        for (auto& w : workers) w.join();
        indexes[mkey] = std::move(idx);
    }
    double build_ms = now_ms() - build0;
    std::cerr << "build_ms " << build_ms << "\n";

    std::ofstream out(dir + "/ann_results.csv");
    out << "qid,m,exact_us,exact_dtw,exact_hits,"
        << "ann_us,ann_gen_us,ann_verify_us,ann_candidates,ann_verified,ann_dtw,ann_hits,recall_ann\n";

    double sum_exact = 0, sum_ann = 0, sum_recall = 0, sum_gen = 0, sum_verify = 0;
    long long sum_dtw_exact = 0, sum_dtw_ann = 0;
    int valid = 0;

    int nq = (limit > 0) ? std::min(limit, (int)n_queries) : n_queries;
    for (int qi = 0; qi < nq; ++qi) {
        const Query& q = queries[qi];
        std::vector<int> all_users;
        for (int u = 0; u < n_users; ++u) if (u != q.source) all_users.push_back(u);

        ScanStats es;
        double t0 = now_ms();
        auto exact = topk_from_users(q, pts, off, len, all_users, lambda, k, &es);
        double t1 = now_ms();
        if ((int)exact.size() < k) continue;
        ++valid;
        sum_exact += t1 - t0;
        sum_dtw_exact += es.dtw;

        // Candidate generation: per query point, search months overlapping W.
        // Hits are grouped by user; verification enforces temporal validity.
        // The sum of best per-query-point distances is a lower bound on
        // unnormalized DTW and ranks candidates better than a match count.
        std::unordered_map<int, std::array<float, MAXM>> dists;
        std::unordered_map<int, uint8_t> mask;
        std::unordered_map<int, std::vector<HitInfo>> hits;
        double g0 = now_ms();
        int m0 = month_key(q.ws), m1 = month_key(q.we);
        for (auto it = indexes.lower_bound(m0); it != indexes.end() && it->first <= m1; ++it) {
            Index* idx = it->second.get();
            for (int j = 0; j < q.m; ++j) {
                auto res = idx->searchKnn(q.emb[j].data(), topn);
                while (!res.empty()) {
                    auto [dist, label] = res.top(); res.pop();
                    if (label >= pts.size()) {
                        std::cerr << "bad label " << label << " >= " << pts.size() << "\n";
                        continue;
                    }
                    if (pts[label].ts < q.ws || pts[label].ts > q.we) continue;
                    if (!in_bbox(q, pts[label])) continue;
                    int u = pts[label].user;
                    if (u == q.source) continue;
                    int64_t key = u;
                    mask[key] |= (uint8_t)(1u << j);
                    hits[key].push_back({j, pts[label].ts, dist});
                    auto itd = dists.find(key);
                    if (itd == dists.end()) {
                        std::array<float, MAXM> arr;
                        arr.fill(std::numeric_limits<float>::infinity());
                        arr[j] = dist;
                        dists.emplace(key, arr);
                    } else if (dist < itd->second[j]) {
                        itd->second[j] = dist;
                    }
                }
            }
        }
        struct Ranked { int order_ok; float lb; int matches; int64_t key; };
        std::vector<Ranked> ranked;
        ranked.reserve(mask.size());
        for (auto& [key, mk] : mask) {
            const auto& arr = dists[key];
            float lb = 0.0f;
            for (int j = 0; j < q.m; ++j) {
                if (std::isfinite(arr[j])) lb += arr[j];
                else lb += 2.0f;  // ranking heuristic: missing query point is bad
            }
            lb /= (float)q.m;
            int ord = ordered_hits(q.m, hits[key]) ? 1 : 0;
            ranked.push_back({ord, lb, __builtin_popcount(mk), key});
        }
        std::sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
            if (a.order_ok != b.order_ok) return a.order_ok > b.order_ok;
            if (a.lb != b.lb) return a.lb < b.lb;
            if (a.matches != b.matches) return a.matches > b.matches;
            return a.key < b.key;
        });
        if ((int)ranked.size() > cand_cap) ranked.resize(cand_cap);
        std::vector<std::pair<float, int>> cand;
        std::unordered_set<int> seen;
        cand.reserve(ranked.size());
        for (auto& r : ranked) {
            int u = (int)r.key;
            if (u < 0 || u >= n_users) {
                std::cerr << "bad candidate user " << u << " n_users " << n_users << "\n";
                continue;
            }
            if (seen.insert(u).second) cand.push_back({r.lb, u});
        }
        double g1 = now_ms();

        ScanStats as;
        long long verified = 0;
        double v0 = now_ms();
        auto ann = verify_sorted(q, pts, off, len, cand, lambda, k, &as, &verified);
        double v1 = now_ms();
        std::unordered_set<int> exact_set;
        for (const auto& h : exact) exact_set.insert(h.user);
        int hit = 0;
        for (const auto& h : ann) if (exact_set.count(h.user)) ++hit;
        double recall = exact.empty() ? 0.0 : (double)hit / exact.size();
        sum_ann += v1 - g0;
        sum_gen += g1 - g0;
        sum_verify += v1 - v0;
        sum_dtw_ann += as.dtw;
        sum_recall += recall;

        out << qi << "," << q.m << ","
            << (t1 - t0) << "," << es.dtw << "," << exact.size() << ","
            << (v1 - g0) << "," << (g1 - g0) << "," << (v1 - v0) << ","
            << cand.size() << "," << verified << "," << as.dtw << "," << ann.size() << "," << recall << "\n";
    }
    out.close();
    std::cerr << "valid " << valid << "/" << n_queries << "\n";
    if (valid) {
        std::cerr << "exact_ms " << sum_exact / valid
                  << " ann_ms " << sum_ann / valid
                  << " gen_ms " << sum_gen / valid
                  << " verify_ms " << sum_verify / valid << "\n";
        std::cerr << "exact_dtw " << (double)sum_dtw_exact / valid
                  << " ann_dtw " << (double)sum_dtw_ann / valid << "\n";
        std::cerr << "recall_ann " << sum_recall / valid << "\n";
    }
    std::cerr << "build_ms_total " << build_ms << "\n";
    return 0;
}
