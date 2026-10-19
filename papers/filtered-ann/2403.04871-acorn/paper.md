# 2403.04871v1 原始 PDF 文本提取

来源：https://arxiv.org/pdf/2403.04871v1

提取日期：2026-10-18。由 PyMuPDF 提取，未经 MinerU 版式转换；公式和表格需回看 PDF。

## PDF page 1

ACORN: Performant and Predicate-Agnostic Search Over Vector
Embeddings and Structured Data
Liana Patel
Stanford University
Stanford, USA
lianapat@stanford.edu
Peter Kraft
DBOS, Inc.
USA
peter.kraft@dbos.dev
Carlos Guestrin
Stanford University
Stanford, USA
guestrin@stanford.edu
Matei Zaharia
UC Berkeley
Berkeley, USA
matei@berkeley.edu
ABSTRACT
Applications increasingly leverage mixed-modality data, and must
jointly search over vector data, such as embedded images, text and
video, as well as structured data, such as attributes and keywords.
Proposed methods for this hybrid search setting either suffer from
poor performance or support a severely restricted set of search pred-
icates (e.g., only small sets of equality predicates), making them im-
practical for many applications. To address this, we present ACORN,
an approach for performant and predicate-agnostic hybrid search.
ACORN builds on Hierarchical Navigable Small Worlds (HNSW), a
state-of-the-art graph-based approximate nearest neighbor index,
and can be implemented efficiently by extending existing HNSW
libraries. ACORN introduces the idea of predicate subgraph traver-
sal to emulate a theoretically ideal, but impractical, hybrid search
strategy. ACORN’s predicate-agnostic construction algorithm is
designed to enable this effective search strategy, while supporting a
wide array of predicate sets and query semantics. We systematically
evaluate ACORN on both prior benchmark datasets, with simple,
low-cardinality predicate sets, and complex multi-modal datasets
not supported by prior methods. We show that ACORN achieves
state-of-the-art performance on all datasets, outperforming prior
methods with 2–1,000× higher throughput at a fixed recall.
CCS CONCEPTS
• Information systems →Information retrieval query pro-
cessing; Data structures.
KEYWORDS
Vector Search, Approximate Nearest Neighbor Search, Hybrid Search
1
INTRODUCTION
Due to the representation strength of modern deep learning mod-
els, vector embeddings have become a powerful first-class datatype
for wide-ranging applications that use retrieval-augmented gener-
ation [3, 65] or similarity-based search [18, 21, 42]. As a result,
vector databases and indices are seeing increasing adoption in
many production use cases. These systems provide an efficient
approximate-nearest-neighbor (ANN) search interface over embed-
ded unstructured data e.g., images, text, video, or user profiles.
However, many applications must jointly query both unstructured
and structured data, requiring ANN search in combination with
predicate filtering. For example, customers on an e-commerce site
can search for t-shirts similar to a reference image, while filtering on
price [64]. Similarly, researchers performing a literature review may
search with both natural language queries and filters on publication
date, keywords or topics [54]. Likewise, a data scientist working on
outlier detection can find misclassified images by retrieving those
that look similar to a reference dog but have the label "cat" [2, 7].
To leverage diverse data modalities, applications need data man-
agement systems that effectively support hybrid search queries, i.e.,
similarity search with structured predicates. Such systems require
(1) query performance, i.e., efficient and accurate search despite
variance in workload characteristics, such as selectivity, attribute
correlations, and scale, and (2) expressive query semantics: sup-
port for diverse query predicates that may not be known a priori
(e.g., user-entered keywords, range searches, or regex matching).
Unfortunately, existing systems fall short of these goals. Three
commonly used methods are pre-filtering [62, 64], post-filtering
[1, 5, 62, 64, 67], and specialized data structures for low-cardinality
predicate sets [25, 49, 63, 66]. Pre-filtering first finds all records in
the dataset that pass the query predicate, then performs brute force
similarity-search over the filtered vector set. This approach scales
poorly, becoming inefficient for medium to high selectivity predi-
cates on large datasets. Alternatively, post-filtering first searches an
ANN index, then removes results that fail the query predicate. Since
the database vectors closest to the query vector may not pass the
predicate, post-filtering methods must typically expand the search
scope. This is often expensive, particularly for search predicates
with low selectivity or low correlation to the query vector, as we
show in Figure 2. Milvus [62], Weaviate [1], AnalyticDB-V [64],
and FAISS-IVF [5] build systems using these two core methods, and
suffer from their performance limitations.
Recognizing these limitations, recent works construct specialized
indices designed for hybrid search workloads with low-cardinality
predicate sets consisting of equality predicate operators. For ex-
ample, Filtered-DiskANN [25] outperforms prior baselines, but
restricts the cardinality of the predicate set to about 1,000 and only
supports equality predicates. HQANN [66] and NHQ [12] similarly
constrain the predicate set to a small number of equality filters and
in addition allow only a single structured attributes per dataset
entry. These methods are often impractical since many applica-
tions have large, or unbounded predicate sets that are unknown
a priori. In general, the possible predicate set’s cardinality grows
arXiv:2403.04871v1  [cs.IR]  7 Mar 2024


## PDF page 2

exponentially with each attribute’s cardinality, which itself may be
large. Thus, we instead propose a predicate-agnostic index, which
can support arbitrary and unbounded predicate sets.
In this paper, we propose ACORN (ANN Constraint-Optimized
Retrieval Network), a novel approach for performant and predicate-
agnostic hybrid search that can serve high-cardinality and un-
bounded predicate sets. We propose two indices: ACORN-𝛾, de-
signed for high-efficiency search, and ACORN-1, designed for low
construction overhead in resource-constrained settings. Both meth-
ods modify the hierarchical navigable small world (HNSW) index,
a state-of-the-art graph-based index for ANN search, and are easy
to implement in existing HNSW libraries.
ACORN tackles both the performance limitations of pre- and
post- filtering, as well as the semantic limitations of specialized
indices. ACORN proposes the idea of predicate subgraph traversal
during search. As the name implies, the search strategy traverses
the subgraph of the ACORN index induced by the set of nodes
that pass the query predicate. ACORN designs the index such that
these arbitrary predicate subgraphs approximate an HNSW index.
Unlike pre- and post-filtering, this allows ACORN to provide sub-
linear retrieval times despite variance in correlation between query
vectors and predicates, which we find to be a major challenge for
existing hybrid search systems. ACORN also serves wide-ranging
predicate sets by employing a predicate-agnostic construction that
alters HNSW’s algorithm to create a denser graph. Specifically,
we introduce a predicate-agnostic neighbor expansion strategy in
ACORN-𝛾based on target predicate selectivity thresholds, which
can be estimated empirically with or without knowing the predicate
set. In conjunction, we propose a predicate-agnostic compression
heuristic to efficiently manage the index space footprint while main-
taining efficient search. We also explore the trade-off space between
search performance and construction overhead, designing ACORN-
1 to approximate ACORN-𝛾’s search performance while further
reducing the time-to-index (TTI) for resource-constrained settings.
We systematically evaluate ACORN-𝛾and ACORN-1 on four
datasets: SIFT1M [35], Paper [63], LAION [55], and TripClick [54].
Our evaluation includes both prior benchmark datasets, with sim-
ple, low-cardinality predicate sets, which prior specialized indices
can serve, as well as more complex datasets with millions of possi-
ble predicates, which existing indices cannot to handle. On each,
ACORN-𝛾achieves state-of-the-art hybrid search performance with
2–1000× higher queries per second (QPS) at 0.9 recall compared
to prior methods. Specifically, ACORN achieves 2-10× higher QPS
on prior benchmarks, over 30× higher QPS on new benchmarks,
and over 1,000x higher QPS at scale, on a 25-million-vector dataset.
We find that ACORN-1 empirically approximates ACORN-𝛾, at-
taining at most 5× lower QPS at fixed recall but 9–53× lower TTI
compared to ACORN-𝛾. Our detailed evaluation demonstrates the
effectiveness of ACORN’s predicate-subgraph traversal strategy
and predicate-agnostic construction techniques.
2
BACKGROUND
Existing methods for Approximate Nearest Neighbor (ANN) search
can be broadly categorized as tree-based [15–17, 19, 28, 45, 50,
56], hashing-based [9–11, 24, 26, 29, 30, 40, 41, 44, 46, 52, 59, 69],
quantization-based [23, 27, 34, 35, 39], and graph-based [22, 25, 32,
Figure 1: Schematic drawing of search over an HNSW index. The
search path is shown by blue arrows, beginning on level 2 and ending
on level 0 at the query point, shown in green.
47, 48, 58, 68]. In this work build on HNSW, a graph-based method
that is empirically one of the best-performing on high-dimensional
datasets, and we adapt it to support hybrid search.
Graph-based ANN methods have gained popularity due their
state-of-the-art performance on varied ANN benchmarks [13, 57].
These methods typically perform search using a greedy routing
strategy that traverses a graph index, starting from a pre-defined
entry point. The index itself forms a proximity graph 𝐺(𝑉, 𝐸), such
that each dataset point is represented by a vertex and edges connect
nearby points. The index construction algorithm typically aims to
approximate subgraphs of the Delaunay graph [38]. While the De-
launay graph guarantees convergence of a greedy routing algorithm,
it is impossible to efficiently construct for arbitrary metric spaces
[51]. Thus graph methods focus on more tractable approximations
of Delaunay subgraphs, such as the Relative Neighbor Graph (RNG)
[37, 60], and the Nearest-Neighbor Graph (NNG) [8, 20].
2.1
Hierarchical Navigable Small Worlds
As Figure 1 illustrates, HNSW forms a hierarchical, multi-level
graph index with bounded degree. Below we briefly summarize the
HNSW search and construction algorithm.
The HNSW construction algorithm iteratively inserts each
point into the graph index, to construct a navigable graph with
bounded degree, specified by parameter 𝑀. For each inserted ele-
ment 𝑣, first, a maximum layer index 𝑙is stochastically chosen using
an exponentially decaying probability distribution, normalized by
the constant 𝑚𝐿= 1/𝑙𝑛(𝑀). The level assignment probability en-
sures that the expected characteristic path length increases with the
level index. Intuitively, the upper-most level contains the longest-
range links, which will be traversed first by the search algorithm,
and the bottom-most level contains the shortest-range links, which
are traversed last by the search algorithm. The insertion procedure
then proceeds in two phases. In the first phase, a greedy search is
performed iteratively from the top layer, beginning at a pre-defined
entrypoint down to the (𝑙+ 1)th layer. At each of these levels, the
greedy subroutine chooses a single node that becomes the entry-
point into the next layer. In the second phase, the greedy search
iterates over level 𝑙to level 0. The greedy search at each level now
chooses efc nodes as candidate edges. Of these candidates, at most
𝑀are selected to become neighbors of 𝑣according to an RNG-based
pruning algorithm [31]. At level 0, the degree bound is increased
2 × 𝑀, which is shown to empirically improve performance.
The HNSW search algorithm begins its traversal from a pre-
defined entry point at the upper-most layer of the multilayer graph,


## PDF page 3

