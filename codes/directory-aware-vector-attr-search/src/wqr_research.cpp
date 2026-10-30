// WQR replay runner — same data/trace protocol as dynamic_triad.
// Data: tree.txt (node count + "id parent"), bindings.txt (owner node per item),
// attrs.txt ("category numeric tags alive" per item), base.f32, test.f32, meta.txt.
// Trace: first line = declared event count; then events in order.
//   Q qidx recursive anchor category lo hi tagmode tags combine
//   V id category numeric | T id tags | M id dst | B src dst | R src dst
//   D id | I id node category numeric tags
// Usage: wqr_research --data DIR --trace FILE --arm G|B|W|E --out PREFIX
//        [--probe-blocks L] [--portal-ef N] [--local-ef N] [--k 10]
// Arms: G=GeometryRoute B=BlockGate W=WQR E=EagerWQR.
#include "wqr_index.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <sys/resource.h>
#include <unordered_set>
#include <vector>

using Clock = std::chrono::steady_clock;
static double elapsedUs(Clock::time_point t) {
    return std::chrono::duration<double, std::micro>(Clock::now() - t).count();
}
[[noreturn]] static void die(const std::string& m) { throw std::runtime_error(m); }

struct Args {
    std::string data, trace, out, gt_cache;
    char arm = 0;
    size_t L = 4, portal_ef = 256, local_ef = 1024, k = 10;
};

static Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 == argc) die("missing value for " + std::string(argv[i]));
        const std::string key = argv[i], value = argv[i + 1];
        if (key == "--data") a.data = value;
        else if (key == "--trace") a.trace = value;
        else if (key == "--out") a.out = value;
        else if (key == "--arm") { if (value.size() != 1) die("arm must be G|B|W|E"); a.arm = value[0]; }
        else if (key == "--probe-blocks") a.L = std::stoull(value);
        else if (key == "--portal-ef") a.portal_ef = std::stoull(value);
        else if (key == "--local-ef") a.local_ef = std::stoull(value);
        else if (key == "--k") a.k = std::stoull(value);
        else if (key == "--gt-cache") a.gt_cache = value;
        else die("unknown option " + key);
    }
    if (a.data.empty() || a.trace.empty() || a.out.empty() ||
        (a.arm != 'G' && a.arm != 'B' && a.arm != 'W' && a.arm != 'E'))
        die("usage: wqr_research --data DIR --trace FILE --arm G|B|W|E --out PREFIX [--probe-blocks L --portal-ef N --local-ef N --k 10]");
    return a;
}

static std::vector<float> floats(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) die("cannot open " + path);
    const auto bytes = in.tellg();
    if (bytes < 0 || static_cast<uint64_t>(bytes) % sizeof(float)) die("bad float32 size: " + path);
    std::vector<float> r(static_cast<size_t>(bytes) / sizeof(float));
    in.seekg(0);
    if (bytes && !in.read(reinterpret_cast<char*>(r.data()), bytes)) die("short read: " + path);
    for (float v : r) if (!std::isfinite(v)) die("nonfinite vector in " + path);
    return r;
}

