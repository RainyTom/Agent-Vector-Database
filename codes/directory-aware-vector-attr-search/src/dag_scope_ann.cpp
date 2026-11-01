// End-to-end probe: DAG scope resolution feeding a filtered HNSW search.
//
// Each query resolves its scope under three strategies (online expansion,
// materialise everything, materialise by break-even rule), then either scans the
// eligible set exactly or searches one global HNSW with an eligibility filter.
// Reports per-strategy metadata/search time and recall against exact ground truth.
#include "dag_scope_common.h"

#include "raw_l2_space.h"

#include "hnswlib/hnswlib.h"

#include <filesystem>
#include <fstream>
#include <queue>
#include <set>

namespace {

struct Vectors {
    size_t dim = 0;
    std::vector<float> data;   // row-major, count * dim
    size_t count() const { return dim ? data.size() / dim : 0; }
};

Vectors load_fvecs(const std::string& path, size_t limit) {
    std::ifstream in(path, std::ios::binary);
    if (!in) fail("cannot open " + path);
    Vectors out;
    std::vector<float> buffer;
    while (out.count() < limit) {
        int32_t dim = 0;
        if (!in.read(reinterpret_cast<char*>(&dim), sizeof(dim))) break;
        if (dim <= 0) fail("bad fvecs record header in " + path);
        if (!out.dim) out.dim = static_cast<size_t>(dim);
        if (static_cast<size_t>(dim) != out.dim) fail("inconsistent fvecs dimension");
        buffer.assign(out.dim, 0.0f);
        if (!in.read(reinterpret_cast<char*>(buffer.data()), out.dim * sizeof(float)))
            fail("truncated fvecs record in " + path);
        out.data.insert(out.data.end(), buffer.begin(), buffer.end());
    }
    if (!out.count()) fail("empty fvecs file " + path);
    return out;
}

struct CountedSpace : RawL2Space {
    uint64_t calls = 0;
    explicit CountedSpace(size_t dim) : RawL2Space(dim) {}
    static float counted(const void* a, const void* b, const void* param) {
        auto* self = const_cast<CountedSpace*>(static_cast<const CountedSpace*>(param));
        ++self->calls;
        return self->dist(static_cast<const float*>(a), static_cast<const float*>(b));
    }
    hnswlib::DISTFUNC<float> get_dist_func() override { return counted; }
    void* get_dist_func_param() override { return this; }
};

struct ScopeFilter : hnswlib::BaseFilterFunctor {
    const Bitmap& bitmap;
    explicit ScopeFilter(const Bitmap& b) : bitmap(b) {}
    bool operator()(hnswlib::labeltype label) override { return bitmap.test(static_cast<Id>(label)); }
};

// Same pattern as the packed runner: manual upper-layer descent, then the
// pinned base-layer search with the eligibility filter and hop metrics.
std::vector<std::pair<float, Id>> search_graph(hnswlib::HierarchicalNSW<float>& graph,
                                               const float* query, size_t k, size_t ef,
                                               ScopeFilter& filter) {
    std::vector<std::pair<float, Id>> result;
    if (!k || !graph.cur_element_count) return result;
    auto current = graph.enterpoint_node_;
    float best = graph.fstdistfunc_(query, graph.getDataByInternalId(current), graph.dist_func_param_);
    for (int level = graph.maxlevel_; level > 0; --level) {
        bool changed = true;
        while (changed) {
            changed = false;
            auto* links = graph.get_linklist(current, level);
            const size_t count = graph.getListCount(links);
            const auto* neighbors = reinterpret_cast<const hnswlib::tableint*>(links + 1);
            for (size_t i = 0; i < count; ++i) {
                const auto candidate = neighbors[i];
                const float distance = graph.fstdistfunc_(query, graph.getDataByInternalId(candidate),
                                                          graph.dist_func_param_);
                if (distance < best) {
                    best = distance;
                    current = candidate;
                    changed = true;
                }
            }
        }
    }
    auto found = graph.searchBaseLayerST<false, true>(current, query, ef, &filter);
    while (!found.empty()) {
        result.emplace_back(found.top().first, static_cast<Id>(graph.getExternalLabel(found.top().second)));
        found.pop();
    }
    std::sort(result.begin(), result.end());
    if (result.size() > k) result.resize(k);
    return result;
}

} // namespace

