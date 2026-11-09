// Driver for the official ACORN implementation (guestrin-lab/ACORN, a faiss fork).
// It loads our unified dataset, builds faiss::IndexACORN with per-object attribute
// metadata, and runs the query grid so the native implementation can be compared
// against the hnswlib port on the same workload.
//
// Build (after `make -C /tmp/acorn-official/build -j faiss`):
//   g++ -std=c++17 -O3 -mavx2 -I /tmp/acorn-official \
//       acorn_native_driver.cpp -o /tmp/acorn_native \
//       /tmp/acorn-official/build/faiss/libfaiss.a -lgomp -lblas -llapack -lpthread
#include <faiss/IndexACORN.h>

#include <algorithm>
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
        else if (arg == "--efs") ef_list = next();
        else { std::fprintf(stderr, "unknown flag %s\n", arg.c_str()); return 2; }
    }

    Matrix base = load_float_bin(data + "/base.bin", 128);
    Matrix query_vectors = load_float_bin(data + "/queries.bin", 128);
    const std::vector<long long> owner = load_int_lines(data + "/objects.txt");

    // attribute id -> dense index, and the child/parent relation of the DAG
    std::vector<int> attribute_ids;
    {
        std::unordered_set<int> ids;
        for (long long value : owner) ids.insert(int(value));
        attribute_ids.assign(ids.begin(), ids.end());
        std::sort(attribute_ids.begin(), attribute_ids.end());
    }
    const int attribute_count = int(attribute_ids.size());
    std::vector<int> dense_of_attribute{std::vector<int>(size_t(attribute_count), -1)};
    {
        std::vector<int> max_id{std::vector<int>(attribute_ids.empty() ? size_t(0) : size_t(attribute_ids.back()) + 1, -1)};
        for (int i = 0; i < attribute_count; ++i) {
            if (attribute_ids[size_t(i)] >= int(max_id.size()))
                max_id.resize(size_t(attribute_ids[size_t(i)]) + 1, -1);
            max_id[size_t(attribute_ids[size_t(i)])] = i;
        }
        dense_of_attribute = std::move(max_id);
    }
    // dense edges
    std::vector<std::vector<int>> dense_children{std::vector<std::vector<int>>(size_t(attribute_count))};
    {
        std::ifstream in(data + "/edges.tsv");
        int child = 0, parent = 0;
        while (in >> child >> parent) {
            if (child >= int(dense_of_attribute.size()) || parent >= int(dense_of_attribute.size())) continue;
            const int c = dense_of_attribute[size_t(child)];
            const int p = dense_of_attribute[size_t(parent)];
            if (c >= 0 && p >= 0) dense_children[size_t(p)].push_back(c);
        }
    }

    // query predicates: "1 <attribute>" per line (or several)
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
                const int dense = value < int(dense_of_attribute.size())
                    ? dense_of_attribute[size_t(value)] : -1;
                if (dense >= 0) list.push_back(dense);
            }
            query_attributes.push_back(list);
        }
    }

    // ground truth gt_<k>.txt, int32, m x k with -1 padding
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

    // metadata: dense attribute id per object, exactly what IndexACORN expects
    std::vector<int> metadata(size_t(base.rows));
    for (size_t i = 0; i < size_t(base.rows); ++i) {
        const long long attribute = owner[i];
        metadata[i] = attribute < int64_t(dense_of_attribute.size())
            ? dense_of_attribute[size_t(attribute)] : -1;
    }

    faiss::IndexACORNFlat index(int(base.cols), int(M), int(gamma), metadata, int(M_beta),
                                faiss::METRIC_L2);
    index.acorn.efConstruction = int(ef_construction);
    index.add(faiss::idx_t(base.rows), base.data.data());
    std::printf("{\"stage\":\"build\",\"objects\":%zu,\"attributes\":%d,\"M\":%zu,\"gamma\":%zu,\"M_beta\":%zu}\n",
                size_t(base.rows), attribute_count, M, gamma, M_beta);
    std::fflush(stdout);

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

    const size_t nq = std::min({queries, query_vectors.rows, query_attributes.size(), gt_rows});
    std::vector<char> filter_map(size_t(base.rows), 0);
    std::vector<float> distances{std::vector<float>(size_t(k))};
    std::vector<faiss::idx_t> labels{std::vector<faiss::idx_t>(size_t(k))};
    std::ofstream out_file;
    std::ostream* out = &std::cout;
    if (!out_path.empty()) {
        out_file.open(out_path);
        out = &out_file;
    }

    for (int ef : efs) {
        index.acorn.efSearch = ef;
        double total_ms = 0.0;
        size_t hits = 0, relevant = 0, returned_total = 0;
        for (size_t qi = 0; qi < nq; ++qi) {
            // filter_map: objects whose owner attribute is in the descendant closure
            std::fill(filter_map.begin(), filter_map.end(), 0);
            std::vector<int> pending = query_attributes[qi];
            std::vector<char> in_scope(size_t(attribute_count), 0);
            while (!pending.empty()) {
                const int attribute = pending.back();
                pending.pop_back();
                if (in_scope[size_t(attribute)]) continue;
                in_scope[size_t(attribute)] = 1;
                for (int child : dense_children[size_t(attribute)]) pending.push_back(child);
            }
            for (size_t object = 0; object < size_t(base.rows); ++object)
                if (metadata[object] >= 0 && in_scope[size_t(metadata[object])]) filter_map[object] = 1;

            const auto started = std::chrono::steady_clock::now();
            index.search(1, query_vectors.data.data() + qi * size_t(base.cols), k,
                         distances.data(), labels.data(), filter_map.data());
            total_ms += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();

            std::unordered_set<faiss::idx_t> found(labels.begin(), labels.end());
            size_t local_hits = 0, local_relevant = 0;
            for (int rank = 0; rank < k; ++rank) {
                const int32_t truth = gt[qi * size_t(k) + size_t(rank)];
                if (truth < 0) continue;
                ++local_relevant;
                if (found.count(faiss::idx_t(truth))) ++local_hits;
            }
            hits += local_hits;
            relevant += local_relevant;
            returned_total += size_t(std::count_if(labels.begin(), labels.end(),
                                                   [](faiss::idx_t v) { return v >= 0; }));
            (void)total_ms;
        }
        const double recall = relevant ? double(hits) / double(relevant) : 0.0;
        const double qps = total_ms > 0.0 ? double(nq) * 1000.0 / total_ms : 0.0;
        *out << "{\"method\":\"acorn_native\",\"ef\":" << ef << ",\"k\":" << k
             << ",\"queries\":" << nq << ",\"recall\":" << recall
             << ",\"qps\":" << qps
             << ",\"avg_returned\":" << (nq ? double(returned_total) / double(nq) : 0.0)
             << "}\n";
        out->flush();
    }
    return 0;
}
