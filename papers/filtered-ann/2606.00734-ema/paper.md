URL: https://www.alphaxiv.org/abs/2606.00734.md
Content-Type: text/markdown
Method: alternate-markdown
Notes: Used markdown alternate: https://www.alphaxiv.org/abs/2606.00734.md

---

EMA: Approximate Nearest Neighbor Search with General
Attribute Filtering and Dynamic Updates
Mocheng Li
mochengli1@link.cuhk.edu.cn
The Chinese University of Hong Kong, Shenzhen
Shenzhen, China
Baotong Lu
Microsoft Research
Beijing, China
baotonglu@microsoft.com
James Cheng
The Chinese University of Hong Kong
Hong Kong, China
jcheng@cse.cuhk.edu.hk
Chenhao Ma
∗
The Chinese University of Hong Kong, Shenzhen
Shenzhen, China
machenhao@cuhk.edu.cn
ABSTRACT
Filtering Approximate Nearest Neighbor (FANN) search is a crit-
ical and emerging task for strengthening the query capability of
vector databases, supporting applications such as recommenda-
tion systems, retrieval-augmented generation (RAG), and agent
memory. However, most existing methods are limited to range
or label filtering, often incurring unacceptable index construction
time and memory overhead. Predicate-agnostic approaches further
struggle to handle a wide range of predicate selectivities effectively.
In this paper, we propose EMA, a filtering ANN algorithm that
supports multi-predicate queries over mixed numerical and cate-
gorical attributes, and efficient dynamic updates. EMA introduces
Markers as compact summaries attached to graph edges, providing
conservative predicate- and geometric-aware guidance with zero
false negatives at the Marker level. During query processing, EMA
performs Marker-augmented joint search with a bounded edge
recovery mechanism, enabling efficient filtering while preserving
graph navigability. Extensive experiments demonstrate that EMA
achieves 1.68×–12.25× speedup over state-of-the-art general fil-
tering ANN methods across diverse workloads.
PVLDB Reference Format:
Mocheng Li, Baotong Lu, James Cheng, and Chenhao Ma. EMA:
Approximate Nearest Neighbor Search with General Attribute Filtering and
Dynamic Updates. PVLDB, 14(1): XXX-XXX, 2020.
doi:XX.XX/XXX.XX
PVLDB Artifact Availability:
The source code, data, and/or other artifacts have been made available at
https://github.com/lmccccc/EMA.
1 INTRODUCTION
Embedding-based representations have become widely adopted in
applications such as retrieval-augmented generation (RAG) [8 , 45 ,
54 ], recommendation systems [ 30 , 32 , 35 ], and semantic search [33].
To efficiently retrieve semantically similar items from large-scale
∗
Corresponding author.
This work is licensed under the Creative Commons BY-NC-ND 4.0 International
License. Visit https://creativecommons.org/licenses/by-nc-nd/4.0/ to view a copy of
this license. For any use beyond those covered by this license, obtain permission by
emailing info@vldb.org. Copyright is held by the owner/author(s). Publication rights
licensed to the VLDB Endowment.
Proceedings of the VLDB Endowment, Vol. 14, No. 1 ISSN 2150-8097.
doi:XX.XX/XXX.XX
vector datasets, Approximate Nearest Neighbor (ANN) search has
emerged as a critical technique and has been rapidly advancing in
recent years.
However, ANN search based solely on vector distance does not
always meet practical application requirements. As vector datasets
grow to millions or even billions of items, semantically similar em-
beddings may originate from different timestamps, data versions, or
data domains. This motivates the need for Filtering ANN (FANN)
search, which augments vector similarity search with predicate
constraints over structured attributes, ensuring that retrieved re-
sults are not only semantically relevant but also satisfy required
attribute conditions. For example, in legal document retrieval, mul-
tiple versions of a statute may be highly similar in embedding space
but apply to different effective periods; a valid retrieval must there-
fore select the semantically relevant provision whose effective date
attribute satisfies the temporal constraint of the case.
As the predominant foundation for FANN search, modern large-
scale ANN systems overwhelmingly adopt graph-based indices [6,
18 , 21 , 27 ], such as HNSW [28 ], due to their superior recall–latency
trade-offs in high-dimensional spaces. In contrast, tree-based [2, 4,
12 , 17 , 51], hashing-based [ 13 , 19 , 20 ] and IVF-based [ 7] methods
rely on explicit space partitioning and suffer from the curse of
dimensionality, while quantization-based approaches [14 , 16, 23 ]
often trade accuracy for efficiency. As a result, most recent efforts on
FANN search are built upon graph-based ANN indices.
Based on the types of supported predicates, existing approaches
can be broadly categorized into range-based [ 9, 26 , 48 , 55 ], label-
based [15, 43 ], and general arbitrary filtering methods [ 34 , 39 , 42 ].
Range-based methods primarily target numerical attributes, while
label-based methods focus on discrete categorical constraints; both
typically assume specific attribute types and restricted predicate
forms. General approaches aim to support arbitrary predicates, but
face significant design challenges in integrating flexible filtering
with efficient ANN search, limiting their effectiveness in practice.
Within the FANN retrieval pipeline, filtering can be applied
at different stages. Table 1 summarizes representative FANN ap-
proaches across different filtering stages. Pre-filtering methods,
such as iRangeGraph [48 ], achieve high efficiency when predicates
can be tightly embedded into the index structure, but their appli-
cability is limited to specific attribute types or restricted predicate
arXiv:2606.00734v1 [cs.DB] 30 May 2026

Table 1: Comparison of FANN algorithms.
Filtering Stage Algorithm Range Label Multi-predicate Low selectivity OCQ Dynamic Efficiency
Pre-filtering 
iRangeGraph [48] ✓ × △ ✓ ✓ × High↑
Milvus [42] ✓ ✓ ✓ △ △ ✓ Low↓
Post-filtering 
NHQ [43] × △ × × × × Moderate→
VBase [52] ✓ ✓ ✓ △ △ △ Low↓
Joint filtering
SeRF [55] ✓ × × × × × Moderate→
Filtered DiskANN [15] × ✓ × × × ✓ Moderate→
ACORN [34] ✓ ✓ ✓ × × △ Low↓
NaviX [39] ✓ ✓ ✓ △ △ △ Moderate→
EMA(Ours) ✓ ✓ ✓ ✓ ✓ ✓ High↑
△ indicates limited or non-robust support.
forms. Post-filtering systems, including VBase [52] and vector data-
base engines such as Faiss-HNSW [ 7 ], provide broad predicate sup-
port, yet incur substantial overhead by retrieving and filtering large
candidate sets, leading to poor efficiency. Joint filtering methods,
such as ACORN [ 34 ] and NaviX [39 ], strike a better balance be-
tween generality and efficiency by integrating predicate checks into
graph traversal. However, as shown in Table 1, their performance
degrades under low-selectivity conditions and remains non-robust
for more challenging query types.
Based on Table 1, several important observations can be drawn.
1 
Limited support for general predicates. Range-based and
label-based methods are designed for specific predicate types and
cannot naturally handle mixed numerical and categorical predicates
or multi-predicate conjunctions. More general methods improve
predicate coverage but often sacrifice robustness or efficiency.
2 Lack of robustness under low selectivity. When only
a small fraction of the dataset satisfies the predicate constraints,
valid points become sparse, weakening local connectivity in graph-
based ANN indices. As a result, many joint filtering methods fail
to maintain sufficient navigability, leading to degraded recall or
excessive search cost.
3 Failure under off-cluster queries (OCQ). In OCQ, predicate-
satisfying points are sparse in the local neighborhood of the query
but form clusters in distant semantic regions. This localized sparsity
violates the locality assumptions underlying existing joint filtering
designs and exposes a fundamental limitation of current FANN
approaches.
4 Lack of dynamic support. Most FANN methods assume
static data and lack efficient update mechanisms. Range-based ap-
proaches rely on ordered structures that are difficult to maintain
under updates, while existing systems often depend on coarse-
grained or asynchronous maintenance, limiting performance under
dynamic workloads.
Core challenge. Taken together, these observations reveal a
fundamental gap in existing FANN designs: there is no unified
solution for selectivity-robust approximate nearest neighbor search
with general attribute filtering. Addressing this challenge requires
an index design that 1 supports expressive, mixed predicates, 2
remains robust under extreme predicate selectivity, 3 preserves
graph navigability even when predicate-satisfying points are locally
sparse, as in OCQ scenarios, and 4 supports efficient dynamic
updates without frequent index reconstruction.
Motivated by this challenge, we propose Edge Marker (EMA),
a FANN algorithm that improves search robustness by explicitly
encoding predicate-relevant information into graph edges. By aug-
menting the graph structure itself, EMA enables attribute-aware
navigation that remains effective across diverse predicate selectivi-
ties. EMA supports multi-predicate queries with both range and la-
bel constraints and achieves consistently strong performance across
a wide range of selectivities, including challenging low-selectivity
and OCQ scenarios. Moreover, EMA supports dynamic updates
with scale-aware maintenance strategies.
Our main contributions are summarized as follows:
• We propose EMA, a filtering approximate nearest neighbor
(FANN) algorithm for expressive multi-predicate queries
over both numerical and categorical attributes. At its core,
EMA introduces Marker, a compact edge-centric and geometry-
aware summary that enables efficient joint filtering during
graph traversal without false negatives at the Marker level.
• To improve robustness under low-selectivity and off-cluster
queries, we design a diversity-aware pruning strategy dur-
ing index construction and an edge recovery mechanism at
query time, preserving graph navigability when predicate-
satisfying points are sparse or unevenly distributed.
• We design dynamic maintenance strategies for EMA, includ-
ing incremental insertions, lightweight attribute updates,
and a query-guided patch mechanism that records invalid
edges encountered during traversal and repairs the graph
locally without frequent full rebuilds.
• We provide a complexity analysis of EMA index construc-
tion and storage, and present a theoretical characterization
of Marker-induced false positives, clarifying when and why
Marker-based pruning remains effective.
• We conduct extensive experimental evaluations across se-
lectivity levels ranging from 1% to 100%, demonstrating
that EMA achieves 1.68× to 12.25× speedups over state-of-
the-art FANN methods.
2 PRELIMINARIES
Here, we first define the ANN and Filtering ANN (FANN) problems
and then review graph-based ANN indexes and the RNG pruning
principle that underlies their connectivity.
2

