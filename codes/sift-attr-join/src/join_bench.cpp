// 层级品类 × facet 约束下的阈值向量连接：B0(oracle) / B2(posting) 四段计时基线
// 用法: ./join_bench --data datasets/sift-attr --sift datasets/sift-128-euclidean.hdf5 \
//                    --method b0|b2 --quantiles 0.001,0.005,0.01,0.02,0.05 --repeat 3
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <numeric>
#include <sstream>
#include <string>
#include <sys/resource.h>
#include <unordered_map>
#include <vector>

struct Obj {
    int vec = 0;
    int cat = 0;
    int f[3] = {-1, -1, -1};
};

struct Query {
    int u = 0;
    std::vector<std::pair<int, int>> phi;
};

static const int UNKNOWN = -1;

struct Tree {
    std::vector<int> parent;
    std::vector<std::vector<int>> children;
    std::vector<std::vector<int>> anc;   // 含自身
    std::vector<std::vector<int>> desc;  // 含自身
    int root = -1;

    void build(int n) {
        parent.assign(n, -2);
        children.assign(n, {});
        anc.assign(n, {});
        desc.assign(n, {});
    }
    void finalize() {
        for (size_t v = 0; v < parent.size(); ++v) {
            if (parent[v] == -1) root = int(v);
            if (parent[v] >= 0) children[parent[v]].push_back(int(v));
        }
        for (size_t v = 0; v < parent.size(); ++v) {
            for (int c = int(v); c >= 0; c = parent[c]) anc[v].push_back(c);
        }
        for (size_t v = 0; v < parent.size(); ++v)
            for (int a : anc[v]) desc[a].push_back(int(v));
    }
};

static std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string t;
    while (ss >> t) out.push_back(t);
    return out;
}

static double now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

static long peak_rss_kb() {
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss;
}

