#pragma once
// OraclePartition search: expand each query's attribute set to its OR-descendant
// closure, search every in-scope attribute graph, merge the candidates and keep
// the k nearest objects.

#include <vector>

#include "dataset.h"
#include "nodeann_index.h"

struct SearchStats {
    int per_n = 0;
    int ef = 0;
    double recall = 0.0;
    double qps = 0.0;
    double dists_per_query = 0.0;
    double graph_searches_per_query = 0.0;
    double eligible_per_query = 0.0;
};

// Attributes in the OR-descendant closure of `query_attributes` that own at
// least one object, sorted ascending for a reproducible traversal order.
std::vector<int> expand_attribute_closure(const Dataset& dataset,
                                          const std::vector<int>& query_attributes);

// Runs every query `repeat` times; recall is measured on the first pass, qps on
// the fastest pass, distance calls and graph searches over all passes.
SearchStats run_search(const Dataset& dataset, OraclePartitionIndex& index, int per_n, int ef, int repeat,
                       int query_limit);