Table 2: Notations in our paper.
Notations Descriptions
D = (V, A) Dataset with vector set V and attribute set A.
𝑞 = (𝑣, 𝑅) Query vector and predicate(s).
𝑚 Cardinality of attribute set A.
A 
( 𝑗 ) 
The set of 𝑗-th attribute in A, 0 < 𝑗 ≤ 𝑚.
𝐴
( 𝑗 )
𝑖 
The 𝑗-th attribute of item 𝑖, 0 < 𝑗 ≤ 𝑚.
𝑑𝑖𝑠 (𝑣
𝑖 
, 𝑣 
𝑗 
) Distance between vector 𝑣
𝑖 
and 𝑣 
𝑗 
.
(𝑙, 𝑢) Lower/upper bound for range query.
𝑑 Dimension per vector in 𝑉 .
𝑛 Size of dataset D.
𝑀 Maximum degree for graph ANN index.
𝑒 𝑓 𝑐 Candidate set size in index construction.
𝑒 𝑓 𝑠 Candidate set size in ANN search.
2.1 Problem Definition
Let D = {V, A} denote a dataset, where V is a set of 𝑑-dimensional
vectors and A is the associated attribute set. Each vector 𝑣
𝑖 
∈ V is
associated with an attribute record 𝐴
𝑖 
∈ A. Given a query 𝑞 = (𝑣, 𝑅)
consisting of a query vector 𝑣 and an attribute predicate 𝑅, the
goal of filtering approximate nearest neighbor (FANN) search is to
retrieve vectors that are both close to 𝑣 and satisfy 𝑅.
The approximate nearest neighbor (ANN) search aims to retrieve
𝑘 vectors {𝑣
𝑖 
} from V whose distances to 𝑣 approximately minimize
dis(𝑣, 𝑣
𝑖 
). FANN further restricts the search space to the filtered
subset
V
𝑅 
= {𝑣
𝑖 
∈ V | 𝑅(𝐴
𝑖 
) = true},
and returns approximate nearest neighbors within V
𝑅 
.
Table 2 summarizes the frequent notations used throughout this
paper.
2.2 Graph-based ANN and RNG Pruning
Graph-based approximate nearest neighbor (ANN) methods orga-
nize data points into a proximity graph and perform greedy graph
traversal during search, achieving strong performance in high-
dimensional spaces. Representative systems include HNSW [28 ],
DiskANN [21 ], and FANNG [18]. Despite differences in index con-
struction and search strategies, these methods share a common
structural principle: maintaining a sparse yet navigable graph through
carefully designed neighbor pruning rules.
A widely adopted pruning strategy is inspired by the Relative
Neighborhood Graph (RNG) [ 25, 41 ], which promotes structural
diversity by removing neighbors that are dominated by closer ones.
Formally, given a source node 𝑢, a neighbor 𝑣 is dominated by
another neighbor 𝑤 if
dis(𝑢, 𝑣) > dis(𝑢, 𝑤) and dis(𝑤, 𝑣) < dis(𝑢, 𝑣).
Geometrically, as illustrated in Figure 1a, fixing a source node 𝑢
and one of its neighbors 𝑤, the neighbors 𝑣 that can be dominated
by 𝑤 lie within a truncated half-space determined by the perpen-
dicular bisector of (𝑢, 𝑤). This geometric property preserves graph
sparsity while maintaining diversity among retained neighbors. In
practice, graph-based ANN indexes commonly adopt RNG-style
(a). Node a connects to b and c,
where c is not in b’s domination.
DP (a,b) and (a,c) keeps b and
c’s attributes respectively.
u
w
v
Visiting
Node
Q
No Valid
Edges
u
v
w
v
w
u
attr: A
attr: B
(a) Classical RNG pruning.
(a). Node a connects to b and c,
where c is not in b’s domination.
DP (a,b) and (a,c) keeps b and
c’s attributes respectively.
u
w
v
Visiting
Node
Query
No Valid
Edges
u
v
w
v
w
u
attr: A
attr: B
 (b) Failure of joint search on RNG.
(a). Node a connects to b and c,
where c is not in b’s domination.
DP (a,b) and (a,c) keeps b and
c’s attributes respectively.
u
w
v
Visiting
Node
Query
No Valid
Edges
u
v
w
v
w
u
attr: A
attr: B
(c) ACORN pruning.
(a). Node a connects to b and c,
where c is not in b’s domination.
DP (a,b) and (a,c) keeps b and
c’s attributes respectively.
u
w
v
Visiting
Node
Query
No Valid
Edges
u
v
w
v
w
u
attr: A
attr: B
 (d) Filtered DiskANN pruning.
Figure 1: Classical RNG pruning, its failure under joint fil-
tering, and existing relaxations (𝑀 = 2).
pruning rules that relax this classical condition for efficiency, while
preserving its diversification intuition.
While RNG-style pruning is effective for unconstrained ANN
search, it relies solely on geometric proximity and is agnostic to
attribute-level constraints. When applied to FANN search with joint
filtering, this property can lead to severe connectivity failures. As
shown in Figure 1b, a visiting node 𝑢 may satisfy the predicate,
yet all of its remaining outgoing edges point to nodes that violate
the predicate. As a result, no valid edges are available for further
traversal, rendering predicate-satisfying nodes unreachable even
when they are geometrically close to the query. Such failures sig-
nificantly degrade graph connectivity under filtering constraints
and undermine the effectiveness of joint search.
Several existing methods attempt to mitigate this issue by re-
laxing RNG-style pruning. ACORN [ 34 ] adopts a two-hop pruning
strategy, as illustrated in Figure 1c, where a candidate neighbor is
pruned only if it remains reachable through an alternative two-hop
path. Although this relaxation improves reachability under filter-
ing, it departs from the original RNG principle and often reduces
structural diversity.
Filtered DiskANN [ 15] introduces attribute-aware pruning by
removing an edge only when the attribute sets of both endpoints
are covered by a common neighbor. Specifically, RNG-style pruning
is applied only if 𝑢.𝐴 ∪ 𝑣.𝐴 ⊆ 𝑤 .𝐴, as shown in Figure 1d. Such re-
strictive conditions are rarely satisfied in practice, resulting in most
edges being preserved solely based on nearest-neighbor proximity.
Consequently, the resulting graph tends to degenerate toward a
near-naive nearest neighbor structure, sacrificing both pruning
effectiveness and scalability.
In summary, while RNG-style pruning is central to the success of
graph-based ANN indexes, its geometry-only nature fundamentally
conflicts with attribute filtering. Existing relaxations either weaken
structural diversity or impose overly restrictive conditions, moti-
vating the need for a pruning and connectivity mechanism that
jointly accounts for geometric proximity and attribute constraints.
3

3 EMA
This section presents the design of EMA, a graph-based index for
filtering-aware approximate nearest neighbor search. EMA extends
RNG-style pruning with a compact Marker that encodes predicate-
relevant neighborhood information on each edge, enabling efficient
joint navigation under attribute filtering.
As illustrated in Figure 2, EMA adopts a two-layer HNSW-style
graph structure. During index construction, EMA follows the stan-
dard HNSW insertion procedure. At the bottom layer, Markers are
augmented to edges by leveraging the RNG-style pruning process,
aggregating attributes from nearby (dominated) nodes. Markers
thus summarize attribute information associated with geometric-
aware neighborhoods. At query time, EMA navigates the top layer
without applying attribute filters to locate entry points, and then
conducts Marker-guided joint search in the bottom layer. Edges
are selectively traversed based on Marker–predicate compatibility,
dynamically inducing a predicate-specific subgraph for efficient
filtering-aware ANN search.
3.1 Core Abstractions: Marker and Codebook
To support efficient joint search under general attribute filtering,
EMA introduces two core abstractions: the Marker and the Code-
book. Together, they provide a compact and conservative edge-
centric, hop-agnostic representation of predicate-relevant attribute
information associated with local graph neighborhoods, enabling
predicate prechecking during search without introducing extra
false negatives. We next formalize their definitions and encoding
mechanisms.
Definition 3.1 (Marker). A Marker encodes the presence of at-
tribute values associated with a subset of vectors 𝑆 ⊆ V with
respect to the global attribute set.
Let 𝐴 = {𝐴
(1) 
, . . . , 𝐴
(𝑚) 
} denote the set of attributes of a vector,
where 𝐴
( 𝑗 ) 
is a set of values for categorical attributes and a singleton
for numerical attributes. Given a subset of attribute values {𝐴
( 𝑗 )
𝑖 
|
𝑣
𝑖 
∈ 𝑆 }
, a fixed-length bit array Marker
𝑗 
∈ {0, 1}
𝑠 
is constructed for
attribute 𝐴
( 𝑗 ) 
, where each bit position corresponds to a discretized
value region. A bit is set to 1 if there exists at least one vector in 𝑆
whose attribute value falls into the corresponding region.
The overall Marker for subset 𝑆 is obtained by concatenating the
per-attribute Markers:
𝑆.Marker = 

Marker
1 
∥ Marker
2 
∥ · · · ∥ Marker
𝑚
 