// Independent canonical oracle: scalar per-object predicate + parent walk.
struct Canonical {
    const std::vector<wqr::Item>& items;
    const std::vector<int>& parent;
    bool beneath(size_t n, size_t anc) const {
        for (size_t steps = 0; steps <= parent.size(); ++steps) {
            if (n == anc) return true;
            if (parent[n] < 0) return false;
            n = static_cast<size_t>(parent[n]);
        }
        die("topology cycle");
    }
    bool extra(const wqr::Item& it, const wqr::Query& q) const {
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
    bool matches(size_t id, const wqr::Query& q) const {
        if (id >= items.size()) return false;
        const auto& it = items[id];
        return it.alive && (q.recursive ? beneath(it.node, q.anchor) : it.node == q.anchor) && extra(it, q);
    }
};

static wqr::Arm armOf(char c) {
    switch (c) {
        case 'G': return wqr::Arm::GeometryRoute;
        case 'B': return wqr::Arm::BlockGate;
        case 'W': return wqr::Arm::WQR;
        case 'E': return wqr::Arm::EagerWQR;
    }
    die("bad arm");
}

int main(int argc, char** argv) {
    try {
        const Args args = parseArgs(argc, argv);
        // ---- load canonical data ----
        std::ifstream binding(args.data + "/bindings.txt");
        if (!binding) die("cannot open bindings.txt");
        std::vector<wqr::Item> items;
        size_t node;
        while (binding >> node) { wqr::Item it; it.node = node; items.push_back(it); }
        if (!binding.eof() || items.empty()) die("malformed/empty bindings.txt");
        const auto base = floats(args.data + "/base.f32");
        if (base.empty() || base.size() % items.size()) die("base.f32 not divisible by binding count");
        const size_t dim = base.size() / items.size();
        const auto tests = floats(args.data + "/test.f32");
        if (tests.empty() || tests.size() % dim) die("test.f32 not divisible by dim");
        const size_t nq = tests.size() / dim;
        {
            std::ifstream meta(args.data + "/meta.txt");
            if (meta) {
                size_t dn, dd, dq; std::string trailing;
                if (!(meta >> dn >> dd >> dq) || (meta >> trailing) ||
                    dn != items.size() || dd != dim || dq != nq)
                    die("meta.txt disagrees with raw vector/binding sizes");
            }
        }
        std::ifstream tree(args.data + "/tree.txt");
        size_t nodes;
        if (!(tree >> nodes) || !nodes) die("bad tree node count");
        std::vector<int> parent(nodes, -2);
        size_t root = wqr::NONE;
        for (size_t i = 0; i < nodes; ++i) {
            size_t id; int p;
            if (!(tree >> id >> p) || id >= nodes || p < -1 || p >= static_cast<int>(nodes) || parent[id] != -2)
                die("malformed/duplicate tree node");
            parent[id] = p;
            if (p == -1) { if (root != wqr::NONE) die("multiple tree roots"); root = id; }
        }
        if (root == wqr::NONE) die("missing tree root");
        std::ifstream attr(args.data + "/attrs.txt");
        if (!attr) die("cannot open attrs.txt");
        for (auto& it : items) {
            int alive;
            if (!(attr >> it.category >> it.numeric >> it.tags >> alive) || (alive != 0 && alive != 1))
                die("malformed attrs row");
            it.alive = alive;
            if (it.node >= nodes) die("binding node out of range");
        }
        std::string surplus;
        if (attr >> surplus) die("extra attrs rows");

        // ---- build index ----
        wqr::WQRIndex index;
        index.init(items.size(), dim);
        const auto buildStart = Clock::now();
        const uint64_t buildD0 = index.build_space->calls;
        index.build(parent, items, base);
        const double buildUs = elapsedUs(buildStart);
        const uint64_t buildDist = index.build_space->calls - buildD0;
        uint64_t buildGraphCalls = index.pspace ? index.pspace->calls : 0;
        for (const auto& b : index.blocks) if (b.space) buildGraphCalls += b.space->calls;

        auto dir = std::filesystem::path(args.out).parent_path();
        if (!dir.empty()) std::filesystem::create_directories(dir);
        std::ofstream qout(args.out + ".queries.csv");
        std::ofstream uout(args.out + ".updates.csv");
        std::ofstream sout(args.out + ".summary.txt");
        if (!qout || !uout || !sout) die("cannot open outputs");
        qout << std::setprecision(12);
        qout << "event,version,query_index,qidx,recursive,anchor,gt_count,hit_count,recall,"
                "invalid,duplicate,state_errors,e2e_us,portal_distances,local_distances,"
                "masks_prepared,owner_rank_queries,owner_cache_hits,posting_slots_read,bitset_word_ops,"
                "portal_returned,blocks_selected,local_searches,route_coverage,cond_local_recall,"
                "ref_coverage,cov_delta_signed,gt_distance_calls,gt_ids,result_ids,reference_blocks\n";
        uout << std::setprecision(12);
        uout << "event,version,op,latency_us,distance_calls,ann_insertions,ann_deletions,"
                "rebuilds,splits,compactions,registry_compactions,posting_writes\n";
        sout << "build_us " << buildUs << "\nbuild_partition_distance_calls " << buildDist
             << "\nbuild_graph_distance_calls " << buildGraphCalls << "\n";

        Canonical canon{index.items, index.parent};
        wqr::CountedL2 oracleSpace(dim);

        // GT cache: same trace+data → identical state sequence → identical GT.
        std::vector<std::pair<size_t, std::vector<size_t>>> gtCache;  // (eligible_count, gt ids)
        bool gtCacheLoaded = false;
        if (!args.gt_cache.empty()) {
            std::ifstream gc(args.gt_cache);
            if (gc) {
                std::string magic; size_t ncache;
                if (!(gc >> magic >> ncache) || magic != "WQRGT1") die("bad GT cache header");
                gtCache.resize(ncache);
                for (size_t i = 0; i < ncache; ++i) {
                    size_t eligible, k2;
                    if (!(gc >> eligible >> k2)) die("bad GT cache row");
                    gtCache[i].first = eligible;
                    gtCache[i].second.resize(k2);
                    for (size_t j = 0; j < k2; ++j) if (!(gc >> gtCache[i].second[j])) die("bad GT cache id");
                }
                gtCacheLoaded = true;
            }
        }
        std::vector<std::pair<size_t, std::vector<size_t>>> gtComputed;
        size_t version = 0, queryIndex = 0;
        uint64_t totalGt = 0, totalHit = 0, totalInvalid = 0, totalDup = 0, totalState = 0;
        std::vector<double> queryTimes;
        std::vector<double> recalls;

        // ---- event loop ----
        std::ifstream trace(args.trace);
        if (!trace) die("cannot open trace");
        size_t count = 0;
        if (!(trace >> count)) die("cannot read trace event count");
        std::string line;
        std::getline(trace, line);
        if (line.find_first_not_of(" \t\r") != std::string::npos) die("extra trace header fields");
        for (size_t current = 1; current <= count; ++current) {
            if (!std::getline(trace, line)) die("trace ends before declared event count");
            std::istringstream in(line);
            char op = 0; in >> op;
            if (op == 'Q') {
                wqr::Query q;
                if (!(in >> q.qidx >> q.recursive >> q.anchor >> q.category >> q.lo >> q.hi
                        >> q.tagmode >> q.tags >> q.combine)) die("malformed Q event");
                if (q.qidx >= nq) die("query index out of range");
                if (q.anchor >= nodes) die("anchor out of range");
                const float* qvec = tests.data() + q.qidx * dim;
                const auto t0 = Clock::now();
                wqr::QueryCounters qc;
                auto result = index.query(qvec, q, args.k, args.L, args.portal_ef, args.local_ef,
                                          armOf(args.arm), qc);
                const double e2e = elapsedUs(t0);
                // Offline exact proxy reference (after measurement; not online).
                wqr::QueryCounters refQc;
                qc.reference_blocks = index.exactPortalBlocks(qvec, q, armOf(args.arm),
                    std::min(4 * args.L, index.live_proxies), args.L, refQc);
                // GT: from verified cache or independent oracle (post-measurement).
                const uint64_t gtD0 = oracleSpace.calls;
                std::vector<size_t> gt;
                size_t eligibleCount = 0;
                if (gtCacheLoaded) {
                    if (queryIndex >= gtCache.size()) die("GT cache too short");
                    // Verify cache against current canonical state.
                    size_t elig = 0;
                    for (size_t id = 0; id < index.items.size(); ++id) if (canon.matches(id, q)) ++elig;
                    if (elig != gtCache[queryIndex].first)
                        die("GT cache stale: eligible count mismatch");
                    gt = gtCache[queryIndex].second;
                    for (size_t id : gt) if (!canon.matches(id, q)) die("GT cache stale: id mismatch");
                } else {
                    std::vector<std::pair<float, size_t>> gtAll;
                    for (size_t id = 0; id < index.items.size(); ++id)
                        if (canon.matches(id, q)) {
                            ++eligibleCount;
                            gtAll.emplace_back(oracleSpace.cdist(qvec, index.vec(id)), id);
                        }
                    std::sort(gtAll.begin(), gtAll.end());
                    if (gtAll.size() > args.k) gtAll.resize(args.k);
                    for (auto& [d, id] : gtAll) gt.push_back(id);
                    gtComputed.emplace_back(eligibleCount, gt);
                }
                const uint64_t gtDist = oracleSpace.calls - gtD0;
                std::unordered_set<size_t> gtSet(gt.begin(), gt.end());
                // Validation: legality, duplicates, state.
                size_t invalid = 0, dup = 0;
                std::unordered_set<size_t> seenIds;
                for (auto& [d, id] : result) {
                    if (!canon.matches(id, q)) ++invalid;
                    if (!seenIds.insert(id).second) ++dup;
                }
                size_t stateErrors = invalid + dup;
                size_t hit = 0;
                for (auto& [d, id] : result) if (gtSet.count(id)) ++hit;
                const double recall = gt.empty() ? (result.empty() ? 1.0 : 0.0) : double(hit) / gt.size();
                // Route coverage: fraction of GT located in selected blocks.
                size_t covered = 0;
                for (size_t gid : gt) {
                    const auto lc = index.loc[gid];
                    for (size_t sb : qc.selected_blocks) if (sb == lc.block) { ++covered; break; }
                }
                const double coverage = gt.empty() ? 1.0 : double(covered) / gt.size();
                // Conditional local recall: hits among GT that were in selected blocks.
                std::unordered_set<size_t> inSelected;
                for (size_t gid : gt) {
                    const auto lc = index.loc[gid];
                    for (size_t sb : qc.selected_blocks) if (sb == lc.block) inSelected.insert(gid);
                }
                size_t hitInSelected = 0;
                for (auto& [d, id] : result) if (inSelected.count(id)) ++hitInSelected;
                const double condLocal = inSelected.empty() ? -1.0
                    : double(hitInSelected) / inSelected.size();
                // Exact-reference coverage (signed delta vs online).
                size_t refCovered = 0;
                for (size_t gid : gt) {
                    const auto lc = index.loc[gid];
                    for (size_t sb : qc.reference_blocks) if (sb == lc.block) { ++refCovered; break; }
                }
                const double refCoverage = gt.empty() ? 1.0 : double(refCovered) / gt.size();
                const double covDelta = coverage - refCoverage;  // signed
                auto idsStr = [](const std::vector<size_t>& v) {
                    std::string s;
                    for (size_t x : v) { if (!s.empty()) s += ';'; s += std::to_string(x); }
                    return s;
                };
                std::vector<size_t> resultIds;
                for (auto& [d, id] : result) resultIds.push_back(id);
                qout << current << ',' << version << ',' << queryIndex << ',' << q.qidx << ','
                     << q.recursive << ',' << q.anchor << ',' << gt.size() << ',' << hit << ','
                     << recall << ',' << invalid << ',' << dup << ',' << stateErrors << ','
                     << e2e << ',' << qc.portal_distances << ',' << qc.local_distances << ','
                     << qc.masks_prepared << ',' << qc.mask_work.owner_rank_queries << ','
                     << qc.mask_work.owner_cache_hits << ',' << qc.mask_work.posting_slots_read << ','
                     << qc.mask_work.bitset_word_ops << ',' << qc.portal_returned << ','
                     << qc.blocks_selected << ',' << qc.local_searches << ',' << coverage << ','
                     << condLocal << ',' << refCoverage << ',' << covDelta << ',' << gtDist << ','
                     << idsStr(gt) << ',' << idsStr(resultIds) << ',' << idsStr(qc.reference_blocks) << '\n';
                qout.flush();
                ++queryIndex;
                queryTimes.push_back(e2e);
                recalls.push_back(recall);
                totalGt += gt.size(); totalHit += hit;
                totalInvalid += invalid; totalDup += dup; totalState += stateErrors;
                if (stateErrors) die("query state violation: invalid=" + std::to_string(invalid) +
                                     " duplicate=" + std::to_string(dup));
                continue;
            }
            // updates
            const auto t0 = Clock::now();
            wqr::MaintenanceCounters mc;
            const uint64_t d0 = index.build_space->calls;
            if (op == 'V') {
                size_t id; int64_t cat, num;
                if (!(in >> id >> cat >> num)) die("malformed V");
                if (id >= items.size()) die("V id out of range");
                index.apply_V(id, cat, num);
            } else if (op == 'T') {
                size_t id; uint64_t tags;
                if (!(in >> id >> tags)) die("malformed T");
                if (id >= items.size()) die("T id out of range");
                index.apply_T(id, tags);
            } else if (op == 'M') {
                size_t id, dst;
                if (!(in >> id >> dst)) die("malformed M");
                if (id >= items.size() || dst >= nodes) die("M out of range");
                index.apply_M(id, dst);
            } else if (op == 'B') {
                size_t src, dst;
                if (!(in >> src >> dst)) die("malformed B");
                if (src >= nodes || dst >= nodes) die("B out of range");
                index.apply_B(src, dst, mc);
            } else if (op == 'R') {
                size_t src, dst;
                if (!(in >> src >> dst)) die("malformed R");
                if (src >= nodes || dst >= nodes) die("R out of range");
                index.apply_R(src, dst);
            } else if (op == 'D') {
                size_t id;
                if (!(in >> id)) die("malformed D");
                if (id >= items.size()) die("D id out of range");
                index.apply_D(id, mc);
            } else if (op == 'I') {
                size_t id, node2; int64_t cat, num; uint64_t tags;
                if (!(in >> id >> node2 >> cat >> num >> tags)) die("malformed I");
                if (id >= items.size() || node2 >= nodes) die("I out of range");
                index.apply_I(id, node2, cat, num, tags, mc);
            } else die("unknown event op " + std::string(1, op));
            const double us = elapsedUs(t0);
            ++version;
            mc.distance_calls += index.build_space->calls - d0;
            // graph distances from inserts/rebuilds during this event
            uint64_t gcalls = index.pspace ? index.pspace->calls : 0;
            for (const auto& b : index.blocks) if (b.space) gcalls += b.space->calls;
            uout << current << ',' << version << ',' << op << ',' << us << ','
                 << mc.distance_calls << ',' << mc.ann_insertions << ',' << mc.ann_deletions << ','
                 << mc.rebuilds << ',' << mc.splits << ',' << mc.compactions << ','
                 << mc.registry_compactions << ',' << mc.posting_writes << '\n';
            uout.flush();
        }
        std::string extraLine;
        while (std::getline(trace, extraLine))
            if (extraLine.find_first_not_of(" \t\r") != std::string::npos)
                die("extra trace event after declared count");
        if (!qout || !uout) die("CSV write failure");
        if (!args.gt_cache.empty() && !gtCacheLoaded) {
            std::ofstream gc(args.gt_cache);
            if (!gc) die("cannot write GT cache");
            gc << "WQRGT1 " << gtComputed.size() << "\n";
            for (const auto& [elig, ids] : gtComputed) {
                gc << elig << " " << ids.size();
                for (size_t id : ids) gc << " " << id;
                gc << "\n";
            }
        }
        double recallSum = 0;
        for (double r : recalls) recallSum += r;
        const double macroRecall = recalls.empty() ? 1.0 : recallSum / recalls.size();
        const double pairRecall = totalGt ? double(totalHit) / totalGt : 1.0;
        // Full space accounting: canonical vectors + graph payloads + proxies + bitplanes.
        uint64_t graphBytes = index.pgraph ? index.pgraph->indexFileSize() : 0;
        uint64_t bitplaneBytes = 0, proxyCopyBytes = 0;
        size_t liveBlocks = 0, totalProxies = 0;
        for (const auto& b : index.blocks) {
            if (b.retired) continue;
            ++liveBlocks;
            if (b.graph) graphBytes += b.graph->indexFileSize();
            bitplaneBytes += 3 * 64 * b.words() * 8;            // category+numeric+tag planes
            bitplaneBytes += b.zone_bits.size() * b.words() * 8; // zone masks
            for (const auto& [o, l] : b.owner_posting) bitplaneBytes += l.size() * sizeof(size_t);
            for (const auto& p : b.proxies) proxyCopyBytes += p.size() * sizeof(float);
            totalProxies += b.proxies.size();
        }
        const uint64_t canonicalBytes = index.vectors.size() * sizeof(float);
        struct rusage ru {};
        getrusage(RUSAGE_SELF, &ru);
        sout << "live_blocks " << liveBlocks << "\nlive_proxies " << index.live_proxies
             << "\nproxy_slots_total " << totalProxies
             << "\ngraph_bytes " << graphBytes << "\nbitplane_posting_zone_bytes " << bitplaneBytes
             << "\nproxy_vector_copy_bytes " << proxyCopyBytes
             << "\ncanonical_vector_bytes " << canonicalBytes
             << "\npeak_rss_bytes " << uint64_t(ru.ru_maxrss) * 1024 << "\n";
        sout << "queries " << queryIndex << "\nupdates " << version
             << "\nmacro_recall " << macroRecall << "\npair_recall " << pairRecall
             << "\ntotal_gt " << totalGt << "\ntotal_hit " << totalHit
             << "\ninvalid " << totalInvalid << "\nduplicate " << totalDup
             << "\nstate_errors " << totalState << "\n";
        std::cout << "wqr_research done: queries=" << queryIndex << " updates=" << version
                  << " macro_recall=" << macroRecall << " pair_recall=" << pairRecall << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "wqr_research: " << e.what() << '\n';
        return 1;
    }
}