ACORN: Search Over Vector Embeddings and Structured Data
Algorithm 1: HNSW-ANN-SEARCH(𝑥𝑞, 𝐾, efs)
Input: query vector 𝑥𝑞, number of nearest neighbors to return 𝐾,
size of dynamic candidate list efs
Output: 𝐾nearest elements to 𝑥𝑞
𝑒←entry-point to hnsw graph
𝑊←∅// set of current nearest
𝐿←𝑙𝑒𝑣𝑒𝑙(𝑒) // Top hnsw level
for 𝑙←𝐿...1 do
𝑒←SEARCH-LAYER(𝑥𝑞,𝑒, ef = 1,𝑙)
end
𝑊←SEARCH-LAYER(𝑥𝑞,𝑒, ef = efs,𝑙= 0)
return 𝐾nearest elements from 𝑊to 𝑥𝑞
illustrated in Figure 1. The traversal then follows an iterative search
strategy from the top level downwards. At each level a greedy
search is used to choose a single node, which becomes the entry-
point into the next level. Once the bottom level is reached, rather
than greedily choosing a single node, the search algorithm greedily
chooses 𝐾nearest elements to return. We outline this process in
Algorithm 1. The search parameter efs provides a tradeoff between
search quality and efficiency by controlling the size of the dynamic
candidate list stored during the bottom level’s greedy search.
3
PROBLEM DEFINITION AND CHALLENGES
In this section we formally define the hybrid search setting and then
analyze the performance challenges that existing predicate-agnostic
methods, i.e., pre- and post-filtering, face. Our analysis leads us
to explore several important workload characteristics. Specifically,
we will consider predicate selectivity, the dataset size, and query
correlation, which we introduce, formally define and find to be a
major challenge for post-filtering methods.
We will later leverage our understanding of existing performance
challenges in Section 4 to formulate a theoretically ideal hybrid
search solution. Then, in Section 7, we will revisit the workload char-
acteristics discussed in this section to rigorously evaluate ACORN’s
search performances.
3.1
Hybrid Search Definitions
Let 𝐷= {𝑒1,𝑒2, ...,𝑒𝑛} = {(𝑥1,𝑎1), (𝑥2,𝑎2), ..., (𝑥𝑛,𝑎𝑛)} be a dataset
consisting of 𝑛entities, each with a vector component, 𝑥𝑖∈R𝑑,
and a structured attribute-tuple, 𝑎𝑖, associated with entity 𝑒𝑖. Let
𝑋= {𝑥1,𝑥2, ...,𝑥𝑛} denote the set of vectors in the dataset, and
𝑑𝑖𝑠𝑡(𝑎,𝑏) is the metric distance between any two points. Let 𝐴=
{𝑎1,𝑎2, ...,𝑎𝑛} be the set of structured attributes in the dataset. We
will denote 𝑋𝑝⊆𝑋as the subset of vectors corresponding to
entities in the dataset that pass a given predicate 𝑝. We refer to the
selectivity (𝑠) of predicate 𝑝as the fraction of entities from 𝐷that
satisfy the predicate, where 0 ≤𝑠≤1.
We consider the hybrid search problem, described as follows.
Given a dataset 𝐷, target 𝐾, and query 𝑞= (𝑥𝑞, 𝑝𝑞), where 𝑥𝑞∈
R𝑑, and 𝑝𝑞is a predicate, retrieve 𝑥𝑞’s 𝐾nearest neighbors that
pass the predicate 𝑝𝑞. We will specifically focus on the problem
of approximate nearest neighbor search w.r.t 𝑥𝑞. Here, our goal is
to maximize both search accuracy and search efficiency. We will
measure accuracy by recall@𝐾= 𝐺∩𝑅
𝐾, where 𝐺is the ground
Figure 2: Schematic drawing of a dataset with no predicate clus-
tering (top), a dataset with predicate clustering and positive query
correlation (middle), and a dataset with predicate clustering and
negative query correlation (bottom). Dark blue circles show points
that pass the predicate, and light gray circles show points that fail
the predicate. The query vectors are shown in green.
truth set of 𝐾nearest neighbors to 𝑥𝑞that satisfy 𝑝𝑞, and 𝑅is the
retrieved set.
3.2
Search Performance of Baseline Methods
We now analyze the search complexity of two predominant baseline
methods, pre- and post-filtering. We will consider how varied work-
load characteristics impact the search behavior of these methods.
Through our analysis, we will make the standard assumption that
distance computations dominate search performance. We note that
HNSW’s unfiltered search complexity is 𝑂(log(𝑛) + 𝐾).
Pre-filtering linearly scans 𝑋𝑝, computing distances over each
point that passes the search predicate. This yields a hybrid search
complexity of 𝑂(|𝑋𝑝|) = 𝑂(𝑠𝑛+ 𝐾). While pre-filtering always
achieves perfect recall, its search complexity scales poorly for large
dataset sizes or selectivities, growing linearly in either variable.
Post-filtering, by contrast, performs ANN-search over 𝑋to find
the closet query vector to 𝑥𝑞, then expands the search scope to
find 𝐾vectors that pass the query predicate, 𝑝. Intuitively, search
performance varies depending on correlation between the query
vector and the vectors in 𝑋𝑝. When the vectors of 𝑋𝑝are close to the
query vector, post-filtering over HNSW has a search complexity of
𝑂(log(𝑛) +𝐾). If the vectors in 𝑋𝑝are uniformly distributed within
𝑋, then post-filtering’s expected search complexity is 𝑂(log(𝑛) +
𝐾/𝑠). However, vectors of 𝑋𝑝may be far away from the query
vector, leading to a worst case of 𝑂(𝑛) search performance.
We see that the search performance of either baseline is not robust
to variations in selectivity, dataset size, and query correlation. We
empirically verify these limitations in section 7 (figures 9, 10).
3.2.1
Formalizing Query Correlation. We will now formalize the
notion of query correlation, which we find is key challenge for
post-filtering-based systems. As Figure 2 shows, query correlation
occurs when the vectors of 𝑋𝑝are non-uniformly distributed in
𝑋and instead cluster together relative to the vectors in X. We
refer to this phenomenon as predicate clustering. When predicate
clustering occurs, a query vector may be either close or far away
from the predicate cluster containing its search targets, inducing
query correlation.


## PDF page 4

Definition: Query Correlation. We will consider the query-
to-target distances for the given dataset compared to the expected
query-to-target distances for a hypothetical dataset, under which
no clustering is present. Formally, we define the query correlation
of the hybrid search workload 𝑄over dataset 𝐷as:
𝐶(𝐷,𝑄) = E(𝑥𝑖,𝑝𝑖)∈𝑄

E𝑅𝑖[𝑔(𝑥𝑖, 𝑅𝑖)] −𝑔(𝑥𝑖,𝑋𝑝𝑖)

We let 𝑅𝑖be a random set variable of |𝑋𝑝𝑖| vectors drawn uni-
formly from 𝑋, defined for each hybrid query (𝑥𝑖, 𝑝𝑖) ∈𝑄. We
define 𝑔(𝑥,𝑆) = min𝑦∈𝑆𝑑𝑖𝑠𝑡(𝑥,𝑦) to be the function mapping the
query vector 𝑥to the minimum distance of neighbors from the
given vector set 𝑆⊆𝑅𝑑. Note that 𝑔(𝑥𝑖,𝑋𝑝𝑖) is the ground-truth
hybrid-search target of the query (𝑥𝑖, 𝑝𝑖).
If, on average, query vectors are closer to their targets in 𝑋𝑝𝑖, the
true dataset of hybrid search targets, than in 𝑅𝑖, the no-clustering
dataset, then the workload has positive query correlation. If the
reverse is true, the workload has negative query correlation. We
may also consider nearest-neighbor distance rather than the metric
distance in the above definition. We also note that we can easily
extend this definition to consider 𝐾targets of the hybrid search,
rather than one, by summing distances over the 𝐾search targets.
4
THEORETICAL IDEAL HYBRID SEARCH
PERFORMANCE WITH HNSW
For a given hybrid search query, we define the theoretically ideal
search performance using HNSW data structures as the perfor-
mance attainable if we knew the search predicate 𝑝𝑞during con-
struction. In this case, we could construct an HNSW index over
𝑋𝑝. We call this the oracle partition index for that query. The
complexity of searching this index is 𝑂𝑠(log𝑠𝑛+ 𝐾). Notably, the
search performance of the oracle partition index outperforms both
pre- and post-filtering across variations in predicate selectivity, data
size, and query correlation. While pre-filtering’s search scales in
|𝑋𝑝|, search over the oracle partition scales sublinearly in |𝑋𝑝|. The
oracle partition is also robust under variations in query correlation:
it does not require the search scope expansion used in post-filtering.
Despite its ideal search performance, the oracle partition index
requires us to know all search predicates in advance and to create
a full HNSW index per predicate. In practice, the oracle partition
index is not possible to construct because query predicate sets are
often unknown during construction and have high or unbounded
cardinality. Building an HNSW per predicate would require prohib-
itive amounts of space and time. Thus, in this work, we will instead
approximate search over the oracle partition index for a particular
query, without ever explicitly constructing this index.
5
ACORN OVERVIEW
We now describe ACORN, a predicate-agnostic approach for state-
of-the-art hybrid search. We propose two variants, which we refer
to as ACORN-𝛾(5.1, 5.2) and ACORN-1 (5.3). We design ACORN-𝛾
to achieve efficient search performance, and we design ACORN-
1 to approximate ACORN-𝛾’s search performance while further
reducing the algorithm’s time to index (TTI) and space footprint
for resource-constrained settings.
ACORN’s core idea is to search over the index’s predicate sub-
graph, i.e., the subgraph induced by 𝑋𝑝for a given search predicate
Figure 3: An illustration of the predicate subgraph, shown by the
green nodes. ACORN searches over the predicate subraph to emulate
search over an oracle partition index.
Table 1: Summary of Notation
Symbol
Description
𝛾
neighbor expansion factor for ACORN index
𝑀𝛽
compression parameter for ACORN index
ef
size of dynamic candidate list in ACORN greedy search
𝑀
degree bound for traversed nodes during ACORN search
𝑚𝐿= 1/ln 𝑀
level normalization constant for ACORN index
𝑒
entry-point to ACORN index
𝑒𝑝
entry-point to ACORN’s predicate 𝑝’s subgraph
𝑙(𝑣)
maximum level index of node 𝑣in ACORN index
𝑁𝑙(𝑣)
neighbor list of node 𝑣at level 𝑙
𝑁𝑙𝑝(𝑣)
filtered neighbors of node 𝑣at level 𝑙under predicate 𝑝
𝑋𝑝
vector dataset that passes predicate 𝑝
𝑠
selectivity
𝑛
size of dataset
𝑝, as shown in Figure 3. We modify the HNSW construction algo-
rithms so that arbitrary predicate subgraphs emulate an HNSW
oracle partition index without the need to explicitly construct one.
ACORN-𝛾achieves this by constructing a denser version of HNSW,
which we parameterize by a neighbor list expansion factor,𝛾, a com-
pression factor, 𝑀𝛽, and the HNSW parameters, efc and 𝑀. Then by
adding a filter step during search to ignore neighbors that fail the
predicate, we find ACORN-𝛾’s search can efficiently navigate to and
traverse over the predicate subgraph, even under variations in query
correlation. Meanwhile, ACORN-1 expands neighbor lists during
search rather than during construction to approximate ACORN-𝛾’s
dense graph structure without building it.1
Overall, ACORN prescribes a simple and general framework for
performant hybrid search based on the idea of predicate-subgraph
traversal. The core techniques we propose are predicate-agnostic
neighbor-list expansions and pruning during construction in com-
bination with predicate-based filtering during search. While this
framework can be applied to a variety of graph-based ANN in-
dices, in this work we focus on HNSW due their state-of-the-art
performance and widespread use.
5.1
ACORN-𝛾Search Algorithm
Algorithm 2 outlines the greedy search algorithm ACORN uses
at each level, beginning from the top level at a pre-defined entry-
point. The main difference between ACORN’s search algorithm and
that of HNSW is how neighbor look-ups (line 9) are performed at
1For highly selective queries where even ACORN’s predicate subgraph would be
disconnected within the larger ACORN graph, ACORN falls back to pre-filtering,
which is effective for such queries. ACORN is configured with a minimum selectivity,
𝑠𝑚𝑖𝑛, under which it should use pre-filtering when a query is estimated to be more
selective than 𝑠𝑚𝑖𝑛. We describe how to configure 𝛾based on 𝑠𝑚𝑖𝑛in Section 5.2.


## PDF page 5

