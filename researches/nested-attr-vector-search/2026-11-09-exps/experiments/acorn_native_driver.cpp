// Driver for the official ACORN implementation (guestrin-lab/ACORN, a faiss fork).
//
// Predicate handling follows the paper: ACORN's construction is metadata-agnostic,
// so the attribute semantics never enter the graph. Everything is decided by the
// filter_id_map passed to search():
//
//   * every item owns a bitset over the attribute vocabulary, holding its own
//     attribute plus all of its DAG ancestors;
//   * every attribute keeps an inverted bitmap postings[a] = items whose bitset
//     contains a, built by propagating owner bitmaps up the DAG;
//   * a query on attribute a is mask = postings[a] (containment: item contains a);
//   * all queries are packed into one (nq x N) char mask and handed over in a
//     single search() call;
//   * queries whose passing count falls below 1/gamma are answered by brute force
//     over the mask, which is the paper's selective fallback.
#include <faiss/IndexACORN.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct Matrix {
    size_t rows = 0, cols = 0;
    std::vector<float> data;
};

Matrix load_float_bin(const std::string& path, size_t cols) {
    Matrix out;
    out.cols = cols;
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); std::exit(2); }
    in.seekg(0, std::ios::end);
    const size_t bytes = size_t(in.tellg());
    in.seekg(0, std::ios::beg);
    out.rows = bytes / sizeof(float) / cols;
    out.data.resize(out.rows * cols);
    in.read(reinterpret_cast<char*>(out.data.data()), std::streamsize(bytes));
    return out;
}

std::vector<long long> load_int_lines(const std::string& path) {
    std::vector<long long> values;
    std::ifstream in(path);
    if (!in) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); std::exit(2); }
    long long value = 0;
    while (in >> value) values.push_back(value);
    return values;
}

}  // namespace

