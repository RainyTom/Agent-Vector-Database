"""Small, independent regression checks for the reproduction audit findings."""

import json
import pathlib
import resource
import struct
import tempfile

import vector_join_py as vj


with tempfile.TemporaryDirectory(prefix="vj-regression-") as directory:
    root = pathlib.Path(directory)
    base = root / "base.f32"
    copy = root / "copy.f32"
    data = struct.pack("4f", 0, 1, 2, 3)
    base.write_bytes(data)
    copy.write_bytes(data)
    config = vj.SimJoinConfig()
    config.epsilon = 0.1
    config.graph_m = 2
    config.ef_construction = 10
    config.window_size = 4
    for query in (base, copy):
        result = vj.run_simjoin_files(str(query), str(base), 1, config, 1, 4, 1)
        metrics = json.loads(result.metrics.to_json())
        assert metrics["query_rows"] == 1
        assert metrics["base_rows"] == 4
        assert metrics["result_pairs"] == 1
        assert metrics["paper_formula_nselectivity"] == 0.25
        assert metrics["true_mean_results_per_query"] == 1
        assert metrics["total_online_ms"] >= metrics["join_ms"]
        assert metrics["total_online_ndc"] >= metrics["distance_computations"]
        assert metrics["build_type"]
        assert "compile_flags" in metrics
    metrics = json.loads(vj.run_simjoin_files(str(copy), str(base), 1, config).metrics.to_json())
    assert metrics["quality_status"] == "not-evaluated"
    assert metrics["average_recall"] is None
    assert metrics["average_precision"] is None
    assert metrics["paper_formula_nselectivity"] is None
    metrics = json.loads(vj.run_simjoin_files(str(copy), str(base), 1, config, 0, 0, 1).metrics.to_json())
    assert metrics["quality_status"] == "prefix-sample"
    assert metrics["paper_formula_nselectivity"] is None

    graph = root / "graph.txt"
    graph.write_text("0 1 2 3\n1 0 2 3\n2 0 1 3\n3 0 1 2\n")
    source = 'URL/commit; flags="-O3"; Unicode=来源\nbackslash=\\'
    result = vj.run_simjoin_graph_files(str(copy), str(base), 1, str(graph), str(graph),
                                       0, 0, 'test "graph"', source, config, 0, 0, 4)
    assert json.loads(result.metrics.to_json())["graph_provenance"] == source
    graph.write_text("0 1 bad-token\n1 0\n2\n3\n")
    try:
        vj.run_simjoin_graph_files(str(copy), str(base), 1, str(graph), str(graph),
                                  0, 0, "test", "test", config)
    except RuntimeError:
        pass
    else:
        raise AssertionError("malformed graph was silently accepted")

soft, hard = resource.getrlimit(resource.RLIMIT_AS)
assert 0 < soft <= hard <= vj.MEMORY_LIMIT_BYTES
print("audit regressions: OK")
