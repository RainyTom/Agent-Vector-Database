// WQR E0 comprehensive correctness suite.
// Independent canonical verification: scalar per-object predicate + parent walk.
// Every case records pass/fail; exits 1 on any failure. No performance claims.
#include "wqr_index.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace wqr;

static int failures = 0;
static std::ostringstream log_;

static void check(bool cond, const std::string& name, const std::string& detail = "") {
    if (!cond) {
        ++failures;
        log_ << "FAIL " << detail << "\n";
        std::cerr << "FAIL: " << detail << "\n";
    }
}

// Canonical predicate, identical semantics to dynamic_triad.
static bool canExtra(const Item& it, const Query& q) {
    bool any = false, conj = true, disj = false;
    auto add = [&](bool v) { any = true; conj &= v; disj |= v; };
    if (q.category != -1) add(it.category == q.category);
    if (q.lo != -1 || q.hi != -1) add(it.numeric >= q.lo && it.numeric <= q.hi);
    if (q.tagmode == 1) add((it.tags & q.tags) == q.tags);
    else if (q.tagmode == 2) add((it.tags & q.tags) != 0);
    else if (q.tagmode == 3) add(it.tags == q.tags);
    else if (q.tagmode == 4) add((it.tags & q.tags) == it.tags);
    return !any || (q.combine ? disj : conj);
}
static bool canBeneath(const std::vector<int>& parent, size_t n, size_t anc) {
    for (size_t steps = 0; steps <= parent.size(); ++steps) {
        if (n == anc) return true;
        if (parent[n] < 0) return false;
        n = static_cast<size_t>(parent[n]);
    }
    return false;
}
static bool canMatch(const WQRIndex& idx, size_t id, const Query& q) {
    if (id >= idx.items.size()) return false;
    const auto& it = idx.items[id];
    return it.alive && (q.recursive ? canBeneath(idx.parent, it.node, q.anchor) : it.node == q.anchor)
        && canExtra(it, q);
}

// Verify every block's ExactMask against canonical scalar predicate.
static void verifyMasks(WQRIndex& idx, const Query& q, const std::string& tag) {
    for (size_t bi = 0; bi < idx.blocks.size(); ++bi) {
        auto& blk = idx.blocks[bi];
        if (blk.retired) continue;
        MaskWork w;
        MaskComputer mc(blk, idx.euler, q, w);
        auto mask = mc.compute();
        for (size_t s = 0; s < blk.capacity; ++s) {
            const bool bit = (mask[s / 64] >> (s % 64)) & 1;
            const Id id = blk.slot_to_id[s];
            const bool expect = id != NONE && canMatch(idx, id, q);
            check(bit == expect, "mask", tag + " block=" + std::to_string(bi) +
                  " slot=" + std::to_string(s) + " got=" + std::to_string(bit) +
                  " expect=" + std::to_string(expect));
        }
    }
}

// Verify zone invariants: union == live, pairwise disjoint, each live object in exactly one zone.
static void verifyZones(WQRIndex& idx, const std::string& tag) {
    for (size_t bi = 0; bi < idx.blocks.size(); ++bi) {
        auto& blk = idx.blocks[bi];
        if (blk.retired) continue;
        for (size_t s = 0; s < blk.capacity; ++s) {
            const Id id = blk.slot_to_id[s];
            if (id == NONE || !blk.isAlive(s)) continue;
            const uint8_t z = blk.zone_of[s];
            check(z != 255 && z < blk.zone_bits.size(), "zone", tag + " invalid zone");
            if (z != 255 && z < blk.zone_bits.size())
                check((blk.zone_bits[z][s / 64] >> (s % 64)) & 1, "zone", tag + " zone bit missing");
            // no other zone contains this slot
            for (size_t z2 = 0; z2 < blk.zone_bits.size(); ++z2) {
                if (z2 == z) continue;
                check(!((blk.zone_bits[z2][s / 64] >> (s % 64)) & 1), "zone",
                      tag + " slot in two zones");
            }
        }
    }
}

