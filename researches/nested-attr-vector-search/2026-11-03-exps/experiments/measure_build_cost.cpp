// Static build cost only: the current index has no update entry point.
#define DAG_COMPOSITION_LIBRARY
#include "../../2026-11-02-exps/experiments/dag_composition_bench.cpp"

int main(int argc, char** argv) try {
    require(argc == 4, "usage: measure_build_cost BASE QUERY_OR_DAG DAG OUT");
    Workload w = load_workload(argv[2]);
    Vectors base = load_vectors(argv[1], w.owner.size());
    RawL2Space l2(base.dim);
    const auto begin = Clock::now();
    auto graph = build_variant(base, w, l2, Knobs{});
    const double build = us(begin);
    size_t edges = 0, max_row = 0, nonempty = 0, portals = 0;
    for (const auto& row : graph.edges) { edges += row.size(); max_row = std::max(max_row, row.size()); }
    for (const auto& g : graph.local) if (g) ++nonempty;
    for (const auto& p : graph.portals) portals += p.size();
    std::ofstream out(argv[3]);
    require(bool(out), "cannot write evidence");
    out << std::setprecision(15);
    out << "items,attributes,edges,nonempty_local,portals,total_directed_edges,max_row,"
           "local_edges,connector_edges,jump_edges,build_ms\n";
    out << base.n << ',' << w.members.size() << ',' << w.owner.size() << ',' << nonempty << ',' << portals
        << ',' << edges << ',' << max_row << ',' << graph.local_edges << ',' << graph.connector_edges
        << ',' << graph.jump_edges << ',' << build / 1000.0 << '\n';
    std::cout << "build_ms=" << build / 1000.0 << " edges=" << edges << " portals=" << portals
              << " local=" << graph.local_edges << " connector=" << graph.connector_edges
              << " jump=" << graph.jump_edges << std::endl;
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