int main(int argc, char** argv) {
    std::string data = "datasets/sift1m/ccs-attr";
    std::string out_path;
    size_t M = 16, gamma = 2, M_beta = 8, ef_construction = 200;
    int k = 10;
    size_t queries = 200;
    size_t build_threads = 1;
    bool use_fallback = true;
    std::string ef_list = "10,50,100,150,200,250,300,350,400,450,500,550,600,650,700,750,800";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() { return std::string(argv[++i]); };
        if (arg == "--data") data = next();
        else if (arg == "--out") out_path = next();
        else if (arg == "--M") M = std::stoul(next());
        else if (arg == "--gamma") gamma = std::stoul(next());
        else if (arg == "--M-beta") M_beta = std::stoul(next());
        else if (arg == "--efC") ef_construction = std::stoul(next());
        else if (arg == "--k") k = std::stoi(next());
        else if (arg == "--queries") queries = std::stoul(next());
        else if (arg == "--build-threads") build_threads = std::stoul(next());
        else if (arg == "--efs") ef_list = next();
        else if (arg == "--no-fallback") use_fallback = false;
        else { std::fprintf(stderr, "unknown flag %s\n", arg.c_str()); return 2; }
    }

    Matrix base = load_float_bin(data + "/base.bin", 128);
    Matrix query_vectors = load_float_bin(data + "/queries.bin", 128);
    const std::vector<long long> owner = load_int_lines(data + "/objects.txt");
    const size_t n = size_t(base.rows);

    // dense attribute ids over sorted vocabulary
    std::vector<int> vocabulary;
    {
        std::unordered_set<int> ids;
        for (long long value : owner) ids.insert(int(value));
        {
            std::ifstream in(data + "/edges.tsv");
            int child = 0, parent = 0;
            while (in >> child >> parent) { ids.insert(child); ids.insert(parent); }
        }
        vocabulary.assign(ids.begin(), ids.end());
        std::sort(vocabulary.begin(), vocabulary.end());
    }
    const size_t attribute_count = vocabulary.size();
    std::unordered_map<long long, int> dense;
    dense.reserve(attribute_count * 2);
    for (size_t i = 0; i < attribute_count; ++i) dense.emplace(vocabulary[i], int(i));

    std::vector<std::vector<int>> children(attribute_count);
    std::vector<std::vector<int>> parents(attribute_count);
    {
        std::ifstream in(data + "/edges.tsv");
        int child = 0, parent = 0;
        while (in >> child >> parent) {
            const auto c = dense.find(child);
            const auto p = dense.find(parent);
            if (c == dense.end() || p == dense.end()) continue;
            children[size_t(p->second)].push_back(c->second);
            parents[size_t(c->second)].push_back(p->second);
        }
    }

    // ---- inverted postings: postings[a] = items whose attribute set contains a ----
    const size_t words = (attribute_count + 63) / 64;
    const size_t bitmap_words = (n + 63) / 64;
    std::vector<uint64_t> postings(attribute_count * bitmap_words, 0);
    auto posting_bit = [&](size_t attribute, size_t item) {
        postings[attribute * bitmap_words + item / 64] |= uint64_t(1) << (item % 64);
    };
    for (size_t item = 0; item < n; ++item) {
        const auto found = dense.find(owner[item]);
        if (found != dense.end()) posting_bit(size_t(found->second), item);
    }
    {
        // propagate owner bitmaps up the DAG: postings[parent] |= postings[child]
        std::vector<int> in_degree(attribute_count, 0);
        for (size_t a = 0; a < attribute_count; ++a) in_degree[a] = int(children[a].size());
        std::vector<int> ready;
        for (size_t a = 0; a < attribute_count; ++a)
            if (in_degree[a] == 0) ready.push_back(int(a));
        std::vector<char> done(attribute_count, 0);
        while (!ready.empty()) {
            const int a = ready.back();
            ready.pop_back();
            if (done[size_t(a)]) continue;
            done[size_t(a)] = 1;
            for (int parent : parents[size_t(a)]) {
                uint64_t* dst = &postings[size_t(parent) * bitmap_words];
                const uint64_t* src = &postings[size_t(a) * bitmap_words];
                for (size_t w = 0; w < bitmap_words; ++w) dst[w] |= src[w];
                if (--in_degree[size_t(parent)] == 0) ready.push_back(parent);
            }
        }
    }

    // ---- queries: "<count> <attribute>..." = union of the listed attributes ----
    std::vector<std::vector<int>> query_attributes;
    {
        std::ifstream in(data + "/queries.tsv");
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            std::istringstream stream(line);
            int count = 0;
            stream >> count;
            std::vector<int> list;
            for (int i = 0; i < count; ++i) {
                int value = 0;
                stream >> value;
                const auto found = dense.find(value);
                if (found != dense.end()) list.push_back(found->second);
            }
            query_attributes.push_back(list);
        }
    }

    std::vector<int32_t> gt;
    {
        std::ifstream in(data + "/gt_" + std::to_string(k) + ".txt", std::ios::binary);
        if (!in) { std::fprintf(stderr, "missing gt file\n"); return 2; }
        in.seekg(0, std::ios::end);
        const size_t bytes = size_t(in.tellg());
        in.seekg(0, std::ios::beg);
        gt.resize(bytes / sizeof(int32_t));
        in.read(reinterpret_cast<char*>(gt.data()), std::streamsize(bytes));
    }
    const size_t gt_rows = gt.size() / size_t(k);

    // metadata is irrelevant to ACORN's construction (metadata-agnostic), so a
    // constant is a correct choice; the predicate lives entirely in the mask.
    std::vector<int> metadata(n, 0);

    faiss::IndexACORNFlat index(int(base.cols), int(M), int(gamma), metadata, int(M_beta),
                                faiss::METRIC_L2);
    index.acorn.efConstruction = int(ef_construction);
    {
        const auto started = std::chrono::steady_clock::now();
        const size_t threads = std::max<size_t>(1, build_threads);
        if (threads <= 1) {
            index.add(faiss::idx_t(n), base.data.data());
        } else {
            // faiss's add is itself parallel; keep the same knob for reporting
            index.add(faiss::idx_t(n), base.data.data());
        }
        const double seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count();
        std::printf("{\"stage\":\"build\",\"objects\":%zu,\"attributes\":%zu,\"words\":%zu,"
                    "\"M\":%zu,\"gamma\":%zu,\"M_beta\":%zu,\"build_s\":%.1f}\n",
                    n, attribute_count, words, M, gamma, M_beta, seconds);
        std::fflush(stdout);
    }

    const size_t nq = std::min({queries, query_vectors.rows, query_attributes.size(), gt_rows});

    // ---- pack every query's mask once: nq x N bytes ----
    std::vector<char> mask(nq * n, 0);
    std::vector<size_t> passed_per_query(nq, 0);
    for (size_t qi = 0; qi < nq; ++qi) {
        std::vector<uint64_t> bits(bitmap_words, 0);
        for (int attribute : query_attributes[qi]) {
            const uint64_t* src = &postings[size_t(attribute) * bitmap_words];
            for (size_t w = 0; w < bitmap_words; ++w) bits[w] |= src[w];
        }
        char* row = mask.data() + qi * n;
        size_t passed = 0;
        for (size_t w = 0; w < bitmap_words; ++w) {
            uint64_t word = bits[w];
            while (word) {
                const size_t bit = size_t(__builtin_ctzll(word));
                word &= word - 1;
                const size_t item = w * 64 + bit;
                if (item < n) { row[item] = 1; ++passed; }
            }
        }
        passed_per_query[qi] = passed;
    }

    std::vector<int> efs;
    {
        size_t begin = 0;
        while (begin < ef_list.size()) {
            const size_t comma = ef_list.find(',', begin);
            const std::string token = ef_list.substr(begin, comma == std::string::npos
                                                         ? std::string::npos : comma - begin);
            if (!token.empty()) efs.push_back(std::atoi(token.c_str()));
            if (comma == std::string::npos) break;
            begin = comma + 1;
        }
    }

    std::ofstream out_file;
    std::ostream* out = &std::cout;
    if (!out_path.empty()) { out_file.open(out_path); out = &out_file; }

    std::vector<float> distances(nq * size_t(k));
    std::vector<faiss::idx_t> labels(nq * size_t(k));

    for (int ef : efs) {
        index.acorn.efSearch = ef;
        std::fill(distances.begin(), distances.end(), 0.0f);
        std::fill(labels.begin(), labels.end(), faiss::idx_t(-1));
        const auto started = std::chrono::steady_clock::now();
        index.search(faiss::idx_t(nq), query_vectors.data.data(), k, distances.data(),
                     labels.data(), mask.data());
        const double graph_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        double elapsed_ms = graph_ms;   // extended below when the fallback runs

        // selective fallback: queries below 1/gamma are answered exactly
        size_t fallback_queries = 0;
        if (use_fallback) {
            const size_t threshold = n / std::max<size_t>(1, gamma);
            for (size_t qi = 0; qi < nq; ++qi) {
                if (passed_per_query[qi] >= threshold) continue;
                ++fallback_queries;
                const float* qv = query_vectors.data.data() + qi * size_t(base.cols);
                std::vector<std::pair<float, faiss::idx_t>> best;
                best.reserve(k);
                for (size_t item = 0; item < n; ++item) {
                    if (!mask[qi * n + item]) continue;
                    const float* v = base.data.data() + item * size_t(base.cols);
                    float d = 0.0f;
                    for (int dim = 0; dim < int(base.cols); ++dim) {
                        const float diff = qv[dim] - v[dim];
                        d += diff * diff;
                    }
                    if (best.size() < size_t(k)) {
                        best.emplace_back(d, faiss::idx_t(item));
                        std::push_heap(best.begin(), best.end());
                    } else if (d < best.front().first) {
                        std::pop_heap(best.begin(), best.end());
                        best.back() = {d, faiss::idx_t(item)};
                        std::push_heap(best.begin(), best.end());
                    }
                }
                std::sort(best.begin(), best.end());
                for (size_t slot = 0; slot < best.size(); ++slot) {
                    distances[qi * size_t(k) + slot] = best[slot].first;
                    labels[qi * size_t(k) + slot] = best[slot].second;
                }
            }
        }

        elapsed_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();

        size_t hits = 0, relevant = 0, returned_total = 0;
        for (size_t qi = 0; qi < nq; ++qi) {
            std::unordered_set<faiss::idx_t> found;
            for (int slot = 0; slot < k; ++slot) {
                const faiss::idx_t value = labels[qi * size_t(k) + size_t(slot)];
                if (value >= 0) found.insert(value);
            }
            returned_total += found.size();
            for (int rank = 0; rank < k; ++rank) {
                const int32_t truth = gt[qi * size_t(k) + size_t(rank)];
                if (truth < 0) continue;
                ++relevant;
                if (found.count(faiss::idx_t(truth))) ++hits;
            }
        }
        const double recall = relevant ? double(hits) / double(relevant) : 0.0;
        *out << "{\"method\":\"acorn_native\",\"ef\":" << ef << ",\"k\":" << k
             << ",\"queries\":" << nq << ",\"recall\":" << recall
             << ",\"qps\":" << (elapsed_ms > 0.0 ? double(nq) * 1000.0 / elapsed_ms : 0.0)
             << ",\"avg_returned\":" << (nq ? double(returned_total) / double(nq) : 0.0)
             << ",\"graph_ms\":" << graph_ms << ",\"fallback_queries\":" << fallback_queries
             << ",\"mean_passed\":" << (nq ? double(std::accumulate(passed_per_query.begin(),
                                                                    passed_per_query.end(), size_t(0))) / double(nq) : 0.0)
             << "}\n";
        out->flush();
    }
    return 0;
}
