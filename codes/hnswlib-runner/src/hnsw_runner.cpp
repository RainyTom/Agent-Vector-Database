// Plain hnswlib runner: build and search an HNSW graph with a fixed parameter set.
// Used as the shared ANN baseline for the DSM motivation study; every distance goes
// through hnswlib's pinned L2Space via RawL2Space.
#include "raw_l2_space.h"

#include "hnswlib/hnswlib.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Matrix {
    size_t rows = 0, cols = 0;
    std::vector<float> data;
};

[[noreturn]] void fail(const std::string& message) {
    std::fprintf(stderr, "hnsw_runner: %s\n", message.c_str());
    std::exit(2);
}

Matrix load_fvecs(const std::string& path, size_t limit) {
    std::ifstream in(path, std::ios::binary);
    if (!in) fail("cannot open " + path);
    Matrix out;
    std::vector<float> row;
    while (out.rows < limit) {
        int32_t dim = 0;
        if (!in.read(reinterpret_cast<char*>(&dim), sizeof(dim))) break;
        if (dim <= 0) fail("bad fvecs header");
        if (!out.cols) out.cols = size_t(dim);
        if (size_t(dim) != out.cols) fail("inconsistent dim");
        row.assign(out.cols, 0.0f);
        if (!in.read(reinterpret_cast<char*>(row.data()), out.cols * sizeof(float)))
            fail("truncated fvecs");
        out.data.insert(out.data.end(), row.begin(), row.end());
        ++out.rows;
    }
    return out;
}

std::vector<std::vector<int32_t>> load_groundtruth(const std::string& path, size_t queries) {
    std::ifstream in(path, std::ios::binary);
    if (!in) fail("cannot open " + path);
    std::vector<std::vector<int32_t>> out;
    std::vector<int32_t> row;
    for (size_t q = 0; q < queries; ++q) {
        int32_t dim = 0;
        if (!in.read(reinterpret_cast<char*>(&dim), sizeof(dim))) break;
        if (dim <= 0) fail("bad ivecs header");
        row.assign(size_t(dim), 0);
        if (!in.read(reinterpret_cast<char*>(row.data()), size_t(dim) * sizeof(int32_t)))
            fail("truncated ivecs");
        out.push_back(row);
    }
    return out;
}

// hnswlib's filter hook: one bool per row, supplied by the scope representation.
struct MaskFilter : hnswlib::BaseFilterFunctor {
    const char* row = nullptr;
    bool operator()(hnswlib::labeltype id) override { return row[id] != 0; }
};

struct CountingSpace : RawL2Space {
    explicit CountingSpace(size_t dim) : RawL2Space(dim) {}
    static float counted(const void* a, const void* b, const void* param) {
        auto* self = const_cast<CountingSpace*>(static_cast<const CountingSpace*>(param));
        ++self->calls;
        return self->dist(static_cast<const float*>(a), static_cast<const float*>(b));
    }
    hnswlib::DISTFUNC<float> get_dist_func() override { return counted; }
    void* get_dist_func_param() override { return this; }
    uint64_t calls = 0;
};