∈ {0, 1}
𝑠 ·𝑚 
.
Due to discretization and aggregation, Markers may admit false
positives during predicate checking. However, the encoding is con-
servative: any attribute value that satisfies a query predicate is
guaranteed to activate the corresponding Marker bits, ensuring the
absence of false negatives introduced by Marker.
To construct and interpret Markers, EMA requires a unified
representation that maps heterogeneous attribute values into a
compact bit-level form. We therefore introduce a Codebook to define
how attribute domains are discretized and encoded.
Definition 3.2 (Codebook). Given a dataset D = (V, A), a Code-
book C = {𝐶
𝑗 
}
𝑚
𝑗=1 
is a collection of attribute-specific discretization
functions, where each
𝐶
𝑗 
: X
𝑗 
→ {1, . . . , 𝑠}
maps values from the domain X
𝑗 
of attribute 𝐴
( 𝑗 ) 
to one of 𝑠 discrete
buckets.
The Codebook provides a unified and fixed-size discretization
scheme for heterogeneous attributes, enabling different attribute
types to be encoded into a unified bit-level representation. The
construction of the Codebook and its use during index construction
are described in the following subsection.
3.2 EMA Construction
EMA follows the standard HNSW index construction pipeline,
which incrementally inserts nodes into a two-layer proximity graph.
For each inserted node, candidate neighbors are identified via
greedy search and pruned to maintain a bounded out-degree. EMA
preserves this overall structure and modifies only the neighbor
pruning stage in the bottom layer to incorporate Marker-based
attribute aggregation. We first describe the construction of Code-
book, which is a prerequisite for the subsequent index construction
process.
Algorithm 1: Codebook Generation
Input: Attribute columns {{𝐴
( 𝑗 )
𝑖 
}
𝑖 
}
𝑚
𝑗=1
, number of buckets 𝑠
Output: Codebook C = {𝐶
𝑗 
}
𝑚
𝑗=1
1 for 𝑗 ← 1 to 𝑚 do
2 if attribute 𝑗 is numerical then
3 sort values in {𝐴
( 𝑗 )
𝑖 
}
𝑖 
;
4 partition into 𝑠 contiguous buckets;
5 define 𝐶
𝑗 
using bucket lower bounds;
6 else
7 compute category frequencies in {𝐴
( 𝑗 )
𝑖 
}
𝑖 
;
8 sort categories by frequency;
9 assign categories to 𝑠 balanced buckets;
10 define 𝐶
𝑗 
as a category-to-bucket mapping;
Codebook generation. Before index construction, EMA builds
a Codebook that defines a fixed discretization scheme for each
attribute. For attribute 𝑗, the Codebook is constructed from the
empirical distribution of all values {𝐴
( 𝑗 )
𝑖 
| 𝑣
𝑖 
∈ V} 
in the dataset,
yielding a deterministic mapping shared across index construction
and query processing.
Algorithm 1 outlines the procedure. Numerical attributes are
discretized by sorting values and partitioning them into 𝑠 contigu-
ous frequency-balanced buckets, with bucket boundaries defining
the mapping. Categorical attributes are discretized by grouping
categories into 𝑠 buckets according to their frequencies to balance
bucket sizes, inducing a category-to-bucket mapping.
Attribute encoding. With the Codebook defined, we describe
how Markers are constructed during index insertion. Figure 3 illus-
trates Codebook-based Marker encoding and aggregation for both
numerical and categorical attributes.
Algorithm 2 formalizes the encoding process. For each attribute,
its value (or values, for categorical attributes) is mapped to a bucket
index using the Codebook, and the corresponding bit in the Marker
is set.
4

Visiting Node
EMA Construction EMA Joint Search
Codebook
.. 1
0 1 
0 
.. 
0 0
Marker
Codebook
2-layer index
Insert
w
x 
v
u
dominating
area
Vector
Predicate 
Query
Attributes 
[1,3]
Edge
Filtering
{a}
Query Marker
.. 0
0 1 
1 
.. 
1 0
Figure 2: EMA workflow.
Codebook
Marker
[0,3) [3,6) [6,9) 
{a,d} {b,e} {c,f}
1 0 
0 
1 
0 
1
Vector w
2 
{a,c}
Vector x
5 
{d}
Vector v
6 
{c}
1 0 1 1 0 1
Gather 
Gather
Figure 3: Illustration of Codebook-based Marker encoding
and gathering.
Algorithm 2: MEncode
Input: Attribute set 𝐴, Codebook C
Output: Marker 𝑀𝑎𝑟𝑘𝑒𝑟
1 for 𝑗 ← 1 to 𝑚 do Marker
𝑗 
← 0
𝑠 
;
2 Marker ← Concat
𝑚
𝑗 =1 
(Marker
𝑗 
) ;
3 foreach attribute 𝑗 = 1, . . . , 𝑚 do
4 foreach value 𝑎 ∈ 𝐴
( 𝑗 ) 
do
5 𝑏 ← 𝐶
𝑗 
(𝑎);
6 𝑀𝑎𝑟𝑘𝑒𝑟 
𝑗 
[𝑏 ] ← 1;
7 return Marker;
Marker construction with RNG-style pruning. As a core
component of graph-based ANN indexes, RNG-style pruning plays a
crucial role in maintaining a geometrically sparse, well-connected,
and diverse neighborhood structure. While MEncode enables at-
tribute information to be encoded on individual edges, naive prun-
ing would discard such information together with the pruned edges.
This would eliminate attribute cues associated with the dominated
regions and degrade the navigability of the graph under filtering
constraints. To address this issue, EMA propagates the Marker
information from pruned edges to the dominating edges that domi-
nate them, thereby preserving predicate-relevant reachability after
graph sparsification.
As illustrated in Figure 2, when inserting node 𝑢 into the bottom
layer, multiple candidate nodes (e.g., 𝑥 and 𝑣) may fall within the
dominating region of a selected neighbor 𝑤. During RNG-style
neighbor pruning, instead of implicitly losing the attribute infor-
mation associated with the pruned edges (𝑢, 𝑥) and (𝑢, 𝑣), EMA
aggregates their Markers into the Marker attached to the surviving
edge (𝑢, 𝑤). As a result, the edge (𝑢, 𝑤) compactly represents both
geometric proximity and the possible presence of attribute values
within its dominated region, thereby preserving predicate-relevant
information for subsequent navigation. The detailed Codebook-
based Marker encoding and aggregation mechanism is illustrated
in Figure 3.
Importantly, because this propagation follows RNG-style domi-
nation defined by local geometric relations (pairwise distances), the
resulting Marker aggregation is hop-agnostic and geometric-aware:
attribute evidence is gathered during pruning without multi-hop
exploration and is inherently geometry-dependent.
Diversity-aware Pruning. While Marker-based aggregation
preserves predicate-relevant attribute information during RNG-
style pruning, it does not by itself guarantee sufficient graph con-
nectivity under extremely sparse queries. In particular, even when
Markers encode rich attribute coverage, RNG-style pruning may
still retain neighbors that are geometrically diverse but attribute-
wise redundant, leading to sparse or fragmented connectivity once
restrictive predicates are applied.
Motivated by this observation and prior findings that neigh-
bor diversity is critical to graph-based ANN performance [44 ], we
introduce diversity-aware pruning. The core idea is to explicitly
encourage the retention of neighbors with diverse attribute values
during graph construction, thereby improving graph connectivity
and navigability under selective attribute filtering.
Pruning algorithm design. Combining the above designs, Al-
gorithm 3 describes EMA’s modified neighbor pruning step in the
bottom layer during HNSW-style insertion. Similar to HNSW, EMA
maintains a candidate list 𝐶𝑎𝑛𝑑 obtained by greedy search and
prunes it to satisfy an out-degree budget 𝑀. We assume 𝐶𝑎𝑛𝑑 is
sorted in ascending order of 𝑑𝑖𝑠 (𝑢, ·).
5

Algorithm 3: Marker-augmented RNG Pruning
Input: Node 𝑢; sorted candidate list 𝐶𝑎𝑛𝑑 (ascending by 𝑑𝑖𝑠 (𝑢, ·));
out-degree budget 𝑀; Codebook C; diversity threshold 𝑀
div
Output: Neighbor list 𝑁𝑏𝑟𝑠; Markers on edges incident to 𝑢.
1 𝑁𝑏𝑟𝑠 ← ∅;
2 𝐶𝑇 ← 0 ∈ N
𝑠 ·𝑚 
;
3 foreach 𝑣 ∈ 𝐶𝑎𝑛𝑑 do
4 if |𝑁𝑏𝑟𝑠 | = 𝑀 then break;
5 𝑑𝑜𝑚 ← false;
6 foreach 𝑤 ∈ 𝑁𝑏𝑟𝑠 do
7 if 𝑑𝑖𝑠 (𝑤, 𝑣) < 𝑑𝑖𝑠 (𝑢, 𝑣) then
8 if 𝑒
(𝑢,𝑣) 
is an old edge then
9 𝑒
(𝑢,𝑤) 
.Marker | = 𝑒
(𝑢,𝑣) 
.Marker;
10 else
11 𝑒
(𝑢,𝑤) 
.Marker | = MEncode(𝑣.𝐴, C);
12 𝑑𝑜𝑚 ← true;
13 break;
14 if 𝑑𝑜𝑚 = false then
15 z ← MEncode(𝑣.𝐴, C);
16 if |𝑁𝑏𝑟𝑠 | ≤ 𝑀/3 or min
𝑖 : z
𝑖 
=1 
𝐶𝑇
𝑖 
< 𝑀
div 
then
17 𝑁𝑏𝑟𝑠 ← 𝑁𝑏𝑟𝑠 ∪ {𝑣 };
18 𝑒
(𝑢,𝑣) 
.Marker | = z;
19 𝐶𝑇 ← 𝐶𝑇 + z;
RNG-style domination and Marker propagation. For each can-
didate 𝑣, we test whether it is dominated by an already selected
neighbor 𝑤 ∈ 𝑁𝑏𝑟𝑠 using an RNG-style condition: 𝑤 dominates
𝑣 if 𝑑𝑖𝑠 (𝑤, 𝑣) < 𝑑𝑖𝑠 (𝑢, 𝑣) (Line 7). If 𝑣 is dominated, the edge (𝑢, 𝑣)
will be pruned; however, its attribute information should not be
lost. EMA therefore propagates the Marker information to the dom-
inating edge (𝑢, 𝑤) by updating 𝑒
(𝑢,𝑤) 
.Marker with a bitwise OR
(Lines 8–11). When (𝑢, 𝑣) is an old edge from earlier point insertions,
we reuse its existing Marker; otherwise, we encode 𝑣’s attributes
via MEncode and merge the resulting bit vector.
Diversity-aware retention with counting filter. If 𝑣 is not dom-
inated, EMA optionally applies diversity-aware pruning before
adding it to 𝑁𝑏𝑟𝑠. We maintain a counting filter 𝐶𝑇 ∈ N
𝑠 ·𝑚 
that
tracks the frequency of activated Codebook buckets among re-
tained neighbors. Let z = MEncode(𝑣.𝐴, C), i.e., the Marker, be the
activation vector of 𝑣 (Line 15). We always accept the first 𝑀/3
neighbors to preserve base ANN quality. After that, 𝑣 is retained if
it activates at least one bucket whose current frequency is below a
diversity threshold 𝑀
div 
(Line 16). When 𝑣 is retained, we attach z to
𝑒
(𝑢,𝑣) 
.Marker and update 𝐶𝑇 ← 𝐶𝑇 + z (Lines 17–19). The pruning
terminates early once |𝑁𝑏𝑟𝑠 | = 𝑀.
3.3 Joint Search in EMA
EMA adopts a two-stage graph search strategy. The upper layer
supports fast navigation based solely on vector distance without
attribute filtering, while the bottom layer integrates distance eval-
uation with attribute filtering to enable efficient joint search. By
decoupling global navigation from predicate-aware refinement,
EMA achieves both fast convergence and fine-grained filtering.
Marker-guided joint expansion. As illustrated in Figure 2,
EMA performs predicate-aware traversal in the bottom layer by
leveraging Markers for early filtering. During neighbor expansion,
an edge is traversed only if its Marker is compatible with the query
predicate, enabling early elimination of infeasible paths before
distance evaluation.
Formally, let QMarker denote the Marker constructed from the
query predicate 𝑞.𝑅. Recall that 𝑚 is the number of attributes, and
we partition the attribute set into numerical and categorical subsets,
A = N ∪ C with N ∩ C = ∅. For the 𝑗-th attribute, we define the
attribute-level matching predicate as
MMatch( 𝑗) =






