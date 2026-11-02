// Same executor, three representations, plus the native reference, on the
// single-attribute query subset. Decomposes the measured gap into
// cross-owner edges, representation scale, and executor difference.
#define DAG_VALIDATION_FAMILY 0
#define DAG_COMPOSITION_LIBRARY
#include "dag_composition_bench.cpp"

static constexpr size_t kPasses = 16, kRounds = 10;

struct InducedCase {
    size_t qi = 0, ef = 0;
    Id dir = 0;
    Merged compact;                                // compact local ids
    Vectors comp;                                  // compact vectors, local order
    std::vector<std::vector<Id>> centries;
    std::vector<Id> sorted;                        // local id -> global id
    std::unique_ptr<Scratch> sc_compact;
    Graph* local = nullptr;
};

// a = 0 merged (global), 1 pruned (global), 2 compact, 3 native.
template<bool Count>
static std::vector<Id> run_arm(InducedCase& cs, int a, const Vectors& base, const Vectors& queries,
                               const Workload& w, RawL2Space& l2, const Query& q,
                               const std::vector<std::vector<Id>>& entry, const Merged& graph,
                               Scratch& global_scratch, SearchStats& st) {
    auto qualifies = [&](Id u) { return bool(q.allowed[w.owner[u]]); };
    switch (a) {
        case 0: return search_counted<Count>(10, graph, base, l2, queries.at(cs.qi), q.roots, cs.ef,
                    global_scratch, qualifies, st, &entry);
        case 1: return search_counted<Count>(10, cs.compact, cs.comp, l2, queries.at(cs.qi), q.roots,
                    cs.ef, *cs.sc_compact, [](Id) { return true; }, st, &cs.centries);
        default:
            cs.local->setEf(cs.ef);
            if constexpr (Count) {
                NativeCounter counter(*cs.local, w, q.allowed);
                auto ids = labels(cs.local->searchKnn(queries.at(cs.qi), 10));
                st.distances = counter.distances;
                return ids;
            } else {
                return labels(cs.local->searchKnn(queries.at(cs.qi), 10));
            }
    }
}

