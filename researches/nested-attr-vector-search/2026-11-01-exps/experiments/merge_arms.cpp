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
static std::vector<Id> representatives(std::vector<Id> candidates, const Vectors& base, RawL2Space& l2) {
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    if (candidates.size() <= 8) return candidates;
    std::vector<Id> selected{candidates.front()};
    std::vector<float> nearest(candidates.size(), std::numeric_limits<float>::infinity());
    while (selected.size() < 8) {
        size_t best = 0;
        for (size_t i = 0; i < candidates.size(); ++i) {
            nearest[i] = std::min(nearest[i], l2.dist(base.at(candidates[i]), base.at(selected.back())));
            if (std::find(selected.begin(), selected.end(), candidates[i]) != selected.end()) continue;
            if (std::find(selected.begin(), selected.end(), candidates[best]) != selected.end()
                || nearest[i] > nearest[best]) best = i;
        }
        selected.push_back(candidates[best]);
    }
    return selected;
}
struct Merged {
    std::vector<std::vector<Id>> edges, portals;
    std::vector<std::unique_ptr<Graph>> local;
    size_t local_edges = 0, connector_edges = 0, jump_edges = 0;
};
static Merged build(const Vectors& base, const Workload& w, RawL2Space& l2) {
    Merged m;
    m.edges.resize(base.n); m.portals.resize(w.members.size()); m.local.resize(w.members.size());
    std::vector<std::vector<Id>> own(w.members.size());
    for (Id c = 0; c < w.members.size(); ++c) {
        const auto& members = w.members[c];
        if (members.empty()) continue;
        own[c] = representatives(members, base, l2);
        auto& g = m.local[c];
        g = std::make_unique<Graph>(&l2, members.size(), 16, 200, 100 + c);
        for (Id item : members) g->addPoint(base.at(item), item);
        // Retain every native level-0 edge; add reverse links for merged NSW connectivity.
        for (size_t i = 0; i < members.size(); ++i) {
            Id u = g->getExternalLabel(i);
            auto* list = g->get_linklist0(i);
            auto* ids = reinterpret_cast<hnswlib::tableint*>(list + 1);
            for (size_t j = 0; j < g->getListCount(list); ++j) {
                Id v = g->getExternalLabel(ids[j]);
                m.edges[u].push_back(v); m.edges[v].push_back(u);
            }
        }
    }
    auto normalize = [&]() {
        size_t count = 0;
        for (auto& row : m.edges) {
            std::sort(row.begin(), row.end()); row.erase(std::unique(row.begin(), row.end()), row.end());
            count += row.size();
        }
        return count;
    };
    m.local_edges = normalize();
    auto connect = [&](Id u, Id v) {
        if (u != v) { m.edges[u].push_back(v); m.edges[v].push_back(u); }
    };
    for (auto it = w.topo.rbegin(); it != w.topo.rend(); ++it) {
        Id c = *it;
        std::vector<std::vector<Id>> groups;
        if (!own[c].empty()) groups.push_back(own[c]);
        for (Id child : w.children[c]) if (!m.portals[child].empty()) groups.push_back(m.portals[child]);
        if (groups.empty()) continue;
        // Prim MST over component portals; shared descendants may give zero-distance joins.
        std::vector<char> used(groups.size());
        std::vector<float> best(groups.size(), std::numeric_limits<float>::infinity());
        std::vector<std::pair<Id, Id>> endpoints(groups.size());
        best[0] = 0;
        std::vector<Id> pool;
        for (size_t step = 0; step < groups.size(); ++step) {
            size_t g = groups.size();
            for (size_t i = 0; i < groups.size(); ++i)
                if (!used[i] && (g == groups.size() || best[i] < best[g])) g = i;
            require(g != groups.size(), "merge MST failed");
            used[g] = 1;
            if (step) connect(endpoints[g].first, endpoints[g].second);
            pool.insert(pool.end(), groups[g].begin(), groups[g].end());
            for (size_t h = 0; h < groups.size(); ++h) if (!used[h])
                for (Id u : groups[g]) for (Id v : groups[h]) {
                    float d = l2.dist(base.at(u), base.at(v));
                    if (d < best[h]) { best[h] = d; endpoints[h] = {u, v}; }
                }
        }
        // Geometric joins: every portal gets its nearest portal in each other input.
        for (size_t g = 0; g < groups.size(); ++g)
            for (size_t h = 0; h < groups.size(); ++h) if (g != h)
                for (Id u : groups[g]) {
                    Pair nearest{std::numeric_limits<float>::infinity(), 0};
                    for (Id v : groups[h])
                        nearest = std::min(nearest, Pair{l2.dist(base.at(u), base.at(v)), v});
                    connect(u, nearest.second);
                }
        // Distributed bridge sources: direct items, not only their few portals.
        std::vector<Id> sources{c};
        sources.insert(sources.end(), w.children[c].begin(), w.children[c].end());
        for (Id source : sources) for (Id u : w.members[source])
            for (const auto& group : groups) {
                Pair nearest{std::numeric_limits<float>::infinity(), 0};
                for (Id v : group) if (w.owner[v] != source)
                    nearest = std::min(nearest, Pair{l2.dist(base.at(u), base.at(v)), v});
                if (std::isfinite(nearest.first)) connect(u, nearest.second);
            }
        m.portals[c] = representatives(std::move(pool), base, l2);
    }
    size_t connected = normalize();
    m.connector_edges = connected - m.local_edges;
    // Static cross-level shortcuts: direct-owner portals to inherited subtree representatives.
    for (Id c : w.topo) for (Id u : own[c]) for (Id v : m.portals[c]) connect(u, v);
    m.jump_edges = normalize() - connected;
    return m;
}
struct Scratch {
    std::vector<uint32_t> seen;
    uint32_t epoch = 0;
    explicit Scratch(size_t n) : seen(n) {}
};
static std::vector<Id> search(const Merged& m, const Workload& w, const Vectors& base,
    RawL2Space& l2, const float* query, const std::vector<char>& allowed,
    const std::vector<Id>& roots, size_t ef, Scratch& scratch, size_t& calls) {
    ++scratch.epoch;
    std::priority_queue<Pair, std::vector<Pair>, std::greater<Pair>> frontier;
    std::priority_queue<Pair> best;
    auto offer = [&](Id u) {
        if (!allowed[w.owner[u]] || scratch.seen[u] == scratch.epoch) return;
        scratch.seen[u] = scratch.epoch;
        Pair p{l2.dist(query, base.at(u)), u}; ++calls;
        if (best.size() < ef || p < best.top()) {
            frontier.push(p); best.push(p);
            if (best.size() > ef) best.pop();
        }
    };
    for (Id c : roots) for (Id u : m.portals[c]) offer(u);
    while (!frontier.empty()) {
        Pair p = frontier.top(); frontier.pop();
        if (best.size() == ef && p > best.top()) break;
        for (Id v : m.edges[p.second]) offer(v);
    }
    while (best.size() > 10) best.pop();
    std::vector<Id> result;
    while (!best.empty()) { result.push_back(best.top().second); best.pop(); }
    return result;
}
struct Filter : hnswlib::BaseFilterFunctor {
    const Workload& w;
    const std::vector<char>& allowed;
    Filter(const Workload& a, const std::vector<char>& b) : w(a), allowed(b) {}
    bool operator()(hnswlib::labeltype label) override { return allowed[w.owner[label]]; }
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
int main(int argc, char** argv) try {
    require(argc == 5, "usage: merge_arms BASE.fvecs QUERY.fvecs DAG_DIR RESULT.csv");
    Workload w = load_workload(argv[3]);
    Vectors base = load_vectors(argv[1], w.owner.size()), queries = load_vectors(argv[2], 24);
    require(base.dim == 128 && queries.dim == base.dim, "SIFT128 required");
    RawL2Space l2(base.dim);
    auto start = Clock::now();
    Merged merged = build(base, w, l2);
    double merged_build = us(start) / 1e6;
    start = Clock::now();
    Graph global(&l2, base.n, 16, 200, 100);
    std::vector<Id> order(base.n); std::iota(order.begin(), order.end(), 0);
    std::mt19937 rng(20261101); std::shuffle(order.begin(), order.end(), rng);
    for (Id u : order) global.addPoint(base.at(u), u);
    double global_build = us(start) / 1e6;
    std::vector<Query> workload(24);
    for (size_t i = 0; i < 24; ++i) {
        auto& q = workload[i]; q.allowed.resize(w.members.size());
        // Evenly cover original single-concept and antichain OR workloads.
        for (Id c : w.scopes[i * (w.scopes.size() - 1) / 23])
            for (Id d : w.desc[c]) q.allowed[d] = 1;
        std::vector<char> has_ancestor(w.members.size());
        for (Id c : w.topo) if (q.allowed[c]) {
            if (!has_ancestor[c]) q.roots.push_back(c);
            for (Id d : w.children[c]) has_ancestor[d] = 1;
        }
        std::priority_queue<Pair> truth;
        for (Id u = 0; u < base.n; ++u) if (q.allowed[w.owner[u]]) {
            ++q.count;
            truth.emplace(l2.dist(queries.at(i), base.at(u)), u);
            if (truth.size() > 10) truth.pop();
        }
        while (!truth.empty()) { q.truth.push_back(truth.top().second); truth.pop(); }
    }
    std::vector<char> timed_allowed(w.members.size()), timed_ancestor(w.members.size());
    std::vector<Id> timed_roots;
    timed_roots.reserve(w.members.size());
    Scratch scratch(base.n);
    std::ofstream csv(argv[4]); require(bool(csv), "cannot write results");
    csv << "arm,ef,query,eligible,truth,hits,latency_us,distance_calls\n" << std::setprecision(12);
    double primary = 0, primary_recall = 0, reference = 0, node_reference = 0;
    size_t exhaustive_errors = 0;
    const std::vector<size_t> efs{32, 128, 512, 2048, base.n};
    for (int arm = 0; arm < 3; ++arm) {
        double chosen = std::numeric_limits<double>::infinity();
        for (size_t ef : efs) {
            double total_us = 0, recall_sum = 0;
            size_t nonempty = 0;
            for (size_t i = 0; i < 24; ++i) {
                const auto& q = workload[i];
                if (q.truth.empty()) continue; // explicitly excluded from recall and latency aggregates
                ++nonempty;
                std::vector<Id> result;
                size_t calls = 0;
                // Warm once; median of three timed executions; no GT in timed region.
                std::vector<double> timings;
                for (int repeat = 0; repeat < 4; ++repeat) {
                    auto begin = Clock::now();
                    std::fill(timed_allowed.begin(), timed_allowed.end(), 0);
                    std::fill(timed_ancestor.begin(), timed_ancestor.end(), 0);
                    timed_roots.clear();
                    for (Id c : w.scopes[i * (w.scopes.size() - 1) / 23])
                        for (Id d : w.desc[c]) timed_allowed[d] = 1;
                    for (Id c : w.topo) if (timed_allowed[c]) {
                        if (!timed_ancestor[c]) timed_roots.push_back(c);
                        for (Id d : w.children[c]) timed_ancestor[d] = 1;
                    }
                    if (arm == 0) {
                        calls = 0;
                        result = search(merged, w, base, l2, queries.at(i), timed_allowed, timed_roots,
                                        std::min(ef, q.count), scratch, calls);
                    } else if (arm == 1) {
                        global.setEf(ef);
                        Filter filter(w, timed_allowed);
                        result = labels(global.searchKnn(queries.at(i), 10, &filter));
                    } else {
                        std::priority_queue<Pair> top;
                        for (Id c = 0; c < timed_allowed.size(); ++c) if (timed_allowed[c] && merged.local[c]) {
                            merged.local[c]->setEf(ef);
                            auto local = merged.local[c]->searchKnn(queries.at(i), 10);
                            while (!local.empty()) {
                                top.emplace(local.top().first, Id(local.top().second)); local.pop();
                                if (top.size() > 10) top.pop();
                            }
                        }
                        result.clear();
                        while (!top.empty()) { result.push_back(top.top().second); top.pop(); }
                    }
                    double elapsed = us(begin);
                    if (repeat) timings.push_back(elapsed);
                }
                std::sort(timings.begin(), timings.end());
                size_t hits = 0;
                for (Id u : result)
                    if (l2.dist(queries.at(i), base.at(u)) <=
                        l2.dist(queries.at(i), base.at(q.truth.front()))) ++hits;
                if (ef == base.n && hits != q.truth.size()) ++exhaustive_errors;
                recall_sum += double(hits) / q.truth.size(); total_us += timings[1];
                csv << (arm == 0 ? "merged" : arm == 1 ? "global_filtered" : "nodeann")
                    << ',' << ef << ',' << i << ',' << q.count << ',' << q.truth.size() << ',' << hits
                    << ',' << timings[1] << ',';
                if (arm == 0) csv << calls;
                csv << '\n';
            }
            require(nonempty > 0, "all queries empty");
            double recall = recall_sum / nonempty, latency = total_us / nonempty;
            std::cout << "RESULT arm=" << arm << " ef=" << ef << " recall=" << recall
                      << " latency_us=" << latency << '\n';
            if (recall >= 0.95 && latency < chosen) {
                chosen = latency;
                if (arm == 0) primary_recall = recall;
            }
        }
        require(std::isfinite(chosen), "recall gate not reached");
        if (arm == 0) primary = chosen;
        else if (arm == 1) reference = chosen;
        else node_reference = chosen;
    }
    require(exhaustive_errors == 0, "exhaustive search differs from exact GT");
    require(bool(csv), "result write failed");
    std::cout << std::setprecision(12)
        << "METRIC merged_query_us=" << primary << '\n'
        << "METRIC merged_recall=" << primary_recall << '\n'
        << "METRIC global_filtered_us=" << reference << '\n'
        << "METRIC nodeann_us=" << node_reference << '\n'
        << "METRIC merged_build_s=" << merged_build << '\n'
        << "METRIC global_build_s=" << global_build << '\n'
        << "METRIC local_directed_edges=" << merged.local_edges << '\n'
        << "METRIC connector_directed_edges=" << merged.connector_edges << '\n'
        << "METRIC jump_directed_edges=" << merged.jump_edges << '\n'
        << "METRIC exhaustive_errors=" << exhaustive_errors << '\n';
    return 0;
} catch (const std::exception& e) {
    std::cerr << "merge_arms: " << e.what() << '\n';
    return 1;
}
