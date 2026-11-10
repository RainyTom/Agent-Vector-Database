// Minimal driver for the official ACORN implementation (faiss fork).
//
// Reads the project's unified dataset directory:
//   base.bin    float32 n x d      object vectors
//   queries.bin float32 m x d      query vectors
//   objects.txt one attribute id per object
//   edges.tsv   "child parent"     attribute DAG
//   queries.tsv "<count> <attr>..." query predicates
//   gt_<k>.txt  int32 m x k        exact ground truth
//
// Builds IndexACORNFlat with the paper's SIFT1M parameters and reports
// recall@k and distance-free latency for an efSearch sweep.
#include <faiss/IndexACORN.h>
#include <faiss/impl/ACORN.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct Vectors {
    size_t dim = 0, count = 0;
    std::vector<float> data;
};

Vectors load_float_bin(const std::string& path) {
    Vectors out;
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); std::exit(2); }
    std::fseek(file, 0, SEEK_END);
    const long bytes = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    out.data.resize(size_t(bytes) / sizeof(float));
    if (std::fread(out.data.data(), sizeof(float), out.data.size(), file) != out.data.size()) {
        std::fprintf(stderr, "short read %s\n", path.c_str()); std::exit(2);
    }
    std::fclose(file);
    return out;
}

std::vector<int> load_objects(const std::string& path) {
    std::vector<int> out;
    std::ifstream in(path);
    int value = 0;
    while (in >> value) out.push_back(value);
    return out;
}

std::vector<std::pair<int, int>> load_edges(const std::string& path) {
    std::vector<std::pair<int, int>> out;
    std::ifstream in(path);
    int child = 0, parent = 0;
    while (in >> child >> parent) out.emplace_back(child, parent);
    return out;
}

