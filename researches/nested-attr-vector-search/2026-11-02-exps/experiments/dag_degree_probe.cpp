// Diagnostic: how much extra adjacency the merged construction adds inside a
// single attribute, and what the native local level-0 graph scans instead.
#define DAG_VALIDATION_FAMILY 0
#define DAG_COMPOSITION_LIBRARY
#include "dag_composition_bench.cpp"

static double pct(std::vector<size_t> v, double q) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return double(v[std::min(v.size() - 1, size_t(q * v.size()))]);
}

int main(int argc, char** argv) try {
    require(argc >= 4, "usage: dag_degree_probe BASE.fvecs DAG_DIR OUT.json [NQ]");
    const size_t NQ = argc > 4 ? size_t(std::stoul(argv[4])) : 120;
    Workload w = load_workload(argv[2]);
    Vectors base = load_vectors(argv[1], w.owner.size());
    RawL2Space l2(base.dim);
    auto graph = build_variant(base, w, l2, Knobs{});
    std::vector<Id> dirs;
    for (size_t i = 0; i < NQ; ++i) {
        const auto& named = w.scopes[i * (w.scopes.size() - 1) / (NQ - 1)];
        const Query q = resolve_scope(w, named, true);
        std::vector<Id> owners;
        for (Id c = 0; c < q.allowed.size(); ++c) if (q.allowed[c] && !w.members[c].empty()) owners.push_back(c);
        if (owners.size() == 1) dirs.push_back(owners.front());
    }
    std::sort(dirs.begin(), dirs.end());
    dirs.erase(std::unique(dirs.begin(), dirs.end()), dirs.end());
    require(!dirs.empty(), "no single-attribute query in the sample");

    std::ofstream out(argv[3]);
    require(bool(out), "cannot write probe output");
    out << "{\n  \"nq\":" << NQ << ",\"single_attribute_dirs\":" << dirs.size()
        << ",\n  \"dirs\": [\n";
    size_t total_items = 0;
    double local_sum = 0, merged_sum = 0, in_attr_sum = 0, cross_sum = 0, local_extra_sum = 0;
    double rep_sum = 0, portal_rows = 0;
    for (size_t di = 0; di < dirs.size(); ++di) {
        const Id c = dirs[di];
        auto& local = *graph.local[c];
        std::vector<std::vector<Id>> local_rows(w.members[c].size());
        for (size_t i = 0; i < w.members[c].size(); ++i) {
            auto* list = local.get_linklist0(i);
            auto* ids = reinterpret_cast<hnswlib::tableint*>(list + 1);
            for (size_t j = 0; j < local.getListCount(list); ++j)
                local_rows[i].push_back(local.getExternalLabel(ids[j]));
            std::sort(local_rows[i].begin(), local_rows[i].end());
        }
        std::vector<size_t> local_deg, merged_deg, in_attr, cross, extra;
        size_t dir_reps = 0;
        double dir_rep_sum = 0;
        for (size_t i = 0; i < w.members[c].size(); ++i) {
            const Id u = w.members[c][i];
            size_t inside = 0, outside = 0, added = 0;
            for (Id v : graph.edges[u]) {
                if (w.owner[v] == c) {
                    ++inside;
                    if (!std::binary_search(local_rows[i].begin(), local_rows[i].end(), v)) ++added;
                } else ++outside;
            }
            local_deg.push_back(local_rows[i].size());
            merged_deg.push_back(graph.edges[u].size());
            in_attr.push_back(inside); cross.push_back(outside); extra.push_back(added);
            if (std::find(graph.portals[c].begin(), graph.portals[c].end(), u) != graph.portals[c].end()) {
                dir_rep_sum += double(graph.edges[u].size()); ++dir_reps;
            }
        }
        auto mean = [](const std::vector<size_t>& v) {
            return v.empty() ? 0.0 : double(std::accumulate(v.begin(), v.end(), size_t(0))) / v.size();
        };
        total_items += w.members[c].size();
        local_sum += mean(local_deg) * w.members[c].size();
        merged_sum += mean(merged_deg) * w.members[c].size();
        in_attr_sum += mean(in_attr) * w.members[c].size();
        cross_sum += mean(cross) * w.members[c].size();
        local_extra_sum += mean(extra) * w.members[c].size();
        rep_sum += dir_rep_sum; portal_rows += dir_reps;
        out << "    {\"dir\":" << c << ",\"items\":" << w.members[c].size()
            << ",\"local_deg_mean\":" << mean(local_deg)
            << ",\"local_deg_p50\":" << pct(local_deg, 0.5)
            << ",\"local_deg_max\":" << pct(local_deg, 0.999)
            << ",\"merged_deg_mean\":" << mean(merged_deg)
            << ",\"merged_in_attr_mean\":" << mean(in_attr)
            << ",\"merged_cross_owner_mean\":" << mean(cross)
            << ",\"in_attr_not_local_mean\":" << mean(extra)
            << ",\"reps\":" << dir_reps
            << ",\"rep_deg_mean\":" << (dir_reps ? dir_rep_sum / dir_reps : 0.0)
            << "}" << (di + 1 < dirs.size() ? "," : "") << "\n";
    }
    out << "  ],\n  \"aggregate\": {\n"
        << "    \"items\":" << total_items << ",\n"
        << "    \"local_deg_mean\":" << (total_items ? local_sum / total_items : 0) << ",\n"
        << "    \"merged_deg_mean\":" << (total_items ? merged_sum / total_items : 0) << ",\n"
        << "    \"merged_in_attr_mean\":" << (total_items ? in_attr_sum / total_items : 0) << ",\n"
        << "    \"merged_cross_owner_mean\":" << (total_items ? cross_sum / total_items : 0) << ",\n"
        << "    \"in_attr_not_local_mean\":" << (total_items ? local_extra_sum / total_items : 0) << ",\n"
        << "    \"scan_inflation_vs_local\":"
        << (local_sum ? merged_sum / local_sum : 0) << "\n"
        << "  }\n}\n";
    std::cout << "DEGREE total_items=" << total_items
              << " local=" << (total_items ? local_sum / total_items : 0)
              << " merged=" << (total_items ? merged_sum / total_items : 0)
              << " in_attr=" << (total_items ? in_attr_sum / total_items : 0)
              << " cross=" << (total_items ? cross_sum / total_items : 0)
              << " extra_in_attr=" << (total_items ? local_extra_sum / total_items : 0)
              << " inflation=" << (local_sum ? merged_sum / local_sum : 0) << std::endl;
    return 0;
} catch (const std::exception& e) {
    std::cerr << "dag_degree_probe: " << e.what() << '\n';
    return 1;
}
