from collections.abc import Sequence
from typing import Any

MEMORY_LIMIT_BYTES: int


class Matrix:
    def __init__(self, rows: Sequence[Sequence[float]]) -> None: ...
    @staticmethod
    def load_raw(path: str, dimensions: int, max_rows: int = 0) -> Matrix: ...
    rows: int
    dimensions: int


class Config:
    threshold: float
    graph_m: int
    ef_construction: int
    greedy_beam: int
    hybrid_beam: int
    convergence_threshold: int
    ood_ratio_threshold: float
    seed: int


class QueryResult:
    matches: list[int]
    distance_computations: int
    greedy_distance_computations: int
    expansion_distance_computations: int
    hybrid_used: bool


class Metrics:
    method: str
    graph_backend: str
    implementation_language: str
    distance_metric: str
    def to_json(self) -> str: ...


class RunOutput:
    metrics: Metrics
    queries: list[QueryResult]


class SimJoinOrder:
    BASE_MST: SimJoinOrder
    WINDOW_ORDER_SORT: SimJoinOrder
    WINDOW_ORDER_EPSILON: SimJoinOrder


class SimJoinConfig:
    epsilon: float
    window_size: int
    order_search_window: int
    order_repeats: int
    k: int
    order: SimJoinOrder
    self_join: bool
    graph_m: int
    ef_construction: int
    seed: int


class SimJoinQueryResult:
    matches: list[int]
    distance_computations: int


class SimJoinMetrics:
    method: str
    implementation_status: str
    implementation_language: str
    compiler: str
    graph_backend: str
    estimated_peak_bytes: int
    average_recall: float
    average_precision: float
    def to_json(self) -> str: ...


class SimJoinRunOutput:
    metrics: SimJoinMetrics
    queries: list[SimJoinQueryResult]


def run_independent(*args: Any, **kwargs: Any) -> RunOutput: ...
def run_hard_window_cache(*args: Any, **kwargs: Any) -> RunOutput: ...
def run_fast_avj_soft(*args: Any, **kwargs: Any) -> RunOutput: ...
def run_fast_avj_merged(*args: Any, **kwargs: Any) -> RunOutput: ...
def run_fast_avj_adaptive(*args: Any, **kwargs: Any) -> RunOutput: ...
def run_independent_files(*args: Any, **kwargs: Any) -> RunOutput: ...
def run_hard_window_cache_files(*args: Any, **kwargs: Any) -> RunOutput: ...
def run_fast_avj_soft_files(*args: Any, **kwargs: Any) -> RunOutput: ...
def run_fast_avj_merged_files(*args: Any, **kwargs: Any) -> RunOutput: ...
def run_fast_avj_adaptive_files(*args: Any, **kwargs: Any) -> RunOutput: ...
def run_simjoin_files(
    query_path: str,
    base_path: str,
    dimensions: int,
    config: SimJoinConfig,
    query_limit: int = 0,
    base_limit: int = 0,
    ground_truth_limit: int = 0,
) -> SimJoinRunOutput: ...
def run_simjoin_graph_files(
    query_path: str,
    base_path: str,
    dimensions: int,
    query_graph_path: str,
    base_graph_path: str,
    query_entry: int,
    base_entry: int,
    graph_label: str,
    graph_provenance: str,
    config: SimJoinConfig,
    query_limit: int = 0,
    base_limit: int = 0,
    ground_truth_limit: int = 0,
) -> SimJoinRunOutput: ...