// Verify all Euler ancestor relations against parent walk.
static void verifyEuler(WQRIndex& idx, const std::string& tag) {
    for (size_t a = 0; a < idx.parent.size(); ++a)
        for (size_t o = 0; o < idx.parent.size(); ++o) {
            const bool got = idx.euler.contains(a, o);
            const bool expect = canBeneath(idx.parent, o, a);
            check(got == expect, "euler", tag + " anchor=" + std::to_string(a) +
                  " owner=" + std::to_string(o));
        }
}

// Verify all query results are legal, deduplicated, sorted.
static void verifyResults(WQRIndex& idx, const Query& q,
                          const std::vector<std::pair<float, Id>>& res, const std::string& tag) {
    std::unordered_set<Id> seen;
    float prev = -1;
    for (auto& [d, id] : res) {
        check(canMatch(idx, id, q), "result-legal", tag + " illegal id=" + std::to_string(id));
        check(seen.insert(id).second, "result-dup", tag + " duplicate id=" + std::to_string(id));
        check(d >= prev, "result-order", tag + " unsorted");
        prev = d;
    }
}

// Build an index from explicit fixture (bypasses partition when blockSets given).
static std::unique_ptr<WQRIndex> makeIndex(std::vector<Item> items,
                                           std::vector<int> parent,
                                           std::vector<std::vector<float>> vecs2d) {
    auto idx = std::make_unique<WQRIndex>();
    const size_t dim = vecs2d.empty() ? 2 : vecs2d[0].size();
    idx->init(items.size(), dim);
    idx->parent = parent;
    idx->items = items;
    idx->dim = dim;
    idx->vectors.assign(items.size() * dim, 0);
    for (size_t i = 0; i < items.size(); ++i)
        for (size_t d = 0; d < dim; ++d) idx->vectors[i * dim + d] = vecs2d[i][d];
    idx->euler.build(parent);
    idx->direct.assign(parent.size(), {});
    for (size_t id = 0; id < items.size(); ++id)
        if (items[id].alive) {
            idx->direct_pos[id] = idx->direct[items[id].node].size();
            idx->direct[items[id].node].push_back(id);
        }
    return idx;
}

static Query baseQuery() {
    Query q; q.anchor = 0; q.recursive = 1; q.category = -1; q.lo = -1; q.hi = -1;
    q.tagmode = 0; q.tags = 0; q.combine = 0;
    return q;
}

