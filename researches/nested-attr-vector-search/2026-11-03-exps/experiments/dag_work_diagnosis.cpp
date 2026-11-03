// Direct-only diagnosis, fixed cells. Normal timing calls real implementations;
// native upper/level-0 instrumentation runs exclusively in the untimed pass.
#define DAG_COMPOSITION_LIBRARY
#include "../../2026-11-02-exps/experiments/dag_composition_bench.cpp"
#if DAG_VALIDATION_FAMILY == 0
#include "owner_block_search.h"
#else
#include "native_work_probe.h"
#endif
struct ExtraWork {
    size_t upper_expansions = 0, lower_expansions = 0;
    size_t upper_adjacency = 0, lower_adjacency = 0;
    size_t distinct = 0, block_checks = 0, skipped_entries = 0;
};
int main(int argc, char** argv) try {
    require(argc == 5, "usage: dag_work_diagnosis BASE QUERY DAG OUT");
    constexpr size_t nq = 120, passes = 16, rounds = 10;
    const std::string out = argv[4];
    Workload w = load_workload(argv[3]);
    Vectors base = load_vectors(argv[1], w.owner.size()), vectors = load_vectors(argv[2], nq);
    RawL2Space l2(base.dim);
    auto graph = build_variant(base, w, l2, Knobs{});
    for (const auto& g : graph.local) if (g)
        require(g->M_ == 64 && g->ef_construction_ == 200, "wrong HNSW parameters");
#if DAG_VALIDATION_FAMILY == 0
    const auto begin = Clock::now();
    auto blocks = make_owner_blocks(graph, w);
    const double block_build_us = us(begin);
    const auto& entries = query_entries(graph, false);
    const std::vector<std::string> names = {"dag_current", "dag_owner_blocks"};
    std::ofstream memory(out + "/representation.csv");
    memory << "items,edges,blocks,block_size_bytes,row_offset_bytes,packed_id_bytes,block_header_bytes,build_us\n"
           << base.n << ',' << blocks.ids.size() << ',' << blocks.blocks.size() << ',' << sizeof(OwnerBlocks::Block)
           << ',' << blocks.row.size()*sizeof(size_t) << ',' << blocks.ids.size()*sizeof(Id) << ','
           << blocks.blocks.size()*sizeof(OwnerBlocks::Block) << ',' << block_build_us << '\n';
#else
    const std::vector<std::string> names = {"nodeann_nk"};
#endif
    std::vector<Query> queries(nq);
    std::vector<size_t> groups(nq);
    std::array<std::vector<size_t>,3> subsets;
    for (size_t i = 0; i < nq; ++i) {
        auto& q = queries[i];
        q = resolve_scope(w, w.scopes[i*(w.scopes.size()-1)/(nq-1)], false);
        for (Id c = 0; c < q.allowed.size(); ++c) groups[i] += q.allowed[c] && !w.members[c].empty();
        std::priority_queue<Pair> truth;
        for (Id u = 0; u < base.n; ++u) if (q.allowed[w.owner[u]]) {
            ++q.count; truth.emplace(l2.dist(vectors.at(i), base.at(u)),u);
            if (truth.size() > 100) truth.pop();
        }
        while (!truth.empty()) { q.truth.push_back(truth.top().second); truth.pop(); }
        if (!q.count) continue;
        subsets[0].push_back(i); subsets[groups[i] == 1 ? 1 : 2].push_back(i);
    }
    std::vector<Scratch> spaces;
    for (size_t a = 0; a < names.size(); ++a) spaces.emplace_back(base.n);
    auto execute = [&](auto counted, size_t a, size_t i, size_t k, size_t ef, SearchStats& stats, ExtraWork& extra) {
        constexpr bool Count = decltype(counted)::value;
        const auto& q = queries[i];
        if (!q.count) return std::vector<Id>{};
#if DAG_VALIDATION_FAMILY == 0
        std::vector<Id> result;
        if (!a) result = search_counted<Count>(k, graph, base, l2, vectors.at(i), q.roots,
            std::min(ef,q.count), spaces[a], [&](Id u) { return bool(q.allowed[w.owner[u]]); }, stats, &entries);
        else {
            BlockWork b;
            result = search_owner_blocks<Count>(k, blocks, entries, base, w, l2, vectors.at(i), q,
                std::min(ef,q.count), spaces[a], stats, b);
            extra.block_checks = b.tested; extra.skipped_entries = b.skipped_entries;
        }
        if constexpr (Count) {
            extra.distinct = stats.distances;
            extra.lower_expansions = stats.expansions;
            extra.lower_adjacency = stats.adjacency_entries;
        }
        return result;
#else
        if constexpr (!Count) return search_nodeann_topn<false>(graph,w,q.allowed,vectors.at(i),k,k,ef,stats);
        else {
            // This is a measurement adapter, never a timed path. Use original
            // native pairs for the same external-ID top-k merge; compare the
            // instrumented native pop order for every searched local graph.
            std::priority_queue<Pair> top;
            NativeWork work;
            for (Id attr = 0; attr < q.allowed.size(); ++attr) {
                ++stats.owner_checks;
                if (!q.allowed[attr] || !graph.local[attr]) continue;
                auto& g = *graph.local[attr]; g.setEf(ef);
                auto pairs = g.searchKnn(vectors.at(i),k);
                const auto original_ids = labels(pairs);
                const auto observed = native_work_search(g,w,q.allowed,vectors.at(i),k,ef,work);
                require(observed == original_ids, "native work instrumentation changed ordered IDs");
                while (!pairs.empty()) {
                    top.emplace(pairs.top().first,Id(pairs.top().second)); pairs.pop();
                    if (top.size() > k) top.pop();
                }
            }
            SearchStats existing;
            const auto old_counted = search_nodeann_topn<true>(graph,w,q.allowed,vectors.at(i),k,k,ef,existing);
            std::vector<Id> result;
            while (!top.empty()) { result.push_back(top.top().second); top.pop(); }
            require(result == old_counted && work.distances == existing.distances &&
                    work.rejected_distances == existing.rejected_distances,
                    "native measured work differs from existing distance hook");
            stats.distances = work.distances; stats.rejected_distances = work.rejected_distances;
            stats.expansions = work.upper_expansions + work.lower_expansions;
            stats.adjacency_entries = work.upper_adjacency + work.lower_adjacency;
            extra.upper_expansions = work.upper_expansions; extra.lower_expansions = work.lower_expansions;
            extra.upper_adjacency = work.upper_adjacency; extra.lower_adjacency = work.lower_adjacency;
            extra.distinct = work.distinct;
            return result;
        }
#endif
    };
    std::ofstream rows(out+"/queries.csv"), samples(out+"/batches.csv"), summary(out+"/summary.csv");
    require(bool(rows)&&bool(samples)&&bool(summary),"cannot write diagnosis");
    rows << "arm,k,ef,query,groups,eligible,hits,truth,returned,distances,distinct_distance_items,expansions,adjacency,upper_expansions,lower_expansions,upper_adjacency,lower_adjacency,item_checks,owner_checks,block_checks,skipped_entries,rejected_distances\n";
    samples << "arm,k,ef,subset,queries,passes,sample,batch_us,checksum\n" << std::setprecision(15);
    summary << "arm,k,ef,subset,queries,us_per_query,qps,recall,distances,distinct_distance_items,expansions,adjacency,upper_expansions,lower_expansions,upper_adjacency,lower_adjacency,item_checks,owner_checks,skipped_entries\n" << std::setprecision(15);
    const char* labels[] = {"all","one_owner","many_owners"};
    std::mt19937 rng(2026110301);
    for (const auto& cell : {std::pair<size_t,size_t>{10,32},{100,128}}) {
        const auto k = cell.first, ef = cell.second;
        std::vector<std::vector<std::vector<Id>>> expected(names.size(),std::vector<std::vector<Id>>(nq));
        std::vector<std::vector<SearchStats>> work(names.size(),std::vector<SearchStats>(nq));
        std::vector<std::vector<ExtraWork>> extra(names.size(),std::vector<ExtraWork>(nq));
        std::vector<std::vector<size_t>> hits(names.size(),std::vector<size_t>(nq));
        for (size_t a = 0; a < names.size(); ++a) for (size_t i = 0; i < nq; ++i) {
            SearchStats unused; ExtraWork none;
            auto normal = execute(std::false_type{},a,i,k,ef,unused,none);
            const auto counted = execute(std::true_type{},a,i,k,ef,work[a][i],extra[a][i]);
            require(normal == counted,"normal/count mismatch");
            const auto& s = work[a][i]; const auto& x = extra[a][i];
            require(s.rejected_distances == 0,"unqualified distance");
#if DAG_VALIDATION_FAMILY == 0
            if (a) {
                require(normal == expected[0][i] && s.distances == work[0][i].distances &&
                        s.expansions == work[0][i].expansions &&
                        s.adjacency_entries+x.skipped_entries == work[0][i].adjacency_entries,
                        "owner segmentation changed work or results");
            }
#endif
            hits[a][i] = checked_hits(k,normal,queries[i],w,base,l2,vectors.at(i));
            rows << names[a] << ',' << k << ',' << ef << ',' << i << ',' << groups[i] << ',' << queries[i].count
                 << ',' << hits[a][i] << ',' << std::min(k,queries[i].count) << ',' << normal.size()
                 << ',' << s.distances << ',' << x.distinct << ',' << s.expansions << ',' << s.adjacency_entries
                 << ',' << x.upper_expansions << ',' << x.lower_expansions << ',' << x.upper_adjacency << ',' << x.lower_adjacency
                 << ',' << s.item_checks << ',' << s.owner_checks << ',' << x.block_checks << ',' << x.skipped_entries
                 << ',' << s.rejected_distances << '\n';
            expected[a][i] = std::move(normal);
        }
        for (size_t group = 0; group < subsets.size(); ++group) {
            const auto& subset = subsets[group];
            std::vector<uint64_t> hashes(names.size(),UINT64_C(1469598103934665603));
            for (size_t a = 0; a < names.size(); ++a) for (size_t i : subset)
                hashes[a] = result_checksum(hashes[a],i,expected[a][i]);
            auto batch = [&](size_t a,size_t n) {
                uint64_t consume = 0;
                for (size_t pass = 0; pass < n; ++pass) {
                    uint64_t hash = UINT64_C(1469598103934665603);
                    for (size_t i : subset) {
                        SearchStats unused; ExtraWork none;
                        hash = result_checksum(hash,i,execute(std::false_type{},a,i,k,ef,unused,none));
                    }
                    require(hash == hashes[a],"timed batch differs from audit"); consume ^= hash+pass;
                }
                return consume;
            };
            for (size_t a = 0; a < names.size(); ++a) (void)batch(a,1);
            std::vector<std::array<double,rounds>> timings(names.size());
            std::vector<size_t> order(names.size()); std::iota(order.begin(),order.end(),0);
            for (size_t round = 0; round < rounds; ++round) {
                std::shuffle(order.begin(),order.end(),rng);
                for (size_t a : order) {
                    const auto begin = Clock::now(); const auto checksum = batch(a,passes); const auto elapsed = us(begin);
                    timings[a][round] = elapsed;
                    samples << names[a] << ',' << k << ',' << ef << ',' << labels[group] << ',' << subset.size()
                            << ',' << passes << ',' << round << ',' << elapsed << ',' << checksum << '\n';
                }
            }
            for (size_t a = 0; a < names.size(); ++a) {
                auto timing = timings[a]; std::sort(timing.begin(),timing.end());
                const double time = timing[rounds/2]/passes/subset.size();
                std::array<double,12> sum{};
                for (size_t i : subset) {
                    const auto& s=work[a][i]; const auto& x=extra[a][i];
                    const double values[] = {double(hits[a][i])/std::min(k,queries[i].count),double(s.distances),double(x.distinct),
                        double(s.expansions),double(s.adjacency_entries),double(x.upper_expansions),double(x.lower_expansions),
                        double(x.upper_adjacency),double(x.lower_adjacency),double(s.item_checks),double(s.owner_checks),double(x.skipped_entries)};
                    for (size_t j=0;j<sum.size();++j) sum[j]+=values[j];
                }
                summary << names[a] << ',' << k << ',' << ef << ',' << labels[group] << ',' << subset.size()
                        << ',' << time << ',' << 1e6/time;
                for (double value : sum) summary << ',' << value/subset.size();
                summary << '\n';
                std::cout << names[a] << " k=" << k << " ef=" << ef << " group=" << labels[group]
                          << " us=" << time << " D=" << sum[1]/subset.size() << " unique=" << sum[2]/subset.size()
                          << " expand=" << sum[3]/subset.size() << " adjacency=" << sum[4]/subset.size()
                          << " R=" << sum[0]/subset.size() << std::endl;
            }
        }
    }
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
