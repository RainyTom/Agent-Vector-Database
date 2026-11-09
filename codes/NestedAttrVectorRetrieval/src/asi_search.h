#pragma once
// Query side of the attribute-skeleton index (DQS: one queue, one keep heap).
//
// Entry: one nearest representative per in-scope attribute. Scoring every
// representative of an attribute to pick its nearest one is the entry lookup;
// all chosen entries feed the same single search state.

#include <cstdint>
#include <string>
#include <vector>

#include "asi_index.h"
#include "dataset.h"

struct QueryOptions {
    int ef = 10;                        // keep-heap capacity of the single channel
    int entry_expand_per_attribute = 2;  // force-expand the t nearest stored entries per attribute
    std::string entry_mode = "representative";  // entry lookup: representative | hnsw
    int repeats = 1;
    int max_queries = 0;                // 0 = all queries
    bool rerank_fp32 = false;           // optional final rerank of the kept candidates
    int rerank_pool = 0;                // 0 = rerank the whole keep heap
    std::string trace_path;             // optional per-query JSONL
};

struct QueryStats {
    int ef = 0;
    long long ground_truth_entries = 0;
    long long hits = 0;
    double recall = 0.0;
    double qps = 0.0;
    double seconds = 0.0;
    double distances_per_query = 0.0;
    double distance_cache_hits_per_query = 0.0;
    double expanded_per_query = 0.0;
    double adjacency_slots_per_query = 0.0;
    double representative_scores_per_query = 0.0;
    double eligible_per_query = 0.0;
    double attributes_in_scope_per_query = 0.0;
    double attributes_entered_per_query = 0.0;
    double rerank_calls_per_query = 0.0;
};

QueryStats run_queries(const Dataset& dataset, const AsiIndex& index, const QueryOptions& options);