(Marker
𝑗 
& QMarker
𝑗 
) ≠ 0, A 
( 𝑗 ) 
∈ N,
(Marker
𝑗 
& QMarker
𝑗 
) = QMarker
𝑗 
, A 
( 𝑗 ) 
∈ C,
where Marker
𝑗 
and QMarker
𝑗 
denote the 𝑗-th attribute segment of
the Marker and Query Marker, respectively.
Under conjunctive predicates, an edge is eligible for expansion
if its Marker satisfies
MCheck(Marker) =
𝑚
Û
𝑗=1
MMatch( 𝑗) = true. (1)
Intuitively, a numerical predicate matches if at least one discretized
bucket overlaps, while a categorical predicate requires full coverage
of the query labels. To accelerate Marker checking, EMA leverages
SIMD instructions to perform parallel bitwise operations.
Edge recovery under low selectivity. Despite Marker-guided
expansion, graph connectivity may still degrade under extremely
selective predicates due to the bounded out-degree 𝑀. To address
this issue, EMA incorporates a lightweight edge recovery mecha-
nism. During search, we retain edges that satisfy MMatch as candi-
dates and mark mismatched edges as ineligible. When the number
of MMatch edges drops below a predefined threshold 𝑑
min
, EMA re-
stores the closest mismatched edges into the candidate set, ensuring
a minimum out-degree of 𝑑
min
.
EMA jointly leverages Marker checking and bounded edge recov-
ery to expand the reachable search region during traversal. Markers
steer exploration toward predicate-compatible directions, while
edge recovery maintains sufficient local connectivity to enable tra-
versal beyond the reach of post-filtering and two-hop expansion.
Exact predicate verification. Markers provide a conservative
approximation for predicate pruning and may admit false positives.
Therefore, once a node is accessed, its original attributes are used
to re-evaluate the query predicate to ensure correctness. Predicate
evaluation and predicate-to-Marker translation incur negligible
overhead. Numerical predicates are evaluated via simple compar-
isons, while categorical attributes are represented as compact bit
vectors and evaluated using SIMD-accelerated bitwise operations.
Overall, both Marker checking and exact predicate verification
require only 𝑂 (𝑚) SIMD operations.
3.4 Predicate Aggregation
In this work, we consider general Boolean predicate expressions
over numerical and categorical attributes, supporting arbitrary com-
binations of conjunctive (AND) and disjunctive (OR) operators. This
formulation subsumes conjunctive predicates, which are preva-
lent in real-world analytical workloads [3, 31 ], and aligns with
6

the marker-based design in EMA. Specifically, the aggregation of
Markers in Equation 1 is generalized from conjunctive evaluation
to Boolean evaluation, enabling flexible predicate composition with
minimal changes to the framework.
3.5 Dynamic Support
EMA supports dynamic updates, including insertions, deletions,
and attribute or vector modifications.
Insertion. EMA naturally supports insertions, as enabled by
the underlying HNSW structure. During insertion, Markers are
incrementally constructed and updated through RNG-style prun-
ing, propagating attribute information from dominated nodes to
surviving edges.
Deletion. EMA adopts a lazy deletion strategy. Similar to Fresh
DiskANN [ 40 ], deleted nodes are first marked without immediate
structural updates. When deletions accumulate beyond a threshold,
EMA triggers batched patch operations to repair the graph.
The patch design is built on two key components. First, EMA
records invalid edges along query traversal paths, providing a query-
aware signal of accessed regions. Second, for each edge pointing to
a deleted node, EMA replaces it with the nearest valid neighbor of
the deleted node, restoring local connectivity.
Together, these query-guided and locality-preserving updates
enable efficient graph repair while maintaining index quality. When
the deletion ratio becomes large, EMA performs a full rebuild to
restore global consistency.
Modification. We distinguish two types of modifications. For
attribute-only modifications, edge connectivity remains unchanged.
EMA performs a search and updates the FT of reverse edges within
one-hop neighbors by merging the new attribute information via a
lightweight OR operation. For joint vector and attribute modifications,
EMA adopts a delete-and-insert procedure, combined with batched
patching and periodic rebuilding under large-scale updates.
4 THEORETICAL ANALYSIS OF EMA
In this section, we present a theoretical analysis of EMA. We show
that EMA preserves the asymptotic time and space complexity of the
underlying HNSW index. We further analyze the sources of false
positives introduced by Marker checking and derive theoretical
bounds on their rates. All proofs are deferred to the appendix [10 ].
4.1 Complexity
Lemma 4.1 (Cost of MEncode and Edge Checking). In Algo-
rithm 3, MEncode takes 𝑂 (𝑚) time. Testing whether 𝑒
(𝑛,𝑣) 
previously
exists takes 𝑂 (𝑀) time, as it scans the adjacency list of 𝑛, which
contains at most 𝑀 neighbors.
Lemma 4.2 (Cost of Marker-augmented pruning). The time
complexity of Algorithm 3 is 𝑂 
(
𝑒 𝑓 𝑐 · 𝑀 · (𝑀 + 𝑚)).
Theorem 4.3 (Index construction time complexity). The
total time complexity of EMA index construction is
𝑂 
(
𝑀 · 𝑒 𝑓 𝑐 · 𝑛 log 𝑛 · (𝑀 + 𝑚))
 
.
The time complexity of HNSW index construction is empirically
close to 𝑂 (𝑀 · 𝑒 𝑓 𝑐 · 𝑛 log 𝑛). In EMA, each pruning step incurs
an additional 𝑂 (𝑀 + 𝑚) overhead due to Marker encoding and
propagation (Lemma 4.2). Since both 𝑀 and 𝑚 are treated as small
constants, EMA preserves the asymptotic construction complexity
of HNSW.
Theorem 4.4. The space complexity of EMA is 𝑂 (𝑛𝑀 · 𝑠𝑚), where
𝑠 is the per-attribute Marker size and 𝑚 is the number of attributes.
The base HNSW index stores 𝑂 (𝑛 · 𝑀) edges, yielding a space
complexity of 𝑂 (𝑛 · 𝑀). Since both 𝑠 and 𝑚 are constants indepen-
dent of 𝑛, EMA introduces only a constant-factor space overhead
and preserves the asymptotic space complexity of HNSW.
4.2 Theoretical Analysis of Marker False
Positives
A key challenge in early filtering using Markers is the presence of
Marker-matched nodes that do not satisfy the predicate, resulting in
false positives. Marker false positives arise in two cases: (1) attribute
information aggregated from dominated nodes differs from that of
the dominating node; and (2) the query predicate does not align
with the discretization granularity of the Codebook.
Case (1): Dominance-induced aggregation. Case (1) arises
because a surviving edge aggregates attribute information from
a small dominated neighborhood, which may contain predicate-
matching nodes even when the endpoint itself does not satisfy the
predicate. Such false positives are often beneficial in practice, as
they guide the search toward geometrically nearby regions that
are likely to contain valid results. Although the dominated region
size is not theoretically bounded, it is typically small in practice,
making this form of false positives effective for navigation.
Case (2): Codebook granularity mismatch. Case (2) arises
from the mismatch between predicate granularity and Codebook
discretization. For example, suppose a Marker bit corresponds to the
range [0, 10) via the Codebook. A query predicate [5, 6] will match
this Marker, even if the Marker bit was set by an attribute value
of 4, which does not satisfy the predicate. Unlike Case (1), such
false positives may direct the search toward regions that contain
no predicate-matched nodes, purely due to discretization-induced
over-approximation.
We study these two sources of Marker false positives separately.
For Case (1), we derive an average-case characterization based
on the expected dominated-set size. For Case (2), we establish a
sufficient bound on Codebook resolution to control granularity-
induced false positives.
Theorem 4.5 (Expected Case-(1) False Positive Rate). Con-
sider a directed edge 𝑒
(𝑢,𝑣) 
whose Marker aggregates attribute bitmaps
from the target node 𝑣 and a dominated set 𝐷 (𝑒) (thus 𝑣 ∉ 𝐷 (𝑒)).
Define the average dominated-set size 𝜇 = E[ |𝐷 (𝑒)| ], where the
expectation is taken over edges.
Let 𝑅 be a query predicate with selectivity sel, i.e., for a randomly
drawn node 𝑝, Pr[𝑝 |= 𝑅] = sel. Assume predicate satisfaction events
are i.i.d. across nodes.
Define the Case-(1) false positive event on edge 𝑒 as
FP
case1 
(𝑒) ⇐⇒ 

