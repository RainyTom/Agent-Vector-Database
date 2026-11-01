// Shared metadata structures for the DAG scope probes: concept graph, closure,
// object labels, postings and the workload loader. No vectors here.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;
using Id = uint32_t;

inline double micros(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}

[[noreturn]] inline void fail(const std::string& message) {
    std::cerr << "dag_scope_probe: " << message << "\n";
    std::exit(2);
}

struct Graph {
    std::vector<std::vector<Id>> parents, children, ancestors, descendants;
    size_t active() const { return parents.size(); }
};

inline std::vector<Id> closure(Id start, const std::vector<std::vector<Id>>& step) {
    std::vector<Id> seen;
    std::vector<char> visited;
    visited.assign(step.size(), 0);
    std::vector<Id> stack{start};
    while (!stack.empty()) {
        Id node = stack.back();
        stack.pop_back();
        if (visited[node]) continue;
        visited[node] = 1;
        seen.push_back(node);
        for (Id next : step[node]) stack.push_back(next);
    }
    std::sort(seen.begin(), seen.end());
    return seen;
}

struct Bitmap {
    std::vector<uint64_t> words;
    void reset(size_t n) { std::fill(words.begin(), words.end(), 0); }
    void set(Id id) { words[id >> 6] |= UINT64_C(1) << (id & 63); }
    bool test(Id id) const { return (words[id >> 6] >> (id & 63)) & 1; }
    size_t count() const {
        size_t total = 0;
        for (uint64_t word : words) total += static_cast<size_t>(__builtin_popcountll(word));
        return total;
    }
};

struct Workload {
    size_t objects = 0;
    std::vector<Id> terminal;                 // object -> direct concept label
    std::vector<std::vector<Id>> postings;    // concept -> object ids (ascending)
    std::vector<std::vector<Id>> queries;     // each query is an antichain of concepts
};

inline Workload load(const std::string& dir, uint64_t seed) {
    Workload work;
    std::ifstream edges(dir + "/edges.tsv");
    if (!edges) fail("cannot open edges.tsv");
    std::vector<std::pair<Id, Id>> pairs;
    Id child = 0, parent = 0;
    size_t concepts = 0;
    while (edges >> child >> parent) {
        pairs.emplace_back(child, parent);
        concepts = std::max<size_t>(concepts, std::max<Id>(child, parent) + 1);
    }
    std::ifstream objects(dir + "/objects.txt");
    if (!objects) fail("cannot open objects.txt");
    std::vector<Id> raw;
    Id label = 0;
    while (objects >> label) raw.push_back(label);
    if (raw.empty()) fail("empty objects.txt");

    // Scatter object ids so postings are not contiguous ranges: the probe must
    // not benefit from the generator emitting ids grouped by concept.
    std::vector<Id> order(raw.size());
    std::iota(order.begin(), order.end(), 0U);
    std::mt19937_64 rng(seed);
    std::shuffle(order.begin(), order.end(), rng);
    work.objects = raw.size();
    work.terminal.assign(work.objects, 0);
    work.postings.assign(concepts, {});
    for (size_t i = 0; i < raw.size(); ++i) {
        Id object = order[i];
        work.terminal[object] = raw[i];
        work.postings[raw[i]].push_back(object);
    }
    for (auto& list : work.postings) std::sort(list.begin(), list.end());

    std::ifstream queries(dir + "/queries.tsv");
    if (!queries) fail("cannot open queries.tsv");
    std::string line;
    while (std::getline(queries, line)) {
        if (line.empty()) continue;
        std::istringstream stream(line);
        size_t count = 0;
        if (!(stream >> count) || count == 0) continue;
        std::vector<Id> concepts_in_query;
        for (size_t i = 0; i < count; ++i) {
            Id c = 0;
            if (!(stream >> c)) break;
            concepts_in_query.push_back(c);
        }
        if (!concepts_in_query.empty()) work.queries.push_back(std::move(concepts_in_query));
    }
    return work;
}

inline Graph build_graph(const std::string& dir) {
    Graph graph;
    std::ifstream edges(dir + "/edges.tsv");
    if (!edges) fail("cannot open edges.tsv");
    std::vector<std::pair<Id, Id>> pairs;
    Id child = 0, parent = 0;
    size_t concepts = 0;
    while (edges >> child >> parent) {
        pairs.emplace_back(child, parent);
        concepts = std::max<size_t>(concepts, std::max<Id>(child, parent) + 1);
    }
    graph.parents.assign(concepts, {});
    graph.children.assign(concepts, {});
    for (auto [c, p] : pairs) {
        graph.parents[c].push_back(p);
        graph.children[p].push_back(c);
    }
    graph.ancestors.resize(concepts);
    graph.descendants.resize(concepts);
    for (size_t c = 0; c < concepts; ++c) {
        graph.ancestors[c] = closure(static_cast<Id>(c), graph.parents);
        graph.descendants[c] = closure(static_cast<Id>(c), graph.children);
    }
    return graph;
}

inline double percentile(std::vector<double> values, double q) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    size_t index = static_cast<size_t>(q * (values.size() - 1));
    return values[index];
}

struct Strategy {
    std::string name;
    std::vector<Bitmap> materialised;   // empty bitmap => not materialised
    std::vector<uint64_t> cardinality;  // |S(c)| maintained for every concept
    size_t words = 0;
};