int main() {
    // ---- Case 1: branching tree Euler + R + parent-walk verification ----
    {
        // 0 root; children 1,2; 1→3,4; 2→5,6
        std::vector<int> parent{-1, 0, 0, 1, 1, 2, 2};
        std::vector<Item> items(8);
        for (size_t i = 0; i < 8; ++i) { items[i].alive = true; items[i].node = i % 7; }
        items[3].node = 4;  // internal node direct ownership
        std::vector<std::vector<float>> v(8, {0, 0});
        for (size_t i = 0; i < 8; ++i) v[i] = {float(i), float(i * i)};
        auto idx = makeIndex(items, parent, v);
        // two explicit blocks to cross block boundary
        idx->buildBlockFromIds({0, 1, 2, 3}, B0);
        idx->buildBlockFromIds({4, 5, 6, 7}, B0);
        verifyEuler(*idx, "branching-initial");
        Query q = baseQuery();
        verifyMasks(*idx, q, "branching-scope-root");
        q.anchor = 1; q.recursive = 1;
        verifyMasks(*idx, q, "branching-scope-1");
        q.anchor = 4; q.recursive = 0;  // internal node direct scope
        verifyMasks(*idx, q, "internal-owner-direct");
        idx->apply_R(1, 2);
        idx->parent[1] = 2;
        verifyEuler(*idx, "after-R-1-2");
        q.anchor = 2; q.recursive = 1;
        verifyMasks(*idx, q, "after-R-scope");
        // R back
        idx->apply_R(1, 0);
        idx->parent[1] = 0;
        verifyEuler(*idx, "after-R-back");
        std::cout << "case1 branching/euler/R ok\n";
    }

    // ---- Case 2: attribute semantics: AND/OR, empty tag, literal -1, int64 bounds, negative ----
    {
        std::vector<int> parent{-1};
        std::vector<Item> items(70);
        for (size_t i = 0; i < 70; ++i) {
            items[i].alive = true; items[i].node = 0;
            items[i].category = int64_t(i) - 35;          // includes negatives
            items[i].numeric = int64_t(i) * 2 - 70;       // -70..68
            items[i].tags = uint64_t(1) << (i % 4);
        }
        items[0].numeric = std::numeric_limits<int64_t>::min();
        items[69].numeric = std::numeric_limits<int64_t>::max();
        items[0].category = -5;
        items[5].tags = 0;  // empty tag item
        std::vector<std::vector<float>> v(70, {0, 0});
        for (size_t i = 0; i < 70; ++i) v[i] = {float(i), 0};
        auto idx = makeIndex(items, parent, v);
        idx->buildBlockFromIds([&]{ std::vector<Id> ids(70); std::iota(ids.begin(), ids.end(), 0); return ids; }(), B0);
        Query q = baseQuery();
        // literal -1 endpoint: numeric in [-1, 5]
        q.lo = -1; q.hi = 5;
        verifyMasks(*idx, q, "numeric-literal-neg1");
        // both -1 → disabled
        q.lo = -1; q.hi = -1; q.category = 3;
        verifyMasks(*idx, q, "category-only");
        // negative category
        q.category = -5;
        verifyMasks(*idx, q, "negative-category");
        // int64 min/max range
        q.category = -1; q.lo = std::numeric_limits<int64_t>::min(); q.hi = std::numeric_limits<int64_t>::min();
        verifyMasks(*idx, q, "int64-min");
        q.lo = std::numeric_limits<int64_t>::max(); q.hi = std::numeric_limits<int64_t>::max();
        verifyMasks(*idx, q, "int64-max");
        // OR combine
        q.lo = 60; q.hi = 70; q.category = 0; q.combine = 1;
        verifyMasks(*idx, q, "or-combine");
        // AND combine
        q.combine = 0;
        verifyMasks(*idx, q, "and-combine");
        // tags modes
        q.lo = -1; q.hi = -1;
        q.tagmode = 1; q.tags = 0b1;       // Q⊆ITEM
        verifyMasks(*idx, q, "tag-mode1");
        q.tagmode = 1; q.tags = 0;         // empty Q → all live
        verifyMasks(*idx, q, "tag-mode1-empty");
        q.tagmode = 2; q.tags = 0b1;       // Q∩ITEM≠∅
        verifyMasks(*idx, q, "tag-mode2");
        q.tagmode = 2; q.tags = 0;         // empty Q → none
        verifyMasks(*idx, q, "tag-mode2-empty");
        q.tagmode = 4; q.tags = 0b1;       // ITEM⊆Q
        verifyMasks(*idx, q, "tag-mode4");
        q.tagmode = 3; q.tags = 0b10;      // ITEM=Q
        verifyMasks(*idx, q, "tag-mode3");
        q.tagmode = 3; q.tags = 0;         // empty tag equality
        verifyMasks(*idx, q, "tag-mode3-empty");
        verifyZones(*idx, "case2");
        std::cout << "case2 attributes ok\n";
    }

    // ---- Case 3: D/I restore, zone preservation, interleaved Q/updates ----
    {
        std::vector<int> parent{-1, 0};
        std::vector<Item> items(10);
        for (size_t i = 0; i < 10; ++i) { items[i].alive = true; items[i].node = i < 5 ? 0 : 1; items[i].category = int64_t(i); items[i].numeric = int64_t(i); }
        std::vector<std::vector<float>> v(10, {0, 0});
        // two clusters to force 2 zones per block
        v[0]={0,0}; v[1]={0.1,0}; v[2]={10,0}; v[3]={10.1,0}; v[4]={5,0};
        v[5]={0,10}; v[6]={0.1,10}; v[7]={10,10}; v[8]={10.1,10}; v[9]={5,10};
        auto idx = makeIndex(items, parent, v);
        idx->buildBlockFromIds({0,1,2,3,4,5,6,7,8,9}, B0);
        auto& blk = idx->blocks[0];
        // delete id 1, record its zone
        const auto lc = idx->loc[1];
        const uint8_t z1 = blk.zone_of[lc.slot];
        MaintenanceCounters mc;
        idx->apply_D(1, mc);
        check(!idx->items[1].alive, "D", "item still alive");
        verifyZones(*idx, "after-D");
        Query q = baseQuery();
        verifyMasks(*idx, q, "after-D-mask");
        // restore with new attributes
        idx->apply_I(1, 1, 77, 88, 0b11, mc);
        check(idx->items[1].alive, "I-restore", "not alive");
        check(idx->items[1].category == 77 && idx->items[1].numeric == 88 && idx->items[1].tags == 0b11,
              "I-restore", "attrs wrong");
        check(idx->items[1].node == 1, "I-restore", "owner wrong");
        // zone must equal canonical nearest proxy, not forced 0
        const auto lc2 = idx->loc[1];
        check(lc2.block == lc.block && lc2.slot == lc.slot, "I-restore", "slot not restored");
        const uint8_t z1r = idx->blocks[lc2.block].zone_of[lc2.slot];
        check(z1r == z1, "I-restore-zone", "zone changed: " + std::to_string(z1) + "→" + std::to_string(z1r));
        verifyZones(*idx, "after-I");
        q.category = 77;
        verifyMasks(*idx, q, "after-I-mask");
        // interleaved updates
        idx->apply_V(2, -3, -9);
        idx->apply_T(3, 0);
        idx->apply_M(4, 1);
        idx->apply_B(1, 0, mc);
        idx->apply_R(1, 0);
        verifyEuler(*idx, "case3-euler");
        q = baseQuery(); q.anchor = 0;
        verifyMasks(*idx, q, "interleaved");
        verifyZones(*idx, "interleaved");
        std::cout << "case3 D/I/interleave ok\n";
    }

    // ---- Case 4: empty index first I, reserve first I ----
    {
        std::vector<int> parent{-1};
        std::vector<Item> items(3);  // all dead (reserve)
        std::vector<std::vector<float>> v{{0,0},{1,0},{0,1}};
        auto idx = makeIndex(items, parent, v);
        check(idx->blocks.empty(), "empty-index", "blocks not empty");
        MaintenanceCounters mc;
        idx->apply_I(0, 0, 1, 2, 0, mc);   // first I into empty index
        check(idx->blocks.size() == 1 && idx->blocks[0].live == 1, "first-I", "block not created");
        idx->apply_I(1, 0, 2, 3, 0, mc);   // reserve first I (never alive before)
        check(idx->blocks[0].live == 2, "reserve-I", "reserve not inserted");
        Query q = baseQuery();
        verifyMasks(*idx, q, "reserve-mask");
        verifyZones(*idx, "reserve");
        std::cout << "case4 empty/reserve I ok\n";
    }

    // ---- Case 5: proxy source deletion keeps routing ----
    {
        std::vector<int> parent{-1};
        std::vector<Item> items(6);
        for (auto& it : items) { it.alive = true; it.node = 0; }
        std::vector<std::vector<float>> v{{0,0},{1,0},{2,0},{10,0},{11,0},{12,0}};
        auto idx = makeIndex(items, parent, v);
        idx->buildBlockFromIds({0,1,2,3,4,5}, B0);
        // proxy 0 source = smallest id = 0. Delete it; zone-0 members must remain reachable.
        MaintenanceCounters mc;
        idx->apply_D(0, mc);
        Query q = baseQuery();
        QueryCounters qc;
        auto res = idx->query(v[1].data(), q, 2, 1, 256, 1024, Arm::WQR, qc);
        bool found1 = false;
        for (auto& [d, id] : res) if (id == 1) found1 = true;
        check(found1, "proxy-source-delete", "zone member unreachable after source D");
        verifyMasks(*idx, q, "proxy-source-delete-mask");
        verifyZones(*idx, "proxy-source-delete");
        std::cout << "case5 proxy source deletion ok\n";
    }

    // ---- Case 6: representative aliasing positive + counterexample ----
    {
        // Positive: A ids 0..3 at x=0,10,20,30 (only id3 eligible); B ids 4,5 at 1,1.1 eligible.
        std::vector<int> parent{-1};
        std::vector<Item> items(6);
        for (auto& it : items) { it.alive = true; it.node = 0; it.category = 0; }
        items[3].category = 1; items[4].category = 1; items[5].category = 1;
        std::vector<std::vector<float>> v{{0,0},{10,0},{20,0},{30,0},{1,0},{1.1,0}};
        auto idx = makeIndex(items, parent, v);
        idx->buildBlockFromIds({0,1,2,3}, B0);
        idx->buildBlockFromIds({4,5}, B0);
        Query q = baseQuery(); q.category = 1;
        const std::vector<float> qv{0, 0};
        QueryCounters qcB, qcW;
        auto resB = idx->query(qv.data(), q, 1, 1, 256, 1024, Arm::BlockGate, qcB);
        auto resW = idx->query(qv.data(), q, 1, 1, 256, 1024, Arm::WQR, qcW);
        check(!resB.empty() && std::abs(resB[0].first - 900) < 1e-3, "alias-positive-blockgate",
              resB.empty() ? "empty" : "d=" + std::to_string(resB[0].first));
        check(!resW.empty() && std::abs(resW[0].first - 1) < 1e-2, "alias-positive-wqr",
              resW.empty() ? "empty" : "d=" + std::to_string(resW[0].first));
        std::cout << "case6a aliasing positive ok (BlockGate=" << (resB.empty()?-1:resB[0].first)
                  << " WQR=" << (resW.empty()?-1:resW[0].first) << ")\n";
    }
    {
        // Counterexample: A = circle endpoints ±(10,0),(0,±10) + legal center; B near (1,0).
        std::vector<int> parent{-1};
        std::vector<Item> items(7);
        for (auto& it : items) { it.alive = true; it.node = 0; it.category = 1; }
        std::vector<std::vector<float>> v{{10,0},{-10,0},{0,10},{0,-10},{0,0},{1,0},{1.001,0}};
        auto idx = makeIndex(items, parent, v);
        idx->buildBlockFromIds({0,1,2,3,4}, B0);
        idx->buildBlockFromIds({5,6}, B0);
        Query q = baseQuery();
        const std::vector<float> qv{0, 0};
        QueryCounters qc1, qc2;
        auto res1 = idx->query(qv.data(), q, 1, 1, 256, 1024, Arm::WQR, qc1);
        auto res2 = idx->query(qv.data(), q, 1, 2, 256, 1024, Arm::WQR, qc2);
        // GT = center id4 at distance 0. L=1 misses (block B chosen); L=2 recovers.
        bool gtIn1 = false, gtIn2 = false;
        for (auto& [d, id] : res1) if (id == 4) gtIn1 = true;
        for (auto& [d, id] : res2) if (id == 4) gtIn2 = true;
        check(!gtIn1, "alias-counter-L1", "unexpectedly found GT at L=1");
        check(gtIn2, "alias-counter-L2", "GT not found at L=2");
        std::cout << "case6b aliasing counterexample ok\n";
    }

    // ---- Case 7: capacity/split boundary (B0→2B0) ----
    {
        std::vector<int> parent{-1};
        const size_t N = 2 * B0 + 3;
        std::vector<Item> items(N);
        for (size_t i = 0; i < 4; ++i) { items[i].alive = true; items[i].node = 0; }
        // rest are reserve, inserted one by one; all vectors near origin → same block
        std::vector<std::vector<float>> v(N, {0, 0});
        for (size_t i = 0; i < N; ++i) v[i] = {float(i) * 1e-4f, 0};
        auto idx = makeIndex(items, parent, v);
        idx->buildBlockFromIds({0, 1, 2, 3}, B0);
        MaintenanceCounters mc;
        size_t splits0 = 0;
        for (size_t i = 4; i < N; ++i) {
            const size_t before = idx->blocks.size();
            idx->apply_I(i, 0, int64_t(i), int64_t(i), 0, mc);
            if (idx->blocks.size() > before + 0 && mc.splits > splits0) splits0 = mc.splits;
        }
        check(mc.splits >= 1, "split", "no split at 2B0 boundary");
        size_t totalLive = 0;
        for (auto& b : idx->blocks) if (!b.retired) totalLive += b.live;
        check(totalLive == N, "split-live", "live count " + std::to_string(totalLive) + " != " + std::to_string(N));
        verifyZones(*idx, "split");
        Query q = baseQuery();
        verifyMasks(*idx, q, "split-mask");
        // every live item locates in a non-retired block
        for (size_t i = 0; i < N; ++i) {
            const auto lc = idx->loc[i];
            check(lc.block != NONE && lc.block < idx->blocks.size() && !idx->blocks[lc.block].retired,
                  "split-loc", "id=" + std::to_string(i));
            check(idx->blocks[lc.block].slot_to_id[lc.slot] == i, "split-slot", "id=" + std::to_string(i));
        }
        std::cout << "case7 split boundary ok (splits=" << mc.splits << ")\n";
    }

    // ---- Case 8: non-contiguous live ids (odd ids only) + all-empty result ----
    {
        std::vector<int> parent{-1};
        std::vector<Item> items(10);
        for (size_t i = 0; i < 10; ++i) { items[i].alive = (i % 2 == 1); items[i].node = 0; }
        std::vector<std::vector<float>> v(10, {0, 0});
        for (size_t i = 0; i < 10; ++i) v[i] = {float(i), 0};
        auto idx = makeIndex(items, parent, v);
        std::vector<Id> odd{1, 3, 5, 7, 9};
        idx->buildBlockFromIds(odd, B0);
        Query q = baseQuery();
        verifyMasks(*idx, q, "noncontiguous");
        // empty-eligibility query
        q.category = 999;
        verifyMasks(*idx, q, "empty-eligibility");
        QueryCounters qc;
        const std::vector<float> qv{0, 0};
        auto res = idx->query(qv.data(), q, 10, 1, 256, 1024, Arm::WQR, qc);
        check(res.empty(), "empty-eligibility", "returned results for empty predicate");
        std::cout << "case8 non-contiguous/empty ok\n";
    }

    // ---- Case 9: query interleaved with every update type, canonical each time ----
    {
        std::vector<int> parent{-1, 0, 0};
        std::vector<Item> items(8);
        for (size_t i = 0; i < 8; ++i) { items[i].alive = true; items[i].node = i % 3; items[i].numeric = int64_t(i); }
        std::vector<std::vector<float>> v(8, {0, 0});
        for (size_t i = 0; i < 8; ++i) v[i] = {float(i), float(i % 2)};
        auto idx = makeIndex(items, parent, v);
        idx->buildBlockFromIds({0,1,2,3,4,5,6,7}, B0);
        MaintenanceCounters mc;
        Query q = baseQuery();
        auto step = [&](const std::string& tag) {
            verifyMasks(*idx, q, tag);
            verifyZones(*idx, tag);
            verifyEuler(*idx, tag);
            QueryCounters qc;
            auto res = idx->query(v[0].data(), q, 3, 1, 256, 1024, Arm::WQR, qc);
            verifyResults(*idx, q, res, tag);
        };
        step("t0");
        idx->apply_V(0, 7, 700); step("after-V");
        idx->apply_T(1, 0b101); step("after-T");
        idx->apply_M(2, 2); step("after-M");
        idx->apply_B(1, 2, mc); step("after-B");
        idx->apply_R(1, 2); idx->parent[1] = 2; step("after-R");
        idx->apply_D(3, mc); step("after-D");
        idx->apply_I(3, 0, 1, 2, 3, mc); step("after-I-restore");
        idx->apply_D(3, mc); step("after-D2");
        idx->apply_I(3, 2, 9, 9, 9, mc);  // second delete+insert cycle
        step("after-I2");
        std::cout << "case9 interleaved ok\n";
    }

    std::cout << (failures ? "E0 FAILURES: " : "E0 ALL PASS; failures=") << failures << "\n";
    if (failures) {
        std::ofstream("/tmp/wqr-impl/work/wqr-e0-failures.log") << log_.str();
        return 1;
    }
    return 0;
}
