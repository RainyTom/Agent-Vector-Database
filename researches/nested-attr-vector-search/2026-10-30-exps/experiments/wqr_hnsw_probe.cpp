// Finite mechanism probe, NOT a production WQR implementation or benchmark.
// Uses the repository's RawL2Space and the pinned hnswlib. Full predicate/rope
// semantics are exercised separately by wqr_semantics.py.
#include "raw_l2_space.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <set>
#include <vector>

using Vec = std::array<float, 2>;
using Graph = hnswlib::HierarchicalNSW<float>;
struct Counted : RawL2Space {
    explicit Counted() : RawL2Space(2) {}
    mutable size_t calls = 0;
    static float count(const void* a, const void* b, const void* p) {
        auto* self = static_cast<const Counted*>(p);
        ++self->calls;
        return self->dist(static_cast<const float*>(a), static_cast<const float*>(b));
    }
    hnswlib::DISTFUNC<float> get_dist_func() override { return count; }
    void* get_dist_func_param() override { return this; }
};
struct Object { Vec v; bool eligible; bool alive = true; };
struct Portal { size_t block; uint64_t zone; Vec v; };
struct Block {
    std::vector<Object> objects;
    std::unique_ptr<Graph> graph;
};
struct Result {
    size_t block = SIZE_MAX, slot = SIZE_MAX;
    float distance = 0;
    size_t distinct_blocks = 0, prepared_masks = 0, route_distances = 0;
};
struct Index {
    Counted space;
    std::vector<Block> blocks;
    std::vector<Portal> portals;
    std::unique_ptr<Graph> route;
    explicit Index(std::vector<std::vector<Object>> input) {
        for (auto& values : input) {
            assert(values.size() <= 64 && !values.empty());
            Block b;
            b.objects = std::move(values);
            b.graph = std::make_unique<Graph>(&space, b.objects.size() + 1, 4, 64, 100);
            for (size_t i = 0; i < b.objects.size(); ++i) b.graph->addPoint(b.objects[i].v.data(), i);
            b.graph->setEf(64);
            std::vector<size_t> selected{0};
            while (selected.size() < std::min(size_t(4), b.objects.size())) {
                size_t best = SIZE_MAX; float farthest = -1;
                for (size_t i = 0; i < b.objects.size(); ++i) {
                    if (std::find(selected.begin(), selected.end(), i) != selected.end()) continue;
                    float nearest = std::numeric_limits<float>::infinity();
                    for (size_t j : selected) nearest = std::min(nearest, space.dist(b.objects[i].v.data(), b.objects[j].v.data()));
                    if (nearest > farthest) { best = i; farthest = nearest; }
                }
                selected.push_back(best);
            }
            const size_t offset = portals.size();
            for (size_t s : selected) portals.push_back({blocks.size(), 0, b.objects[s].v});
            for (size_t i = 0; i < b.objects.size(); ++i) {
                size_t winner = 0; float nearest = std::numeric_limits<float>::infinity();
                for (size_t j = 0; j < selected.size(); ++j) {
                    const float distance = space.dist(b.objects[i].v.data(), portals[offset+j].v.data());
                    if (distance < nearest) { winner = j; nearest = distance; }
                }
                portals[offset+winner].zone |= uint64_t(1) << i;
            }
            blocks.push_back(std::move(b));
        }
        route = std::make_unique<Graph>(&space, portals.size() + 1, 4, 64, 100);
        for (size_t i = 0; i < portals.size(); ++i) route->addPoint(portals[i].v.data(), i);
        route->setEf(64);
    }
    Result search(const Vec& q, size_t limit, bool zone_gate, bool oversample = true) {
        struct Filter : hnswlib::BaseFilterFunctor {
            Index& index; bool zones; size_t prepared = 0;
            std::vector<uint64_t> masks;
            std::vector<bool> ready;
            Filter(Index& i, bool z) : index(i), zones(z), masks(i.blocks.size()), ready(i.blocks.size()) {}
            uint64_t get(size_t b) {
                if (!ready[b]) {
                    ready[b] = true; ++prepared;
                    for (size_t s=0; s<index.blocks[b].objects.size(); ++s) {
                        const auto& o=index.blocks[b].objects[s];
                        if (o.alive && o.eligible) masks[b] |= uint64_t(1) << s;
                    }
                }
                return masks[b];
            }
            bool operator()(hnswlib::labeltype label) override {
                const auto& p = index.portals.at(label);
                return (get(p.block) & (zones ? p.zone : UINT64_MAX)) != 0;
            }
        } filter(*this, zone_gate);
        Result result;
        space.calls = 0;
        auto heap = route->searchKnn(q.data(), std::min(portals.size(), (oversample ? 4 : 1)*limit), &filter);
        result.route_distances = space.calls;
        std::vector<std::pair<float,size_t>> ordered;
        while (!heap.empty()) { ordered.push_back(heap.top()); heap.pop(); }
        std::sort(ordered.begin(), ordered.end());
        std::set<size_t> seen;
        float best = std::numeric_limits<float>::infinity();
        for (auto [distance, label] : ordered) {
            (void)distance;
            size_t b = portals[label].block;
            if (seen.count(b) || seen.size() == limit) continue;
            seen.insert(b);
            struct Local : hnswlib::BaseFilterFunctor {
                uint64_t mask;
                explicit Local(uint64_t m): mask(m) {}
                bool operator()(hnswlib::labeltype s) override { return (mask & (uint64_t(1)<<s)) != 0; }
            } local(filter.get(b));
            auto answer = blocks[b].graph->searchKnn(q.data(), 1, &local);
            if (!answer.empty() && answer.top().first < best) {
                best = answer.top().first;
                result.block = b; result.slot = answer.top().second; result.distance = best;
            }
        }
        result.distinct_blocks = seen.size(); result.prepared_masks = filter.prepared;
        return result;
    }
};