int main(int argc, char** argv) {
    std::string dir = "../../work/dag-scope/workload-run1";
    std::string out = "../../work/dag-scope/ann-run1";
    std::string vector_path;
    std::string query_path;
    size_t ef = 20, k = 10, tau = 512, max_queries = 1234;
    size_t budget_bytes = 64ull << 20;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() { if (i + 1 >= argc) fail("missing value for " + arg); return std::string(argv[++i]); };
        if (arg == "--data") dir = next();
        else if (arg == "--out") out = next();
        else if (arg == "--vectors") vector_path = next();
        else if (arg == "--queries") query_path = next();
        else if (arg == "--ef") ef = std::stoull(next());
        else if (arg == "--k") k = std::stoull(next());
        else if (arg == "--tau") tau = std::stoull(next());
        else if (arg == "--max-queries") max_queries = std::stoull(next());
        else if (arg == "--budget-mb") budget_bytes = std::stoull(next()) << 20;
        else fail("unknown argument " + arg);
    }
    if (vector_path.empty() || query_path.empty()) fail("--vectors and --queries are required");

    Graph graph = build_graph(dir);
    Workload work = load(dir, 20261101);
    const size_t n = work.objects;
    const size_t words = (n + 63) / 64;

    Vectors base = load_fvecs(vector_path, n);
    if (base.count() < n) fail("not enough vectors for objects");
    Vectors queries = load_fvecs(query_path, max_queries);
    if (queries.dim != base.dim) fail("query/vector dimension mismatch");
    const size_t dim = base.dim;
    const size_t nq = std::min(queries.count(), std::min(max_queries, work.queries.size()));

    CountedSpace space(dim);
    hnswlib::HierarchicalNSW<float> index(&space, n, 16, 200);
    const auto build_start = Clock::now();
    for (Id object = 0; object < n; ++object)
        index.addPoint(base.data.data() + size_t(object) * dim, object);
    const double build_s = micros(build_start) / 1e6;
    index.setEf(ef);

    // Materialisation plans.
    std::vector<uint64_t> expansion(graph.active(), 0);
    for (size_t c = 0; c < graph.active(); ++c) {
        uint64_t cost = 0;
        for (Id d : graph.descendants[c]) cost += 1 + work.postings[d].size();
        expansion[c] = cost;
    }
    std::vector<Bitmap> mat_all(graph.active()), mat_select(graph.active());
    for (size_t c = 0; c < graph.active(); ++c) {
        bool wide = false;
        for (Id d : graph.descendants[c]) if (!work.postings[d].empty()) wide = true;
        if (!wide) continue;
        Bitmap bitmap;
        bitmap.words.assign(words, 0);
        for (Id d : graph.descendants[c])
            for (Id object : work.postings[d]) bitmap.set(object);
        mat_all[c] = bitmap;
    }
    {
        std::vector<size_t> order;
        for (size_t c = 0; c < graph.active(); ++c) if (expansion[c] > words) order.push_back(c);
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return expansion[a] != expansion[b] ? expansion[a] > expansion[b] : a < b; });
        size_t used = 0;
        for (size_t c : order) {
            if (used + words * sizeof(uint64_t) > budget_bytes) break;
            mat_select[c] = mat_all[c];
            used += words * sizeof(uint64_t);
        }
    }

    const char* names[3] = {"online", "mat_all", "mat_select"};
    std::vector<Bitmap>* plans[3] = {nullptr, &mat_all, &mat_select};
    std::vector<double> scope_us[3], search_us[3], e2e_us[3], recall[3];
    size_t exact_paths[3] = {0, 0, 0}, graph_paths[3] = {0, 0, 0};
    std::vector<double> wall_ns[3];

    for (size_t q = 0; q < nq; ++q) {
        const auto& query = work.queries[q];
        const float* qvec = queries.data.data() + q * dim;

        Bitmap resolved;
        resolved.words.assign(words, 0);
        for (Id c : query)
            for (Id d : graph.descendants[c])
                for (Id object : work.postings[d]) resolved.set(object);

        // Exact ground truth inside the scope.
        std::vector<std::pair<float, Id>> truth;
        const auto gt_start = Clock::now();
        for (size_t w = 0; w < words; ++w)
            for (uint64_t bits = resolved.words[w]; bits; bits &= bits - 1) {
                const Id object = static_cast<Id>(w * 64 + static_cast<unsigned>(__builtin_ctzll(bits)));
                truth.emplace_back(space.dist(qvec, base.data.data() + size_t(object) * dim), object);
            }
        std::sort(truth.begin(), truth.end());
        if (truth.size() > k) truth.resize(k);
        const double gt_us = micros(gt_start);
        const size_t kq = truth.size();

        for (int slot = 0; slot < 3; ++slot) {
            const int s = static_cast<int>((q + slot) % 3);
            Bitmap scope;
            scope.words.assign(words, 0);
            const auto scope_start = Clock::now();
            for (Id c : query) {
                if (plans[s] && !(*plans[s])[c].words.empty()) {
                    const auto& source = (*plans[s])[c].words;
                    for (size_t w = 0; w < words; ++w) scope.words[w] |= source[w];
                } else {
                    for (Id d : graph.descendants[c])
                        for (Id object : work.postings[d]) scope.set(object);
                }
            }
            const double scope_time = micros(scope_start);
            const size_t eligible = scope.count();

            std::vector<std::pair<float, Id>> result;
            const auto search_start = Clock::now();
            if (eligible && eligible <= tau) {
                for (size_t w = 0; w < words; ++w)
                    for (uint64_t bits = scope.words[w]; bits; bits &= bits - 1) {
                        const Id object = static_cast<Id>(w * 64 + static_cast<unsigned>(__builtin_ctzll(bits)));
                        result.emplace_back(space.dist(qvec, base.data.data() + size_t(object) * dim), object);
                    }
                std::sort(result.begin(), result.end());
                if (result.size() > k) result.resize(k);
                ++exact_paths[s];
            } else if (eligible) {
                ScopeFilter filter(scope);
                result = search_graph(index, qvec, std::min(k, eligible), ef, filter);
                ++graph_paths[s];
            }
            const double search_time = micros(search_start);
            (void)gt_us;

            size_t hits = 0;
            for (const auto& entry : result)
                for (size_t t = 0; t < truth.size(); ++t)
                    if (truth[t].second == entry.second) { ++hits; break; }
            scope_us[s].push_back(scope_time);
            search_us[s].push_back(search_time);
            e2e_us[s].push_back(scope_time + search_time);
            recall[s].push_back(kq ? double(hits) / double(kq) : 1.0);
        }
        (void)wall_ns;
    }

    auto mean = [](const std::vector<double>& values) {
        double total = 0;
        for (double v : values) total += v;
        return values.empty() ? 0.0 : total / values.size();
    };

    std::ostringstream report;
    report << "{\n";
    report << "  \"kind\": \"dag_scope_ann_probe\",\n";
    report << "  \"objects\": " << n << ", \"concepts\": " << graph.active()
           << ", \"queries\": " << nq << ", \"ef\": " << ef << ", \"k\": " << k
           << ", \"tau\": " << tau << ",\n";
    report << "  \"build_s\": " << build_s << ",\n";
    report << "  \"strategies\": {";
    for (int s = 0; s < 3; ++s) {
        report << (s ? "," : "") << "\n    \"" << names[s] << "\": {\"scope_us\": "
               << mean(scope_us[s]) << ", \"search_us\": " << mean(search_us[s])
               << ", \"e2e_us\": " << mean(e2e_us[s])
               << ", \"e2e_p95_us\": " << percentile(e2e_us[s], .95)
               << ", \"macro_recall\": " << mean(recall[s])
               << ", \"exact_queries\": " << exact_paths[s]
               << ", \"graph_queries\": " << graph_paths[s] << "}";
    }
    report << "\n  },\n";
    report << "  \"limits\": [\"one global HNSW with an eligibility filter; no partitioning\", "
              "\"vectors are SIFT rows bound to SciHTC labels, so labels carry no vector semantics\", "
              "\"residency warm; no cold-start or independent rebuilds\"]\n";
    report << "}\n";

    std::filesystem::create_directories(out);
    std::ofstream handle(out + "/ann.json");
    handle << report.str();
    std::cout << report.str();
    return 0;
}