int main(int argc, char** argv) try {
    require(argc >= 5, "usage: dag_induced_probe BASE.fvecs QUERY.fvecs DAG_DIR OUT.csv [NQ]");
    const size_t NQ = argc > 5 ? size_t(std::stoul(argv[5])) : 120;
    Workload w = load_workload(argv[3]);
    Vectors base = load_vectors(argv[1], w.owner.size());
    Vectors queries = load_vectors(argv[2], NQ);
    RawL2Space l2(base.dim);
    auto graph = build_variant(base, w, l2, Knobs{});
    const auto& entry = query_entries(graph, true);

    std::vector<Query> workload(NQ);
    std::vector<InducedCase> cases;
    for (size_t i = 0; i < NQ; ++i) {
        const auto& named = w.scopes[i * (w.scopes.size() - 1) / (NQ - 1)];
        workload[i] = resolve_scope(w, named, true);
        Query& q = workload[i];
        std::vector<Id> owners;
        for (Id c = 0; c < q.allowed.size(); ++c)
            if (q.allowed[c] && !w.members[c].empty()) owners.push_back(c);
        if (owners.size() != 1) continue;
        InducedCase cs;
        cs.qi = i;
        cs.dir = owners.front();
        cs.ef = std::min<size_t>(32, w.members[cs.dir].size());
        const auto& S = w.members[cs.dir];
        cs.sc_compact = std::make_unique<Scratch>(S.size());
        cs.local = graph.local[cs.dir].get();
        // (C) the same graph in compact local ids with compact vectors. Local
        // ids follow global-id order, so pair tie-breaking is unchanged and the
        // traversal stays bit-identical to (A)/(B).
        cs.sorted = S;
        std::sort(cs.sorted.begin(), cs.sorted.end());
        cs.comp.dim = base.dim;
        cs.comp.n = cs.sorted.size();
        cs.comp.x.resize(cs.sorted.size() * base.dim);
        std::unordered_map<Id, Id> loc;
        loc.reserve(cs.sorted.size() * 2);
        for (size_t k = 0; k < cs.sorted.size(); ++k) {
            std::copy(base.at(cs.sorted[k]), base.at(cs.sorted[k]) + base.dim,
                      cs.comp.x.begin() + k * base.dim);
            loc.emplace(cs.sorted[k], Id(k));
        }
        cs.compact.edges.resize(cs.sorted.size());
        for (size_t k = 0; k < cs.sorted.size(); ++k) {
            const auto& row = graph.edges[cs.sorted[k]];
            cs.compact.edges[k].reserve(row.size());
            for (Id v : row) {
                if (!q.allowed[w.owner[v]]) continue;
                auto it = loc.find(v);
                require(it != loc.end(), "qualified target outside the single attribute");
                cs.compact.edges[k].push_back(it->second);
            }
        }
        cs.centries.resize(w.members.size());
        for (Id c : q.roots)
            for (Id u : entry[c]) {
                auto it = loc.find(u);
                require(it != loc.end(), "entry outside the single attribute");
                cs.centries[c].push_back(it->second);
            }
        // Exact top-10 truth for this single-attribute scope.
        std::priority_queue<Pair> top;
        for (Id u = 0; u < base.n; ++u) if (q.allowed[w.owner[u]]) {
            ++q.count;
            top.emplace(l2.dist(queries.at(i), base.at(u)), u);
            if (top.size() > 10) top.pop();
        }
        while (!top.empty()) { q.truth.push_back(top.top().second); top.pop(); }
        cases.push_back(std::move(cs));
    }
    require(!cases.empty(), "no single-attribute query in the sample");

    auto consume = [](const std::vector<Id>& ids) {
        uint64_t h = UINT64_C(1469598103934665603);
        for (Id v : ids) h = (h ^ v) * UINT64_C(1099511628211);
        return h;
    };
    Scratch global_scratch(base.n);
    std::array<double, 3> dist_sum{}, exp_sum{}, hit_sum{};
    for (auto& cs : cases) {
        const Query& q = workload[cs.qi];
        SearchStats s0, s1, s2;
        const auto r0 = run_arm<true>(cs, 0, base, queries, w, l2, q, entry, graph, global_scratch, s0);
        const auto r2 = run_arm<true>(cs, 1, base, queries, w, l2, q, entry, graph, global_scratch, s1);
        const auto r3 = run_arm<true>(cs, 2, base, queries, w, l2, q, entry, graph, global_scratch, s2);
        std::vector<Id> r2g;
        r2g.reserve(r2.size());
        for (Id v : r2) r2g.push_back(cs.sorted[v]);
        require(r0 == r2g, "compact representation changed result IDs");
        require(r0.size() == r3.size(), "native returned a different number of results");
        const size_t h[3] = {checked_hits(10, r0, q, w, base, l2, queries.at(cs.qi)),
                             checked_hits(10, r2g, q, w, base, l2, queries.at(cs.qi)),
                             checked_hits(10, r3, q, w, base, l2, queries.at(cs.qi))};
        const SearchStats* sts[3] = {&s0, &s1, &s2};
        for (int a = 0; a < 3; ++a) {
            dist_sum[a] += double(sts[a]->distances);
            exp_sum[a] += double(sts[a]->expansions);
            hit_sum[a] += double(h[a]);
        }
    }

    std::ofstream csv(argv[4]);
    require(bool(csv), "cannot write probe csv");
    csv << "arm,queries,us_per_query,distances_mean,expansions_mean,hits_mean\n" << std::setprecision(12);
    std::array<std::array<double, kRounds>, 3> batches{};
    const char* names[] = {"merged_global", "compact_local", "native_local"};
    for (size_t round = 0; round < kRounds; ++round) {
        for (int step = 0; step < 3; ++step) {
            const int a = int((step + round) % 3);
            const auto begin = Clock::now();
            uint64_t h = 0;
            for (size_t pass = 0; pass < kPasses; ++pass)
                for (auto& cs : cases) {
                    SearchStats unused;
                    h += consume(run_arm<false>(cs, a, base, queries, w, l2, workload[cs.qi], entry,
                                                graph, global_scratch, unused));
                }
            batches[a][round] = ::us(begin);
            require(h != 0, "empty timed pass");
        }
    }
    for (int a = 0; a < 3; ++a) {
        auto sorted = batches[a];
        std::sort(sorted.begin(), sorted.end());
        const double per_query = sorted[kRounds / 2] / kPasses / cases.size();
        csv << names[a] << ',' << cases.size() << ',' << per_query << ','
            << dist_sum[a] / cases.size() << ',' << exp_sum[a] / cases.size() << ','
            << hit_sum[a] / cases.size() << '\n';
        std::cout << "INDUCED arm=" << names[a] << " queries=" << cases.size()
                  << " us=" << per_query << " distances=" << dist_sum[a] / cases.size()
                  << " expansions=" << exp_sum[a] / cases.size()
                  << " hits=" << hit_sum[a] / cases.size() << '\n';
    }
    return 0;
} catch (const std::exception& e) {
    std::cerr << "dag_induced_probe: " << e.what() << '\n';
    return 1;
}
