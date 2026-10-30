// WQR research runner: builds index, applies update tape, runs query tape.
// No modification to existing dynamic_triad behavior; separate binary.
#include "wqr_index.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;

static double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}

struct Update {
    std::string type;
    wqr::Id id = 0;
    wqr::NodeId node = 0;
    int64_t category = 0, numeric = 0;
    uint64_t tags = 0;
    double us = 0;
};

using QueryOut = wqr::WQRIndex::QueryResult;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: wqr_research <data_dir> <tape_file> <out_dir>\n";
        return 1;
    }
    const std::string data_dir = argv[1];
    const std::string tape_file = argv[2];
    const std::string out_dir = argv[3];

    // Load canonical data.
    auto floats = [](const std::string& path) -> std::vector<float> {
        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("missing " + path);
        f.seekg(0, std::ios::end);
        const size_t bytes = f.tellg();
        f.seekg(0);
        std::vector<float> result(bytes / 4);
        f.read(reinterpret_cast<char*>(result.data()), bytes);
        return result;
    };
    auto ints = [](const std::string& path) -> std::vector<int64_t> {
        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("missing " + path);
        f.seekg(0, std::ios::end);
        const size_t bytes = f.tellg();
        f.seekg(0);
        std::vector<int64_t> result(bytes / 8);
        f.read(reinterpret_cast<char*>(result.data()), bytes);
        return result;
    };
    auto uints = [](const std::string& path) -> std::vector<uint64_t> {
        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("missing " + path);
        f.seekg(0, std::ios::end);
        const size_t bytes = f.tellg();
        f.seekg(0);
        std::vector<uint64_t> result(bytes / 8);
        f.read(reinterpret_cast<char*>(result.data()), bytes);
        return result;
    };

    const auto base = floats(data_dir + "/base.f32");
    const auto test = floats(data_dir + "/test.f32");
    const auto owner_i = ints(data_dir + "/bindings.txt");
    const auto category_i = ints(data_dir + "/category.f64");
    const auto numeric_i = ints(data_dir + "/numeric.f64");
    const auto tags_i = uints(data_dir + "/tags.u64");

    const size_t n = category_i.size();
    const size_t dim = base.size() / n;
    const size_t nq = test.size() / dim;

    wqr::VectorStore vectors;
    vectors.init(dim);
    vectors.data = base;

    wqr::WQRIndex index;
    index.init(dim, n);
    index.owner.resize(n);
    index.category.resize(n);
    index.numeric.resize(n);
    index.tags.resize(n);
    for (size_t i = 0; i < n; ++i) {
        index.owner[i] = owner_i[i];
        index.category[i] = category_i[i];
        index.numeric[i] = numeric_i[i];
        index.tags[i] = tags_i[i];
    }

    std::vector<wqr::Id> ids(n);
    std::iota(ids.begin(), ids.end(), 0);
    auto build_start = Clock::now();
    index.build(ids, std::vector<wqr::NodeId>(owner_i.begin(), owner_i.end()), vectors);
    const double build_us = elapsed(build_start);

    // Parse and apply update tape.
    std::ifstream tape(tape_file);
    if (!tape) throw std::runtime_error("missing " + tape_file);
    std::vector<Update> updates;
    std::string line;
    while (std::getline(tape, line)) {
        std::istringstream iss(line);
        Update u;
        iss >> u.type;
        if (u.type == "V") iss >> u.id >> u.category >> u.numeric;
        else if (u.type == "T") iss >> u.id >> u.tags;
        else if (u.type == "M") iss >> u.id >> u.node;
        else if (u.type == "B") iss >> u.node;  // src=node, dst=next token
        else if (u.type == "R") iss >> u.node;
        else if (u.type == "D") iss >> u.id;
        else if (u.type == "I") iss >> u.id >> u.node >> u.category >> u.numeric >> u.tags;
        else if (u.type == "Q") break;
        else continue;
        auto start = Clock::now();
        if (u.type == "V") index.apply_V(u.id, u.category, u.numeric);
        else if (u.type == "T") index.apply_T(u.id, u.tags);
        else if (u.type == "M") index.apply_M(u.id, u.node);
        else if (u.type == "B") {
            wqr::NodeId dst; iss >> dst;
            index.apply_B(u.node, dst);
        } else if (u.type == "R") {
            wqr::NodeId dst; iss >> dst;
            index.apply_R(u.node, dst);
        } else if (u.type == "D") index.apply_D(u.id);
        else if (u.type == "I") index.apply_I(u.id, u.node, u.category, u.numeric, u.tags);
        u.us = elapsed(start);
        updates.push_back(u);
    }

    // Query tape: same format as dynamic_triad.
    std::vector<QueryOut> outputs;
    while (std::getline(tape, line)) {
        std::istringstream iss(line);
        std::string type;
        iss >> type;
        if (type != "Q") continue;
        wqr::ExactMaskComputer::Query q;
        size_t anchor;
        iss >> q.qidx >> anchor >> q.recursive >> q.tagmode >> q.combine_or
            >> q.category >> q.lo >> q.hi >> q.tags;
        q.anchor = anchor;
        const float* query = test.data() + q.qidx * dim;
        auto start = Clock::now();
        auto result = index.query(query, 10, 4, 256, 1024, q);
        result.us = elapsed(start);
        result.qidx = q.qidx;
        outputs.push_back(std::move(result));
    }

    // Write results.
    std::ofstream out(out_dir + "/wqr_results.txt");
    out << "build_us " << build_us << "\n";
    for (const auto& u : updates)
        out << "U " << u.type << " " << u.id << " " << u.us << "\n";
    for (const auto& r : outputs) {
        out << "Q " << r.qidx << " " << r.us << " " << r.portal_distances << " "
            << r.local_distances << " " << r.masks_prepared << " " << r.blocks_selected
            << " " << r.owner_lookups << " " << r.owner_cache_hits;
        for (const auto& [d, id] : r.results) out << " " << id << ":" << d;
        out << "\n";
    }
    return 0;
}
