#include "dataset.h"

#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

namespace {

bool read_float_bin(const std::string& path, std::vector<float>* out) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    std::fseek(file, 0, SEEK_END);
    const long bytes = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    out->resize(size_t(bytes) / sizeof(float));
    const bool ok = std::fread(out->data(), sizeof(float), out->size(), file) == out->size();
    std::fclose(file);
    return ok;
}

bool read_int32_bin(const std::string& path, std::vector<int32_t>* out) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    std::fseek(file, 0, SEEK_END);
    const long bytes = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    out->resize(size_t(bytes) / sizeof(int32_t));
    const bool ok = std::fread(out->data(), sizeof(int32_t), out->size(), file) == out->size();
    std::fclose(file);
    return ok;
}

}  // namespace

bool Dataset::load(const std::string& directory, int ground_truth_k, std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error) *error = directory + ": " + message;
        return false;
    };
    path = directory;
    k = ground_truth_k;

    if (!read_float_bin(directory + "/base.bin", &base)) return fail("missing or unreadable base.bin");
    if (!read_float_bin(directory + "/queries.bin", &query_vectors))
        return fail("missing or unreadable queries.bin");

    // Dimension is fixed by the first two files agreeing on a common factor.
    dim = 128;
    {
        std::ifstream meta(directory + "/meta.json");
        std::string line;
        while (std::getline(meta, line)) {
            const auto pos = line.find("\"dim\"");
            if (pos == std::string::npos) continue;
            const auto colon = line.find(':', pos);
            if (colon == std::string::npos) continue;
            dim = std::atoi(line.c_str() + colon + 1);
            break;
        }
    }
    if (dim <= 0 || base.size() % size_t(dim) != 0 || query_vectors.size() % size_t(dim) != 0)
        return fail("vector size is not a multiple of dim");

    {
        std::ifstream objects(directory + "/objects.txt");
        if (!objects) return fail("missing objects.txt");
        std::string line;
        owner.reserve(num_objects());
        while (std::getline(objects, line)) {
            if (line.empty()) continue;
            owner.push_back(std::atoi(line.c_str()));
        }
    }
    if (owner.size() != num_objects()) return fail("objects.txt length does not match base.bin");

    {
        std::ifstream edges(directory + "/edges.tsv");
        if (!edges) return fail("missing edges.tsv");
        std::string line;
        while (std::getline(edges, line)) {
            if (line.empty()) continue;
            std::istringstream row(line);
            int child = 0, parent = 0;
            if (!(row >> child >> parent)) continue;
            children_of[parent].push_back(child);
            parents_of[child].push_back(parent);
        }
    }

    {
        std::ifstream queries(directory + "/queries.tsv");
        if (!queries) return fail("missing queries.tsv");
        std::string line;
        while (std::getline(queries, line)) {
            if (line.empty()) continue;
            std::istringstream row(line);
            int count = 0;
            if (!(row >> count) || count <= 0) continue;
            std::vector<int> attributes(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) row >> attributes[size_t(i)];
            query_attributes.push_back(attributes);
        }
    }
    if (query_attributes.size() != num_queries())
        return fail("queries.tsv and queries.bin have different lengths");

    const std::string gt_path = directory + "/gt_" + std::to_string(k) + ".txt";
    if (!read_int32_bin(gt_path, &ground_truth)) return fail("missing or unreadable " + gt_path);
    if (ground_truth.size() != num_queries() * size_t(k))
        return fail("ground truth length does not match queries x k");

    return true;
}

std::vector<int> collect_attribute_ids(const Dataset& dataset) {
    std::set<int> ids;
    for (const auto& entry : dataset.children_of) ids.insert(entry.first);
    for (const auto& entry : dataset.parents_of) ids.insert(entry.first);
    for (int attribute : dataset.owner) ids.insert(attribute);
    for (const auto& attributes : dataset.query_attributes)
        for (int attribute : attributes) ids.insert(attribute);
    return std::vector<int>(ids.begin(), ids.end());
}