𝑣̸ |= 𝑅
 
∧

∃ 𝑝 ∈ 𝐷 (𝑒) : 𝑝 |= 𝑅

.
Then the expected Case-(1) false positive rate satisfies
FPR
case1 
= (1 − sel) ·

1 − E

(1 − sel)
|𝐷 (𝑒 ) | 
 

,
7

and admits the upper bound that depends only on 𝜇:
FPR
case1 
≤ (1 − sel) · 

1 − (1 − sel)
𝜇 

.
Example. Using the average-size approximation with |𝐷 (𝑒)| ≈ 𝜇,
when the dominated-set size is small (typically 𝜇 = 2–3 in practice),
the Case-(1) false positive rate remains limited. For sel = 1%, it
evaluates to approximately 2.0% for 𝜇 = 2 and 3.0% for 𝜇 = 3, while
for sel = 50%, it is about 37.5% and 43.8%, respectively.
Discussion. This behavior is desirable for EMA. When the overall
query selectivity is high, EMA effectively reduces to post-filtering,
thereby allowing the search to fully exploit the strong connectivity
of the underlying graph. This behavior is consistent with prior
observations that when predicate selectivity is moderate to high
(e.g., no less than 50%), post-filtering does not significantly de-
grade search performance [ 49 ]. When the selectivity is low, the low
false-positive rate of Case (1) enables effective neighbor pruning,
significantly reducing the search space.
Next, we derive a bound to control Codebook-induced false
positives from case (2) and determine an appropriate Codebook
size 𝑠 for a given selectivity range.
Let 𝑅 = 
Ó
𝑚
𝑗=1 
𝑅
𝑗 
be a conjunctive predicate on 𝑚 attributes, and
let
R = {𝑣 ∈ D | 𝑅(𝑣) = true}
denote the set of records satisfying 𝑅.
Let 
e
R denote the set of records accepted by Marker checking,
i.e., 
e
R = {𝑣 ∈ D | ∃
𝑢 
MMatch(𝑒
(𝑢,𝑣) 
.Marker) = true},
which are the records that are reachable through at least one Marker-
compatible edge during joint search.
Define the Case-(2) false positive rate as
FPR
case2 
(𝑅) = 
Pr[𝑣 ∈ 
e
R \ R]
Pr[𝑣 ∈ 
e
R] 
.
Theorem 4.6 (Bounding Case-(2) False Positives). Assume
that:
(i) For each attribute 𝐴
( 𝑗 ) 
, the Codebook partitions its domain
into 𝑠 disjoint regions, each with probability mass at most
1/𝑠;
(ii) For each attribute 𝐴
( 𝑗 ) 
, the predicate 𝑅
𝑗 
partially intersects
at most 𝑏 
𝑗 
Codebook regions.
If the joint predicate selectivity satisfies Pr[𝑣 ∈ R] ≥ sel, then
FPR
case2 
(𝑅) ≤
Í
𝑚
𝑗=1
𝑏 
𝑗
𝑠
sel + 
Í
𝑚
𝑗=1
𝑏 
𝑗
𝑠
.
Hence, to guarantee FPR
case2 
(𝑅) ≤ FP, it suffices that
𝑠 ≥ 
1 − FP
FP · sel 
·
𝑚
∑︁
𝑗=1
𝑏 
𝑗 
.
Example. Consider a conjunctive predicate 𝑅 = 
Ó
𝑚
𝑗=1 
𝑅
𝑗 
on 𝑚
attributes, and assume each 𝑅
𝑗 
(𝐴
( 𝑗 ) 
) is a 1D range predicate under a
contiguous Codebook partition, so that 𝑏 
𝑗 
≤ 2. Then 
Í
𝑚
𝑗=1 
𝑏 
𝑗 
≤ 2𝑚,
and Theorem 4.6 gives the sufficient condition 𝑠 ≥ 
(1−FP)
FP·sel 
· 
2𝑚.
For instance, with 𝑚 = 4, sel = 50%, and FP = 50%, it suffices to
use 𝑠 ≥ 16 bits per attribute. If sel = 10% and FP = 50%, the bound
becomes 𝑠 ≥ 80.
Table 3: Average speedup of EMA over the best baseline.
Attribute Type Redcaps SIFT YoutubeRGB Wiki
label+range-high 2.72x 5.14x 7.14x 4.88x
label+range 3.88x 11.39x 4.07x 1.68x
composed 3.95 12.25x 4.88x 5.47x
For attribute types,“high” denotes selectivity of 10%–100%; others correspond to
1%–10%.
Discussion. The bound highlights that controlling Case-(2) false
positives mainly requires sufficient Codebook resolution when the
effective selectivity is low. When sel is large, a small 𝑠 already
keeps Case-(2) false positives bounded, and EMA naturally behaves
closer to post-filtering while benefiting from the graph connectivity.
When sel is small, increasing 𝑠 reduces the granularity-induced over-
approximation, enabling stronger pruning and a smaller search
region.
These theorems establish the computational and theoretical foun-
dations of EMA. We analyze the index construction time and space
complexity of EMA, and provide a principled characterization of
false positives arising from dominance-based aggregation (Case 1)
and codebook granularity (Case 2). Together, these results provide
a principled guideline for selecting the Codebook resolution, re-
vealing a clear trade-off between Marker granularity and filtering
effectiveness under different selectivity regimes.
5 EXPERIMENTS
We conduct extensive experiments to evaluate the performance
of EMA under a wide range of query selectivities, predicate sizes,
attribute types, and datasets. Major results include:
• EMA achieves consistent QPS speedups ranging from 1.68×
to 12.25×, as shown in Table 3.
• The performance gains of EMA mainly come from the com-
bined effect of Marker checking and edge recovery, enabling
early elimination while preserving efficient graph traversal.
• Although false positives are allowed during Marker check-
ing, EMA maintains a well-controlled false positive rate.
More experiments are available in our appendix [10].
5.1 Experimental setting
All experiments were conducted on a high-end CPU server running
Ubuntu 24.04, equipped with an Intel Xeon Platinum 8358 CPU
(64 physical cores), 2 TB RAM, and HDD-backed storage for index
persistence.
System settings.
• 
Index construction: Single thread for VBase, 32 threads for
other methods.
• Query execution: Single-threaded, except Milvus (internal
multi-threading with varying threads).
Metrics.
• Distance Metrics: L2 (Euclidean distance) / IP (inner prod-
uct; IP uses ℓ
2
-normalized vectors with distance defined as
the negated inner product).
• Performance report: Queries per second (QPS)
8

Baselines. We include the following 6 baselines in our experi-
ments:
Milvus 
[ 42 ] is a widely used vector database system that sup-
ports predicate filtering. It employs data partitioning to accelerate
filtered ANN search. We use Milvus-HNSW as a representative
system-level baseline, as its query execution relies on internal multi-
threading that is not explicitly controllable.
VBase [52 ] is a vector database system that also supports predi-
cate filtering and serves as another baseline.
ACORN [34] employs a two-hop checking strategy for joint-filtered
traversal. In its original implementation, attribute predicates are
eagerly evaluated for all vectors as a preprocessing step. For a
fair comparison, we replace this with a lazy evaluation strategy,
where predicates are evaluated only upon first access and cached
for reuse, so that predicate checking cost is accounted for during
query execution.
NaviX [39] builds upon ACORN and represents the current state
of the art in general filtering ANN search. We use the faiss-NaviX
implementation and apply the same lazy predicate evaluation strat-
egy as in ACORN for a fair comparison.
Filtered DiskANN [ 15 ] is a representative method for label
filtering ANN search. Although NHQ [43] achieves better perfor-
mance than Filtered DiskANN in some cases, it supports exact-
label matching only and cannot handle subset-style label predicates
(e.g., query labels ⊆ item attributes), and is therefore not applicable
to our setting.
iRangeGraph [ 48 ] is the state-of-the-art method for range fil-
tering ANN search. It supports multi-predicate range query by
performing pre-filtering on the first predicate, followed by post-
filtering on the remaining predicates. We include it as a strong
baseline for range filtering scenario.
Hyper-parameter Settings. Following prior studies [28, 36 ]
and common practice in graph-based ANN indexes, we use a rela-
tively large value of 𝑀 to ensure sufficient graph connectivity. Ac-
cordingly, we set 𝑀 = 40 and 𝑒 𝑓 𝑐 = 300 for Milvus, NaviX, ACORN,
iRangeGraph, Filtered DiskANN, and EMA. For ACORN, we use
𝛾 = 10 for comparable connectivity. We use Milvus-standalone ver-
sion 2.5.24 (Docker) as the baseline implementation. For numerical
attributes, we set the partition size to 64; however, partitioning is
not supported for array-valued categorical attributes. VBase does
not expose tunable hyper-parameters.
EMA introduces additional hyper-parameters. We set the Code-
book size to 𝑠 = 256, the maximum diversity degree to 𝑀
𝑑𝑖𝑣 
= 16,
𝑑
𝑚𝑖𝑛 
= 16, and top-layer search width 𝑒 𝑓
top 
= 1, which yield the
best empirical performance.
To ensure a fair and practical comparison, we set the overall
index construction timeout to 20 hours for all methods.
Datasets. Four datasets are used in our experiments:
SIFT [ 1, 22 ] contains 128-dimensional image embeddings with L2
distance. We sample 10M vectors from SIFT-1B and assign synthetic
numerical and categorical attributes for range and label queries.
RedCaps [5] is a large-scale image–text dataset collected from
Reddit, where each data point is represented by precomputed CLIP
1
https://research.google.com/youtube8m/download.html
2
https://redcaps.xyz/
3
http://corpus-texmex.irisa.fr/
4
https://huggingface.co/datasets/gaurav8297/navix
Table 4: Dataset Statistics.
Dataset Dim Size Query Size Metric
YouTube-RGB
1 
1024 1M 1000 IP
Redcaps 
2 
512 4M 1000 IP
SIFT 
3 
128 10M 1000 L2
Wiki
4 
1024 15.4M 1050 IP
embeddings [38 ] and associated with a timestamp as a numerical
attribute.
YouTube-RGB uses visual embeddings from the YouTube-8M
dataset. We use the upload timestamp and the number of likes as
filterable attributes for range and multi-attribute queries, providing
a challenging and realistic evaluation setting.
Wiki is adopted from NaviX [ 39 ], where two weakly correlated
textual embedding datasets are combined. The dataset includes birth
dates, which enable experiments with OCQ queries. We use the 50
queries provided with the dataset for OCQ evaluation and generate
additional 1,000 queries via random sampling with perturbations
for general experiments.
5.2 Performance for multi-predicate query.
We evaluate our method against strong baselines under different
predicate settings using two-predicate queries. Specifically, we con-
sider (i) one categorical attribute and one numerical attribute, cor-
responding to a label predicate and a range predicate (label+range),
and (ii) two numerical attributes, corresponding to two range pred-
icates (range+range).
For most datasets, numerical attributes are generated by ran-
domly assigning each vector an integer value in the range [0, 100,000].
RedCaps-RGB uses the real timestamp and the number of likes as
attributes, whereas RedCaps adopts combined attributes consist-
ing of one timestamp and one synthetic attribute. For categorical
attributes, we generate 18 labels with different probabilities and
randomly assign them to all vectors. For multi-predicate queries,
selectivity is evenly allocated to each predicate so that their con-
junction achieves the desired overall selectivity.
High selectivity performance. We evaluate high-selectivity
scenarios (10%–100%) under label+range queries. Figure 4 reports
the results at 95% recall. Milvus is included as a system-level base-
line and runs with internal multi-threading (MT); other methods
are reported under single-threaded query execution.
As selectivity increases, EMA consistently achieves the highest
QPS across all datasets, and the performance gap between EMA and
the other baselines widens. This indicates that EMA is particularly
effective when a large fraction of vectors satisfy the filtering predi-
cates.
The robustness of EMA mainly stems from two aspects. First, EMA
combines strong pruning with effective graph navigation using
Marker, which reduces unnecessary candidate expansions under
relaxed predicates. Second, EMA performs fast predicate prechecks
during traversal, thereby reducing random memory accesses and
improving efficiency at high selectivity.
In contrast, existing methods exhibit limited scalability as se-
lectivity grows. NaviX incurs increased random memory accesses
9

