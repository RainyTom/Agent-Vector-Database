# SIEVE原文：复用组件与贡献定位

阅读版本：arXiv2507.11907v2，2025-07-20。原文：https://arxiv.org/html/2507.11907v2 。本轮核§1–3和§4.1开头，不是全篇实验精读。正式PVLDB/DOI为稿件给出的信息，未独立核正式版，暂不新增完整记录。

以下保留§1原文，尤其Our Approach中“This approach isn’t new; what’s new are…”及后续模型/服务机制定位。不得用本文的最高加速宣传预测本项目效果。



Finding *semantically similar* items
satisfying *hard constraints* is a common task.
Moms may search for videos (semantic)
tagged “safe-for-kids” (hard).
Online shoppers may search for costumes (semantic)
with a specific price range (hard) ([Zuo et al., 2024](#bib.bib76)).
This task is called *filtered vector search*:
we query similar *vectors*—encoding semantics—associated with *hard* predicates.
The problem has been increasingly studied ([Patel et al., 2024](#bib.bib45); [Gupta et al., 2023](#bib.bib24); [Douze et al., 2024](#bib.bib16); [Gollapudi et al., 2023](#bib.bib22); [Wu et al., 2022](#bib.bib67); [Mohoney et al., 2023](#bib.bib40); [Wang et al., 2022](#bib.bib62); [Landrum et al., 2025](#bib.bib32); [Simhadri et al., 2024](#bib.bib52); [Zuo et al., 2024](#bib.bib76); [Engels et al., [n.d.]](#bib.bib17); [Sanca and Ailamaki, 2024](#bib.bib49)) as quality vector embeddings become available via modern ML models ([Meta, [n.d.]](#bib.bib36); [Microsoft, [n.d.]](#bib.bib37); [OpenAI, [n.d.]](#bib.bib44)).

Some works tackle filtered vector search by *constraining* graph-based approximate nearest neighbor (ANN) indexes for unfiltered vector search ([Malkov and Yashunin, 2018](#bib.bib34); [Jayaram Subramanya et al., 2019](#bib.bib28); [Fu et al., 2017](#bib.bib18)): FilteredVamana interleaves graph traversal and filter evaluations;
ACORN ([Patel et al., 2024](#bib.bib45)) induces query-time subgraphs by visiting only predicate-passing nodes.
These works outperform
naïve methods like *pre-filtering*,
which uses a (slow) linear scan for similarity computations.
Graph-based methods
navigate items via edges to reach targets with a few hops. Effective filtered vector search aims to serve queries with *compact* graphs; if the property—*small world*—is lost, graph traversal will lose efficiency.

Unfortunately, existing graph-based methods fail to offer the small-world property for low-selectivity predicates, thus delivering poorer performance.
Moreover,
we cannot simply use pre-filtering
as a linear scan is still too costly
unless the selectivity is *too low*.
This selectivity band is called the “unhappy middle” ([Gupta et al., 2023](#bib.bib24)). FilteredVamana aims to mitigate this by linking attribute-sharing vectors into local, dense, per-filter subgraphs, but requires restricting filter forms ([Gollapudi et al., 2023](#bib.bib22)).
ACORN ([Patel et al., 2024](#bib.bib45)) supports general predicates at a cost: its induced subgraph can provably lose the small-world property if it becomes too sparse ([Amaral et al., 2000](#bib.bib5); [Kashyap and Ambika, 2019](#bib.bib30); [Malkov and Yashunin, 2018](#bib.bib34)).
We conjecture that a single graph
is insufficient for handling *all* predicates,
whose support may overlap with one another in a complex way.
We may need multiple graphs,
each specialized for different predicate sets.

##### Our Goal

We aim to offer *compact* graphs
for nearly all filtered queries with varying selectivities or forms by building an *index collection*.
A collection is more expressive than one index.
By leveraging filter stability in real-world filtered vector search workloads ([Mohoney et al., 2023](#bib.bib40); [Sun et al., 2014](#bib.bib54)), we can tailor indexes to observed workloads to maximize expected search quality.
Each index can serve multiple predicates: a graph, e.g., built for stars=1--3, can also serve stars=1 if it is *dense enough* for the sub-predicate.
An index collection requires more memory than one index;
yet, indexes are relatively small versus raw data, i.e., high-dimensional vectors.
In our experiments,
hundreds of (small) additional indexes
took only as much memory as one dataset-wide index,
while they boost performance significantly
versus relying on one index.
Our proposed index collection can succeed
if it offers high-quality filtered search to nearly all queries,
each with a compact graph,
while being memory-efficient.

##### Challenge

Building an effective index collection is challenging
due to conflicting goals.
For construction, graphs can trade recall for smaller size (), allowing more indexes and coverage.
Yet, graphs must be dense enough for high recall.
Likewise, for querying, we can trade search speed for higher recall ().
This relationship must be quantified
to find which index to use for a given query.
These unique properties distinguishes our task from existing problems ([Sharma et al., 2016](#bib.bib50); [Song et al., 2019](#bib.bib53)).
For example,
materialized view selection targets *exact* querying,
whereas ANN is approximate with speed/recall trade-offs.

##### Our Approach

Our framework, called SIEVE (Set of Indexes for Efficient Vector Exploration), builds an index collection tailored to an observed query workload
to maximize expected throughput
with a memory budget and specified recall.
Every candidate index is assigned *benefit*—its marginal performance gain when added to a collection, and memory *cost*, with which we build a collection.
This approach isn’t new;
what’s new are
(1) how to estimate the benefit/cost
and (2) how to serve queries with the index collection.

First, we design an analytical, predicate form-agnostic benefit/cost model capturing three-dimensional relationships among
index size, search time, and recall,
allowing us to find the minimal (i.e., most sparse) graph satisfying a specific recall.
Since each index is smaller,
more indexes are allowed within a memory budget,
thus accelerating search for more predicates.
Our model is based on empirical observations
and existing small-world network theories ([Amaral et al., 2000](#bib.bib5)).

Second, query serving dynamically chooses the fastest-searching index given a specific recall.
This query-time selection is needed
since our indexes may overlap: a query may be servable by multiple indexes.
For optimal selection,
we again employ our model
to determine (1) which index to use,
and (2) its search parameterization.

##### Difference from Existing Work

SIEVE significantly differs from existing vector search works (). Versus works in MV selection ([Zhang et al., 2001](#bib.bib72); [Agrawal et al., 2000](#bib.bib3)), partitioning ([Sun et al., 2014](#bib.bib54); [Yang et al., 2020](#bib.bib69)) and query rewriting ([Goldstein and Larson, 2001](#bib.bib21); [Godfrey et al., 2009](#bib.bib20)) for exact queries, SIEVE’s optimizations notably considers recall, and performs theory-driven index tuning ([section 4.2](#S4.SS2 "4.2. SIEVE-Opt: Problem Setup ‣ 4. Index Collection Construction ‣ SIEVE: Effective Filtered Vector Search with Collection of Indexes")) and dynamic, recall-aware serving ([section 5.2](#S5.SS2 "5.2. Determining Optimal Search Strategy ‣ 5. Query Serving ‣ SIEVE: Effective Filtered Vector Search with Collection of Indexes")) for desired memory/speed/recall tradeoffs.

##### Contributions

We propose SIEVE, an indexing framework for filtered vector search ([section 3](#S3 "3. SIEVE Framework Overview ‣ SIEVE: Effective Filtered Vector Search with Collection of Indexes")) with the following contributions:

- •

  Index Selection: We introduce a three-dimensional cost model for evaluating the search speed, recall, and memory cost of vector search strategies, which we use to jointly perform index selection and parameterization under bounded memory. ([section 4](#S4 "4. Index Collection Construction ‣ SIEVE: Effective Filtered Vector Search with Collection of Indexes"))

- •

  Query Serving: We utilize our derived cost model to derive a dynamic search strategy that selects the most efficient serving method and parameterization at any target recall. ([section 5](#S5 "5. Query Serving ‣ SIEVE: Effective Filtered Vector Search with Collection of Indexes"))

- •

  Effective Filtered Vector Search: We show via experimentation that SIEVE achieves up to 8.06×\times speedup over existing indexes with <<2.15×\times memory of a standard HNSW and modest past workload knowledge on diverse query filter formats. ([section 7](#S7 "7. Experiments ‣ SIEVE: Effective Filtered Vector Search with Collection of Indexes"))

