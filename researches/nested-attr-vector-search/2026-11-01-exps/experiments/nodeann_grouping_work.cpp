// Controlled grouping-only comparison on data/dag-worked-example.json.
// Both arms use the same DAG run_query/search_graph, not the legacy NodeANN CLI.
// Query work is counted; no elapsed-time comparison is made.
#define main dag_scope_ann_cli_main
#include "../../../../codes/directory-aware-vector-attr-search/src/dag_scope_ann.cpp"
#undef main
#include <stdexcept>
#include <unordered_map>

namespace {
void require(bool ok, const char* reason) {
    if (!ok) throw std::runtime_error(reason);
}

class CountedL2 final : public RawL2Space {
public:
    CountedL2() : RawL2Space(2) {}
    uint64_t calls = 0, ineligible_calls = 0, target_ids = 0;
    bool measuring = false;
    const Bitmap* eligible = nullptr;
    std::unordered_map<const void*, Id> object_at;

    static float counted(const void* a, const void* b, const void* context) {
        auto& self = *const_cast<CountedL2*>(static_cast<const CountedL2*>(context));
        if (self.measuring) {
            const auto found = self.object_at.find(b);
            require(found != self.object_at.end(), "unknown query distance target");
            ++self.calls;
            self.target_ids |= UINT64_C(1) << found->second;
            self.ineligible_calls += !self.eligible->test(found->second);
        }
        return self.dist(static_cast<const float*>(a), static_cast<const float*>(b));
    }
    hnswlib::DISTFUNC<float> get_dist_func() override { return counted; }
    void* get_dist_func_param() override { return this; }
};

void write_ids(std::ostream& out, uint64_t ids) {
    out << '[';
    bool first = true;
    for (Id i = 0; i < 8; ++i) {
        if (!(ids & (UINT64_C(1) << i))) continue;
        if (!first) out << ',';
        out << i;
        first = false;
    }
    out << ']';
}

std::string measure(const Graph& graph, const Workload& work, const Vectors& base,
                    const std::vector<Id>& group_of, size_t groups) {
    CountedL2 space;
    // The global-arm parameter is unused when the local graph array is present.
    hnswlib::HierarchicalNSW<float> global(&space, 1, 16, 200);
    std::vector<std::unique_ptr<hnswlib::HierarchicalNSW<float>>> parts;
    for (size_t g = 0; g < groups; ++g) {
        const size_t size = std::count(group_of.begin(), group_of.end(), g);
        require(size != 0, "fixture requires nonempty groups");
        parts.push_back(std::make_unique<hnswlib::HierarchicalNSW<float>>(&space, size, 16, 200));
    }
    for (Id i = 0; i < 8; ++i) {
        parts[group_of[i]]->addPoint(base.data.data() + 2 * i, i);
    }
    for (auto& part : parts)
        for (size_t local = 0; local < part->cur_element_count; ++local)
            space.object_at.emplace(part->getDataByInternalId(local),
                                    static_cast<Id>(part->getExternalLabel(local)));

    const Bitmap reference = reference_scope({2}, graph, work);
    require(reference.count() == 4, "unexpected B scope");
    space.eligible = &reference;
    const float query[] = {1, 1};
    space.measuring = true;
    const auto result = run_query({2}, query, graph, work, base, space, nullptr,
                                  2, 20, 0, global, group_of, &parts);
    space.measuring = false;
    require(result.path[0] == 'g', "scan path must be disabled");
    require(result.scope.words == reference.words, "scope mismatch");
    require(result.neighbors == std::vector<std::pair<float, Id>>{{1, 1}, {2, 3}},
            "unexpected top-2");

    std::ostringstream out;
    out << "{\"groups\":" << groups << ",\"queried_graphs\":" << result.touched
        << ",\"distance_evaluations\":" << space.calls
        << ",\"ineligible_distance_evaluations\":" << space.ineligible_calls
        << ",\"unique_distance_targets\":" << __builtin_popcountll(space.target_ids)
        << ",\"distance_target_ids\":";
    write_ids(out, space.target_ids);
    out << ",\"returned_ids\":[1,3],\"group_of_object\":[";
    for (size_t i = 0; i < group_of.size(); ++i) out << (i ? "," : "") << group_of[i];
    out << "]}";
    return out.str();
}
} // namespace

int main(int argc, char** argv) try {
    require(argc == 3 && std::string(argv[1]) == "--out", "usage: nodeann_grouping_work --out FILE");
    Graph graph;
    graph.parents.resize(7);
    graph.children.resize(7);
    graph.ancestors.resize(7);
    graph.descendants.resize(7);
    for (const auto [parent, child] : std::vector<std::pair<Id, Id>>{
             {0, 1}, {0, 2}, {0, 6}, {1, 3}, {1, 4}, {2, 3}, {2, 5}}) {
        graph.children[parent].push_back(child);
        graph.parents[child].push_back(parent);
    }
    for (Id c = 0; c < 7; ++c) {
        graph.ancestors[c] = closure(c, graph.parents);
        graph.descendants[c] = closure(c, graph.children);
    }
    Workload work;
    work.objects = 8;
    work.terminal = {1, 3, 4, 3, 2, 5, 6, 0};
    work.postings.resize(7);
    for (Id i = 0; i < 8; ++i) work.postings[work.terminal[i]].push_back(i);
    Vectors base;
    base.dim = 2;
    base.data = {0, 0, 1, 0, 2, 0, 0, 2, 3, 0, 0, 3, 4, 4, 5, 5};
    const auto direct = measure(graph, work, base, work.terminal, 7);
    const auto merged = measure(graph, work, base, {1, 1, 1, 1, 2, 2, 3, 0}, 4);
    std::ofstream out(argv[2]);
    require(bool(out), "cannot open output");
    out << "{\n\"kind\":\"eight_object_grouping_work_not_full_nodeann_benchmark\",\n"
        << "\"fixture\":\"dag-worked-example.json\",\n"
        << "\"query\":{\"concept\":\"B\",\"vector\":[1,1],\"k\":2,\"ef\":20,\"tau\":0},\n"
        << "\"nodeann_style_direct_groups\":" << direct << ",\n"
        << "\"merged_groups\":" << merged << ",\n"
        << "\"distance_count_scope\":\"query HNSW distance function invocations, including wrapper/entry and base-layer calls; repeats counted; builds and oracle excluded\",\n"
        << "\"controls\":\"same production run_query/search_graph/merge, DAG scope, raw FP32 squared-L2, M16, efConstruction200, insertion order; no materialization or scan\",\n"
        << "\"limits\":[\"Fixed8-object2D fixture only\",\"Both arms share the DAG preselection wrapper to isolate grouping; the legacy pure NodeANN CLI is not run\",\"No wall-time or end-to-end performance comparison\",\"No dynamic update cost measured; no general recall equality claimed\"]\n}\n";
    require(bool(out), "write failed");
} catch (const std::exception& error) {
    std::cerr << "nodeann_grouping_work: " << error.what() << '\n';
    return 1;
}