Figure 4: QPS for label+range multi-predicate query at 10%-100% selectivity with 95% recall@10.
Figure 5: QPS for label+range multi-predicate query at 1%-10% selectivity with 95% recall@10.
Figure 6: QPS for composed multi-predicate queries at 1%–10% selectivity with 95% recall@10.
during search, which constrains its performance at high selectivity.
Milvus partitions the index into multiple sub-indexes, reducing its
effectiveness when a large fraction of vectors satisfy the predicates.
ACORN relies on two-hop pruning rather than RNG-style pruning,
leading to less effective searching in high-selectivity settings. VBase
adopts a relaxed monotonicity post-filtering strategy; however, its
search parameters are not configurable, and it does not consistently
achieve the 95% recall across all datasets. Its performance on 90%
recall is reported on SIFT. Due to severe I/O bottlenecks under
HDD storage, Milvus-standalone fails to reliably complete index
construction on the Wiki dataset and is therefore excluded there.
Low selectivity performance. Figure 5 reports QPS at 95%
recall with the selectivity levels ranging from 1% to 10% under
label+range queries.
At low selectivity, EMA maintains stable and consistently high
throughput across all datasets, demonstrating strong robustness
when filtering predicates are highly restrictive. In addition to effec-
tive pruning and navigation, EMA benefits from its neighbor recovery
mechanism, which mitigates the loss of graph connectivity caused
by aggressive filtering and avoids premature search termination.
As a result, EMA sustains efficient traversal even when the candidate
set is severely reduced.
In contrast, other methods exhibit more limited improvements
at low selectivity. The behavior of NaviX and ACORN suggests that
their two-hop expansion strategy requires accessing attributes of
neighbors up to two hops away, which substantially increases ran-
dom memory accesses and expands the memory footprint. This
overhead limits their optimization potential under highly restrictive
10

predicates, compared to post-filtering approaches. Partition-based
methods such as Milvus exhibit clear performance improvements at
low selectivity, benefiting from effective partitioning, multi-thread
parallelization, and linear scan when selectivity becomes extremely
low (e.g., below 3%). In contrast, VBase performs competitively on
some datasets but still shows higher performance variability across
datasets.
Overall, these results indicate that EMA delivers robust perfor-
mance across highly restrictive label+range queries by effectively
balancing pruning, connectivity preservation, and memory effi-
ciency.
Complex Predicate Combination We evaluate EMA on com-
plex Boolean predicates that combine numerical ranges and cate-
gorical constraints. Specifically, we use the following predicate:
𝑝 = 

num ∈ [𝑎
1
, 𝑏
1
] ∧ cate ⊇ 𝐿
1
 
∨ 

num ∈ [𝑎
1
, 𝑏
2
] ∧ cate ⊇ 𝐿
2

,
which captures a representative class of composed predicates with
mixed attribute types and AND/OR operators. We vary the selectivity
from 1% to 10% while fixing recall at 95% (recall@10).
Figure 6 reports the results. Under controlled selectivity, EMA
maintains stable QPS for complex predicate compositions, demon-
strating its robustness in handling arbitrary Boolean combinations
over numerical and categorical attributes. Notably, performance
remains largely unaffected by predicate complexity, indicating that
EMA is insensitive to the structure of AND/OR expressions. Even for
highly complex predicates with mixed range and label conditions,
EMA achieves performance comparable to simpler cases, showing
that expressive predicate composition does not introduce additional
overhead.
5.3 Single attribute performance
Range filtering performance. iRangeGraph is a state-of-the-art
method for range-based filtered ANN search, demonstrating strong
performance across a wide range of selectivities. However, its pri-
mary limitation lies in the high index construction time cost, which
can reach up to 𝑂 (𝑛 log
2 
𝑛). As a result, we evaluate iRangeGraph
on RedCaps and YouTube-RGB, where index construction remains
practical.
Figure 7 compares EMA with iRangeGraph and other baseline
methods under selectivities ranging from 1% to 10% at 95% recall.
Dashed lines indicate results reported at 90% recall. At very low se-
lectivity, iRangeGraph achieves higher QPS due to its pre-filtering
design and the use of large sub-indexes with space complexity
𝑂 (𝑀𝑛 log 𝑛), which can be impractical for general and arbitrary
filtering workloads. As selectivity increases, the performance gap
between EMA and iRangeGraph narrows to a modest level, while EMA
avoids the substantial indexing overhead required by iRangeGraph.
Milvus attains high QPS primarily by leveraging its highly par-
allel execution model, which makes its performance less indicative
of single-thread algorithmic efficiency. Compared with NaviX, EMA
consistently achieves higher throughput across the evaluated selec-
tivity range on both datasets.
Label filtering performance. We evaluate label filtering by
comparing our approach with the baselines.
Figure 8 reports label filtering performance at 95% recall under
low-selectivity settings. We note that Filtered DiskANN is not well
suited to extremely low selectivity settings, as aggressive filtering
substantially reduces candidate availability during search. In such
cases, we report Filtered DiskANN’s performance at its highest
attainable recall, i.e., 80%, while all other methods are evaluated at
95% recall.
Despite operating at a higher recall target, EMA consistently
achieves competitive performance compared to all evaluated base-
lines, including Filtered DiskANN, under highly restrictive label
filtering conditions.
5.4 Dynamic support
EMA supports dynamic updates, including insertions, deletions, and
vector or/and attribute modifications. We maintain the number of
deleted or updated items, and trigger an edge patch when the ratio
exceeds a threshold (20% in EMA), with subsequent patches every
additional 10% of changes.
Figure 9a shows that EMA maintains robust performance under
insertions. Since insertions do not alter the index structure, the
overall performance remains stable.
Figure 9b shows the performance as the dataset size decreases
from 10M to 5M. When 20% of items are deleted, EMA triggers
a patch, resulting in a performance improvement. A full rebuild
is triggered when cumulative deletions reach 50%, leading to a
significant gain. The dashed line shows the QPS without patch or
rebuild, illustrating that our patch strategy effectively maintains
stable performance.
Figure 9c and Figure 9d show the modification performance
on SIFT10M under attribute-only modifications and joint vector-
attribute modifications, respectively. We initialize the dataset with
the first 5M vectors and attributes, and progressively modify them
using the remaining 5M points until all entries are replaced. Com-
pared with attribute-only modifications, joint vector-attribute mod-
ifications introduce higher maintenance overhead and thus lead to
a more noticeable QPS degradation. Nevertheless, EMA maintains
robust query performance under intensive modification workloads,
demonstrating the effectiveness of the patch mechanism.
EMA supports efficient dynamic updates with low overhead.
Deletions and attribute-only updates are lightweight, requiring
only 1.3 s and 2.5 s per 1M operations, respectively, as they involve
marking or local metadata updates. Insertions incur moderate cost
(90.8 s per 1M) and scale well under parallel execution.
Batched patch operations are efficient (45.2 s per call), costing
only about 12% of a full rebuild (371.1 s) on SIFT5M, while effectively
maintaining index quality. Overall, EMA enables dynamic updates
with minimal maintenance overhead while sustaining high QPS.
5.5 Additional studies
Off-clustering queries (OCQ). The Wiki dataset consists of two
largely uncorrelated subsets corresponding to person and resource
entities. We assign birth dates as numerical attributes to person
entities, while setting the attribute to zero for resource entities. To
construct off-clustering queries (OCQ), we construct query vectors
exclusively from the resource domain and pair them with randomly
generated birth-date range predicates defined over the person at-
tributes. This design intentionally decouples vector similarity from
attribute relevance.
11

