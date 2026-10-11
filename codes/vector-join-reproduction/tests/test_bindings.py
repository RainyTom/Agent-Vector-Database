#!/usr/bin/env python3
"""Smoke-test every scheme-specific pybind11 entry point."""

from __future__ import annotations

import vector_join_py as vj


base = vj.Matrix([[0.0, 0.0], [0.1, 0.0], [5.0, 5.0], [5.1, 5.0]])
queries = vj.Matrix([[0.05, 0.0], [5.05, 5.0]])
config = vj.Config()
config.threshold = 0.02
config.graph_m = 4
config.ef_construction = 20
config.convergence_threshold = 20

expected_methods = {
    "run_independent": "independent",
    "run_hard_window_cache": "hard",
    "run_fast_avj_soft": "soft",
    "run_fast_avj_merged": "mi",
    "run_fast_avj_adaptive": "mi-adapt",
}
for runner_name, expected_method in expected_methods.items():
    output = getattr(vj, runner_name)(queries, base, config, 2)
    assert output.metrics.method == expected_method
    assert output.metrics.graph_backend == "hnswlib"
    assert output.metrics.implementation_language == "C++20"
    assert output.metrics.distance_metric == "squared_l2"
    assert output.metrics.graph_m == config.graph_m
    assert output.metrics.ef_construction == config.ef_construction
    assert output.metrics.seed == config.seed
    assert output.metrics.precision == 1.0
    assert len(output.queries) == 2

print("test_bindings: OK")