int main() {
    const Vec q{0,0};
    Index benefit({{{{0,0},false},{{10,0},false},{{20,0},false},{{30,0},true}},
                   {{{1,0},true},{{2,0},true},{{3,0},true},{{4,0},true}}});
    auto coarse = benefit.search(q,1,false), witness = benefit.search(q,1,true);
    assert(coarse.block==0 && coarse.distance==900);
    assert(witness.block==1 && witness.distance==1);
    Index counter({{{{10,0},false},{{-10,0},false},{{0,10},false},{{0,-10},false},{{0,0},true}},
                   {{{1,0},true},{{1,.01f},true},{{1.01f,0},true},{{1.01f,.01f},true},{{1.005f,.005f},true}}});
    auto missed = counter.search(q,1,true), covered = counter.search(q,2,true);
    assert(missed.block==1 && missed.distance==1);
    assert(covered.block==0 && covered.slot==4 && covered.distance==0);
    // Proxy remains valid after its source object's deletion; it is not a result.
    for (size_t s=0; s<4; ++s) { counter.blocks[0].objects[s].alive=false; counter.blocks[0].graph->markDelete(s); }
    auto after_delete = counter.search(q,2,true);
    assert(after_delete.block==0 && after_delete.slot==4);
    Index duplicate({{{{.01f,0},true},{{.02f,0},true},{{.03f,0},true},{{.04f,0},true}},
                     {{{1,0},true},{{2,0},true},{{3,0},true},{{4,0},true}}});
    auto undersample = duplicate.search(q,2,true,false), full = duplicate.search(q,2,true,true);
    assert(undersample.distinct_blocks==1 && full.distinct_blocks==2);
    for (auto& b:duplicate.blocks) for(auto& x:b.objects) x.eligible=false;
    auto empty = duplicate.search(q,2,true);
    assert(empty.block==SIZE_MAX && empty.prepared_masks==2);
    std::cout << "{\n  \"kind\":\"finite_real_hnsw_mechanism_probe\",\n"
              << "  \"hnsw_revision\":\"ca426729609b3221563047ceb269cc1213e0d376\",\n"
              << "  \"block_gate_squared_distance\":" << coarse.distance << ",\n"
              << "  \"zone_gate_squared_distance\":" << witness.distance << ",\n"
              << "  \"routing_counterexample_squared_distance\":" << missed.distance << ",\n"
              << "  \"counterexample_gt_squared_distance\":0,\n"
              << "  \"all_blocks_recovers_counterexample\":true,\n"
              << "  \"deleted_source_proxy_retained\":true,\n"
              << "  \"undersampled_distinct_blocks\":" << undersample.distinct_blocks << ",\n"
              << "  \"oversampled_distinct_blocks\":" << full.distinct_blocks << ",\n"
              << "  \"empty_query_masks_prepared\":" << empty.prepared_masks << ",\n"
              << "  \"empty_query_portal_distance_calls\":" << empty.route_distances << ",\n"
              << "  \"performance_claim\":false\n}\n";
}