Figure 7: QPS for range query at 95% recall@10. Figure 8: QPS for label query at 95% recall@10.
(a) Insertion (b) Deletion (c) Attribute modification (d) Vector + attribute modification
Figure 9: QPS of EMA under different update operations at 95% recall@10.
Figure 10 reports the results. Under OCQ workloads, ACORN and
NaviX fail to reach the target recall of 95%, with ACORN capped be-
low 60% recall and NaviX plateauing at around 90%. VBase similarly
cannot achieve 95% recall. In contrast, EMA consistently satisfies the
recall requirement and maintains stable performance, demonstrat-
ing robustness under off-clustering query conditions.
Index construction. Table 5 reports index construction time
and index size. Results for Milvus and iRangeGraph on Wiki are
omitted as index construction does not reliably complete within the
time or resource budget at this scale. Overall, EMA achieves construc-
tion times comparable to existing methods. NaviX exhibits faster
construction due to its lock-free design, which prioritizes build
efficiency over graph stability and connectivity quality. In compar-
ison, EMA incurs only a modest construction overhead relative to
NaviX, indicating that Marker construction does not significantly
impact build efficiency. The index size of Milvus is not directly
controllable at the algorithm level; the disk footprint is estimated
using the official Milvus sizing tool
5 
and the actual allocated size
is determined by fixed segment granularity.
EMA results in a moderately larger index size compared to several
baselines. This increase primarily stems from the storage of Markers,
where each edge maintains an additional compact Marker along-
side the neighbor identifier to encode attribute information. While
this design introduces extra storage overhead, it enables efficient
Marker checking prior to neighbor dereferencing, thereby reducing
unnecessary random memory accesses during query processing. As
5
https://milvus.io/tools/sizing
shown in our evaluation, this trade-off leads to substantial perfor-
mance benefits, particularly under restrictive and multi-predicate
filtering scenarios.
6 RELATED WORK
Filtered Approximate Nearest Neighbor (FANN) Algorithms.
Early work on filtered ANN search can be traced back to RII [ 29],
which considers subset search and can be viewed as an early form
of pre-filtering. Many filtered ANN methods have since been pro-
posed for diverse predicates. For numerical attributes with range
predicates, SeRF [55] stores edges for query ranges during HNSW
construction, and ESF [37 ] further supports dynamic insertions.
𝛽-WST [ 9 ] constructs a binary tree index with bounded depth to re-
duce construction cost, allowing limited overlap to ensure efficient
search over a small number of indexes. UNIFY [ 26 ] merges multiple
subgraphs with cross-subgraph edges to avoid querying multiple
indexes, while iRangeGraph [48 ] dynamically selects subgraphs
from a binary tree and performs efficient at low selectivity. For cat-
egorical attributes and label filtering, several label-aware methods
have been proposed. AIRSHIP [ 53 ] considers label distributions
and constructs a label-aware HNSW index. Filtered DiskANN [ 15 ]
performs label-aware search by modifying the pruning strategy of
DiskANN. NHQ [43 ] integrates label information directly into the
distance metric, achieving strong performance, but requires fixed
attribute cardinality and query predicate sizes, limiting its applica-
bility. Recently, predicate-agnostic filtering methods have gained
attention. ACORN [34 ] performs joint filtering and search via two-
hop expansion to mitigate connectivity issues under restrictive
12

Figure 10: QPS in OCQ at 95%
recall@10.
Table 5: Index size (GB) and time(s).
Algorithm 
YoutubeRGB1M (3.9GB) Redcaps4M (7.7GB) SIFT10M (4.9GB) Wiki15.4M (59GB)
Size Time Size Time Size Time Size Time
VBase - 1,429 - 4,162 - 7,549 - 48,609
Milvus 40 221 40 368 40 565 - -
ACORN 4.4 973 9.7 2,412 9.9 4,110 67 21,085
NaviX 4.2 51 8.9 73 7.9 82 64 391
Filtered DiskANN 4.0 474 8.4 2,219 18 4,794 62 7,070
iRangeGraph 5.4 4,582 13.9 11,001 24.9 22,002 - -
EMA 6.6 293 19 591 32 1,137 101 3,396
predicates. NaviX [ 39 ] further improves ACORN by introducing
more efficient cost estimation and traversal strategies.
System-Level FANN Methods. At the system level, relational
databases with vector support naturally enable predicate filtering
via post-filtering. PostgreSQL-based systems such as PGVector [24 ],
PASE [ 50 ], and VBase [52 ] adopt this paradigm. PASE performs iter-
ative search to improve reliability under filtering constraints, while
VBase relaxes monotonicity to support filtered queries. Modern
vector databases also incorporate filtering mechanisms. AnalyticDB-
V [ 47 ] supports both pre-filtering and post-filtering based on cost
estimation. Milvus [42 ] partitions indexes into multiple sub-indexes
to support subset search and parallel execution. Weaviate [46 ] inte-
grates ACORN [34] to enable efficient predicate filtering. Similarly,
Kuzu [ 11 ] incorporated NaviX [ 39 ] to enable high-performance
filtered ANN search.
7 CONCLUSION
In this paper, we propose EMA, a general attribute-filtering ANN
indexing framework that supports both range and label predicates
with arbitrary combinations. EMA introduces an edge-centric, hop-
agnostic Marker as a compact attribute collector, enabling efficient
predicate-aware navigation under joint filtering. The Marker con-
struction is theoretically guaranteed to avoid introducing false nega-
tives during Marker checking, while bounding the false positive rate.
To further enhance graph connectivity under low-selectivity set-
tings, we incorporate an edge recovery mechanism. EMA supports
patch, a lightweight local repair mechanism that enables efficient
dynamic updates and delays costly reconstruction while maintain-
ing stable performance. Extensive experiments demonstrate that
EMA consistently delivers strong performance across a wide range
of workloads, achieving speedups of 1.68×–12.25× over existing
methods while maintaining high recall.
REFERENCES
[1] 
Martin Aumüller, Erik Bernhardsson, and Alexander Faithfull. 2020. ANN-
Benchmarks: A benchmarking tool for approximate nearest neighbor algorithms.
Information Systems 87 (2020), 101374.
[2] 
Jon Louis Bentley. 1975. Multidimensional binary search trees used for associative
searching. Commun. ACM 18, 9 (1975), 509–517.
[3] Angela Bonifati, Wim Martens, and Thomas Timm. 2017. An analytical study of
large SPARQL query logs. arXiv preprint arXiv:1708.00363 (2017).
[4] Lawrence Cayton. 2008. Fast nearest neighbor retrieval for bregman divergences.
In Proceedings of the 25th international conference on Machine learning. 112–119.
[5] Karan Desai, Gaurav Kaul, Zubin Aysola, and Justin Johnson. 2021. Redcaps:
Web-curated image-text data created by the people, for the people. arXiv preprint
arXiv:2111.11431 (2021).
[6] Wei Dong. 2011. Kgraph: A library for approximate nearest neighbor search.
Retrieved July 12 (2011), 2020.
[7] 
Matthijs Douze, Alexandr Guzhva, Chengqi Deng, Jeff Johnson, Gergely Szilvasy,
Pierre-Emmanuel Mazaré, Maria Lomeli, Lucas Hosseini, and Hervé Jégou. 2024.
The faiss library. arXiv preprint arXiv:2401.08281 (2024).
[8] Darren Edge, Ha Trinh, Newman Cheng, Joshua Bradley, Alex Chao, Apurva
Mody, Steven Truitt, Dasha Metropolitansky, Robert Osazuwa Ness, and Jonathan
Larson. 2024. From local to global: A graph rag approach to query-focused
summarization. arXiv preprint arXiv:2404.16130 (2024).
[9] Joshua Engels, Benjamin Landrum, Shangdi Yu, Laxman Dhulipala, and Julian
Shun. 2024. Approximate Nearest Neighbor Search with Window Filters. arXiv
preprint arXiv:2402.00943 (2024).
[10] Mocheng Li et al. 2026. EMA: Appendix. https://github.com/lmccccc/EMA/blob/
main/EMA_appendix.pdf. Supplementary material.
[11] Xiyang Feng, Guodong Jin, Ziyi Chen, Chang Liu, and Semih Salihoğlu. 2023.
Kùzu graph database management system. In The Conference on Innovative Data
Systems Research, Vol. 7. 25–35.
[12] 
Jerome H Friedman, Jon Louis Bentley, and Raphael Ari Finkel. 1977. An algo-
rithm for finding best matches in logarithmic expected time. ACM Transactions
on Mathematical Software (TOMS) 3, 3 (1977), 209–226.
[13] 
Junhao Gan, Jianlin Feng, Qiong Fang, and Wilfred Ng. 2012. Locality-sensitive
hashing scheme based on dynamic collision counting. In Proceedings of the 2012
ACM SIGMOD international conference on management of data. 541–552.
[14] Tiezheng Ge, Kaiming He, Qifa Ke, and Jian Sun. 2013. Optimized product
quantization. IEEE transactions on pattern analysis and machine intelligence 36, 4
(2013), 744–755.
[15] Siddharth Gollapudi, Neel Karia, Varun Sivashankar, Ravishankar Krishnaswamy,
Nikit Begwani, Swapnil Raz, Yiyong Lin, Yin Zhang, Neelam Mahapatro, Premku-
mar Srinivasan, et al. 2023. Filtered-diskann: Graph algorithms for approximate
nearest neighbor search with filters. In Proceedings of the ACM Web Conference
2023. 3406–3416.
[16] Ruiqi Guo, Philip Sun, Erik Lindgren, Quan Geng, David Simcha, Felix Chern,
and Sanjiv Kumar. 2020. Accelerating large-scale inference with anisotropic
vector quantization. In International Conference on Machine Learning. PMLR,
3887–3896.
[17] Antonin Guttman. 1984. R-trees: A dynamic index structure for spatial searching.
In Proceedings of the 1984 ACM SIGMOD international conference on Management
of data. 47–57.
[18] 
Ben Harwood and Tom Drummond. 2016. Fanng: Fast approximate nearest
neighbour graphs. In Proceedings of the IEEE Conference on Computer Vision and
Pattern Recognition. 5713–5722.
[19] Qiang Huang, Jianlin Feng, Yikai Zhang, Qiong Fang, and Wilfred Ng. 2015.
Query-aware locality-sensitive hashing for approximate nearest neighbor search.
Proceedings of the VLDB Endowment 9, 1 (2015), 1–12.
[20] 
Piotr Indyk and Rajeev Motwani. 1998. Approximate nearest neighbors: towards
removing the curse of dimensionality. In Proceedings of the thirtieth annual ACM
symposium on Theory of computing. 604–613.
[21] Suhas Jayaram Subramanya, Fnu Devvrit, Harsha Vardhan Simhadri, Ravishankar
Krishnawamy, and Rohan Kadekodi. 2019. Diskann: Fast accurate billion-point
nearest neighbor search on a single node. Advances in neural information pro-
cessing Systems 32 (2019).
[22] Hervé Jégou, Romain Tavenard, Matthijs Douze, and Laurent Amsaleg. 2011.
Searching in one billion vectors: re-rank with source coding. In 2011 IEEE Inter-
national Conference on Acoustics, Speech and Signal Processing (ICASSP). IEEE,
861–864.
[23] 
Jeff Johnson, Matthijs Douze, and Hervé Jégou. 2019. Billion-scale similarity
search with GPUs. IEEE Transactions on Big Data 7, 3 (2019), 535–547.
[24] Andrew Kane. n.d.. PGVector. https://github.com/pgvector/pgvector.
13