ACORN: Search Over Vector Embeddings and Structured Data
Figure 4: Diagram of ACORN’s neighbor selection strategies. Blue nodes represent neighbors that pass the query predicate. Sub-figure (a)
shows the simple predicate-based filter applied to uncompressed edge lists of size 𝑀· 𝛾, followed by truncation to size 𝑀= 3. Sub-figure (b)
shows the compression-based heuristic. Sub-figure (c) shows the neighbor expansion strategy used in ACORN-1.
Algorithm 2: ACORN-SEARCH-LAYER(𝑥𝑞, 𝑝𝑞,𝑒,𝑒𝑓,𝑙)
Input: query vector 𝑥𝑞, query predicate 𝑝𝑞, entry-point 𝑒, number
of nearest neighbors to return 𝑒𝑓, level to search 𝑙
Output: 𝑒𝑓nearest elements to 𝑥𝑞
1 𝑇←𝑒// visited set
2 𝐶←𝑒// candidate set
3 𝑊←𝑒// dynamic list of found nearest neighbors
4 while |𝐶| > 0 do
5
𝑐←extract arg min𝑥∈𝐶∥𝑥𝑞−𝑥∥
6
𝑓←get arg max𝑥∈𝑊∥𝑥𝑞−𝑥∥
7
if 𝑑𝑖𝑠𝑡(𝑐,𝑥𝑞) > 𝑑𝑖𝑠𝑡(𝑓,𝑥𝑞) and |𝑊| ≥𝑒𝑓𝑐
8
break
9
𝑛𝑒𝑖𝑔ℎ𝑏𝑜𝑟ℎ𝑜𝑜𝑑←GET-NEIGHBORS(𝑐,𝑙, 𝑝𝑞)
10
for each 𝑣∈neighborhood[1:𝑀]
11
if 𝑣∉𝑇
12
𝑇←𝑇∪𝑣
13
𝑓←arg max𝑥∈𝑊∥𝑥𝑞−𝑥∥
14
if 𝑑𝑖𝑠𝑡(𝑣,𝑥𝑞) < 𝑑𝑖𝑠𝑡(𝑓,𝑥𝑞) or |𝑊| < 𝑒𝑓
15
𝐶←𝐶∪𝑣
16
𝑊←𝑊∪𝑣
17
if |𝑊| > 𝑒𝑓
18
remove furthest element from 𝑊to 𝑥𝑞
19
20 end
21 return 𝑊
each visited node, 𝑐. While HNSW simply checks the neighbor list,
𝑁𝑙(𝑐), ACORN performs additional steps to recover an appropriate
neighborhood for the given search predicates.
Specifically, ACORN-𝛾uses two neighbor look-up strategies, a
simple filter method, shown in Figure 4(a), and a compression-based
heuristic, shown in Figure 4(b), which is compatible with the com-
pression strategy we optionally apply during construction, detailed
in Section 5.2. For each visited node, 𝑣, the filter-based neighbor
look-ups simply scan the neighbor list 𝑁𝑙(𝑣) to find the sub-list of
neighbors that pass the predicate, 𝑁𝑙𝑝(𝑣). If 𝑁𝑙𝑝(𝑣) contains more
than 𝑀nodes, we take the first 𝑀and return this as 𝑣’s neighbor-
hood. The compression-based neighbor look-ups instead partially
expand the neighbor set 𝑁𝑙(𝑣) to include a subset of 𝑣’s two-hop
neighbors, before performing filtering and truncation. This proce-
dure entails two phases. The first phase iterates through the first
𝑀𝛽nodes of 𝑁𝑙(𝑣), simply filtering as in the previous strategy.
The second phase iterates over the remainder of the neighbor list,
expanding the search neighborhood to include neighbors of neigh-
bors, before again filtering according to the query predicate. 𝑀𝛽is
a construction parameter which we will discuss in the next section.
5.2
ACORN-𝛾Construction Algorithm
We construct the ACORN-𝛾index by applying two core modifica-
tions to the HNSW indexing algorithm: first, we expand each node’s
neighbor list, and then we apply a novel predicate-agnostic pruning
method to compress the index. Both of these steps are summarized
in Figure 5.
Neighbor List Expansion. While HNSW collects 𝑀approxi-
mate nearest neighbors as candidate edges for each node in the
index, ACORN collects 𝑀· 𝛾approximate nearest neighbors as
candidate edges per node. To find these candidates during construc-
tion, ACORN uses a metadata-agnostic search over its graph index.
Specifically, the neighbor lookup strategy at each node, 𝑣, on level
𝑙, simply accesses the neighbor list 𝑁𝑙(𝑣) and returns the first 𝑀
nodes. Note that although each node contains up to 𝑀·𝛾neighbors,
we assume by construction that 𝑀neighbors per node are sufficient
for maintaining navigability of the graph index. Thus, considering
truncated neighbor lists while traversing the graph allows us to
avoid unnecessary distance computations and TTI slowdowns.
One simple choice for𝛾is
1
𝑠𝑚𝑖𝑛, where 𝑠𝑚𝑖𝑛is the minimum pred-
icate selectivity we plan to serve before resorting to pre-filtering. As
we discuss in Section 6, ACORN’s indexing time and space footprint
increase proportionally to 𝛾. Meanwhile, pre-filtering becomes a
competitive baseline at low predicate selectivity values, as we show
in Figure 9a. Thus, ACORN is able to balance construction and
search efficiency by using pre-filtering as a fall-back for queries
with low-selectivity predicates. This leads to a simple cost-based
model during search: if the estimated predicate selectivity of a given
query is greater than 1/𝛾, search the ACORN-𝛾index, otherwise
pre-filter. We note that leveraging pre-filtering in this way may de-
grade search efficiency, but not result quality, when errors occur in
selectivity estimates. If a query’s true predicate selectivity is above
1/𝛾, but the estimate is below, the system will mistakenly pre-filter,


## PDF page 6

Figure 5: A comparison of HNSW and ACORN-𝛾’s strategies for (a)
selecting candidate edges, shown for 𝑀=3, and (b) pruning candidate
edges for each inserted node 𝑣, shown for 𝑀=3, 𝑀𝛽=2, 𝛾=2.
obtaining perfect recall at possibly lower QPS than if the ACORN
index was instead searched. If the reverse is true, the system will
mistakenly search the ACORN index, whereas pre-filtering would
have offered similar QPS and perfect recall.
Compression. A key challenge with ACORN-𝛾’s neighbor ex-
pansion step is that it increases index size and TTI. The increased
index size poses a significant issue particularly for memory-resident
graph indices, like HNSW. To address this, we introduce a predicate-
agnostic pruning technique. While we could apply compression to
the full index, as discussed in Section 6.1, we specifically target
the bottom level’s neighbor lists since they contribute most signifi-
cantly to the indexing overhead. This follows from the exponentially
decaying level assignment probability ACORN uses.
The core idea of the pruning procedure is to precisely retain
each node’s nearby neighbors in the index, while approximating
farther away neighbor during search. We use the tunable compres-
sion parameter, 𝑀𝛽, where 0 ≤𝑀𝛽, ≤𝑀· 𝛾. During construction,
ACORN chooses each node’s final neighbor list by automatically
retaining the nearest 𝑀𝛽candidate edges and aggressively prun-
ing the remaining candidates. During search we can recover the
first 𝑀𝛽neighbors of each node 𝑣directly from the neighbor list
𝑁𝑙(𝑣), and approximate remaining neighbors by looking at 2-hop
neighbors during search, as we described in Section 5.1.
Figure 5 outlines this pruning procedure applied to node 𝑣’s
candidate neighbor list. The algorithm iterates over the ordered
candidate edge list and keeps the first 𝑀𝛽candidates. Over the
remaining sub-list of candidates, the algorithm applies the following
pruning procedure at each node. Let H be the dynamic set of 𝑣’s
chosen two-hop neighbors, initialized to ∅. We prune candidate
𝑐if it is contained in 𝐻; otherwise, we keep 𝑐and add all of its
neighbors to 𝐻. The pruning procedure stops after iterating over
all candidates, or if |𝐻| plus the number of chosen edges exceeds
𝑀· 𝛾. The pruned and ordered neighbor list is then stored in the
ACORN index and H is discarded.
We highlight that the neighbor expansion during search, de-
scribed in Section 5.1, can recover pruned neighbors regardless of
the query predicate. It follows from ACORN’s pruning rule that any
node 𝑥that was pruned from some node 𝑣’s neighbor list, 𝑁𝑙(𝑣),
must be in the neighbor list 𝑁𝑙(𝑦) such that 𝑦is a neighbor of 𝑣
with index greater than 𝑀𝛽. During search, the neighbor lookup at
𝑣on level 𝑙will perform a neighbor-list expansion for all neighbors
with an index greater than 𝑀𝛽, thus checking 𝑁𝑙(𝑦) and finding 𝑥.
We now briefly describe why HNSW’s pruning, a metadata-
blind mechanism, is insufficient for hybrid search. Consider the
simple scenario shown in Figure 5. For a node, 𝑣, inserted into
the HNSW index at an arbitrary level 𝑙, the algorithm generates
candidates neighbors 𝑎,𝑏and 𝑐. HNSW’s pruning rule iterates over
𝑣’s candidate neighbor list in order of nearest to farthest neighbors.
Node 𝑏is pruned since there exists a neighbor 𝑎such that 𝑏is closer
to 𝑎than to 𝑣. This RNG-approximation strategy corresponds to
pruning the longest edge of the triangle formed by a triplet 𝑣,𝑎,𝑏.
In this case, we can prune the edge 𝑣−𝑏and expect a search path
to traverse from 𝑣to 𝑏via 𝑎. The problem with this technique
arises when we consider the hybrid search setting for an arbitrary
predicate. Say 𝑣and 𝑏pass a given query predicate 𝑝𝑞, but 𝑎does
not. Then 𝑣,𝑏,𝑎do not form a triangle in the predicate subgraph,
and we cannot expect to find the path from 𝑣to 𝑏through 𝑎. As a
result, HNSW’s pruning mechanism will falsely prune edge 𝑣−𝑏.
If we had complete knowledge of all possible query predicates, we
could ensure that we only prune edges of triangles such that all
three vertices always exist in the same subset of possible predicate
subgraphs. FilteredDiskANN [25] takes this approach by restricting
the set of possible query predicates. However, for arbitrary query
predicates, ensuring this property holds becomes intractable.
5.3
ACORN-1
We now describe ACORN-1, an alternative approach which aims to
approximate ACORN-𝛾’s search performance, while further min-
imizing index size and TTI. ACORN-1 achieves this by perform-
ing the neighbor expansion step solely during search, rather than
during construction, as ACORN-𝛾does. ACORN-1’s construction
corresponds to the original HNSW index without pruning. This
construction corresponds to ACORN-𝛾’s construction algorithm,
with fixed parameters 𝛾= 1 and 𝑀𝛽= 𝑀.
ACORN-1’s main difference from ACORN-𝛾during search, is
its neighbor lookup strategy. Specifically, at each visited node, 𝑣,
during greedy search, ACORN-1 uses a full neighbor list expansion
to consider all one-hop and two-hop neighbors of 𝑣, before applying
the predicate filter and truncating the resulting neighbor list to size
𝑀. Figure 4(c) outlines this procedure.
6
DISCUSSION
In this section we analyze the ACORN index’s space complexity,
construction complexity and search performance. We focus our
attention on ACORN-𝛾, since ACORN-1’s index construction rep-
resents a special case of ACORN-𝛾for fixed parameters (𝛾= 1,
𝑀𝛽= 𝑀), and we empirically show that ACORN-1 search approxi-
mates ACORN-𝛾in Section 7. We note that our analysis in Sections
6.2 and 6.3 considers the complexity scaling of the search procedure
under the assumption that we build the exact Delaunay graphs
rather than approximate ones.
6.1
Index Size
The average memory consumption per node of the ACORN-𝛾index
is 𝑂(𝑀𝛽+ 𝑀+𝑚𝐿· 𝑀· 𝛾), assuming the number of bytes per edge
is constant. For comparison, average memory consumption per
node for the HNSW index scales 𝑂(𝑀+𝑚𝐿·𝑀). Overall, ACORN-𝛾
increases the bottom-level’s memory consumption by 𝑂(𝑀𝛽) per


## PDF page 7