std::vector<std::vector<int>> load_queries(const std::string& path) {
    std::vector<std::vector<int>> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::vector<int> attributes;
        size_t start = 0;
        while (start < line.size()) {
            const size_t end = line.find(' ', start);
            const std::string token = line.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!token.empty()) attributes.push_back(std::atoi(token.c_str()));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        if (attributes.empty()) continue;
        const int count = attributes.front();
        attributes.erase(attributes.begin());
        if (count > 0 && int(attributes.size()) > count) attributes.resize(size_t(count));
        out.push_back(attributes);
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    std::string data;
    std::string efs = "16,32,64,128";
    int k = 10, M = 32, gamma = 12, M_beta = 32, dim = 128;
    size_t query_limit = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return std::string(argv[++i]); };
        if (a == "--data") data = next();
        else if (a == "--efs") efs = next();
        else if (a == "--k") k = std::atoi(next().c_str());
        else if (a == "--M") M = std::atoi(next().c_str());
        else if (a == "--gamma") gamma = std::atoi(next().c_str());
        else if (a == "--M-beta") M_beta = std::atoi(next().c_str());
        else if (a == "--queries") query_limit = size_t(std::atoi(next().c_str()));
        else if (a == "--dim") dim = std::atoi(next().c_str());
        else { std::fprintf(stderr, "unknown flag %s\n", a.c_str()); return 2; }
    }
    if (data.empty()) { std::fprintf(stderr, "--data required\n"); return 2; }

    const Vectors base = load_float_bin(data + "/base.bin");
    const Vectors queries = load_float_bin(data + "/queries.bin");
    std::vector<int> owner = load_objects(data + "/objects.txt");
    const auto edges = load_edges(data + "/edges.tsv");
    const auto query_attributes = load_queries(data + "/queries.tsv");

    const int d = dim;
    if (base.data.size() % size_t(d) || queries.data.size() % size_t(d)) {
        std::fprintf(stderr, "vector buffers are not a multiple of dim %d\n", d);
        return 2;
    }
    const size_t nb = base.data.size() / size_t(d);
    const size_t nq = query_limit ? std::min(query_limit, queries.data.size() / size_t(d))
                                  : queries.data.size() / size_t(d);
    if (owner.size() != nb) { std::fprintf(stderr, "objects.txt has %zu rows, base has %zu\n", owner.size(), nb); return 2; }

    // attribute -> dense id, children map for the descendant closure
    std::set<int> attribute_set(owner.begin(), owner.end());
    for (const auto& e : edges) { attribute_set.insert(e.first); attribute_set.insert(e.second); }
    std::vector<int> dense(attribute_set.begin(), attribute_set.end());
    std::unordered_map<int, int> dense_of;
    for (size_t i = 0; i < dense.size(); ++i) dense_of[dense[i]] = int(i);
    const size_t attributes = dense.size();

    // objects grouped by dense attribute
    std::vector<size_t> attribute_begin(attributes + 1, 0);
    std::vector<int> owner_dense(nb, 0);
    for (size_t i = 0; i < nb; ++i) {
        owner_dense[i] = dense_of[owner[i]];
        ++attribute_begin[size_t(owner_dense[i]) + 1];
    }
    for (size_t a = 0; a < attributes; ++a) attribute_begin[a + 1] += attribute_begin[a];
    std::vector<std::vector<int>> children(attributes);
    for (const auto& e : edges) {
        const auto c = dense_of.find(e.first);
        const auto p = dense_of.find(e.second);
        if (c != dense_of.end() && p != dense_of.end()) children[size_t(p->second)].push_back(c->second);
    }

    std::printf("{\"stage\":\"load\",\"n\":%zu,\"dim\":%d,\"queries\":%zu,\"attributes\":%zu,"
                "\"M\":%d,\"gamma\":%d,\"M_beta\":%d,\"k\":%d}\n",
                nb, d, nq, attributes, M, gamma, M_beta, k);

    std::vector<int> metadata(owner_dense.begin(), owner_dense.end());
    faiss::IndexACORNFlat index(d, M, gamma, metadata, M_beta);
    const auto build_start = std::chrono::steady_clock::now();
    index.add(faiss::idx_t(nb), base.data.data());
    const double build_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - build_start).count();
    std::printf("{\"stage\":\"build\",\"seconds\":%.2f}\n", build_s);

    // exact ground truth if present, else computed here
    std::vector<std::vector<int>> truth(nq);
    {
        std::ifstream gt(data + "/gt_" + std::to_string(k) + ".txt", std::ios::binary);
        if (gt) {
            gt.seekg(0, std::ios::end);
            const size_t bytes = size_t(gt.tellg());
            gt.seekg(0, std::ios::beg);
            std::vector<int> flat(bytes / sizeof(int));
            gt.read(reinterpret_cast<char*>(flat.data()), std::streamsize(bytes));
            const size_t rows = flat.size() / size_t(k);
            for (size_t q = 0; q < std::min(nq, rows); ++q) {
                truth[q].assign(flat.begin() + q * size_t(k), flat.begin() + (q + 1) * size_t(k));
            }
        }
    }

    std::vector<char> filter(size_t(nq) * nb, 0);
    std::vector<std::vector<uint64_t>> closure_bits(attributes, std::vector<uint64_t>((attributes + 63) / 64, 0));
    for (size_t a = 0; a < attributes; ++a) {
        // iterative post-order closure computation
        std::vector<int> stack{int(a)};
        std::vector<uint64_t>& bits = closure_bits[a];
        bits[a / 64] |= 1ull << (a % 64);
        while (!stack.empty()) {
            const int v = stack.back();
            stack.pop_back();
            for (int c : children[size_t(v)]) {
                if ((bits[size_t(c) / 64] >> (c % 64)) & 1ull) continue;
                bits[size_t(c) / 64] |= 1ull << (c % 64);
                stack.push_back(c);
            }
        }
    }

    for (size_t q = 0; q < nq; ++q) {
        std::vector<uint64_t> bits((attributes + 63) / 64, 0);
        for (int attribute : query_attributes[q % query_attributes.size()]) {
            const auto it = dense_of.find(attribute);
            if (it == dense_of.end()) continue;
            const auto& closure = closure_bits[size_t(it->second)];
            for (size_t w = 0; w < bits.size(); ++w) bits[w] |= closure[w];
        }
        char* row = filter.data() + q * nb;
        for (size_t i = 0; i < nb; ++i) {
            const int a = owner_dense[i];
            row[i] = ((bits[size_t(a) / 64] >> (a % 64)) & 1ull) ? 1 : 0;
        }
    }

    std::vector<size_t> ef_values;
    {
        size_t start = 0;
        const std::string& text = efs;
        while (start <= text.size()) {
            const size_t comma = text.find(',', start);
            const std::string token = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            if (!token.empty()) ef_values.push_back(size_t(std::atoi(token.c_str())));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }

    std::vector<float> distances(size_t(k) * nq);
    std::vector<faiss::idx_t> neighbors(size_t(k) * nq);

    for (size_t ef : ef_values) {
        faiss::SearchParametersACORN params;
        params.efSearch = int(ef);
        // The upstream hybrid_search() sizes its candidate heap from the member
        // ACORN::efSearch (default 16) rather than from params->efSearch, so the
        // parameter is set on both to make the sweep actually take effect.
        index.acorn.efSearch = int(ef);
        const auto start = std::chrono::steady_clock::now();
        index.search(faiss::idx_t(nq), queries.data.data(), k, distances.data(), neighbors.data(),
                     filter.data(), &params);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

        size_t hits = 0, total = 0, returned = 0;
        for (size_t q = 0; q < nq; ++q) {
            if (truth[q].empty()) continue;
            total += size_t(k);
            const auto* row = neighbors.data() + q * size_t(k);
            size_t row_returned = 0;
            for (int j = 0; j < k; ++j) {
                if (row[j] < 0) continue;
                ++row_returned;
                if (std::find(truth[q].begin(), truth[q].end(), int(row[j])) != truth[q].end()) ++hits;
            }
            returned += row_returned;
        }
        std::printf("{\"stage\":\"search\",\"ef\":%zu,\"recall\":%.6f,\"qps\":%.2f,"
                    "\"queries\":%zu,\"returned_per_q\":%.2f}\n",
                    ef, total ? double(hits) / double(total) : 0.0, double(nq) / seconds, nq,
                    double(returned) / double(nq));
        std::fflush(stdout);
    }
    return 0;
}