[25] Philip M Lankford. 1968. Regionalization, Theory and alternative algorithms. Ph.D.
Dissertation. The University of Chicago.
[26] Anqi Liang, Pengcheng Zhang, Bin Yao, Zhongpu Chen, Yitong Song, and
Guangxu Cheng. 2024. UNIFY: Unified Index for Range Filtered Approximate
Nearest Neighbors Search. arXiv preprint arXiv:2412.02448 (2024).
[27] Yury Malkov, Alexander Ponomarenko, Andrey Logvinov, and Vladimir Krylov.
2014. Approximate nearest neighbor algorithm based on navigable small world
graphs. Information Systems 45 (2014), 61–68.
[28] 
Yu A Malkov and Dmitry A Yashunin. 2018. Efficient and robust approximate
nearest neighbor search using hierarchical navigable small world graphs. IEEE
transactions on pattern analysis and machine intelligence 42, 4 (2018), 824–836.
[29] Yusuke Matsui, Ryota Hinami, and Shin’ichi Satoh. 2018. Reconfigurable Inverted
Index. In Proceedings of the 26th ACM international conference on Multimedia.
1715–1723.
[30] Yitong Meng, Xinyan Dai, Xiao Yan, James Cheng, Weiwen Liu, Jun Guo, Benben
Liao, and Guangyong Chen. 2020. Pmd: An optimal transportation-based user
distance for recommender systems. In Advances in Information Retrieval: 42nd
European Conference on IR Research, ECIR 2020, Lisbon, Portugal, April 14–17, 2020,
Proceedings, Part II 42. Springer, 272–280.
[31] Thomas Neumann and Guido Moerkotte. 2011. Characteristic sets: Accurate
cardinality estimation for RDF queries with multiple joins. In 2011 IEEE 27th
International Conference on Data Engineering. IEEE, 984–994.
[32] Shumpei Okura, Yukihiro Tagami, Shingo Ono, and Akira Tajima. 2017.
Embedding-based news recommendation for millions of users. In Proceedings of
the 23rd ACM SIGKDD international conference on knowledge discovery and data
mining. 1933–1942.
[33] Liana Patel, Siddharth Jha, Carlos Guestrin, and Matei Zaharia. 2024. Lotus:
Enabling semantic queries with llms over tables of unstructured and structured
data. arXiv e-prints (2024), arXiv–2407.
[34] 
Liana Patel, Peter Kraft, Carlos Guestrin, and Matei Zaharia. 2024. Acorn: Per-
formant and predicate-agnostic search over vector embeddings and structured
data. Proceedings of the ACM on Management of Data 2, 3 (2024), 1–27.
[35] Arkadiusz Paterek. 2007. Improving regularized singular value decomposition
for collaborative filtering. In Proceedings of KDD cup and workshop, Vol. 2007.
5–8.
[36] Zhencan Peng, Miao Qiao, Wenchao Zhou, Feifei Li, and Dong Deng. [n.d.].
Dynamic Range-Filtering Approximate Nearest Neighbor Search. ([n. d.]).
[37] Zhencan Peng, Miao Qiao, Wenchao Zhou, Feifei Li, and Dong Deng. 2025.
Dynamic Range-Filtering Approximate Nearest Neighbor Search. Proceedings of
the VLDB Endowment 18, 10 (2025), 3256–3268.
[38] 
Alec Radford, Jong Wook Kim, Chris Hallacy, Aditya Ramesh, Gabriel Goh, Sand-
hini Agarwal, Girish Sastry, Amanda Askell, Pamela Mishkin, Jack Clark, et al.
2021. Learning transferable visual models from natural language supervision. In
International conference on machine learning. PmLR, 8748–8763.
[39] Gaurav Sehgal and Semih Salihoglu. 2025. NaviX: A Native Vector Index Design
for Graph DBMSs With Robust Predicate-Agnostic Search Performance. arXiv
preprint arXiv:2506.23397 (2025).
[40] Aditi Singh, Suhas Jayaram Subramanya, Ravishankar Krishnaswamy, and Har-
sha Vardhan Simhadri. 2021. Freshdiskann: A fast and accurate graph-based ann
index for streaming similarity search. arXiv preprint arXiv:2105.09613 (2021).
[41] 
Godfried T Toussaint. 1980. The relative neighbourhood graph of a finite planar
set. Pattern recognition 12, 4 (1980), 261–268.
[42] Jianguo Wang, Xiaomeng Yi, Rentong Guo, Hai Jin, Peng Xu, Shengjun Li, Xi-
angyu Wang, Xiangzhou Guo, Chengming Li, Xiaohai Xu, et al . 2021. Milvus:
A purpose-built vector data management system. In Proceedings of the 2021
international conference on management of data. 2614–2627.
[43] Mengzhao Wang, Lingwei Lv, Xiaoliang Xu, Yuxiang Wang, Qiang Yue, and
Jiongkang Ni. 2024. An efficient and robust framework for approximate near-
est neighbor search with attribute constraint. Advances in Neural Information
Processing Systems 36 (2024).
[44] Mengzhao Wang, Xiaoliang Xu, Qiang Yue, and Yuxiang Wang. 2021. A com-
prehensive survey and experimental comparison of graph-based approximate
nearest neighbor search. arXiv preprint arXiv:2101.12631 (2021).
[45] Shu Wang, Yixiang Fang, Yingli Zhou, Xilin Liu, and Yuchi Ma. 2025. ArchRAG:
Attributed Community-based Hierarchical Retrieval-Augmented Generation.
arXiv preprint arXiv:2502.09891 (2025).
[46] Weaviate. 2024. Weaviate. https://weaviate.io/.
[47] Chuangxian Wei, Bin Wu, Sheng Wang, Renjie Lou, Chaoqun Zhan, Feifei Li,
and Yuanzhe Cai. 2020. AnalyticDB-V: a hybrid analytical engine towards query
fusion for structured and unstructured data. Proceedings of the VLDB Endowment
13, 12 (2020), 3152–3165.
[48] Yuexuan Xu, Jianyang Gao, Yutong Gou, Cheng Long, and Christian S Jensen.
2024. irangegraph: Improvising range-dedicated graphs for range-filtering near-
est neighbor search. Proceedings of the ACM on Management of Data 2, 6 (2024),
1–26.
[49] Mingyu Yang, Wentao Li, Zhitao Shen, Chuan Xiao, and Wei Wang. 2025. ESG:
Elastic Graphs for Range-Filtering Approximate k-Nearest Neighbor Search.
arXiv preprint arXiv:2504.04018 (2025).
[50] Wen Yang, Tao Li, Gai Fang, and Hong Wei. 2020. Pase: Postgresql ultra-high-
dimensional approximate nearest neighbor search extension. In Proceedings of the
2020 ACM SIGMOD international conference on management of data. 2241–2253.
[51] Peter Yianilos. 1993. Data structures and algorithms for nearest neighbor search
in general metric spaces. (1993).
[52] Qianxi Zhang, Shuotao Xu, Qi Chen, Guoxin Sui, Jiadong Xie, Zhizhen Cai, Yaoqi
Chen, Yinxuan He, Yuqing Yang, Fan Yang, et al . 2023. {VBASE}: Unifying
online vector similarity search and relational queries via relaxed monotonicity.
In 17th USENIX Symposium on Operating Systems Design and Implementation
(OSDI 23). 377–395.
[53] Weijie Zhao, Shulong Tan, and Ping Li. 2022. Constrained approximate similarity
search on proximity graph. arXiv preprint arXiv:2210.14958 (2022).
[54] 
Yingli Zhou, Yaodong Su, Youran Sun, Shu Wang, Taotao Wang, Runyuan He,
Yongwei Zhang, Sicong Liang, Xilin Liu, Yuchi Ma, et al. 2025. In-depth Analysis
of Graph-based RAG in a Unified Framework. arXiv preprint arXiv:2503.04338
(2025).
[55] Chaoji Zuo, Miao Qiao, Wenchao Zhou, Feifei Li, and Dong Deng. 2024. SeRF: seg-
ment graph for range-filtering approximate nearest neighbor search. Proceedings
of the ACM on Management of Data 2, 1 (2024), 1–26.
14