ACORN: Search Over Vector Embeddings and Structured Data
node, and increases the higher levels memory consumption by a
factor of 𝛾per node.
To understand ACORN’s memory consumption we evaluate the
average number of neighbors stored per node. At level 0, com-
pression is applied to the candidate edge lists of size 𝑀∗𝛾result-
ing in neighbor sets of length 𝑀𝛽plus a compressed set which
scales 𝑂(𝑀). We show this empirically in figure 12. On higher
levels, nodes have at most 𝑀∗𝛾edges. We multiply this by the
average number of levels that an element is added to, given by
E[𝑙+ 1] = 𝐸[−ln(unif(0, 1)) ∗𝑚𝐿] = 𝑚𝐿+ 1.
While we specifically target compression to level 0 in this work,
because it uses the most space, compression could be applied to
more levels in bottom-up order to further reduce the index size for
large datasets. Denoting𝑛𝑐as the chosen number of compressed lev-
els, the average memory consumption per node in this generalized
case is 𝑂(𝑛𝑐(𝑀𝛽+ 𝑀) + (𝑚𝐿−𝑛𝑐)(𝑀· 𝛾).
6.2
Construction Complexity
For fixed parameters 𝑀, 𝑀𝛽and efc, ACORN-𝛾’s overall expected
construction complexity scales𝑂(𝑛·𝛾·log(𝑛)·log(𝛾)). Compared to
HNSW, which has 𝑂(𝑛· log(𝑛)) expected construction complexity,
ACORN-𝛾increases TTI by a factor of𝛾·log(𝛾)) due to the expanded
edge lists it generates.
We now describe ACORN’s construction complexity in detail
by decomposing it into the following three factors (i) the number
of nodes in the dataset, given by 𝑛(ii) the expected number of
levels searched to insert each node into the index, and (iii) the ex-
pected complexity of searching on each level. By design, ACORN’s
expected maximum level index scales𝑂(log𝑛) according to its level-
assignment probability, which is the same as HNSW. This provides
our bound on (ii).
Turning our attention to (iii), we will first consider the length of
the search path and then consider the computation cost incurred at
each visited node. For the HNSW level probability assignment, it is
known that the expected greedy search path length is bounded by
a constant 𝑆=
1
1−exp(−𝑚𝐿) [48]. We can bound ACORN’s expected
search path length by 𝑂(𝛾) since the path reaches a greedy minima
in a constant number of steps and proceeds to expand the search
scope by at most 𝑀·𝛾nodes to collect up to 𝑀·𝛾candidate neighbors
during construction.
The computation complexity at each visited node along the
search path is 𝑂(log(𝛾)), seen as follows. For each node visited,
we first check its neighbor list to find at most 𝑀un-visited nodes,
on which we perform distance computations in 𝑂(𝑀· 𝑑) time.
Then, we update the sorted lists of candidate nodes and results in
𝑂(𝑀·𝑑·𝑙𝑜𝑔(𝛾·𝑀)) time. Treating 𝑀and𝛾as constants, we see that
at each visited node the computation complexity is 𝑂(log𝛾) and
for greedy search at each level, the complexity is 𝑂(𝛾· log(𝛾)). Mul-
tiplying by 𝑛· log(𝑛) yields ACORN’s final expected construction
complexity, 𝑂(𝑛· 𝛾· log(𝑛) · log(𝛾).
6.3
Search Analysis
Turning our attention to ACORN-𝛾’s search algorithm, we will
first point out several properties of HNSW that ACORN’s predicate
subgraphs aim to emulate. In Figure 7 we empirically show that
ACORN’s search performance approximates that of the HNSW
oracle partition index. We will then describe ACORN’s expected
search complexity. We define 𝑙: 𝑋→N to be the mapping of nodes
to there maximum level index in ACORN-𝛾.
6.3.1
Index and Search Properties. Intuitively, for a given query,
ACORN’s predicate subgraph will emulate the HNSW oracle parti-
tion index when the predicate subgraph forms a hierarchical struc-
ture, each node in the subgraph has degree close to 𝑀, the subgraph
has a fixed entrypoint at its maximum level index that we can effi-
ciently find during search, and the subgraph is connected. We will
examine each of these properties separately and consider when they
hold. We also note one main difference between ACORN’s predi-
cate subgraphs and HNSW that arises due to ACORN’s predicate-
agnostic pruning: each level of ACORN approximates a KNN graph,
while each level of HNSW approximates a RNG graph. While this
difference does not affect ACORN’s expected search complexity in
Section 6.3.2, Malkov et al. [48] demonstrated that the RNG-based
pruning empirically improves performance.
Hierarchy. First, we observe that the arbitrary predicate sub-
graph 𝐺(𝑋𝑝) forms a controllable hierarchy similar to the HNSW
oracle partition index built over 𝑋𝑝with parameter 𝑀. This is by
design. ACORN-𝛾’s construction fixes 𝑀, and consequently 𝑚𝐿,
the level normalization constant. As a result, nodes of 𝑋𝑝in the
ACORN-𝛾index are sampled at rates equal to the level probabilities
of the HNSW partition. Ensuring this level sampling holds allows
us to bound the expected greedy search path length at each level
by a constant, 𝑆, as Malkov et al. [48] previously show.
Bounded Degree. Next, we will describe degree bounds, an im-
portant factor that impacts greedy search efficiency and conver-
gence. While HNSW upper bounds the degree of each node by M
during construction, ACORN-𝛾enforces this upper bound during
search. This ensures ACORN’s search performs a constant num-
ber of distance computations per visited node. We now focus our
attention on lower bounding the degree of nodes visited during
ACORN-𝛾’s search over the predicate subgraph.
If a node in the predicate subgraph has degree much lower than
𝑀, this could adversely impact the search convergence and thus
recall. For a dataset and query predicate that exhibit no predicate
clustering, for any node 𝑣in 𝐺(𝑋𝑝),
E

|𝑁𝑙
𝑝(𝑣)|

= |𝑁𝑙(𝑣)| · 𝑠= 𝛾· 𝑀· 𝑠> 𝑀, ∀𝑠> 𝑠𝑚𝑖𝑛
This also holds as a lower bounds for datasets with predicate clus-
tering, in which case 𝑃𝑟(𝑥∈𝑁𝑙𝑝(𝑣)) > 𝑠, ∀𝑥∈𝑁𝑙(𝑣) where 𝑣is
a node in the predicate cluster. Thus we will continue our lower
bound analysis of node degrees under the worst case assumption of
no predicate clustering. Using the binomial concentration inequal-
ity with parameter 𝑠, and union-bounding over the expected search
path length, we show that for the search path P = 𝑣1 −... −𝑣𝑦over
an arbitrary predicate subgraph:
𝑃𝑟
h Ø
𝑣∈P
|𝑁𝑝(𝑣)| ≤(1 −𝛿)𝑀i
≤𝑂log𝑛· exp(−𝛿2𝛾𝑀𝑠/2)
We also analyze the probability that the subgraph traversal gets
disconnected, which we bound by:
𝑃𝑟
h Ø
𝑣∈P
|𝑁𝑝(𝑣)| ≤0i
≤𝑂log𝑛· (1 −𝑠)𝑀·𝛾


## PDF page 8

We see that both bounds decay exponentially in 𝛾.
Fixed Entry-point. Similar to HNSW, ACORN’s search begins
from a fixed entry-point, chosen during construction. This pre-
defined entry-point provides a simple and effective strategy that
is also predicate-independent and robust to variations in query
correlation, as we empirically show in Figure 10.
Intuitively, we expect the search to successfully navigate from
ACORN’s fixed entry-point, 𝑒, to the predicate-subgraph entry-
point, 𝑒𝑝, when we find a node that passes the predicate on an upper
level of the index that is fully connected. In this case, there will
exist a one-hop path from 𝑒to 𝑒𝑝. We consider 𝑒𝑝to be an arbitrary
node that passes a given predicate 𝑝and is on the maximum level of
the predicate subgraph. The index’s neighbor expansion parameter,
𝛾, causes the index’s upper levels to be denser and, specifically
those with less than 𝑀·𝛾nodes, to be fully connected. When these
fully connected levels contain at least one node that passes the
predicate, the search is guaranteed to route from 𝑒to 𝑒𝑝. Since
ACORN samples all nodes with equal probability at each level, the
probability that nodes passing a given predicate, 𝑝, exist on some
level is proportional to the predicate’s selectivity, which takes a
lower bound of 𝑠𝑚𝑖𝑛= 1/𝛾.
Connectivity. We note that neither HNSW nor ACORN provides
theoretical guarantees on connectivity over its level graphs for arbi-
trary datasets. Thus we instead rely primarily on empirical results
for our analysis. However, for some cases, we can expect ACORN’s
predicate subgraph to be connected when the HNSW oracle parti-
tion is connected. Two such cases are when 𝑋𝑝exhibits no predicate
clustering, or 𝑋𝑝is clustered around a single region. In either case,
each node has an expected degree of at least 𝑀and each level ap-
proximates a KNN graph, which is connected when 𝐾>> log𝑛. We
empirically show in Figure 13a that ACORN’s predicate subgraphs
exhibit connectivity for real datasets and hybrid search queries. To
analyze potential connectivity problems, we recommend bench-
marking ACORN’s hybrid search performance against HNSW’s
ANN search performance using equivalent 𝑀and efc parameters. If
a significant gap in accuracy exists, we recommend incrementally
increasing 𝛾from its its initial value of 1/𝑠𝑚𝑖𝑛.
6.3.2
Search Complexity. ACORN-𝛾’s expected search complexity
scales:
𝑂((𝑑+ 𝛾) · log(𝑠· 𝑛) + log(1/𝑠))
This approximates the HNSW oracle partition’s expected search
complexity, 𝑂(𝑑·log(𝑠·𝑛)). Intuitively, ACORN-𝛾’s search path per-
forms some filtering at the upper levels before likely entering and
traversing the predicate sub-graph, during which ACORN incurs a
small overhead compared to HNSW search in order to perform the
predicate filtering step over each neighbor list.
We derive ACORN-𝛾’s search complexity by considering two
stages of its search traversal. In the first stage, search begins from a
pre-defined entry-point 𝑒, which need not pass the query predicate.
In this stage, the search performs filtering only, dropping down
each level on which the filtered neighbor list, 𝑁𝑝(𝑒), is found to be
empty. Once the traversal reaches the first node, 𝑒𝑝that passes the
predicate, it enters the second stage, beginning its traversal over
the predicate subgraph 𝐺(𝑋𝑝).
In stage 1 the greedy search path on each layer has length 1,
and occurs over 𝑂(log𝑛−log(𝑠· 𝑛)) expected levels, yielding the
complexity 𝑂(log(1/𝑠). We see this because the expected maximum
level index of the full ACORN index graph scales 𝑂(log𝑛) based
on its level-assignment probability [48]. Meanwhile, the predicate
subgraph𝐺(𝑋𝑝) of size𝑠·𝑛has an expected maximum level index of
𝑂(log(𝑠· 𝑛)), once again according to its level sampling procedure.
The second stage of the search traverses the predicate subgraph
in expected 𝑂((𝑑+𝛾) · log(𝑠· 𝑛)) complexity. As we previously de-
scribe, the expected maximum level index of the predicate subgraph
scales 𝑂(log(𝑠· 𝑛)). At each level, the expected greedy path length
can be bounded by a constant 𝑆due to the index level sampling pro-
cedure employed during construction. For each node visited along
the greedy path, we perform distance computations in 𝑂(𝑑) time
on at most 𝑀neighbors, and perform a constant-time predicate
evaluations over at most 𝑀· 𝛾neighbors.
7
EVALUATION
We evaluate ACORN through a series of experiments on real and
synthetic datasets. Overall, our results show the following:
• ACORN-𝛾achieves state-of-the-art hybrid search perfor-
mance, outperforming existing methods by 2-1,000× higher
QPS at 0.9 recall on both prior benchmark datasets with
simple, low-cardinality predicate sets, and more complex
datasets with high-cardinality predicate sets. Specifically,
ACORN achieves 2-10× higher QPS on prior benchmarks,
over 30× higher QPS on new benchmarks, and over 1,000x
higher QPS at scale on a 25-million-vector dataset.
• ACORN-𝛾and ACORN-1 are predicate-agnostic methods,
providing robust search performance under variations in
predicate operators, predicate selectivity, query correlation,
and dataset size.
• ACORN-1 and ACORN-𝛾exhibit trade-offs between search
performance and construction overhead. While ACORN-
𝛾achieves up to 5× higher QPS than ACORN-1 at fixed
recalls, ACORN-1 can be constructed with 9-53× lower
time-to-index (TTI).
We now discuss our results in detail. We first describe the datasets
(7.1) and baselines (7.2) we use. Then, we present a systematic
evaluation of ACORN’s search peformance (7.3). Finally, we assess
ACORN’s construction efficiency (7.4). We run all experiments on
an AWS m5d.24xlarge instance with 370 GB of RAM, 96 vCPUs,
and 196 threads.
7.1
Datasets
We conduct our experiments on two datasets with low-cardinality
predicate sets (LCPS) and two datasets with high-cardinality predi-
cate sets (HCPS). The LCPS datasets allow us to benchmark prior
works that only support a constrained set of query predicates. The
HCPS datasets consist of more complex and realistic query work-
loads, allowing us to more rigorously evaluate ACORN’s search
performance. Table 2 provides a concise summary of all datasets.
3On the TripClick dataset, we create two distinct query workloads, described in Section
7.1.2. The average selectivity for either workload is .17 (keywords), and .26 (dates).
3On the LAION dataset, we create four distinct query workloads, described in Section
7.1.2. These workloads have average selectivities of .10 (no-cor), .13 (pos-cor), .069
(neg-cor), .056 (regex).


## PDF page 9

ACORN: Search Over Vector Embeddings and Structured Data
Table 2: Datasets
Base Data
Query Workload
# Vectors
Vector
Dim
Vector Source
Data
Structured Data
Predicate Operators
Avg. Query
Selectivity
Predicate
Cardinality
SIFT1M
1,000,000
128
images
random int.
equals(𝑦)
0.083
12
Paper
2,029,997
200
passages
random int.
equals(𝑦)
0.083
12
TripClick
1,055,976
768
passages
clinical area list &
publication date
contains(𝑦1 ∨𝑦2 ∨...) &
between(𝑦1, 𝑦2)
0.17, 0.36 2
> 108
LAION (1M)
1,000,448
512
images
text captions &
keyword list
regex-match(𝑦) &
contains(𝑦1 ∨𝑦2 ∨...)
0.056 - 0.13 3
> 1011
LAION (25M)
24,653,427
512
same as above
same as above
same as above
same as above
same as above
7.1.1
Datasets with Low Cardinality Predicate Sets. We use SIFT1M
[35] and Paper [63], the two largest publically-available datasets
used to evaluate recent specialized indices [25, 63]. For both datasets,
we follow related works [25, 62, 63] to generate structured attributes
and query predicates: for each base vector, we assign a random
integer in the range 1 −12 to represent structured attributes; and
for each query vector, the associated query predicate performs an
exact match with a randomly chosen integer in the attribute value
domain. The resulting query predicate set has a cardinality of 12.
SIFT1M: The SIFT1M dataset was introduced by Jegou et al. in
2011 for ANN search. It consists of a collection of 1M base vectors,
and 10K query vectors. All of the vectors are 128-dimensional local
SIFT descriptors [43] from INRIA Holidays images [33].
Paper: Introduced by Wang et al. in 2022, the Paper dataset con-
sists of about 2M base vectors and 10K query vectors. The dataset
is generated by extracting and embedding the textual content from
an in-house corpus of academic papers.
7.1.2
Datasets with High Cardinality Predicate Sets. We use the
TripClick and LAION datasets in our experiments with HCPS datasets.
TripClick: The TripClick dataset, introduced by Rekabsaz et al.
in 2021 for text retrieval, contains a real hybrid search query work-
load and base dataset from the click logs of a health web search
engine. Each query consists of natural language search terms along
with optional filters on clinical areas (e.g. "cardiology", "infectious
disease", "surgery") and publication years. Each entity in the base
dataset consists of a text passage, with a list of associated clinical
areas and a publication date. The dataset contains 28 unique clinical
areas and publication dates ranging from 1900 to 2020, resulting in
over 228 possible query predicates total. We construct two query
workloads, one consisting of queries that used date filters (dates)
and another consisting of queries that used clincal area filters (ar-
eas). We generate 768-dimensional vectors from the query texts and
passage texts using DPR [36], a widely-used, pre-trained encoder
for open-domain Q&A. The resulting dataset has about 1𝑀base
vectors, and we use a random sample of 1K queries for each query
workload.
LAION: The LAION dataset [55] consists of 400M image embed-
dings and captions describing each image. The vector embeddings
are generated from web-scraped images using CLIP [53], a multi-
modal language-vision model. In our evaluation, we construct two
base datasets using 1M and 25M LAION subsets, both consisting of
image vectors and text captions as a structured attribute. We also
generate an additional structured attribute consisting of a keyword
list. We assign each image embedding its keyword list by taking the
3 words with highest text-to-image CLIP scores from a candidate
list of 30 common adjectives and nouns (e.g., "animal", "scary").
To evaluate a series of micro-benchmarks, we generate four
query workloads. For each query workload, we sample 1K vectors
from the dataset as query vectors. We construct the regex query
workload with predicates that perform regex-matching over the im-
age captions. For each query predicate, we randomly choose strings
of 2-10 regex tokens (e.g., "^[0-9]"). In addition, we construct three
query workloads with predicates, similar to TripClick, that take
a keyword list and filter out entities that do not have at least one
matching keyword. Using this setup, we are able to easily control
for correlation in the workload, and we generate a no correlation
(no-cor), positive correlation (pos-cor), and negative correlation
(neg-cor) workload. Figure 6 demonstrates some example queries
and multi-modal retrieval results taken from each.
7.2
Benchmarked Methods
We briefly overview the methods we benchmark along with tested
parameters. We implement ACORN-𝛾, ACORN-1, pre-filtering, and
HNSW post-filtering in C++ in the FAISS codebase [5].
HNSW Post-filtering: To implement HNSW post-filtering, for
each hybrid query with predicate selectivity 𝑠, we over-search the
HNSW index, gathering 𝐾/𝑠candidate results before applying the
query filter. We note that this differes to some prior work [25],
where HNSW post-filtering is implemented by collecting only 𝐾
candidate results, leading to significantly worse baseline query
performance than ours. For the SIFT1M, Paper and LAION datasets,
we use the FAISS’s default HNSW construction parameters: 𝑀=
32, efc = 40. For the TripClick dataset, we find that the HNSW
index for these parameters is unable to obtain high recalls for the
standard ANN search task, thus we perform parameter tuning, as
is standard. We perform a grid search for 𝑀∈{32, 64, 128} and
efc ∈{40, 80, 120, 160, 200} and choose the pair the obtains the
highest QPS at 0.9 Recall for ANN search. For TripClick, we choose
𝑀= 128, efc = 200. We generate each recall-QPS curve by varying
the search parameter efs from 10 to 800 in step sizes of 50.
Pre-filtering: We implement pre-filtering by first generating a list
of dataset entries that pass the query predicate and then perform-
ing brute force search using FAISS’s optimized implementation for
distance comparisons. We also efficiently implement all contains
predicate evaluations using bitsets since the corresponding struc-
tured attributes have low cardinality.


## PDF page 10

Figure 6: The figure contrasts retrieval results using vector-only similarity search (bottom left) versus hybrid search (right) on the LAION
dataset. Both use the same query image (top left), and the hybrid search queries also include a structured query filter consisting of a keyword
list, here containing a single keyword. The table on the right shows examples from three hybrid search query workloads: positive query
correlation (top), no query correlation (middle), and negative query correlation (bottom).
Filtered-DiskANN: We evaluate both algorithms implemented in
FilteredDiskANN [4], namely FilteredVamana and StitchedVamana.
For both, we follow the recommended construction and search
parameters according to the hyper-parameter tuning procedure
described by Gollapudi et al. [25]. For FilteredVamana, we use
construction parameters 𝐿= 90, 𝑅= 96, which generated the
Pareto-Optimal recall-QPS curve from a parameter sweep over 𝑅∈
{32, 64, 96} and L between 50 and 100. For StitchedVamana, we use
construction parameters 𝑅𝑠𝑚𝑎𝑙𝑙= 32, 𝐿𝑠𝑚𝑎𝑙𝑙= 100, 𝑅𝑠𝑡𝑖𝑡𝑐ℎ𝑒𝑑= 64
and 𝛼= 1.2, which generated the Pareto-Optimal recall-QPS curve
from a parameter sweep over 𝑅𝑠𝑚𝑎𝑙𝑙, 𝑅𝑠𝑡𝑖𝑡𝑐ℎ𝑒𝑑∈{32, 64, 96} and
𝐿𝑠𝑚𝑎𝑙𝑙between 50 and 100. To generate the recall-QPS curves we
vary 𝐿from 10 to 650 in increments of 20 for FilteredVamana, and
𝐿𝑠𝑚𝑎𝑙𝑙from 10 to 330 in increments of 20 for StitchedVamana.
NHQ: We evaluate the two algorithms, NHQ-NPG_NSW and
NHQ-NPG_KGraph, proposed in [63]. For both we use the recom-
mended parameters in the released codebase [12]. These parameters
were selected using a hyperparameter grid search in order to gener-
ate the Pareto-optimal recall-QPS curve for either algorithm on the
SIFT1M and Paper datasets. We generate the recall-QPS curve by
varying 𝐿between 10 and 310 in steps of 20. In Figures 8b and 7b,
we show the query performance of KGraph, the more performant
of the two algorithms.
Milvus: We test four Milvus algorithms: IVF-Flat, IVF-SQ8, HNSW,
and IVF-PQ [6]. For each we test the same parameters as Gollapudi
et al. [25]. Since we find that the four Milvus algorithms achieve
similar search performance, for simplicity, Figures 8b and 7b show
only the method with Pareto-Optimal recall-QPS performance.
Oracle Partition Index: We implement this method by construct-
ing an HNSW index for each possible query predicate in the LCPS
datasets. For a given hybrid query, we search the HNSW partition
corresponding to the query’s predicate. To construct each HNSW
partition and generate the recall-QPS curve, we use the same pa-
rameters as the HNSW post-filtering method, described above.
ACORN-𝛾: We choose the construction parameters 𝑀and efc
to be the same as the HNSW post-filtering baseline, described
above. We find that ACORN-𝛾’s search performance is relatively in-
sensitive to the choice of the construction parameter 𝑀𝛽, as Figure
12c shows. Thus, to maintain modest construction overhead, we
choose 𝑀𝛽to be a small multiple of 𝑀, i.e., 𝑀𝛽= 𝑀or 𝑀𝛽= 2𝑀,
picking the parameter for each dataset that obtains higher QPS at
0.9 Recall. Specifically, we constrain the memory budget of the index
to be no larger than the Vamana indices on the LCPS datasets and
no larger than twice the size of the flat indices for HCPS datasets.
We use 𝑀𝛽values of 32 for LAION-1M and LAION-25M, 64 for
SIFT1M, Paper, and 128 for TripClick. We choose the construction
parameter 𝛾according to the expected minimum selectivity query
predicates of each dataset i.e., 𝛾= 12 for SIFT1M and Paper, 𝛾= 30
for LAION, and 𝛾= 80 for TripClick. To generate the recall-QPS
curve, we follow the same procedure described above for HNSW
post-filtering.
ACORN-1: We construct ACORN-1 and generate the recall-QPS
curve following the same procedure we use for ACORN-𝛾, except
that we fix 𝛾= 1 and 𝑀𝛽= 𝑀.
7.3
Search Performance Results
We will begin our evaluation with benchmarks on the LCPS datasets,
on which we are able to run all baseline methods as well as the
oracle partition method. We will then present an evaluation on the
HCPS datasets. On these datasets, the FilteredDiskANN and NHQ
algorithms fail because they assume are unable to handle the high
cardinality query predicate sets and non-equality predicate oper-
ators. As of this writing, we also find that Milvus cannot support
regex-match predicates and contains predicates over variable
length lists. As a result, we instead focus on comparing ACORN
to the pre- and post-filtering baselines for the HCPS datasets. We
report QPS averaged over 50 trials.
7.3.1
Benchmarks on LCPS Datasets. Figure 7 shows that ACORN-
𝛾achieves state-of-the-art hybrid search performance and best
approximates the theoretically ideal oracle partition strategy on
the SIFT1M and Paper datasets. Notably, even compared to NHQ
and FilteredDiskANN, which specialize for LCPS datasets, ACORN-
𝛾consistently achieves 2-10× higher QPS at fixed recall values,
while maintaining generality. Additionally, we see ACORN-1 ap-
proximates ACORN-𝛾’s search performance, attaining about 1.5-5×
lower QPS than ACORN-𝛾across a range of recall values.
To further investigate the relative search efficiency of ACORN-
𝛾and ACORN-1, we turn our attention to Table 3, which shows
the number of distance computations required of either method
to obtain Recall@10 equal to 0.8. We see that the oracle partition
method is the most efficient, requiring the fewest number of dis-
tance computations on both datasets. ACORN-𝛾is the next most
efficient according to number of distance computations. While


## PDF page 11

ACORN: Search Over Vector Embeddings and Structured Data
(a) SIFT1M Dataset
(b) Paper Dataset
Figure 7: Recall@10 vs QPS on SIFT1M and Paper
ACORN-𝛾approximates the oracle partition method, it’s predicate-
agnostic design precludes the same RNG-based pruning used to
construct the oracle partitions. Rather than approximating RNG-
graphs, ACORN-𝛾’s levels approximate KNN-graphs, which are less
efficient to search over explaining the performance gap. The table
additionally shows that ACORN-1 is less efficient than ACORN-
𝛾, which is explained by the candidate edge generation used in
ACORN-1. While the ACORN-𝛾index stores up to 𝑀× 𝛾edges
per node during construction, ACORN-1 stores only up to 𝑀edges
per node during construction, and approximates an edge list of size
𝑀∗𝛾for each node during search using its neighbor expansion
strategy. This approximation results in slight degradation to neigh-
bor list quality and thus search performance. Finally, we see from
the table, that HNSW post-filtering is the least efficient of the listed
methods. This is because while ACORN-1 and ACORN-𝛾almost
exclusively traverse over nodes that pass the query predicates, the
post-filtering algorithm is less discriminating and wastes distance
computations on nodes failing the query predicate.
Returning to Figure 7, we see that the relative search efficiency,
measured by QPS versus recall, of the oracle partition method,
ACORN-𝛾, and ACORN-1 is not only affected by distance compu-
tations, but is also affected by vector dimensionality. We see that
both ACORN-1 and ACORN-𝛾perform closer to the oracle partition
method on the Paper dataset, while the performance gap grows
slightly on SIFT1M. This is due to the cost of performing the fil-
tering step over neighbor lists during search, which, relative to
the cost of distance computations, is higher on SIFT1M than Paper
since SIFT1M uses slightly lower-dimensional vectors.
7.3.2
Benchmarks on HCPS Datasets. Figure 8 shows that ACORN
outperforms the baselines by 30 −50× higher QPS at 0.9 recall on
TripClick and LAION-1M, and as before, ACORN-1 approximates
ACORN-𝛾’s search performance. On both datasets, pre-filtering is
prohibitively expensive, obtaining perfect recall at the price of effi-
ciency. Meanwhile, post-filtering fails to obtain high recall, likely
Table 3: # Distance Computations to Achieve 0.8
Recall
SIFT 1M
Paper
Oracle Partition
398.0
281.1
ACORN-𝛾
611.0 (+53.5%)
383.7 (+36.6%)
ACORN-1
999.6 (+151.0%)
567.8 (+101.2%)
HNSW Post-filter
1837.8 (+362.6%)
1425.5 (+406.2%)
* Percentage difference is shown in parenthesis and is relative to
oracle partition method
(a) TripClick (areas)
(b) TripClick (dates) (c) LAION1M (regex)
Figure 8: Recall@10 vs QPS on TripClick and LAION-1M
due to the presence of varied query correlation and predicate selec-
tivity, which we further explore further next.
Varied Predicate Selectivity: We use the Tripclick dataset to eval-
uate ACORN’s search performance across a range of realistic pred-
icate selectivities. Figure 9 demonstrates that for each predicate
selectivity percentile, ACORN-𝛾achieves 5-50x higher QPS at 0.9
recall compared to the next best-performing baseline. Once again
ACORN-1 trails behind ACORN-𝛾. We see that for low selectiv-
ity predicates, the pre-filtering method is most competitive, while
the post-filtering baselines suffers from over 10× lower QPS than
ACORN at fixed recall. However, for high selectivity predicates, pre-
filtering becomes less competitive while the post-filtering baseline
obtains higher throughput, although its recall remains low.
Varied Query Correlation: Next we control for query correlation
and evaluate ACORN on three different query workloads using
the LAION-1M dataset. Figure 10 demonstrates that ACORN-𝛾
is robust to variations in query correlation and attains 28-100×
higher QPS at 0.9 recall than the next best baseline in each case. In
the negative correlation case, the performance gap between post-
filtering and the ACORN methods is the largest since post-filtering
cannot successfully route towards nodes that pass the predicate. In
the positive correlation case, ACORN-𝛾once again outperforms the
baselines, but post-filtering become more competitive, although it
is still unable to attain recall above 0.9. The pre-filtering method’s
QPS remains relatively unchanged, and is only affected by small
variations in predicate selectivity for each query workload. As
before, ACORN-1 approaches ACORN-𝛾’s search performance.
Scaling Dataset Size: Figure 11 shows ACORN’s search perfor-
mance on LAION-25M with the no-correlation query workload,
demonstrating that the performance gap between ACORN and ex-
isting baselines only grows as the dataset size scales. At 0.9 recall,
ACORN-𝛾achieves over three orders of magnitude higher QPS than
the next best-performing baseline. As before, ACORN-1’s search
performance approximates that of ACORN-𝛾.
7.4
Index Construction
We will now evaluate ACORN’s construction procedure, includ-
ing its indexing time and space footprint, ACORN-𝛾’s compres-
sion procedure, and the predicate subgraph quality resulting from
ACORN-𝛾’s neighbor expansion approach.
7.4.1
TTI and Space Footprint. First, we analyze ACORN’s space
footprint and indexing time. Table 4 and 5 show the time-to-index
and index size of ACORN-𝛾and ACORN-1 compared to the best-
performing baselines. The reported index sizes for each method
show the total space footprint of both vector storage and the index


## PDF page 12

(a) 1p Sel (s=0.0127)
(b) 25p Sel (s=0.0485)
(c) 50p Sel (s=0.1215)
(d) 75p Sel (s=0.2529)
(e) 99p Sel (s=0.6164)
Figure 9: Recall@10 vs QPS for Varied Selectivity Query Filters on TripClick
(a) Neg. Correlation
(b) No Correlation
(c) Pos. Correlation
Figure 10: Recall@10 vs QPS on LAION1M
Figure 11: Recall@10 vs QPS on LAION-25M
itself. All methods are measured using the parameters reported in
Section 7.2.
We first consider ACORN-𝛾’s construction overhead. Table 4
shows that across all datasets, ACORN-𝛾’s TTI is at most 11× higher
than HNSW’s, and at most 2.15× higher than that of StitchedVa-
mana, the best performing specialized index. Table 5 shows that
ACORN-𝛾’s index size is at most 1.3× larger than that of HNSW,
and at least 25% smaller than that of StitchedVamana. The reason
for ACORN-𝛾’s increased index size and TTI compared to HNSW
is it’s candidate-edge generation step during construction, which
expands each neighbor list. Meanwhile, ACORN-1 achieves the low-
est TTI of all listed baselines in table 4, and its index size is at most
1.25× HNSW’s index size and at least 25% smaller than StitchedVa-
mana’s index size. We see that while ACORN-𝛾achieves superior
search performance by leveraging a neighbor-list expansion during
construction, ACORN-1 provides a close approximation at lower
TTI and space footprint by instead performing the neighbor-list
expansion during search. The two algorithms exhibit a trade-off
between search performance and construction overhead.
7.4.2
ACORN-𝛾Pruning. Given ACORN-𝛾’s higher construction
overhead, we investigate the efficiency of its predicate-agnostic
compression strategy in reducing index construction costs while
maintaining search performance. First, Table 6 shows ACORN-
𝛾’s average out-degree per level for each dataset, confirming that
compression on level 0 leads to significantly smaller neighbor lists,
compared to level without compression, which may have neighbor
lists as large as 𝑀· 𝛾.
Turning our attention to Figure 12, we evaluate three different
pruning strategies applied to ACORN-𝛾’s neighbor lists during
Table 4: TTI (s)
TripClick
LAION-1M
LAION-25M
Sift1M
Paper
ACORN-𝛾
9902.9
835.8
38,007.5
148.9
255.6
ACORN-1
322.9
25.9
705.3
8.6
27.0
HNSW
891.0
32.9
1,147.2
11.3
29.2
FilteredVamana
NA
NA
NA
18.3
51.9
StitchedVamana
NA
NA
NA
69.2
189.7
Table 5: Index Size (GB)
TripClick
LAION-1M
LAION-25M
Sift1M
Paper
ACORN-𝛾
4.9
2.4
59
0.98
2.5
ACORN-1
4.6
2.3
59
0.93
2.4
HNSW
4.1
2.2
54
.75
2.1
Flat Index
3.1
1.9
47
.51
1.6
FilteredVamana
NA
NA
NA
.61
1.8
StitchedVamana
NA
NA
NA
1.3
3.5
Table 6: ACORN-𝛾Average Out Degree
TripClick
LAION-1M
LAION-25M
Sift1M
Paper
Level 0 (compressed)
191
50.1
49.4
87.5
86.0
Level 1
8,075
960
960
384
384
Level 2
54.0
919
937
363
359
Level 3
0
25.3
689
25.3
57.4
Level 4
NA
0
16
0
1.0
𝑀· 𝛾
10,240
960
960
384
384
𝑀𝛽
128
32
32
64
64
construction: i) ACORN’s predicate-agnostic pruning strategy at
varied levels of compression indicated by different 𝑀𝛽(Mb) values,
where 𝑀𝑏= 768 represents no pruning, and lower values represent
more aggressive pruning, ii) a metadata-aware RNG-based pruning
approach, which is employed by FilteredDiskANN’s algorithms,
and iii) HNSW’s metadata-blind pruning. We consider TTI, space
footprint, the number of candidates edges pruned per node and
search performance. The figure represents space footprint measured
by the average out degree of nodes on on level 0, the level on which
each pruning strategy is applied. In addition, the figure shows
search performance measured by recall at 20,000 QPS. We note that
the recall ranges of the recall-QPS curve generated by different
pruning methods varied significantly, leading us to choose a QPS
threshold rather than a recall threshold. Interestingly, Figure 12
shows that ACORN’s pruning can significantly reduce both the TTI
and space footprint by aggressively pruning candidate edges, while
maintaining search performance. In comparison, applying HNSW
pruning to the index results in significantly degraded hybrid search
performance. Meanwhile the metadata-aware RNG-base pruning
results in similar search performance to ACORN-𝛾’s pruning, but it
is less efficient by TTI and space footprint than ACORN’s pruning
for small values of 𝑀𝛽(e.g., 𝑀𝛽= 32, 64).


