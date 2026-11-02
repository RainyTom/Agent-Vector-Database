#pragma once
#include <array>
#include <type_traits>
// Static owner-local HNSW + bottom-up DAG merge, no quantization.
#include "raw_l2_space.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <queue>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using Id = uint32_t;
using Pair = std::pair<float, Id>;
using Graph = hnswlib::HierarchicalNSW<float>;
using Clock = std::chrono::steady_clock;
static void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
static double us(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}
struct Vectors {
    size_t dim = 0, n = 0;
    std::vector<float> x;
    const float* at(Id i) const { return x.data() + size_t(i) * dim; }
};
static Vectors load_vectors(const std::string& path, size_t limit) {
    std::ifstream in(path, std::ios::binary);
    require(bool(in), "cannot open " + path);
    Vectors v;
    int32_t d;
    while (v.n < limit && in.read(reinterpret_cast<char*>(&d), 4)) {
        require(d > 0 && d <= 4096, "invalid dimension");
        if (!v.dim) { v.dim = d; v.x.reserve(limit * v.dim); }
        require(size_t(d) == v.dim, "inconsistent dimension");
        size_t offset = v.x.size();
        v.x.resize(offset + d);
        require(bool(in.read(reinterpret_cast<char*>(v.x.data() + offset), d * 4)), "truncated vectors");
        ++v.n;
    }
    require(v.n == limit, "insufficient vectors in " + path);
    return v;
}
struct Workload {
    std::vector<Id> owner, topo;
    std::vector<std::vector<Id>> children, members, desc, scopes;
};
static Workload load_workload(const std::string& dir) {
    Workload w;
    std::ifstream objects(dir + "/objects.txt"), edges(dir + "/edges.tsv"), queries(dir + "/queries.tsv");
    require(bool(objects) && bool(edges) && bool(queries), "missing DAG workload");
    Id c, p;
    size_t nc = 0;
    while (objects >> c) { w.owner.push_back(c); nc = std::max(nc, size_t(c) + 1); }
    std::vector<std::pair<Id, Id>> links;
    while (edges >> c >> p) { links.emplace_back(p, c); nc = std::max(nc, size_t(std::max(c, p)) + 1); }
    require(nc && !w.owner.empty(), "empty DAG workload");
    w.children.resize(nc); w.members.resize(nc); w.desc.resize(nc);
    std::vector<size_t> degree(nc);
    for (auto [parent, child] : links) { w.children[parent].push_back(child); ++degree[child]; }
    for (Id i = 0; i < nc; ++i) if (!degree[i]) w.topo.push_back(i);
    for (size_t i = 0; i < w.topo.size(); ++i)
        for (Id child : w.children[w.topo[i]]) if (!--degree[child]) w.topo.push_back(child);
    require(w.topo.size() == nc, "cyclic DAG");
    // Preserve owner counts; remove incidental alignment with SIFT row order.
    std::mt19937 rng(20261101);
    std::shuffle(w.owner.begin(), w.owner.end(), rng);
    for (Id i = 0; i < w.owner.size(); ++i) w.members[w.owner[i]].push_back(i);
    for (Id start = 0; start < nc; ++start) {
        std::vector<char> seen(nc);
        std::vector<Id> stack{start};
        while (!stack.empty()) {
            Id u = stack.back(); stack.pop_back();
            if (seen[u]) continue;
            seen[u] = 1; w.desc[start].push_back(u);
            for (Id v : w.children[u]) stack.push_back(v);
        }
    }
    std::string line;
    while (std::getline(queries, line)) {
        std::istringstream row(line);
        size_t count;
        if (!(row >> count)) continue;
        std::vector<Id> scope(count);
        for (Id& id : scope) require(bool(row >> id) && id < nc, "invalid query scope");
        require(count > 0, "empty query expression");
        w.scopes.push_back(std::move(scope));
    }
    require(w.scopes.size() >= 24, "insufficient scope queries");
    return w;
}
struct Merged {
    std::vector<std::vector<Id>> edges, portals;
    std::vector<std::unique_ptr<Graph>> local;
    size_t local_edges = 0, connector_edges = 0, jump_edges = 0;
};
struct Scratch {
    std::vector<uint32_t> seen;
    uint32_t epoch = 0;
    explicit Scratch(size_t n) : seen(n) {}
};
struct Query {
    std::vector<char> allowed;
    std::vector<Id> roots, truth;
    size_t count = 0;
};
static std::vector<Id> labels(std::priority_queue<std::pair<float, hnswlib::labeltype>> heap) {
    std::vector<Id> out;
    while (!heap.empty()) { out.push_back(heap.top().second); heap.pop(); }
    return out;
}