double now_ms() {
    using clock = std::chrono::steady_clock;
    static const clock::time_point start = clock::now();
    return std::chrono::duration<double, std::milli>(clock::now() - start).count();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) fail("usage: build|search");
    const std::string command = argv[1];
    std::string base_path, query_path, gt_path, index_path, mask_path, out_path;
    size_t M = 32, ef_construction = 40, queries = 10000, threads = 1, query_limit = 10000;
    int k = 10;
    std::string ef_list = "16,32,64,128,256";
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() { return std::string(argv[++i]); };
        if (arg == "--base") base_path = next();
        else if (arg == "--query") query_path = next();
        else if (arg == "--gt") gt_path = next();
        else if (arg == "--index") index_path = next();
        else if (arg == "--mask") mask_path = next();
        else if (arg == "--out") out_path = next();
        else if (arg == "--M") M = std::stoul(next());
        else if (arg == "--efC") ef_construction = std::stoul(next());
        else if (arg == "--efs") ef_list = next();
        else if (arg == "--k") k = std::stoi(next());
        else if (arg == "--queries") query_limit = std::stoul(next());
        else if (arg == "--threads") threads = std::stoul(next());
        else fail("unknown flag " + arg);
    }

    CountingSpace space(128);

    if (command == "build") {
        Matrix base = load_fvecs(base_path, std::numeric_limits<size_t>::max());
        const double started = now_ms();
        hnswlib::HierarchicalNSW<float> graph(&space, base.rows, M, ef_construction, 42);
        std::atomic<size_t> next_object{0};
        std::vector<std::thread> pool;
        for (size_t t = 0; t < std::max<size_t>(1, threads); ++t) {
            pool.emplace_back([&]() {
                for (;;) {
                    const size_t object = next_object.fetch_add(1);
                    if (object >= base.rows) return;
                    graph.addPoint(base.data.data() + object * base.cols,
                                   static_cast<hnswlib::labeltype>(object));
                }
            });
        }
        for (auto& worker : pool) worker.join();
        const double build_ms = now_ms() - started;
        graph.saveIndex(index_path);
        std::printf("{\"stage\":\"build\",\"objects\":%zu,\"dim\":%zu,\"M\":%zu,\"efC\":%zu,"
                    "\"threads\":%zu,\"build_ms\":%.1f}\n",
                    base.rows, base.cols, M, ef_construction, threads, build_ms);
        return 0;
    }

    if (command == "search") {
        Matrix queries_all = load_fvecs(query_path, query_limit);
        auto groundtruth = load_groundtruth(gt_path, query_limit);
        const size_t nq = std::min({queries_all.rows, groundtruth.size(), query_limit});

        std::vector<char> mask;
        if (!mask_path.empty()) {
            std::ifstream in(mask_path, std::ios::binary);
            if (!in) fail("cannot open mask");
            in.seekg(0, std::ios::end);
            mask.resize(size_t(in.tellg()));
            in.seekg(0, std::ios::beg);
            in.read(mask.data(), std::streamsize(mask.size()));
        }

        hnswlib::HierarchicalNSW<float> graph(&space, index_path, false, 0);
        const size_t n = size_t(graph.cur_element_count.load());

        std::vector<size_t> efs;
        {
            size_t begin = 0;
            while (begin <= ef_list.size()) {
                const size_t comma = ef_list.find(',', begin);
                const std::string token = ef_list.substr(
                    begin, comma == std::string::npos ? std::string::npos : comma - begin);
                if (!token.empty()) efs.push_back(std::stoul(token));
                if (comma == std::string::npos) break;
                begin = comma + 1;
            }
        }

        std::ofstream file;
        std::FILE* sink = stdout;
        if (!out_path.empty()) { file.open(out_path); }

        for (size_t ef : efs) {
            graph.setEf(std::max<size_t>(ef, size_t(k)));
            const uint64_t calls_before = space.calls;
            const double started = now_ms();
            size_t hits = 0, relevant = 0, short_results = 0;
            for (size_t qi = 0; qi < nq; ++qi) {
                const float* query = queries_all.data.data() + qi * queries_all.cols;
                std::vector<hnswlib::labeltype> returned;
                if (mask.empty()) {
                    auto found = graph.searchKnn(query, size_t(k));
                    while (!found.empty()) { returned.push_back(found.top().second); found.pop(); }
                } else {
                    MaskFilter filter;
                    filter.row = mask.data() + qi * n;
                    auto found = graph.searchKnn(query, size_t(k), &filter);
                    while (!found.empty()) { returned.push_back(found.top().second); found.pop(); }
                }
                if (returned.size() < size_t(k)) ++short_results;
                const std::vector<int32_t>& truth = groundtruth[qi];
                for (int rank = 0; rank < k; ++rank) {
                    if (rank >= int(truth.size()) || truth[size_t(rank)] < 0) continue;
                    ++relevant;
                    if (std::find(returned.begin(), returned.end(),
                                  hnswlib::labeltype(truth[size_t(rank)])) != returned.end())
                        ++hits;
                }
            }
            const double elapsed = now_ms() - started;
            const double recall = relevant ? double(hits) / double(relevant) : 0.0;
            std::printf("{\"stage\":\"search\",\"ef\":%zu,\"k\":%d,\"queries\":%zu,"
                        "\"recall\":%.6f,\"qps\":%.1f,\"dists_per_q\":%.1f,"
                        "\"short_result_rate\":%.6f}\n",
                        ef, k, nq, recall, elapsed > 0 ? double(nq) * 1000.0 / elapsed : 0.0,
                        double(space.calls - calls_before) / double(nq),
                        double(short_results) / double(nq));
        }
        (void)sink;
        return 0;
    }

    fail("unknown command " + command);
}