## PDF page 13

ACORN: Search Over Vector Embeddings and Structured Data
(a) TTI
(b) Space footprint
(c) # Edges Pruned
(d) Search Perf.
Figure 12: Comparison of pruning methods on SIFT1M and their
impact on TTI (a), space footprint of the index (b), the number of
candidate edges pruned (c) and search performance (d). 𝑀𝛽values
used for ACORN-𝛾are shown along the x-axis.
7.4.3
Graph Quality. Finally, we investigate the graph quality of
ACORN-𝛾’s predicate subgraphs. Figure 13 compares graph connec-
tivity, graph height, and out degrees for HNSW oracle partitions and
ACORN-𝛾predicate subgraphs across varied predicate selectivities
on the TripClick dataset’s real hybrid search queries.
From Figure 13a, we see ACORN-𝛾’s predicate-subgraph con-
nectivity empirically matches or exceeds that of the HNSW oracle
partition across selectivities, demonstrating the effectiveness of
ACORN-𝛾’s neighbor expansion strategy. Next, Figure 13b shows
that the controlled hierarchy of ACORN-𝛾’s predicate subgraphs
emulate that of the HNSW oracle partitions. Malkov et al. show
that HNSW search performance is sensitive to graph height [48];
thus, this result helps explain ACORN-𝛾’s ability to emulate the
search efficiency of the oracle partition. Lastly, Figure 13c examines
the average out degree resulting from performing the search-time
filtering, described in Figure 4(a), over the ACORN-𝛾index. We note
that sufficiently high, but bounded, out-degrees are important for
emulating HNSW’s navigability properties, as discussed in Section
6.3. The figure confirms that ACORN’s predicate subgraphs have
average out-degrees consistently close to and bounded by 𝑀. As
expected, the HNSW oracle partition has significantly lower aver-
age out-degrees than nodes on ACORN-𝛾’s uncompressed levels
because HNSW applies RNG-based pruning. We also note, that the
ACORN predicate subgraph with 1 percentile selectivity has lower
average out degrees than the other predicate subgraphs because
the low selectivity predicates result in fewer than 128 nodes on the
largest uncompressed levels, thus capping the maximum out degree
per node below 𝑀= 128. Overall, we observe that ACORN-𝛾pro-
duces high quality predicate subgraphs, which empirically emulate
several HNSW properties related to search efficiency.
8
RELATED WORK
Pre- & Post-filtering-based Systems. Many hybrid search sys-
tems rely on pre- and post-filtering. While several systems have
developed pre-processing methods to perform faster filtering dur-
ing search, these systems fail to reduce the excessive and expensive
distance computations which bottleneck performance. Weaviate [1]
creates an inverted index for structured data ahead of time, then
uses it at query time to create a bitmap of eligible candidates during
(a) # SCC
(b) Graph Height
(c) Avg Out Degree
Figure 13: Graph quality of ACORN-𝛾predicate subgraph evalu-
ated by (a) average number of strongly connected components per
level, (b) graph height, and (c) average out degree of nodes across
uncompressed levels. Results are shown for the TripClick dataset
with 1, 25, 50, 75, and 99 percentile selectivity predicates to generate
the predicate subgraph and HNSW oracle partition.
post-filtering. Milvus [62] likewise creates an approved list of points
by maintaining a distribution of attributes over the dataset in order
to map commonly used query filters to a list of approved points
before performing pre- or post-filtering. Several space-partitioning
indices like FAISS-IVF [14, 34] and LSH [10] store metadata infor-
mation in the index, allowing them to rapidly filter entities during
post-filtering. Despite the optimized filtering steps in each of these
approaches, the core problems of pre- and post-filtering remain,
particularly for low correlation or selectivity predicates.
Specialized Indices. Alternatively, several recent works develop
novel graph-based algorithms for hybrid search, often improving
performance for a constrained set of predicates. NHQ [63] encodes
attributes alongside vectors, and then uses a "fusion distance" dur-
ing search that accounts for vector distances as well as attribute
matches. This approach supports only equality query predicates
and assumes each dataset entity has only one structured attribute.
Filtered-DiskANN [25] proposes two algorithms: FilteredVamana
and StitchedVamana. Both methods constrain the query filter cardi-
nality to about 1, 000 with only equality predicates so that the index
construction steps can use this knowledge to appropriately generate
and prune candidate edge lists. Similarly HQI [49] optimizes batch
query-processing by assuming a limited cardinality of 20 query
predicates to design an efficient partitioning scheme. On the other-
hand, Qdrant [61] proposes to densify an HNSW graph and perform
a filtered greedy search. While this approach aligns intuitively with
ACORN’s neighbor-list expansions during construction, Qdrant’s
proposal inadvertently flattens the graph by directly increasing the
HNSW parameter 𝑀, which impacts HNSW’s level normalization
constant. Malkov et al. show that HNSW’s performance is sensitive
to its number of levels, and flattening the graph degrades search
performance [48]. In addition, Qdrant’s proposed method does not
provide a solution for dealing with the increased memory overhead
after creating a denser HNSW.
9
CONCLUSION
We proposed ACORN, the first approach for efficient hybrid search
across vectors and structured data that supports large and diverse
sets of query predicates. ACORN uses a simple, yet effective, search
strategy based on the core idea of predicate subgraph traversal. We
presented two indices, ACORN-𝛾and ACORN-1, that implement
this search strategy by modifying the HNSW indexing algorithm.
Our results show that ACORN achieves state-of-the-art hybrid
search performance on both prior benchmarks, involving simple,