int main(int argc, char** argv) {
    std::string data = "datasets/sift-attr", sift, method = "b2", treepath = "datasets/scihtc/scihtc_tree.txt", quant = "0.001,0.005,0.01,0.02,0.05";
    int repeat = 3;
    bool skip_dist = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() { return std::string(argv[++i]); };
        if (a == "--data") data = next();
        else if (a == "--sift") sift = next();
        else if (a == "--method") method = next();
        else if (a == "--quantiles") quant = next();
        else if (a == "--repeat") repeat = std::stoi(next());
        else if (a == "--tree") treepath = next();
        else if (a == "--skip-dist") skip_dist = true;
    }
    std::vector<double> qs;
    { std::stringstream ss(quant); std::string t; while (std::getline(ss, t, ',')) qs.push_back(std::stod(t)); }

    // ---- 树
    Tree tree;
    {
        std::ifstream in(treepath);
        std::string line;
        std::getline(in, line);
        int edges = std::stoi(line);
        tree.build(0);
        std::vector<std::pair<int, int>> rows;
        int maxnode = 0;
        for (int k = 0; k < edges; ++k) {
            std::getline(in, line);
            auto f = split(line);
            if (f.empty()) continue;
            int c = std::stoi(f[0]), p = std::stoi(f[1]);
            rows.push_back({c, p});
            maxnode = std::max(maxnode, std::max(c, p));
        }
        tree.build(maxnode + 1);
        for (auto& [c, p] : rows) tree.parent[c] = p;
        tree.finalize();
    }

    // ---- 对象
    auto load = [&](const std::string& path) {
        std::vector<Obj> v;
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            auto f = split(line);
            if (f.size() < 6) continue;
            Obj o;
            o.vec = std::stoi(f[1]);
            o.cat = std::stoi(f[2]);
            for (int d = 0; d < 3; ++d) o.f[d] = std::stoi(f[3 + d]);
            v.push_back(o);
        }
        return v;
    };
    std::vector<Obj> A = load(data + "/objects_A.tsv");
    std::vector<Obj> B = load(data + "/objects_B.tsv");

    // ---- 查询
    std::vector<Query> queries;
    {
        std::ifstream in(data + "/queries.tsv");
        std::string line;
        while (std::getline(in, line)) {
            auto f = split(line);
            if (f.size() < 3) continue;
            Query q;
            q.u = std::stoi(f[1]);
            int k = std::stoi(f[2]);
            for (int i = 0; i < k; ++i) q.phi.push_back({std::stoi(f[3 + 2 * i]), std::stoi(f[4 + 2 * i])});
            queries.push_back(q);
        }
    }

    // ---- B 侧 posting：概念 -> 局部下标
    std::vector<std::vector<int>> locB(tree.children.size());
    std::vector<std::vector<int>> locA(tree.children.size());
    for (size_t i = 0; i < B.size(); ++i) locB[B[i].cat].push_back(int(i));
    for (size_t i = 0; i < A.size(); ++i) locA[A[i].cat].push_back(int(i));

    auto in_phi = [](const Obj& o, const std::vector<std::pair<int, int>>& phi) {
        for (auto& [d, v] : phi)
            if (o.f[d] != v) return false;
        return true;
    };
    auto attr_ok = [](const Obj& a, const Obj& b) {
        for (int d = 0; d < 3; ++d)
            if (a.f[d] != UNKNOWN && b.f[d] != UNKNOWN && a.f[d] != b.f[d]) return false;
        return true;
    };

    // ---- 向量（由 sift 转出的 fvecs 简化：这里直接从原始 hdf5 导出的 .bin 读取）
    std::vector<float> VA(size_t(A.size()) * 128), VB(size_t(B.size()) * 128);
    {
        FILE* f = std::fopen((data + "/cache/vectors.bin").c_str(), "rb");
        if (!f) { std::fprintf(stderr, "missing %s/cache/vectors.bin (run export_vectors.py)\n", data.c_str()); return 2; }
        size_t n1 = std::fread(VA.data(), sizeof(float), VA.size(), f);
        size_t n2 = std::fread(VB.data(), sizeof(float), VB.size(), f);
        std::fclose(f);
        if (n1 != VA.size() || n2 != VB.size()) { std::fprintf(stderr, "vectors.bin size mismatch\n"); return 2; }
    }
    std::vector<float> nrmA(A.size()), nrmB(B.size());
    for (size_t i = 0; i < A.size(); ++i) { float s = 0; for (int d = 0; d < 128; ++d) s += VA[i * 128 + d] * VA[i * 128 + d]; nrmA[i] = s; }
    for (size_t i = 0; i < B.size(); ++i) { float s = 0; for (int d = 0; d < 128; ++d) s += VB[i * 128 + d] * VB[i * 128 + d]; nrmB[i] = s; }

    const bool use_b2 = (method == "b2");
    struct Totals {
        long long cand = 0, dist = 0, out = 0, pred = 0;
        double t_gen = 0, t_pred = 0, t_dist = 0, t_out = 0, t_total = 0;
        std::vector<long long> out_by_q;
    } tot;
    tot.out_by_q.assign(qs.size(), 0);

    std::vector<double> dists;
    for (int rep = 0; rep < repeat; ++rep) {
        Totals cur;
        cur.out_by_q.assign(qs.size(), 0);
        dists.clear();
        double t_all0 = now();
        for (auto& q : queries) {
            // 作用域：Desc(u) 且满足 φ
            std::vector<int> Ai, Bi;
            double t0 = now();
            for (int c : tree.desc[q.u]) {
                for (int i : locA[c]) if (in_phi(A[i], q.phi)) Ai.push_back(i);
                for (int i : locB[c]) if (in_phi(B[i], q.phi)) Bi.push_back(i);
            }
            std::vector<char> inScope(tree.children.size(), 0);
            for (int c : tree.desc[q.u]) inScope[c] = 1;
            cur.t_gen += now() - t0;

            dists.clear();
            double t1 = now();
            if (!use_b2) {
                // B0：A_Q × B_Q 逐对判定
                for (int i : Ai) {
                    const Obj& a = A[i];
                    for (int j : Bi) {
                        const Obj& b = B[j];
                        ++cur.pred;
                        bool cat = false;
                        for (int x : tree.anc[b.cat]) if (x == a.cat) { cat = true; break; }
                        if (!cat) for (int x : tree.anc[a.cat]) if (x == b.cat) { cat = true; break; }
                        if (!cat) continue;
                        if (!attr_ok(a, b)) continue;
                        float s = 0.0f;
                        if (!skip_dist) {
                            s = nrmA[i] + nrmB[j];
                            for (int d = 0; d < 128; ++d) s -= 2.0f * VA[i * 128 + d] * VB[j * 128 + d];
                        }
                        dists.push_back(s);
                        ++cur.dist;
                    }
                }
            } else {
                // B2：仅枚举品类兼容块（anc(c_a) ∪ desc(c_a) 的 B 局部表），块内判属性
                for (int i : Ai) {
                    const Obj& a = A[i];
                    for (int c : tree.anc[a.cat]) {
                        if (!inScope[c]) continue;
                        for (int j : locB[c]) {
                            if (!in_phi(B[j], q.phi)) continue;
                            ++cur.pred;
                            if (!attr_ok(a, B[j])) continue;
                            float s = 0.0f;
                            if (!skip_dist) {
                                s = nrmA[i] + nrmB[j];
                                for (int d = 0; d < 128; ++d) s -= 2.0f * VA[i * 128 + d] * VB[j * 128 + d];
                            }
                            dists.push_back(s);
                            ++cur.dist;
                        }
                    }
                    for (int c : tree.desc[a.cat]) {
                        if (c == a.cat) continue;
                        for (int j : locB[c]) {
                            if (!in_phi(B[j], q.phi)) continue;
                            ++cur.pred;
                            if (!attr_ok(a, B[j])) continue;
                            float s = 0.0f;
                            if (!skip_dist) {
                                s = nrmA[i] + nrmB[j];
                                for (int d = 0; d < 128; ++d) s -= 2.0f * VA[i * 128 + d] * VB[j * 128 + d];
                            }
                            dists.push_back(s);
                            ++cur.dist;
                        }
                    }
                }
            }
            cur.t_pred += now() - t1;   // 生成+判定合并计时（dist 在内部，下面减去）
            double t2 = now();
            if (!dists.empty()) {
                for (size_t qi = 0; qi < qs.size(); ++qi) {
                    std::vector<float> tmp(dists.begin(), dists.end());
                    size_t kth = size_t(qs[qi] * (tmp.size() - 1));
                    std::nth_element(tmp.begin(), tmp.begin() + kth, tmp.end());
                    float eps = tmp[kth];
                    size_t c = 0;
                    for (float v : tmp) c += (v <= eps);
                    cur.out_by_q[qi] += (long long)c;
                }
            }
            cur.t_out += now() - t2;
        }
        cur.t_total = now() - t_all0;
        if (rep == 0) tot = cur;
        else {
            tot.t_gen += cur.t_gen; tot.t_pred += cur.t_pred; tot.t_out += cur.t_out; tot.t_total += cur.t_total;
            for (size_t qi = 0; qi < qs.size(); ++qi) tot.out_by_q[qi] += cur.out_by_q[qi];
        }
    }
    if (repeat > 1) {
        tot.t_gen /= repeat; tot.t_pred /= repeat; tot.t_out /= repeat; tot.t_total /= repeat;
        for (auto& v : tot.out_by_q) v /= repeat;
    }
    std::printf("{\n  \"method\": \"%s\", \"skip_dist\": %s, \"repeat\": %d, \"A\": %zu, \"B\": %zu, \"queries\": %zu,\n",
                method.c_str(), skip_dist ? "true" : "false", repeat, A.size(), B.size(), queries.size());
    std::printf("  \"candidate_pred_checks\": %lld, \"distance_calls\": %lld,\n", tot.pred, tot.dist);
    std::printf("  \"t_gen_s\": %.4f, \"t_pred_dist_s\": %.4f, \"t_out_s\": %.4f, \"t_total_s\": %.4f,\n",
                tot.t_gen, tot.t_pred, tot.t_out, tot.t_total);
    std::printf("  \"out_by_quantile\": {");
    for (size_t i = 0; i < qs.size(); ++i) std::printf("%s\"%g\": %lld", i ? ", " : "", qs[i], tot.out_by_q[i]);
    std::printf("},\n  \"peak_rss_kb\": %ld\n}\n", peak_rss_kb());
    return 0;
}
