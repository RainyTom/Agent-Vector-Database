# BriskFlow integration notes

Audited upstream: `DataSysResearch/BriskFlow` at commit `89d4aa5b387fc603346a7ce3f349fc9cc393ce26`.

## What can be reused

- `JoinOperator` and `JoinMethodRegistry` provide the execution/runtime registration path.
- `HNSWJoinMethod` is the natural independent-query baseline.
- `JoinMetricsCollector` can carry latency, throughput and result counters.
- Window state and streaming operators are useful for a later incremental vector-join study.

## Missing contract for the two papers

The current `BaseMethod::ExecuteEager` accepts one arriving record. SimJoin and Fast AVJ require a known query batch, a query-side graph/MST order, and parent-query cache state. Adding them directly as another eager method would silently remove the main contribution.

A faithful adapter therefore needs a batch lifecycle:

```cpp
prepareBatch(query_span);       // query graph, MST/forest, optional merged graph
executeOrderedBatch();          // hard/soft parent cache
finalizeBatch();                // metrics and cache lifetime
```

For streaming use, a micro-batch operator can close a window, invoke these methods, then publish results. The cache must be scoped to one batch/window and invalidated when the data-side index version changes.

## Recommended integration order

1. Wrap this reproduction library behind a BriskFlow batch operator without changing its algorithms.
2. Map `independent`, `hard`, `soft`, `mi`, and `mi-adapt` into configuration values.
3. Add graph/index version IDs to prevent reuse across stale window state.
4. Report index build and batch-order time separately from online join time.
5. Only then study incremental merged-index maintenance; it is outside the two papers' reproduced core.