## PDF page 14

low-cardinality query predicate sets, as well as more complex bench-
marks involving new predicate operators and high cardinality pred-
icate sets. Across both types of benchmarks, ACORN-𝛾achieves
2–1,000× higher QPS at 0.9 recall than prior methods, and ACORN-
1 approximates ACORN-𝛾’s search performance with 9–53× lower
TTI for resource-constrained settings.
ACKNOWLEDGMENTS
The authors would like to thank Peter Bailis for his valuable feed-
back on this work.
This research was supported in part by affiliate members and
other supporters of the Stanford DAWN project, including Meta,
Google, and VMware, as well as Cisco, SAP, and a Sloan Fellow-
ship. Any opinions, findings, and conclusions or recommendations
expressed in this material are those of the authors and do not nec-
essarily reflect the views of the sponsors.
REFERENCES
[1] [n. d.]. Filtered Vector Search | Weaviate - vector database. https://weaviate.io/
developers/weaviate/concepts/prefiltering
[2] [n. d.]. Pre-label and enrich data with bulk classifications. https://labelbox.ghost.
io/blog/pre-label-and-enrich-your-data-with-bulk-classifications/
[3] [n. d.]. Q&A over Documents - LlamaIndex 0.8.43. https://gpt-index.readthedocs.
io/en/latest/
[4] 2023. DiskANN. https://github.com/microsoft/DiskANN original-date: 2020-06-
18T06:18:06Z.
[5] 2023. Faiss. https://github.com/facebookresearch/faiss
[6] 2023.
Milvus Documentation.
https://github.com/milvus-io/milvus-docs
original-date: 2020-05-27T09:12:23Z.
[7] 2023. visual-layer/fastdup. https://github.com/visual-layer/fastdup
[8] Ann Arbor Algorithms. 2023. KGraph: A Library for Approximate Nearest
Neighbor Search.
https://github.com/aaalgo/kgraph original-date: 2015-05-
29T12:38:24Z.
[9] Alexandr Andoni and Piotr Indyk. 2008. Near-optimal hashing algorithms for
approximate nearest neighbor in high dimensions. Commun. ACM 51, 1 (Jan.
2008), 117–122. https://doi.org/10.1145/1327452.1327494
[10] Alexandr Andoni, Piotr Indyk, Thijs Laarhoven, Ilya Razenshteyn, and Ludwig
Schmidt. 2015. Practical and optimal LSH for angular distance. In Proceedings
of the 28th International Conference on Neural Information Processing Systems -
Volume 1 (NIPS’15). MIT Press, Cambridge, MA, USA, 1225–1233.
[11] Alexandr Andoni and Ilya Razenshteyn. 2015. Optimal Data-Dependent Hashing
for Approximate Near Neighbors. In Proceedings of the forty-seventh annual ACM
symposium on Theory of Computing (STOC ’15). Association for Computing Ma-
chinery, New York, NY, USA, 793–801. https://doi.org/10.1145/2746539.2746553
[12] AshenOn3. 2023. NHQ: An Efficient and Robust Framework for Approximate
Nearest Neighbor Search with Attribute Constraint.
https://github.com/
AshenOn3/NHQ original-date: 2021-09-09T08:28:21Z.
[13] Martin Aumüller, Erik Bernhardsson, and Alexander Faithfull. 2020. ANN-
Benchmarks: A benchmarking tool for approximate nearest neighbor algorithms.
Information Systems 87 (Jan. 2020), 101374. https://doi.org/10.1016/j.is.2019.02.
006
[14] Dmitry Baranchuk, Artem Babenko, and Yury Malkov. 2018. Revisiting the
Inverted Indices for Billion-Scale Approximate Nearest Neighbors.
https:
//doi.org/10.48550/arXiv.1802.02422 arXiv:1802.02422 [cs].
[15] Jon Louis Bentley. 1975. Multidimensional binary search trees used for associative
searching. Commun. ACM 18, 9 (Sept. 1975), 509–517. https://doi.org/10.1145/
361002.361007
[16] Erik Bernhardsson. [n. d.].
annoy: Approximate Nearest Neighbors in
C++/Python optimized for memory usage and loading/saving to disk.
https:
//github.com/spotify/annoy
[17] Alina Beygelzimer, Sham Kakade, and John Langford. 2006. Cover trees for
nearest neighbor. In Proceedings of the 23rd international conference on Machine
learning (ICML ’06). Association for Computing Machinery, New York, NY, USA,
97–104. https://doi.org/10.1145/1143844.1143857
[18] Fedor Borisyuk, Siddarth Malreddy, Jun Mei, Yiqun Liu, Xiaoyi Liu, Piyush Ma-
heshwari, Anthony Bell, and Kaushik Rangadurai. 2021. VisRel: Media Search at
Scale. In Proceedings of the 27th ACM SIGKDD Conference on Knowledge Discovery
& Data Mining (KDD ’21). Association for Computing Machinery, New York, NY,
USA, 2584–2592. https://doi.org/10.1145/3447548.3467081
[19] Sanjoy Dasgupta and Yoav Freund. 2008. Random projection trees and low
dimensional manifolds. In Proceedings of the fortieth annual ACM symposium on
Theory of computing. ACM, Victoria British Columbia Canada, 537–546. https:
//doi.org/10.1145/1374376.1374452
[20] Wei Dong, Charikar Moses, and Kai Li. 2011. Efficient k-nearest neighbor graph
construction for generic similarity measures. In Proceedings of the 20th interna-
tional conference on World wide web (WWW ’11). Association for Computing Ma-
chinery, New York, NY, USA, 577–586. https://doi.org/10.1145/1963405.1963487
[21] Ming Du, Arnau Ramisa, Amit Kumar K C, Sampath Chanda, Mengjiao Wang,
Neelakandan Rajesh, Shasha Li, Yingchuan Hu, Tao Zhou, Nagashri Laksh-
minarayana, Son Tran, and Doug Gray. 2022. Amazon Shop the Look: A Vi-
sual Search System for Fashion and Home. In Proceedings of the 28th ACM
SIGKDD Conference on Knowledge Discovery and Data Mining (KDD ’22). As-
sociation for Computing Machinery, New York, NY, USA, 2822–2830.
https:
//doi.org/10.1145/3534678.3539071
[22] Cong Fu, Chao Xiang, Changxu Wang, and Deng Cai. 2019. Fast approximate
nearest neighbor search with the navigating spreading-out graph. Proceedings
of the VLDB Endowment 12, 5 (Jan. 2019), 461–474.
https://doi.org/10.14778/
3303753.3303754
[23] Tiezheng Ge, Kaiming He, Qifa Ke, and Jian Sun. 2014. Optimized Product
Quantization. IEEE Transactions on Pattern Analysis and Machine Intelligence 36,
4 (April 2014), 744–755. https://doi.org/10.1109/TPAMI.2013.240 Conference
Name: IEEE Transactions on Pattern Analysis and Machine Intelligence.
[24] Aristides Gionis, Piotr Indyk, and Rajeev Motwani. 1999. Similarity Search in
High Dimensions via Hashing. In Proceedings of the 25th International Conference
on Very Large Data Bases (VLDB ’99). Morgan Kaufmann Publishers Inc., San
Francisco, CA, USA, 518–529.
[25] Siddharth Gollapudi, Neel Karia, Varun Sivashankar, Ravishankar Krishnaswamy,
Nikit Begwani, Swapnil Raz, Yiyong Lin, Yin Zhang, Neelam Mahapatro, Premku-
mar Srinivasan, Amit Singh, and Harsha Vardhan Simhadri. 2023. Filtered-
DiskANN: Graph Algorithms for Approximate Nearest Neighbor Search with
Filters. In Proceedings of the ACM Web Conference 2023. ACM, Austin TX USA,
3406–3416. https://doi.org/10.1145/3543507.3583552
[26] Long Gong, Huayi Wang, Mitsunori Ogihara, and Jun Xu. 2020. iDEC: indexable
distance estimating codes for approximate nearest neighbor search. Proceedings
of the VLDB Endowment 13, 9 (May 2020), 1483–1497. https://doi.org/10.14778/
3397230.3397243
[27] Ruiqi Guo, Philip Sun, Erik Lindgren, Quan Geng, David Simcha, Felix Chern, and
Sanjiv Kumar. 2020. Accelerating large-scale inference with anisotropic vector
quantization. In Proceedings of the 37th International Conference on Machine
Learning (ICML’20, Vol. 119). JMLR.org, 3887–3896.
[28] Michael E. Houle and Michael Nett. 2015. Rank-Based Similarity Search: Reduc-
ing the Dimensional Dependence. IEEE Transactions on Pattern Analysis and
Machine Intelligence 37, 1 (Jan. 2015), 136–150. https://doi.org/10.1109/TPAMI.
2014.2343223 Conference Name: IEEE Transactions on Pattern Analysis and
Machine Intelligence.
[29] Piotr Indyk and Rajeev Motwani. 1998. Approximate nearest neighbors: towards
removing the curse of dimensionality. In Proceedings of the thirtieth annual
ACM symposium on Theory of computing (STOC ’98). Association for Computing
Machinery, New York, NY, USA, 604–613. https://doi.org/10.1145/276698.276876
[30] Omid Jafari, Parth Nagarkar, and Jonathan Montaño. 2020. mmLSH: A Practical
and Efficient Technique for Processing Approximate Nearest Neighbor Queries
on Multimedia Data. In Similarity Search and Applications (Lecture Notes in
Computer Science), Shin’ichi Satoh, Lucia Vadicamo, Arthur Zimek, Fabio Carrara,
Ilaria Bartolini, Martin Aumüller, Björn Þór Jónsson, and Rasmus Pagh (Eds.).
Springer International Publishing, Cham, 47–61. https://doi.org/10.1007/978-3-
030-60936-8_4
[31] J.W. Jaromczyk and G.T. Toussaint. 1992. Relative neighborhood graphs and
their relatives. Proc. IEEE 80, 9 (Sept. 1992), 1502–1517. https://doi.org/10.1109/
5.163414 Conference Name: Proceedings of the IEEE.
[32] Suhas Jayaram Subramanya, Fnu Devvrit, Harsha Vardhan Simhadri, Ravishankar
Krishnawamy, and Rohan Kadekodi. 2019. DiskANN: Fast Accurate Billion-point
Nearest Neighbor Search on a Single Node. In Advances in Neural Information
Processing Systems, Vol. 32. Curran Associates, Inc. https://papers.nips.cc/paper_
files/paper/2019/hash/09853c7fb1d3f8ee67a61b6bf4a7f8e6-Abstract.html
[33] Herve Jegou, Matthijs Douze, and Cordelia Schmid. 2008. Hamming Embedding
and Weak Geometric Consistency for Large Scale Image Search. In Computer
Vision – ECCV 2008 (Lecture Notes in Computer Science), David Forsyth, Philip
Torr, and Andrew Zisserman (Eds.). Springer, Berlin, Heidelberg, 304–317. https:
//doi.org/10.1007/978-3-540-88682-2_24
[34] Jeff Johnson, Matthijs Douze, and Hervé Jégou. 2017. Billion-scale similarity
search with GPUs. http://arxiv.org/abs/1702.08734 arXiv:1702.08734 [cs].
[35] Herve Jégou, Matthijs Douze, and Cordelia Schmid. 2011. Product Quantization
for Nearest Neighbor Search. IEEE Transactions on Pattern Analysis and Machine
Intelligence 33, 1 (Jan. 2011), 117–128. https://doi.org/10.1109/TPAMI.2010.57
Conference Name: IEEE Transactions on Pattern Analysis and Machine Intelli-
gence.
[36] Vladimir Karpukhin, Barlas Oğuz, Sewon Min, Patrick Lewis, Ledell Wu, Sergey
Edunov, Danqi Chen, and Wen-tau Yih. 2020. Dense Passage Retrieval for Open-
Domain Question Answering. https://arxiv.org/abs/2004.04906v3


