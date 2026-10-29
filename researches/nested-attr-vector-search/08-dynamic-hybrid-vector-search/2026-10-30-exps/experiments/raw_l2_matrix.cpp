// Offline distance oracle only: no ANN, filtering index, or candidate pruning.
#include "raw_l2_space.h"
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

static std::vector<float> prefix(const std::string& path, size_t rows, size_t dim) {
    if (rows > std::numeric_limits<size_t>::max() / dim / sizeof(float))
        throw std::runtime_error("input size overflow");
    const auto bytes = rows * dim * sizeof(float);
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in || in.tellg() < 0 || uint64_t(in.tellg()) < bytes ||
        uint64_t(in.tellg()) % (dim * sizeof(float)))
        throw std::runtime_error("missing, short, or malformed float matrix: " + path);
    std::vector<float> values(rows * dim);
    in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(values.data()), bytes))
        throw std::runtime_error("short matrix read");
    for (float v : values) if (!std::isfinite(v))
        throw std::runtime_error("nonfinite input");
    return values;
}

int main(int argc, char** argv) {
    try {
        std::string base, queries, output;
        size_t n = 0, nq = 0, dim = 0;
        for (int i = 1; i < argc; i += 2) {
            if (i + 1 == argc) throw std::runtime_error("missing argument value");
            std::string key = argv[i], value = argv[i + 1];
            if (key == "--base") base = value;
            else if (key == "--queries") queries = value;
            else if (key == "--out") output = value;
            else if (key == "--n") n = std::stoull(value);
            else if (key == "--nq") nq = std::stoull(value);
            else if (key == "--dim") dim = std::stoull(value);
            else throw std::runtime_error("unknown argument: " + key);
        }
        if (base.empty() || queries.empty() || output.empty() || !n || !nq || !dim)
            throw std::runtime_error("required: --base --queries --out --n --nq --dim");
        if (std::filesystem::exists(output)) throw std::runtime_error("refusing output overwrite");
        auto x = prefix(base, n, dim);
        auto q = prefix(queries, nq, dim);
        RawL2Space space(dim);
        std::vector<float> row(n);
        std::ofstream out(output, std::ios::binary);
        if (!out) throw std::runtime_error("cannot create output");
        for (size_t j = 0; j < nq; ++j) {
            const float* query = q.data() + j * dim;
            for (size_t i = 0; i < n; ++i) {
                row[i] = space.dist(query, x.data() + i * dim);
                if (!std::isfinite(row[i]) || row[i] < 0)
                    throw std::runtime_error("invalid kernel distance");
            }
            if (!out.write(reinterpret_cast<const char*>(row.data()), row.size() * sizeof(float)))
                throw std::runtime_error("distance matrix write failed");
        }
        out.close();
        if (!out) throw std::runtime_error("distance matrix close failed");
        std::cout << "{\"n\":" << n << ",\"nq\":" << nq << ",\"dim\":" << dim
                  << ",\"distance_calls\":" << uint64_t(n) * nq
                  << ",\"kernel\":\"hnswlib_RawL2Space\"}\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "raw_l2_matrix: " << e.what() << '\n';
        return 1;
    }
}