## PDF page 15

ACORN: Search Over Vector Embeddings and Structured Data
[37] Philip M. Lankford. 1969. Regionalization: Theory and Alternative Algorithms.
Geographical Analysis 1, 2 (1969), 196–212. https://doi.org/10.1111/j.1538-4632.
1969.tb00615.x _eprint: https://onlinelibrary.wiley.com/doi/pdf/10.1111/j.1538-
4632.1969.tb00615.x.
[38] D. T. Lee and B. J. Schachter. 1980. Two algorithms for constructing a Delaunay
triangulation. International Journal of Computer & Information Sciences 9, 3 (June
1980), 219–242. https://doi.org/10.1007/BF00977785
[39] V. Lempitsky and A. Babenko. 2012. The inverted multi-index. IEEE Computer
Society, 3069–3076. https://doi.org/10.1109/CVPR.2012.6248038 ISSN: 1063-6919.
[40] Mingjie Li, Ying Zhang, Yifang Sun, Wei Wang, Ivor W. Tsang, and Xuemin Lin.
2020. I/O Efficient Approximate Nearest Neighbour Search based on Learned
Functions. 2020 IEEE 36th International Conference on Data Engineering (ICDE)
(April 2020), 289–300. https://doi.org/10.1109/ICDE48307.2020.00032 Conference
Name: 2020 IEEE 36th International Conference on Data Engineering (ICDE)
ISBN: 9781728129037 Place: Dallas, TX, USA Publisher: IEEE.
[41] Wanqi Liu, Hanchen Wang, Ying Zhang, Wei Wang, Lu Qin, and Xuemin Lin. 2021.
EI-LSH: An early-termination driven I/O efficient incremental c-approximate
nearest neighbor search. The VLDB Journal 30, 2 (March 2021), 215–235. https:
//doi.org/10.1007/s00778-020-00635-4
[42] Yiding Liu, Weixue Lu, Suqi Cheng, Daiting Shi, Shuaiqiang Wang, Zhicong
Cheng, and Dawei Yin. 2021. Pre-trained Language Model for Web-scale Retrieval
in Baidu Search. In Proceedings of the 27th ACM SIGKDD Conference on Knowledge
Discovery & Data Mining (KDD ’21). Association for Computing Machinery, New
York, NY, USA, 3365–3375. https://doi.org/10.1145/3447548.3467149
[43] David G. Lowe. 2004. Distinctive Image Features from Scale-Invariant Keypoints.
International Journal of Computer Vision 60, 2 (Nov. 2004), 91–110. https://doi.
org/10.1023/B:VISI.0000029664.99615.94
[44] Kejing Lu and Mineichi Kudo. 2020. R2LSH: A Nearest Neighbor Search Scheme
Based on Two-dimensional Projected Spaces. In 2020 IEEE 36th International
Conference on Data Engineering (ICDE). 1045–1056.
https://doi.org/10.1109/
ICDE48307.2020.00095 ISSN: 2375-026X.
[45] Kejing Lu, Hongya Wang, Wei Wang, and Mineichi Kudo. 2020. VHP: approxi-
mate nearest neighbor search via virtual hypersphere partitioning. Proceedings
of the VLDB Endowment 13, 9 (May 2020), 1443–1455. https://doi.org/10.14778/
3397230.3397240
[46] Qin Lv, William Josephson, Zhe Wang, Moses Charikar, and Kai Li. 2017.
Intelligent probing for locality sensitive hashing: multi-probe LSH and be-
yond.
Proceedings of the VLDB Endowment 10, 12 (Aug. 2017), 2021–2024.
https://doi.org/10.14778/3137765.3137836
[47] Yury Malkov, Alexander Ponomarenko, Andrey Logvinov, and Vladimir Krylov.
2014. Approximate nearest neighbor algorithm based on navigable small world
graphs. Information Systems 45 (Sept. 2014), 61–68. https://doi.org/10.1016/j.is.
2013.10.006
[48] Yu A. Malkov and D. A. Yashunin. 2018. Efficient and robust approximate
nearest neighbor search using Hierarchical Navigable Small World graphs. http:
//arxiv.org/abs/1603.09320 arXiv:1603.09320 [cs].
[49] Jason Mohoney, Anil Pacaci, Shihabur Rahman Chowdhury, Ali Mousavi, Ihab F.
Ilyas, Umar Farooq Minhas, Jeffrey Pound, and Theodoros Rekatsinas. 2023.
High-Throughput Vector Similarity Search in Knowledge Graphs. http://arxiv.
org/abs/2304.01926 arXiv:2304.01926 [cs].
[50] Marius Muja and David G. Lowe. 2014. Scalable Nearest Neighbor Algorithms
for High Dimensional Data. IEEE Transactions on Pattern Analysis and Machine
Intelligence 36, 11 (Nov. 2014), 2227–2240. https://doi.org/10.1109/TPAMI.2014.
2321376 Conference Name: IEEE Transactions on Pattern Analysis and Machine
Intelligence.
[51] Gonzalo Navarro. 2002. Searching in metric spaces by spatial approximation. The
VLDB Journal 11, 1 (Aug. 2002), 28–46. https://doi.org/10.1007/s007780200060
[52] Yongjoo Park, Michael Cafarella, and Barzan Mozafari. 2015. Neighbor-sensitive
hashing. Proceedings of the VLDB Endowment 9, 3 (Nov. 2015), 144–155. https:
//doi.org/10.14778/2850583.2850589
[53] Alec Radford, Jong Wook Kim, Chris Hallacy, Aditya Ramesh, Gabriel Goh,
Sandhini Agarwal, Girish Sastry, Amanda Askell, Pamela Mishkin, Jack Clark,
Gretchen Krueger, and Ilya Sutskever. 2021. Learning Transferable Visual Models
From Natural Language Supervision. https://doi.org/10.48550/arXiv.2103.00020
arXiv:2103.00020 [cs].
[54] Navid Rekabsaz, Oleg Lesota, Markus Schedl, Jon Brassey, and Carsten Eickhoff.
2021. TripClick: The Log Files of a Large Health Web Search Engine. In Proceed-
ings of the 44th International ACM SIGIR Conference on Research and Development
in Information Retrieval. 2507–2513. https://doi.org/10.1145/3404835.3463242
arXiv:2103.07901 [cs].
[55] Christoph Schuhmann, Richard Vencu, Romain Beaumont, Robert Kaczmarczyk,
Clayton Mullis, Aarush Katta, Theo Coombes, Jenia Jitsev, and Aran Komatsuzaki.
2021. LAION-400M: Open Dataset of CLIP-Filtered 400 Million Image-Text Pairs.
https://doi.org/10.48550/arXiv.2111.02114 arXiv:2111.02114 [cs].
[56] Chanop Silpa-Anan and Richard Hartley. 2008. Optimised KD-trees for fast
image descriptor matching. IEEE Computer Society, 1–8.
https://doi.org/10.
1109/CVPR.2008.4587638
[57] Harsha Vardhan Simhadri, George Williams, Martin Aumüller, Matthijs Douze,
Artem Babenko, Dmitry Baranchuk, Qi Chen, Lucas Hosseini, Ravishankar Kr-
ishnaswamy, Gopal Srinivasa, Suhas Jayaram Subramanya, and Jingdong Wang.
2022. Results of the NeurIPS’21 Challenge on Billion-Scale Approximate Nearest
Neighbor Search. http://arxiv.org/abs/2205.03763 arXiv:2205.03763 [cs].
[58] Aditi Singh, Suhas Jayaram Subramanya, Ravishankar Krishnaswamy, and Har-
sha Vardhan Simhadri. 2021. FreshDiskANN: A Fast and Accurate Graph-Based
ANN Index for Streaming Similarity Search. https://doi.org/10.48550/arXiv.2105.
09613 arXiv:2105.09613 [cs].
[59] Narayanan Sundaram, Aizana Turmukhametova, Nadathur Satish, Todd Mostak,
Piotr Indyk, Samuel Madden, and Pradeep Dubey. 2013. Streaming similar-
ity search over one billion tweets using parallel locality-sensitive hashing.
Proceedings of the VLDB Endowment 6, 14 (Sept. 2013), 1930–1941.
https:
//doi.org/10.14778/2556549.2556574
[60] Godfried T. Toussaint. 1980. The relative neighbourhood graph of a finite planar
set. Pattern Recognition 12, 4 (Jan. 1980), 261–268. https://doi.org/10.1016/0031-
3203(80)90066-7
[61] Andrei Vasnetsov. [n. d.]. Filtrable HNSW - Qdrant. https://qdrant.tech/articles/
filtrable-hnsw/
[62] Jianguo Wang, Xiaomeng Yi, Rentong Guo, Hai Jin, Peng Xu, Shengjun Li, Xi-
angyu Wang, Xiangzhou Guo, Chengming Li, Xiaohai Xu, Kun Yu, Yuxing Yuan,
Yinghao Zou, Jiquan Long, Yudong Cai, Zhenxiang Li, Zhifeng Zhang, Yihua Mo,
Jun Gu, Ruiyi Jiang, Yi Wei, and Charles Xie. 2021. Milvus: A Purpose-Built Vec-
tor Data Management System. In Proceedings of the 2021 International Conference
on Management of Data (SIGMOD ’21). Association for Computing Machinery,
New York, NY, USA, 2614–2627. https://doi.org/10.1145/3448016.3457550
[63] Mengzhao Wang, Lingwei Lv, Xiaoliang Xu, Yuxiang Wang, Qiang Yue, and
Jiongkang Ni. 2022. Navigable Proximity Graph-Driven Native Hybrid Queries
with Structured and Unstructured Constraints. http://arxiv.org/abs/2203.13601
arXiv:2203.13601 [cs].
[64] Chuangxian Wei, Bin Wu, Sheng Wang, Renjie Lou, Chaoqun Zhan, Feifei Li,
and Yuanzhe Cai. 2020. AnalyticDB-V: a hybrid analytical engine towards query
fusion for structured and unstructured data. Proceedings of the VLDB Endowment
13, 12 (Aug. 2020), 3152–3165. https://doi.org/10.14778/3415478.3415541
[65] Brie Wolfson. 2023. Building chat langchain. https://blog.langchain.dev/building-
chat-langchain-2/
[66] Wei Wu, Junlin He, Yu Qiao, Guoheng Fu, Li Liu, and Jin Yu. 2022. HQANN:
Efficient and Robust Similarity Search for Hybrid Queries with Structured and
Unstructured Constraints.
http://arxiv.org/abs/2207.07940 arXiv:2207.07940
[cs].
[67] Qianxi Zhang, Shuotao Xu, Qi Chen, Guoxin Sui, Jiadong Xie, Zhizhen Cai, Yaoqi
Chen, Yinxuan He, Yuqing Yang, Fan Yang, Mao Yang, and Lidong Zhou. 2023.
{VBASE}: Unifying Online Vector Similarity Search and Relational Queries via
Relaxed Monotonicity. 377–395. https://www.usenix.org/conference/osdi23/
presentation/zhang-qianxi
[68] Weijie Zhao, Shulong Tan, and Ping Li. 2020. SONG: Approximate Nearest
Neighbor Search on GPU. In 2020 IEEE 36th International Conference on Data
Engineering (ICDE). 1033–1044. https://doi.org/10.1109/ICDE48307.2020.00094
ISSN: 2375-026X.
[69] Bolong Zheng, Xi Zhao, Lianggui Weng, Nguyen Quoc Viet Hung, Hang Liu,
and Christian S. Jensen. 2020. PM-LSH: A fast and accurate LSH framework for
high-dimensional approximate NN search. Proceedings of the VLDB Endowment
13, 5 (Jan. 2020), 643–655. https://doi.org/10.14778/3377369.3377374


