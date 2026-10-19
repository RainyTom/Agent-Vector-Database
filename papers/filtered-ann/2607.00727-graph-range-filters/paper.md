# 2607.00727v1 原始 PDF 文本提取

来源：https://arxiv.org/pdf/2607.00727v1

提取日期：2026-10-18。由 PyMuPDF 提取，未经 MinerU 版式转换；公式和表格需回看 PDF。

## PDF page 1

Approximate Nearest Neighbor Search with Graph Range Filters
Qian Tao
Beihang University
Beijing, China
qiantao@buaa.edu.cn
Yuntao Jiang
Beihang University
Beijing, China
yuntaojiang@buaa.edu.cn
Yongxin Tong∗
Beihang University
Beijing, China
yxtong@buaa.edu.cn
Yu Sun
Nankai University
Tianjin, China
sunyu@nankai.edu.cn
ABSTRACT
Vector databases have become a fundamental component for high-
dimensional vector retrieval in artificial intelligence applications.
Recent research has focused on filtered approximate nearest neigh-
bor search (filtered ANN), which involves retrieving the nearest
vectors that satisfy a given attribute-based filter. However, exist-
ing filters are generally limited to numerical range constraints or
categorical existence checks, which restricts their applicability in
more complex, real-world scenarios. In this paper, we investigate
filtered ANN using graph range filters, where the retrieved vectors
must be within a specified distance from the query node in a pre-
defined filter graph. To address this problem, we propose DLH, a
Distance-aware Labeling index with Hashing compression. DLH
creates distance-aware labeling sets to enable efficient graph range
filters via the simplified set intersection operations. Large labeling
sets are further compressed into Bloom filters to improve query
efficiency in DLH. Furthermore, recognizing that the query node is
always involved in in-range queries of the graph range filters, we
enhance DLH by memoizing the intermediate hashing index for the
query node, yielding an optimized version called DLH-M. Experi-
mental evaluations on diverse datasets demonstrate that DLH and
DLH-M improve throughput by up to 70.3%, and could maintain
recall rates over 98.5% with limited extra storage, validating the
practical availability of the proposed solution.
Artifact Availability:
The source code has been made available at https://github.com/TaoLbr1993/
DLH.
1
INTRODUCTION
As a fundamental component of modern artificial intelligence, vec-
tor databases have attracted considerable attention due to their
powerful vector search capabilities, and have become a core com-
ponent of Retrieval Augmented Generation (RAG) systems [11, 15].
They underpin a wide range of Intelligent tasks, including domain
knowledge question answering [37], text generation [39], image
generation [50], etc. The core functionality of vector databases lies
in approximate nearest neighbor (ANN) search, which retrieves the
semantically nearest vectors to a given query vector.
In practical applications, vector data is frequently coupled with
diverse attributes, necessitating filtered ANN search to retrieve the
nearest vectors that strictly adhere to specific attribute-based filters.
For instance, a product search on an e-commercial platform may
require identifying similar items that were sold within the past three
weeks. While current filtering criteria effectively handle numerical
ranges [26, 48, 51] and categorical existence checks [3, 17, 30], they
are typically defined on oversimplified or low-cardinality attributes.
∗Yongxin Tong is the corresponding author.
Figure 1: ANN search with graph range filters.
This limitation constrains their applicability in complex, real-world
filtered ANN search scenarios.
Real-world applications require filtered ANN search over com-
plicated structured attributes. In scenarios such as graph-based
RAG [9, 29], agent memory management [7], and anti-money laun-
dering [21], similarity is evaluated among vectors that are con-
strained by a pre-defined filter graph. This motivates a novel yet
practical filtered ANN search setting. In this setting, the attributes
associated with vectors are derived from graph-structured data,
rather than independent labels. To illustrate the necessity of this
formulation, consider the following motivating example.
Example 1. As illustrated in Fig. 1, consider an anti-money laun-
dering (AML) system designed to identify suspicious users by ana-
lyzing capital flows. Each bank account, along with its metadata, is
encoded as an embedding. Notably, embeddings of accounts belonging
to the same user or those involved in similar transaction patterns tend
to exhibit higher proximity in vector space [40]. Given a potentially
threatening account represented by its embedding vector, the bank
retrieves accounts that satisfy two criteria: (1) topological reachability,
i.e., they are within 𝑟transaction hops from the target accounts in the
transaction graph, and (2) semantic similarity, i.e., their embeddings
are among the nearest neighbors to the target’s vector.
Example 1 motivates the Approximate Nearest Neighbor search
with Graph Range filters (ANNGR) problem, where each vector is
associated with a node in a filter graph. The retrieved vectors are
required to be not only similar to the query vector but also located
within a 𝑟-hop neighborhood of the query node.
Different from existing filtered ANN search settings, ANNGR
introduces two key challenges, as illustrated in Fig. 2.
C1: Explosive Volume of Candidate Attributes. In the ANNGR prob-
lem, the attributes, specifically the corresponding nodes in the filter
graph, form a candidate space with a cardinality comparable to the
total number of vectors. Such a massive attribute space renders the
construction of attribute-independent ANN indices (as exploded in
arXiv:2607.00727v1  [cs.DB]  1 Jul 2026


## PDF page 2

Qian Tao, Yuntao Jiang, Yongxin Tong, and Yu Sun
Figure 2: Challenges of ANNGR problem.
[45, 51]) computationally prohibitive, as it would require maintain-
ing an impractical number of separate indexing structures.
C2: Irregular and Unpredictable Candidate Distribution. Due to the
inherent complexity of graph topologies, the 𝑟-hop neighborhoods
of different query nodes, along with their potential overlaps, are
highly irregular and stochastic. This unpredictability precludes the
feasibility of precomputing or exploiting these overlaps to design
specialized indices, as is often done in simpler categorical filtering.
To overcome the above limitations, this paper proposes DLH, a
Distance-aware Labeling index with Hashing compression, specif-
ically engineered for the ANNGR problem. DLH integrates con-
ventional ANN indices (e.g., HNSW [33]) with a novel, lightweight
distance-aware labeling index. Within this framework, graph range
filters are efficiently executed via a limited number of set intersec-
tion operations. To optimize memory footprint and computational
throughput, DLH compresses extensive labeling sets into fixed-size
bit vectors using Bloom filters, thereby enabling approximate set
intersections with bounded error rates. Furthermore, observing
that the query node consistently serves as the source for numerous
in-range queries, we introduce DLH-M. This variant memoizes the
pre-computed hashing indices of query nodes prior to the retrieval
phase, thereby substantially boosting overall query efficiency by
eliminating redundant computations.
In summary, this paper makes the following key contributions.
• To the best of our knowledge, we are the first to formally
define and investigate the Approximate Nearest Neighbor
search with Graph Range filters (ANNGR) problem.
• We propose DLH, a distance-aware labeling index inte-
grated with hashing compression. This framework enables
efficient ANNGR processing in the post-filtering manner.
• Leveraging the asymmetric nature of in-range predicates
in graph-structured data, we introduce DLH-M. This vari-
ant strategically optimizes the post-filtering pipeline by
memoizing query-side indices, leading to superior query
performance.
• Extensive experiments on diverse datasets demonstrate
that our proposed algorithms outperform baseline methods,
improving ANNGR query efficiency by up to 70.3%.
The remainder of this paper is organized as follows. In Sec. 2,
we present the preliminaries. In Sec. 3, we introduce the proposed
DLH, followed by DLH-M in Sec. 4. The experimental evaluation is
presented in Sec. 5. We review related work in Sec. 6 and conclude
the paper in Sec. 7.
2
PRELIMINARIES
2.1
Problem Definition
Define a set of vectors 𝐷= {e1, ..., e𝑛}, where each e𝑖is a 𝑑𝑖𝑚-
dimensional vector and 𝑛denotes the cardinality of 𝐷. For any two
vectors e𝑖and e𝑗, we denote their distance by 𝑑(e𝑖, e𝑗). Common
distance functions include Euclidean distance and cosine similarity-
based distance [45].
Each vector e𝑖∈𝐷is associated with a node e𝑖.𝑣∈𝑉in a filter
graph 𝐺= (𝑉, 𝐸), where 𝑉= {𝑣∗
1, . . . , 𝑣∗
𝑛𝑓} and 𝐸⊆𝑉× 𝑉denote
the sets of nodes and edges, respectively. When the context is clear,
we use 𝑣𝑖interchangeably to refer to e𝑖.𝑣. For any node 𝑢∈𝑉,
𝐺[𝑢] denotes its set of neighbors in 𝐺, and 𝑑𝐺(𝑢, 𝑣) represents the
shortest-path distance between nodes 𝑢and 𝑣in the filter graph.
We assume that 𝐺is undirected and that multiple vectors in 𝐷may
be associated with the same node in 𝐺.
Definition 1 (Nearest Neighbor Search with Graph Range
Filters (NNGR)). Given a set of vectors 𝐷, where each vector e𝑖∈𝐷
is associated with a node 𝑣𝑖in the filter graph 𝐺, a query (e𝑞, 𝑣𝑞,𝑘,𝑟),
and an integer 𝑘, the Nearest Neighbor search with Graph Range
filters (NNGR) problem returns a set 𝑅⊆𝐷of 𝑘vectors satisfying the
following two conditions:
• 𝑅consists of the 𝑘nearest neighbors to the query vector e𝑞
among all vectors in 𝐷. Formally, |𝑅| = 𝑘and for every e𝑖∈𝑅
and e𝑗∈𝐷\ 𝑅, it holds that 𝑑(e𝑞, e𝑖) ≤𝑑(e𝑞, e𝑗);
• the node𝑣𝑖associated with each e𝑖∈𝑅satisfies𝑑𝐺(𝑣𝑞, 𝑣𝑖) ≤𝑟,
i.e., it lies within 𝑟hops of 𝑣𝑞in 𝐺.
Example 2. Fig. 3 illustrates an example for the ANNGR problem
involving 9 vectors. The left part of Fig. 3 lists the vectors e1, . . . , e9
along with the query vector e𝑞, ordered by increasing vector distance to
e𝑞. The right part shows the corresponding filter graph 𝐺, where each
of the 10 vectors (including e𝑞) is associated with a node in𝐺. Consider
the query (e𝑞, 𝑣𝑞, 1, 2) which seeks the single nearest neighbor to e𝑞
among vectors whose associated nodes lie within in a shortest-path
distance of at most 2 hops from 𝑣𝑞in the filter graph. Although e4 is
the closest vector to e𝑞in embedding space, its associated node lies
more than 2 hops away from 𝑣𝑞, violating the graph-range constraint.
Consequently, e2 is the closest valid candidate within the allowed
range and should be returned as the result.
Following recent efforts in approximate nearest neighbor search
queries [36, 38, 45, 51], this paper focuses on the approximate near-
est neighbor search with graph range filters.
Definition 2 (Approximate Nearest Neighbor Search with
Graph Range Filters (ANNGR)). Given a vector set 𝐷, a filter
graph𝐺, and an ANNGR query (e𝑞, 𝑣𝑞,𝑘,𝑟), the Approximate Nearest
Neighbor search with Graph Range filters (ANNGR) problem returns
a set 𝑅⊆𝐷of 𝑘vectors such that:
• the vectors in 𝑅approximately minimize the distances to
the query vector e𝑞, i.e., they form an approximate top-𝑘set
among all vectors in 𝐷whose associated nodes lie within 𝑟
hops of 𝑣𝑞;


## PDF page 3

Approximate Nearest Neighbor Search with Graph Range Filters
Figure 3: An example of the ANNGR problem.
Algorithm 1: ANN Search with NSW Graph
Input: NSW graph 𝐺∗, query vector 𝑞, entry point 𝑒𝑝,
predicate function 𝑓, beam search width 𝑏, 𝑘nearest
neighbors.
1 mark 𝑒𝑝as visited;
2 initialize min-heap 𝑝𝑜𝑜𝑙and max-heap 𝑎𝑛𝑛;
3 push 𝑒𝑝to 𝑝𝑜𝑜𝑙and 𝑎𝑛𝑛;
4 while 𝑝𝑜𝑜𝑙is not empty do
5
𝑢←nearest vector to 𝑞in 𝑝𝑜𝑜𝑙; 𝑝𝑜𝑜𝑙.𝑝𝑜𝑝();
6
𝑣←farthest vector to 𝑞in 𝑎𝑛𝑛; 𝑎𝑛𝑛.𝑝𝑜𝑝();
7
if𝑑𝑖𝑠(𝑞,𝑢) > 𝑑𝑖𝑠(𝑞, 𝑣) and |𝑎𝑛𝑛| = 𝑏then break;
foreach unvisited 𝑜∈𝐺[𝑢] do
8
mark 𝑜as visited and add 𝑜to 𝑝𝑜𝑜𝑙;
9
if 𝑖𝑛_𝑟𝑎𝑛𝑔𝑒(𝑣𝑞, 𝑣𝑜,𝑟) and (|𝑎𝑛𝑛| < 𝑏or
𝑑𝑖𝑠(𝑞,𝑜) < 𝑑𝑖𝑠(𝑞, 𝑣)) then
10
add 𝑜to 𝑎𝑛𝑛;
11 𝑎𝑛𝑛←𝑘nearest vectors to 𝑞in 𝑎𝑛𝑛;
12 return 𝑎𝑛𝑛;
• the node 𝑣𝑖∈𝑉associated to each vector e𝑖∈𝑅satisfies
𝑑𝐺(𝑣𝑞, 𝑣𝑖) ≤𝑟.
Following prior works, we use recall, defined as |𝑅∩𝑅∗|
𝑘
, to eval-
uate the quality of an ANNGR result, where 𝑅∗denotes the set of
exact 𝑘nearest neighbors as defined in Definition 1.
2.2
NSW for ANN Search
A mainstream approach to approximate nearest neighbor search
employs navigation small world (NSW) graphs [12, 33]. In an NSW
graph 𝐺∗= (𝑉∗, 𝐸∗), each node 𝑣∗
𝑖corresponds to a vector e𝑖and is
connected to a small set of neighboring nodes whose vectors are
highly similar (i.e., close under the distance metric 𝑑(·, ·)), thereby
forming a “small world” neighborhood around the node. Given
a query vector e𝑞, an ANN search can be performed using the
precedure outlined in Algorithm 1. Notably, the entry point 𝑒𝑝
could be an arbitrary point in the classic NSW [12] or a specially
selected node in Hierarchical NSW (HNSW) [33].
In Algorithm 1, a min-heap 𝑝𝑜𝑜𝑙and a max-heap 𝑎𝑛𝑛are main-
tained to store candidate nodes and filtered results, respectively
(lines 2-3). The algorithm iteratively pops the vector 𝑢with the
smallest distance to the query vector e𝑞from 𝑝𝑜𝑜𝑙(line 5) and
evaluates its neighbors in 𝐺∗. For each neighbor that satisfies the
predicate function 𝑖𝑛_𝑟𝑎𝑛𝑔𝑒, the algorithm inserts it into 𝑎𝑛𝑛, pro-
vided 𝑎𝑛𝑛has fewer than 𝑘elements or the new point is closer to
e𝑞than the farthest point currently in 𝑎𝑛𝑛(lines 8-10). The search
terminates when all remaining candidates in 𝑝𝑜𝑜𝑙are no closer to
e𝑞than the farthest vector in 𝑎𝑛𝑛. Notably, while the graph tra-
versal considers all reachable candidates, only those satisfying the
predicate 𝑖𝑛_𝑟𝑎𝑛𝑔𝑒are admitted into 𝑎𝑛𝑛.
In-Range Query for ANNGR Problem. For the ANNGR problem,
the predicate function 𝑖𝑛_𝑟𝑎𝑛𝑔𝑒(𝑣𝑞, 𝑣𝑜,𝑟) checks whether the node
𝑣𝑜(associated with vector e𝑜in the filter graph 𝐺) lies within 𝑟
shortest-path distance of the node 𝑣𝑞(associated with query vector
e𝑞). We define this check as in-range query.
Definition 3 (In-Range Query). Given the query node 𝑣𝑞and
a candidate node 𝑣𝑜retrieved from the filter graph 𝐺, an in-range
query determines whether 𝑣𝑜lies within graph range 𝑟of 𝑣𝑞in 𝐺, i.e.,
whether 𝑑𝐺(𝑣𝑞, 𝑣𝑜) ≤𝑟.
The in-range query can be achieved in various ways. For instance,
a straightforward approach is to first perform a breadth-first search
(BFS) from 𝑣𝑞in the filter graph 𝐺to enumerate all nodes within
graph distance 𝑟, and then check whether the candidate node 𝑣𝑜
belongs to this set. Other shortest-path indexing structures can also
be employed for the in-range query, as illustrated in Sec. 2.3.
The search procedure outlined in Algorithm 1, combined with
the in-range query, consists of a post-filtering solution [22] to the
ANNGR problem. Candidate vectors close to the query vectors are
first retrieved and then decided whether they are located in the
𝑟-hop of the query node in the filter graph.
2.3
Naive Solution: NSW with Labeling
In the ANNGR problem, evaluating the predicate requires comput-
ing the shortest-path distance between the query node 𝑣𝑞and the
candidate node 𝑣𝑜to determine whether 𝑑𝐺(𝑣𝑞, 𝑣𝑜) ≤𝑟. This moti-
vates the use of shortest path indexing techniques to enable efficient
in-range queries. Recent works has proposed Pruned Landmarked
Labeling (PLL) [2], which supports fast and exact shortest-path
distance queries between node pairs in a graph.
Definition 4 (Pruned Landmarked Labeling (PLL)). Given a
graph 𝐺= (𝑉, 𝐸), a Pruned Landmarked Labeling (PLL) consists of
a labeling 𝐿that assigns to each node 𝑣∈𝑉a set of node-distance
pairs 𝐿(𝑣) = {(𝑢,𝑑𝐺(𝑢, 𝑣)) | 𝑢∈𝐶(𝑣)}, where 𝐶(𝑣) ⊆𝑉is the set
of hub nodes of 𝑣. The labeling 𝐿satisfies the following 2-hop cover
property: for any pair of nodes 𝑢, 𝑣∈𝑉, there exists at least one node
𝑤∈𝐶(𝑢) ∩𝐶(𝑣) such that 𝑑𝐺(𝑢, 𝑣) = 𝑑𝐺(𝑢,𝑤) + 𝑑𝐺(𝑤, 𝑣). In other
words, the shortest path between 𝑢and 𝑣can be reconstructed via a
common hub 𝑤in their label sets.
Given the PLL of a graph 𝐺, the shortest-path distance between
two nodes 𝑢, 𝑣∈𝑉can be computed by examining the paths that
pass through their common hub nodes in 𝐶(𝑢) ∩𝐶(𝑣). Formally,


## PDF page 4

Qian Tao, Yuntao Jiang, Yongxin Tong, and Yu Sun
Figure 4: An example for Labeling NSW.
the distance is given by
𝑑𝑖𝑠(𝑢, 𝑣) =
min
𝑦∈𝐶(𝑢)∩𝐶(𝑣) 𝑑𝑖𝑠(𝑢,𝑦) + 𝑑𝑖𝑠(𝑦, 𝑣).
(1)
We refer the reader to previous works [2] for details on the correct-
ness and construction of PLL.
Labeling NSW for ANNGR Problem. To address the ANNGR
problem, a straightforward approach is to associate each vector e𝑖
in the NSW index with the computed PLL 𝐿(𝑣𝑖) of its corresponding
node 𝑣𝑖in the filter graph 𝐺, which enables a possible solution to
the in-range filtering process during search. As illustrated in Fig. 4,
when evaluating whether the node 𝑣𝑜of a candidate vector e𝑜lies
within 𝑟hops of the query node 𝑣𝑞(i.e., at line 9 of Algorithm 1),
we compute the shortest-path distance 𝑑𝐺(𝑣𝑞, 𝑣𝑜) using the PLL via
Equation (1), and accept e𝑜if 𝑑𝐺(𝑣𝑖, 𝑣𝑞) ≤𝑟.
Limitations of Labeling NSW. Based on the correctness guaran-
tees of PLL [2], Labeling NSW can always accurately determine
whether a candidate node lies within 𝑟hops of the query node 𝑣𝑞in
the filter graph. However, in practice, the space overhead of the la-
beling is substantial: the size of PLL often grows superlinearly with
the scale and diameter of the graph, leading to significant memory
consumption and cache inefficiency. This overhead degrades query
throughput to the extent that Labeling NSW underperforms even a
naive baseline that explicitly materializes all 𝑟-hop neighbors of 𝑣𝑞
during query processing.
2.4
Bloom Filter
A Bloom filter [5, 31] is a space-efficient probabilistic data structure
for approximate set membership queries. Formally, a Bloom filter
consists of an array 𝐵of 𝑚bits (initially all set to 0) and a family
of 𝑡independent hash functions 𝐻= {ℎ1, ...,ℎ𝑡}, each mapping
elements uniformly to {0, 1, . . . ,𝑚−1}. The best false positive
probability (FPP) of a Bloom filter is 𝛼= ( 1
2)
𝑚
𝑟𝑙𝑛2 where 𝑟is the
number of inserted elements.
Algorithm 2: Bloom Filter Operations
1 Function insert(𝐵, 𝑏):
2
foreach ℎ𝑖∈𝐻do
3
𝐵[ℎ𝑖(𝑏)] ←1;
4 Function contain(𝐵, 𝑏):
5
foreach ℎ𝑖∈𝐻do
6
if 𝐵[ℎ𝑖(𝑏)] = 0 do return false;
7
return true;
8 Function aic(𝐵1, 𝐵2):
9
𝑐←0;
10
foreach 𝑖= 0, ...,𝑚do
11
𝑐←𝑐+ 𝐵1[𝑖]|𝐵2[𝑖];
12
𝑎𝑐←𝐵1.𝑐𝑛𝑡+ 𝐵2.𝑐𝑛𝑡+ 𝑚
𝑘ln {1 −𝑎𝑐
𝑚};
As shown in Algorithm 2, Bloom filters support three key opera-
tions: insertion, containment query, and approximate estimation of set
intersection. To insert an element 𝑏, all bits 𝐵[ℎ1(𝑏)], ..., 𝐵[ℎ𝑡(𝑏)]
are set to 1 (lines 2-3). To query whether the Bloom filter contains
𝑏, we check if all bits 𝐵[ℎ1(𝑏)], . . . , 𝐵= ℎ𝑡[(𝑏)] are set to 1. If any
bit is 0, 𝑏is definitely not in the set (line 6). However, if all bits
are 1 (line 7), the query may yield a false positive, i.e., 𝑏might not
actually be in the set.
For two Bloom filters 𝐵1 and 𝐵2 built over the same parameters,
the size of their set intersection, i.e., the aic function in Algorithm 2,
can be approximated using the number of 1-bits from the bitwise
OR operations. Specifically, let 𝑤= 𝐵1 ∨𝐵2 denote the bitwise
OR cardinality (i.e., the count of positions where either filter has a
1). The intersection size could then be estimated via the inclusion-
exclusion principle, as illustrated in line 12 of Algorithm 2.
3
DLH: A POST-FILTERING APPROACH
This section first presents an overview of DLH, followed by detailed
descriptions of its two core components: Distance-Aware Labeling
and Hash-Based Compression.
3.1
Overview
Figure 5 presents the overview of the proposed DLH. In general,
DLH consists of two main components: Distance-Aware Labeling
introduced in Sec. 3.2, which enables efficient and pruned in-range
queries for the ANNGR problem, and Hash-Based Compression
introduced in Sec. 3.3, which further supports fast approximate
in-range queries using Bloom filters.
Distance-Aware Labeling. For each node in the filter graph, DLH
stores partial labels corresponding to nodes whose distance from
the source is at most the graph range 𝑟. These labels are parti-
tioned into distinct labeling sets based on their distance from the
source node (shown in orange in Figure 5), a strategy referred to
as Distance-Aware Labeling (DAL). DAL not only enables efficient
in-range queries through simple set intersections, but also contains
distance-aware pruning and early stopping strategies that eliminate
unnecessary computations.


## PDF page 5

Approximate Nearest Neighbor Search with Graph Range Filters
𝐷!
…
𝒗𝟑
DLH of 𝒗𝟑
0 1 0 1
…
…
1 1 0 1
…
𝐷!
0-hop labels 𝐷!
…
𝒗𝒒
1-hop labels 𝐷"
DLH of 𝒗𝒒
1 0 0 1
…
…
2-hop labels 𝐷#
1 1 0 0
…
…
…
…
…
𝑰𝟏 ⋁ 𝑰𝟐 ⋁ 𝑰𝟑
Labels in 
Bloom Filter
Unified BF Params
𝑚: Number of Bits
𝐻: Hashing Functions
Labels in Set
…
…
(Sec. 3.3)
(Sec. 3.2)
Set-Set Intersection:
Direct Intersection
Set-Hash Intersection:
BF Containment
…
1 1 0 0
…
1 0 0 0
Hash-Hash Intersection:
Cardinality Estimation
…
1 1 0 0
…
1 0 0 1
…
1 1 0 1
Cnt<1: 
not Intersect
0-hop labels 𝐷!
1-hop labels 𝐷" 2-hop labels 𝐷#
Offline Index Construction
In-Range Query
True
False
Pruned 
Computation
Early 
Stopping
Figure 5: Overview of DLH.
Hash-Based Compression. To further enhance efficiency and
reduce the index size, DLH compresses each labeling set containing
more than 𝑊elements into a Bloom filter (shown in green in Fig-
ure 5). Under this hashing-based transformation, set intersections
can be achieved by containment queries and approximate inter-
section cardinality estimates using Bloom filters, whenever one or
both of the input labeling sets are represented in Bloom filter.
As shown in the right part of Figure 5, under DLH, an in-range
query is answered by performing set intersection operations be-
tween the distance-aware labeling sets of the two query nodes. The
specific manner of this operation depends on the representation of
the labeling sets (i.e., explicit sets or Bloom filters). Furthermore,
thanks to the design of Distance-Aware Labeling in DLH, unneces-
sary set intersections can be pruned early, and an early stopping
mechanism is employed to further enhance efficiency.
3.2
Distance-Aware Labeling: In-Range Query
with Pruned Set Intersection
Rationale. In the in-range queries of the ANNGR problem, we
do not need to know the exact shortest path length between the
query node 𝑣𝑞and a candidate node 𝑣𝑜. Instead, we only need to
determine whether the shortest path between 𝑣𝑞and 𝑣𝑜is at most 𝑟.
This observation motivates the design of a Distance-Aware Labeling
(DAL) index, which partitions the labeling hubs by distance to the
source node, to enable an efficient solution to the ANNGR problem.
Construction of Distance-Aware Labeling. For each node 𝑣in
𝐺, we partition its PLL labeling 𝐿(𝑣) into multiple labeling sets
based on the distance from each labeled node to the source node
(i.e., 𝑣itself). Consequently, during an in-range query, determining
whether a candidate node 𝑣𝑜lies within distance 𝑟of the query node
𝑣𝑞reduces to performing set intersection operations between the
corresponding Distance-Aware Labeling sets of 𝑣𝑞and 𝑣𝑜, enabling
efficient in-range verification for the ANNGR problem.
Algorithm 3: Distance-Aware Labeling Construction
Input: The PLL index 𝐿(𝑣) of 𝑣.
1 𝐷𝑖(𝑣) ←∅for 0 ≤𝑖≤𝑟;
2 for (𝑢,𝑑𝑖𝑠𝐺(𝑢, 𝑣)) ∈𝐿(𝑣) do
3
if 𝑑𝑖𝑠𝐺(𝑢, 𝑣) ≤𝑟then
4
Append 𝑢to 𝐷𝑑𝑖𝑠𝐺(𝑢,𝑣) (𝑣);
5 return {𝐷0(𝑣), ..., 𝐷𝑟(𝑣)};
Formally, we define the 𝑖-path labeling set, denoted by
𝐷𝑖(𝑣) = {𝑢|(𝑢,𝑑𝑖𝑠𝐺(𝑢, 𝑣)) ∈𝐿(𝑣) ∧𝑑𝑖𝑠𝐺(𝑢, 𝑣) = 𝑖}
(2)
as the set of nodes 𝑢such that (i) 𝑢appears in 𝐿(𝑣), and (ii) the
shortest-path distance from 𝑢to 𝑣is exactly 𝑖. The Distance-Aware
Labeling of 𝑣is then defined as the collection of 𝑖-path labeling sets
for 𝑖≤𝑟, denoted by
𝐷(𝑣) = {𝐷0(𝑣), ..., 𝐷𝑟(𝑣)}.
(3)
If the context is clear, we let 𝑑𝑖
𝑣= |𝐷𝑖(𝑣)| denote the size of the set,
and define 𝑑𝑣= Í
𝑖≤𝑟𝑑𝑖
𝑣as the total size of all labeling sets in 𝐷(𝑣).
Algorithm 3 presents the construction of distance-aware labeling
from the shortest path labeling index [2]. Only 𝑖-path labeling sets
with 𝑖≤𝑟are stored. To construct the Distance-Aware Labeling
for a node 𝑣, we simply iterate over the entries in 𝐿(𝑣) (line 2)
and append each qualifying node to the corresponding labeling set
𝐷𝑖(𝑣) (lines 3–4). Labels corresponding to distances greater than 𝑟
are discarded, as they do not affect the results of in-range queries.
In-Range Query with Distance-Aware Labeling. In contrast to
shortest-path queries on traditional shortest path labeling index,
which require computing the exact distance, we only need to deter-
mine whether a path of length at most 𝑟exists in the filter graph.
This observation motivates two key pruning strategies based on
distance-aware labeling to enable efficient in-range queries.


## PDF page 6

Qian Tao, Yuntao Jiang, Yongxin Tong, and Yu Sun
Algorithm 4: In-Range Query with Distance-Aware Label-
ing for ANNGR problem
Input: Node of query vector 𝑣𝑞along with labeling 𝐷(𝑣𝑞), a
candidate node 𝑣𝑜along with labeling 𝐷(𝑣𝑜), range 𝑟.
1 for 𝑖= 0, ...,𝑟do
2
for 𝑗= 0, ...,𝑟−𝑖do
3
if 𝐷𝑖(𝑣𝑞) ∩𝐷𝑗(𝑣𝑜) ≠∅then
4
return true;
5 return false;
Pruning Computations Based on Distance. Certain computations can
be pruned by leveraging distance information. Although we retain
all label entries whose distance to the query node 𝑣𝑞is at most 𝑟,
the intersection between two DAL labeling sets 𝐷𝑖(𝑣𝑞) and 𝐷𝑗(𝑣𝑜)
can be safely skipped whenever 𝑖+ 𝑗> 𝑟, since any path through a
common hub would have length greater than 𝑟.
Early Stop for In-Range Query. In the ANNGR problem, the in-range
query seeks only to determine whether the shortest path between
𝑣𝑞and 𝑣𝑜is at most 𝑟, rather than computing its exact length. This
implies that we can immediately conclude 𝑣𝑜lies within 𝑟-range of
𝑣𝑞as soon as a common hub yielding a path of length ≤𝑟is found,
without enumerating all entries in 𝐷(𝑣𝑞) and 𝐷(𝑣𝑜).
Algorithm 4 presents the pseudocode for the in-range query
based on Distance-Aware Labeling. Since the labels are partitioned
by their distance and the query range 𝑟is pre-defined, the algorithm
iterates over all pairs of indices (𝑖, 𝑗) such that 𝑖+ 𝑗≤𝑟(lines 1–2)
and checks whether the labeling sets 𝐷𝑖(𝑣𝑞) and 𝐷𝑗(𝑣𝑜) intersect. If
an intersection is found, it implies the existence of a path of length
at most 𝑟between 𝑣𝑞and 𝑣𝑜, and the algorithm immediately returns
the result true. Otherwise, the algorithm returns the result false
if no intersection is found (line 5).
Addressing ANNGR with DAL. Leveraging the Distance-Aware
Labeling design, the ANNGR problem is solved by integrating Algo-
rithm 1 and Algorithm 4. Specifically, the DAL index is constructed
offline from the filter graph prior to query processing (as illustrated
in Algorithm 3). Given a query 𝑞, Algorithm 1 is used to enumerate
candidate vectors and select the results whose corresponding nodes
satisfy the graph range filters in a post-filtering manner. Whenever
a candidate 𝑜requires verification, i.e., whether the corresponding
node 𝑣𝑜lies within distance 𝑟of the query node 𝑣𝑞(as in the in-
range query at line 9 of Algorithm 1), Algorithm 4 is invoked to
perform this check using the DAL index.
Example 3. Figure 6 illustrates an example of the construction
and query process using Distance-Aware Labeling. During the offline
construction phase, labels are partitioned into sets according to their
distance from the source node. Labels with distance greater than the
query range 𝑟= 3 (i.e., the gray crossed-out entries) are discarded.
At query phase, suppose we need to determine whether a candidate
node 𝑣𝑜lies within distance 𝑟of the query node 𝑣𝑞. We then evaluate
set intersections between 𝐷𝑖(𝑣𝑞) and 𝐷𝑗(𝑣𝑜) for all pairs (𝑖, 𝑗) such
that 𝑖+ 𝑗≤𝑟(indicated by the black arrows with marks ①and ②in
the bottom part of Figure 6). The intersections with 𝑖+ 𝑗> 𝑟will be
discarded (indicated by the gray arrow with mark ④in Figure 6).
Figure 6: Example of Distance-Aware Labeling.
Time Complexity. The construction of the DAL incurs a time com-
plexity of 𝑂(Í
𝑣∈𝐺|𝐿(𝑣)|), as it requires enumerating all entries in
the shortest-path index. For in-range query using DAL, comput-
ing the intersection between 𝐷𝑖(𝑣𝑞) and 𝐷𝑗(𝑣𝑜) takes 𝑂(|𝐷𝑖(𝑣𝑞)| +
|𝐷𝑗(𝑣)|), when hash tables are used to represent sets. Consequently,
the total time complexity of Algorithm 4 is
∑︁
𝑖+𝑗≤𝑟
𝑂(|𝐷𝑖(𝑣𝑞)| + |𝐷𝑗(𝑣𝑜)|) = 𝑂(𝑑𝑣𝑞+ 𝑑𝑣𝑜)
(4)
where 𝑑𝑣𝑞and 𝑑𝑣𝑜denote the total sizes of the labeling sets in 𝐷(𝑣𝑞)
and 𝐷(𝑣𝑜), respectively.
Building on the correctness of PLL, which always returns the ex-
act shortest-path length between two nodes, the proposed Distance-
Aware Labeling guarantees the correctness of DAL.
Corollary 1. Distance-Aware Labeling, together with Algorithm 4,
correctly determines whether a candidate node 𝑣𝑜lies within the 𝑟-hop
of the query node 𝑣𝑞.
Corollary 1 holds because the shortest-path distance 𝑟′ between
nodes 𝑣𝑞and 𝑣𝑜satisfies 𝑟′ ≤𝑟if and only if there exist labeling
sets 𝐷𝑖(𝑣𝑞) and 𝐷𝑗(𝑣𝑜) such that 𝑖+ 𝑗≤𝑟and 𝐷𝑖(𝑣𝑞) ∩𝐷𝑗(𝑣𝑜) ≠∅.
Consequently, any pair of sets 𝐷𝑖(𝑣𝑞) and 𝐷𝑗(𝑣𝑜) with 𝑖+ 𝑗> 𝑟can
be safely pruned, leading to reduced pruned computation overhead.
Moreover, as long as a non-empty intersection is found, the in-range
query can immediately return true, which validates the correctness
of the early stop strategy.
3.3
DLH: Accelerating DAL via Hashing
Rationale. The employment of hashing techniques is motivated
by the following key observations. (1) ANN search allows false
positives. Approximate nearest neighbor search permits a controlled
relaxation of correctness by allowing false positives. Motivated by
this trade-off, hashing techniques can be leveraged to accelerate
query processing for the ANNGR problem further. (2) The employed
technique should support approximate set intersection. DAL relies
on set intersection operations for in-range queries in the ANNGR
problem. The hash-based representations of the labeling sets must
facilitate efficient set intersection operations to enable in-range


## PDF page 7

Approximate Nearest Neighbor Search with Graph Range Filters
Algorithm 5: In-Range Query With DLH
Input: Node of query vector 𝑣𝑞along with labeling 𝐷(𝑣𝑞)
and bloom filter sets 𝐵(𝑣𝑞), node of a candidate 𝑣𝑜
along with labeling 𝐷(𝑣𝑜) and Bloom filter sets 𝐵(𝑣),
range 𝑟.
1 for 𝑖= 0, ...,𝑟do
2
for 𝑗= 0, ...,𝑟−𝑖do
3
if 𝐵𝑖(𝑣𝑞) ≠∅and 𝐵𝑖(𝑣𝑜) ≠∅then
4
if aic(𝐵𝑖(𝑣𝑞), 𝐵𝑗(𝑣𝑜)) ≥1 then
5
return true;
6
else if 𝐵𝑖(𝑣𝑞) ≠∅then
7
foreach 𝑏∈𝐷𝑗(𝑣𝑜) do
8
if contain(𝐵𝑖(𝑣𝑞),𝑏) then
9
return true;
10
else if 𝐵𝑗(𝑣𝑜) ≠∅then
11
foreach 𝑏∈𝐷𝑖(𝑣𝑞) do
12
if contain(𝐵𝑗(𝑣𝑜),𝑏) then
13
return true;
14
else
15
if 𝐷𝑖(𝑣𝑞) ∩𝐷𝑗(𝑣𝑜) ≠∅then
16
return true;
17 return false;
queries. To this end, this paper adopts the Bloom filter as a compact,
compressed representation of labeling sets.
Bloom Filters for Efficient Set Intersections. Given a set of pre-
defined hash functions 𝐻, each 𝑖-path labeling set 𝐷𝑖(𝑣) whose size
exceeds a threshold 𝑇is mapped to a Bloom filter of bit length 𝑚,
denoted by 𝐵𝑖(𝑣). During the set intersection checks in Algorithm 4,
whenever one or both of the involved labeling sets have correspond-
ing Bloom filters, we instead perform set intersection using these
Bloom filters. For completeness, we define 𝐵𝑖(𝑣) = ∅if no Bloom fil-
ter is constructed for 𝐷𝑖(𝑣), and let 𝐵(𝑣) = {𝐵1(𝑣), ..., 𝐵𝑟(𝑣)} denote
the collection of all Bloom filters associated with node 𝑣.
Algorithm 5 presents the pseudocode for the query procedure in
DLH. DLH retains the distance-aware strategy for in-range queries.
When determining whether the intersection of the 𝑖-path labeling
set of 𝑣𝑞and the 𝑗-path labeling set of 𝑣𝑜is empty, the algorithm
selects different intersection-checking operations depending on the
availability of Bloom filters. (1) If both labeling sets are equipped
with Bloom filters (lines 3-5), an approximate estimation of set
intersection is computed. The two sets are considered to intersect if
this count exceeds zero. (2) If only one set has a Bloom filter (lines
6-13), the algorithm iterates over each element in the other set and
checks its containment in the Bloom filter. (3) Finally, when neither
set has an associated Bloom filter (lines 14-16), the algorithm falls
back to the exact intersection method used in Algorithm 4.
Example 4. Consider the labeling sets in Example 3. Fig. 7 il-
lustrates how the DLH index is derived from the DAL index and
employed during in-range queries. Specifically, for the node 𝑣𝑞in the
filter graph, its labeling sets, namely 𝐷1(𝑣𝑞) and 𝐷2(𝑣𝑞), both exceed
Figure 7: Example of DLH.
the size threshold 𝑇= 3 and are therefore encoded into Bloom filters.
Likewise, 𝐷1(𝑣3) and 𝐷2(𝑣3) of 𝑣3 are also converted into Bloom filters.
During the in-range query between 𝑣𝑞and 𝑣3, the algorithm selects
the appropriate intersection strategy based on the representation types
of the two labeling sets:
• Set–set intersection is used for the intersection between 𝐷0(𝑣𝑞)
and 𝐷0(𝑣3);
• Set–hash intersection is used for the intersection between a
labeling set and a Bloom filter, like 𝐷0(𝑣𝑞) and 𝐵2(𝑣3), and
𝐵2(𝑣𝑞) and 𝐷0(𝑣3);
• Hash–hash intersection is used for the intersection between
𝐵1(𝑣𝑞) and 𝐵1(𝑣3).
Specifically, since the intersection between 𝐵2(𝑣𝑞) and 𝐷0(𝑣3) is non-
empty, the in-range query terminates early and returns true.
Time Complexity. Let D<(𝑣) denote the collection of distance-
aware labeling sets for node 𝑣whose sizes are at most 𝑇, i.e., those
stored explicitly as sets, and let 𝑑<
𝑣be the total size of all sets in
D<(𝑣). Formally,
D<(𝑣) = {𝐷𝑖(𝑣)| |𝐷𝑖(𝑣)| ≤𝑇},𝑑<
𝑣=
∑︁
𝐷𝑗(𝑣)∈D< (𝑣)
|𝐷𝑗(𝑣)|.
(5)
We then obtain the following lemma.
Lemma 1. The time complexity of Algorithm 5 is 𝑂(𝑑<
𝑣𝑞· 𝑡+ 𝑑<
𝑣·
𝑡+ 𝑐>
𝑣𝑞· 𝑐>
𝑣· 𝑡), where 𝑐>
𝑣denotes the number of labeling sets whose
size is at least𝑇and 𝑡is the number of hashing functions in the Bloom
filters. Further considering that 𝑑<
𝑣𝑞,𝑑<
𝑣< 𝑟𝑇and 𝑐>
𝑣𝑞,𝑐>
𝑣≤𝑟, the time
complexity is bounded by 𝑂(𝑟𝑡𝑇+ 𝑟2𝑡).
Proof. We analyze the time complexity by case distinction.
(1) For set intersections between two sets in D<(𝑣), our analysis in
Sec. 3.2 shows the total time complexity is 𝑂(𝑑<
𝑣𝑞+ 𝑑<
𝑣).
(2) Consider the set intersection between a labeling of 𝐷𝑖(𝑣𝑞) in
explicit set form and another labeling set 𝐷𝑗(𝑣) represented as
a Bloom filter 𝐵𝑗(𝑣). To determine whether 𝐵𝑗(𝑣) contains any
element from 𝐷𝑖(𝑣𝑞), each element from 𝐷𝑖(𝑣𝑞) is evaluated against
all 𝑡hash functions of the Bloom filter (see Algorithm 2), which
takes 𝑂(𝑡) time per element. Since this check is performed for
every element in all such set-type labeling sets of 𝑣𝑞, the total time


## PDF page 8

Qian Tao, Yuntao Jiang, Yongxin Tong, and Yu Sun
Algorithm 6: ANN Search for DLH-M
Input: NSW graph 𝐺∗, query vector 𝑞, entry point 𝑒𝑝,
predicate function 𝑓, beam search width 𝑏, 𝑘nearest
neighbors.
1 mark 𝑒𝑝as visited;
2 initialize min-heap 𝑝𝑜𝑜𝑙and max-heap 𝑎𝑛𝑛;
3 push 𝑒𝑝to 𝑝𝑜𝑜𝑙and 𝑎𝑛𝑛;
4 𝑃𝐿←preh(𝐷(𝑣𝑞));
5 while 𝑝𝑜𝑜𝑙is not empty do
6
𝑢←nearest vector to 𝑞in 𝑝𝑜𝑜𝑙; 𝑝𝑜𝑜𝑙.𝑝𝑜𝑝();
7
𝑣←farthest vector to 𝑞in 𝑎𝑛𝑛; 𝑎𝑛𝑛.𝑝𝑜𝑝();
8
if𝑑𝑖𝑠(𝑞,𝑢) > 𝑑𝑖𝑠(𝑞, 𝑣) and |𝑎𝑛𝑛| = 𝑏then break;
foreach unvisited 𝑜∈𝐺[𝑢] do
9
mark 𝑜as visited and add 𝑜to 𝑝𝑜𝑜𝑙;
10
if 𝑖𝑛_𝑟𝑎𝑛𝑔𝑒_𝑝𝑟𝑒(𝑃𝐿, 𝑣𝑜,𝑟) and (|𝑎𝑛𝑛| < 𝑏or
𝑑𝑖𝑠(𝑞,𝑜) < 𝑑𝑖𝑠(𝑞, 𝑣)) then
11
add 𝑜to 𝑎𝑛𝑛;
12 𝑎𝑛𝑛←𝑘nearest vectors to 𝑞in 𝑎𝑛𝑛;
13 return 𝑎𝑛𝑛;
complexity for this case is
∑︁
𝐷𝑖(𝑣𝑞)∈D< (𝑣𝑞)
∑︁
𝑒∈𝐷𝑖(𝑣𝑞)
𝑡= 𝑑<
𝑣𝑞𝑡.
(6)
(3) Similarly to case (2), for the intersection between a Bloom filter
𝐵𝑖(𝑣𝑞) and a labeling set 𝐷𝑗(𝑣) stored in explicit set form, the total
time complexity is 𝑑<
𝑣𝑡.
(4) For the intersection between two labeling sets represented as
Bloom filters, i.e., 𝐵𝑖(𝑣𝑞) and 𝐵𝑗(𝑣), the approximate intersection
count is typically computed using bit-level operations, with the
time complexity 𝑂(𝑡). This comparison is performed for all pairs
of labeling sets that are mapped to Bloom filters, yielding a total
time complexity of 𝑂(𝑐>
𝑣𝑞· 𝑐>
𝑣· 𝑡).
Thus, the total time complexity is 𝑂(𝑑<
𝑣𝑞· 𝑡+𝑑<
𝑣· 𝑡+ 𝑐>
𝑣𝑞· 𝑐>
𝑣· 𝑡).
Besides, since there are at most 𝑟sets in D<(𝑣) and each set has size
< 𝑇, we have 𝑑<
𝑣𝑞and 𝑑<
𝑣< 𝑟𝑇. The time complexity is 𝑂(𝑟𝑡𝑇+𝑟2𝑡).
□
4
DLH-M: DLH WITH MEMOIZATION
Sec. 3 presents a post-filtering approach for ANNGR problem based
on the DLH index. In this section, we observe that range queries in
DLH exhibit asymmetry. This observation motivates a more effi-
cient solution to the ANNGR problem, achieved by the memoization
of intermediate hashing results prior to the ANN search process.
Rationale. In DLH, in-range queries always focus on determin-
ing whether a candidate node lies within the range of the query
node. For set intersections between the explicit labeling sets of the
query node and the Bloom filters of candidate nodes, the intermedi-
ate hashing results can be precomputed and memoized before the
search process, thereby avoiding redundant computations.
Basic Idea. During the intersection test between the set-type label-
ing sets of the query node 𝑣𝑞and the Bloom filter-type labeling sets
Algorithm 7: Memoization and Contain Check functions
for DLH-M
1 Function preh(D(𝑣𝑞)):
2
𝑃𝐿, 𝑃𝑡←∅;
3
foreach 𝐷𝑖(𝑣𝑞) ∈D(𝑣𝑞) do
4
𝑃𝑖
𝐿←∅;
5
foreach 𝑣∈𝐷𝑖(𝑣𝑞) do
6
𝑃𝑖
𝐿[𝑣] ←{ℎ𝑖(𝑣)|ℎ𝑖∈𝐻};
7
Append 𝑃𝑖
𝐿to 𝑃𝐿;
8
return 𝑃𝐿;
9 Function in_range_pre(𝑃𝐿, 𝑣𝑜, 𝑟):
10
Lines 1-10 of Algorithm 5;
11
if lcontain(𝐵𝑗(𝑣), 𝑃𝑖
𝐿) then
12
return 𝑡𝑟𝑢𝑒;
13
Lines 14-17 of Algorithm 5;
14 Function lcontain(𝐵𝑗(𝑣), 𝑃𝑖
𝐿):
15
foreach ℎ𝑖∈𝑃𝑖
𝐿do
16
𝑓𝑙𝑎𝑔←𝑡𝑟𝑢𝑒;
17
foreach 𝑖∈ℎ𝑖do
18
if 𝐵𝑗(𝑣)[𝑖] = 0 then
19
𝑓𝑙𝑎𝑔←𝑓𝑎𝑙𝑠𝑒;
20
break ;
21
if 𝑓𝑙𝑎𝑔then return 𝑡𝑟𝑢𝑒;
22
return 𝑓𝑎𝑙𝑠𝑒;
of a candidate node 𝑣(lines 11–13 of Algorithm 5), the contain
operation always maps elements from 𝑣𝑞’s labeling sets into hash
indices (lines 4–7 of Algorithm 2). These intermediate hash indices
can be memoized prior to the ANN search process to avoid redun-
dant computation.
Algorithm 6 presents the pseudocode of DLH-M. In contrast
to the vanilla search process of DLH (i.e., Algorithm 1), DLH-M
precomputes the hash representations of query node 𝑣𝑞’s labeling
sets and stores them as memoized data (line 4). Consequently, the
in-range query is evaluated using these precomputed hash bits (line
10) instead of original labeling sets of 𝑣𝑞.
Algorithm 7 presents the procedure for generating the prepro-
cessed hash bits (preh()) and for utilizing them (lcontain()). In
preh(), the algorithm first computes the hash indices using the
hash functions 𝐻, as shown in line 4 of Algorithm 6 and lines 1–8
of Algorithm 7. Specifically, each 𝑃𝑖
𝐿stores the hash indices corre-
sponding to the label nodes in 𝐷𝑖(𝑣𝑞) ∈D(𝑣𝑞). When a set inter-
section between 𝐵𝑗(𝑣) and 𝐷𝑖(𝑣𝑞) is required, we use the function
in_range_pre() to achieve the in-range query. in_range_pre()
differs from Algorithm 5 that it uses lcontain(𝐵𝑗(𝑣), 𝑃𝑖
𝐿) for the
intersection test between the set-type labels of 𝑣𝑞and Bloom filter-
type labels of 𝑣. The lcontain(𝐵𝑗(𝑣), 𝑃𝑖
𝐿) function enumerates the
pre-computed hashing indices in 𝑃𝑖
𝐿(lines 16-21) to judge if there
exist intersections (line 19) between current labeling sets.
Example 5. Consider the example in Fig. 8. Assume that 𝐷0(𝑣𝑞) of
the query node 𝑣𝑞is stored as a set and participates in in-range queries
via set–hash intersection with the Bloom filters of different candidate


## PDF page 9

Approximate Nearest Neighbor Search with Graph Range Filters
(a) DLH: Duplicated Hashing Computations.
(b) DLH-M: Pre-Hashing as Memoization.
Figure 8: An example of DLH-M.
nodes𝑣3 and𝑣8. As illustrated in Fig. 8a, in DLH the elements of 𝐷0(𝑣𝑞)
are mapped to Bloom filter indices each time an in-range query is
invoked. In contrast, as shown in Fig. 8b, these hash indices can be
precomputed before the query process of DLH-M, thereby eliminating
a large number of redundant hash computations.
Time Complexity. The memoized intermediate hashing indices
can be reused across multiple in-range queries. Therefore, we ana-
lyze the average time complexity of DLH over all in-range queries
issued by candidate vectors.
Lemma 2. Let 𝑛𝑐denote the number of candidates subject to in-
range queries, and let 𝑡be the average number of retrieved neighbors
per query. The average time complexity of an in-range query is then
𝑂(𝑑<
𝑣𝑞· 𝑡+ 𝑑<
𝑣· 𝑡+ 𝑐>
𝑣𝑞· 𝑐>
𝑣· 𝑡).
Proof. The key difference between DLH and DLH-M lies in
how the set intersection between 𝐷𝑖(𝑣𝑞) (represented explicitly)
and 𝐵𝑗(𝑣) (encoded as a Bloom filter) is performed.
For the 𝑛𝑐in-range queries processed by DLH-M, memoizing the
intermediate hashing indices (i.e., preh(·)) takes 𝑂(𝑑<
𝑣𝑞· 𝑡) time in
total, and each individual in-range query requires 𝑂(𝑑<
𝑣𝑞·𝑡) time to
test containment. Consequently, the total time complexity for 𝑛𝑐in-
range queries is𝑂(𝑑<
𝑣𝑞·𝑡+𝑛𝑐·𝑑<
𝑣𝑞·𝑡) for𝑛𝑐in-range queries. Thus, the
average time complexity per in-range query is𝑂(𝑑<
𝑣𝑞· 𝑡
𝑛𝑐+𝑑<
𝑣𝑞·𝑡). In
practice, the number of candidates 𝑛𝑐is often much larger than the
fixed number of hash functions 𝑡, rendering the first term negligible.
Hence, the average time complexity simplifies to 𝑂(𝑑<
𝑣𝑞· 𝑡).
Based on the above discussion and the result in Lemma 1, the
average time complexity of a single in-range query is 𝑂(𝑑<
𝑣𝑞· 𝑡+
𝑑<
𝑣· 𝑡+ 𝑐>
𝑣𝑞· 𝑐>
𝑣· 𝑡).
□
Table 1: Datasets.
Dataset
Category
#dim
Similarity
SIFT
Image
128
Euclidean
GIST
Image
960
Euclidean
DEEP
Image
96
Euclidean
YFCC
Image
192
Cosine
Discussion. Notably, DLH-M guarantees the exact output to DLH,
as the memoization accurately records the hashing indices of the
labeling sets of the query node. These hash indices are then utilized
to test whether there exist set intersections between the labeling
sets of the query node in set type and that of the candidate in Bloom
filter type, which behaves identically to DLH.
5
EXPERIMENTAL STUDY
5.1
Experimental Setup
Datasets. Following previous works [20, 45], four vector datasets
are considered in our experiment, as illustrated in Table 1. Specifi-
cally, we consider the vector datasets including SIFT [20], GIST [20],
DEEP [4], and YFCC [41]. We generate 1 million vectors from each
dataset for the experiments.
Workloads. To address the performance of DLH on the ANNGR
problem, we generate a filter graph of 𝑛= 80, 000 nodes by the
Erdos-Renyi model 𝐺(𝑛, 𝑝) [10] for each vector dataset with 𝑝=
{0.00025, 0.00027, 0.0003}. The nodes are then associated with the
vectors uniformly. For each query, we retrieve the 𝑘-nearest neigh-
bors (𝑘= 128) within a graph range of 𝑟∈{3, 4, 5, 6}, where 𝑟= 4
serves as the default parameter.
Baselines. We compare DLH and DLH-M with the following base-
lines. In these baselines, a BFS search on the filter graph is first
utilized before the search process to find the 𝑟-hop neighborhood
of the query node. The obtained node set is then employed as a
predicate function for distinguishing whether a candidate locates
in 𝑟-hop neighbors of the query node.
• Pre-Filter [46]: It first obtains those vectors that satisfy the
graph range, and then enumerates all result vectors to find
the closest vectors. No index is used in this baseline.
• Post-Filter (HNSW) [33]: It first searches the vectors in
the increasing order of distance to the query vector based
HNSW, and then validates whether the vectors satisfy the
graph filter.
• ACORN [36]: The index supports ANN search with arbi-
trary filters by a dedicated graph index. We choose default
configurations 𝑀= 32 and 𝑀𝛽= 64, and utilize the inverse
of the ratio of nodes that satisfy the filter in the graph.
• Navix [38]: The index supports ANN with arbitrary filters
by integration of HNSW and a prefiltering strategy.
Implementation. All methods are implemented in C++ with -Ofast
optimization. For DLH, the default maximum number of neighbors
and beam search width in the ANN index are chosen as 𝑀= 16


## PDF page 10

Qian Tao, Yuntao Jiang, Yongxin Tong, and Yu Sun
(a) SIFT (𝑝= 0.00025)
(b) GIST (𝑝= 0.00025)
(c) DEEP (𝑝= 0.00025)
(d) YFCC (𝑝= 0.00025)
(e) SIFT (𝑝= 0.00027)
(f) GIST (𝑝= 0.00027)
(g) DEEP (𝑝= 0.00027)
(h) YFCC (𝑝= 0.00027)
(i) SIFT (𝑝= 0.0003)
(j) GIST (𝑝= 0.0003)
(k) DEEP (𝑝= 0.0003)
(l) YFCC (𝑝= 0.0003)
Figure 9: Overall Performance.
and 𝑏= 200, respectively. For the parameters of the Bloom filters
in DLH, we choose the FPP 𝛼= 0.01. All experiments are repeated
in 5 times, and the average is reported.
Environment. All experiments are conducted on a server with
Intel Xeon(R) Gold 6240 2.60GHZ CPU processors.
5.2
Experiments on Search Performance
To evaluate the search performance of the proposed algorithms,
we compare DLH and DLH-P with the baselines considering the
following metrics.
• Queries per second (QPS) defines the number of processed
queries per second, measuring the efficiency for ANN search.
• Recall rates define the proportion of vectors that satisfy the
filter in all of the obtained results. It reflects the quality of
the ANN search results.
We report the results of QPS vs. recall rates by varying beam
search width 𝑏in Algorithm 1 to evaluate the search performance,
following previous works [24, 44, 45].
Overall Performance. Fig. 9 presents the search performance of
DLH and DLH-M. Generally speaking, DLH and DLH-M obtain
up to 70.3× QPS under identical recall rate compared to the base-
lines in most cases, with DLH-M obtaining higher QPS than DLH.
(a) QPS.
(b) Recall Rate.
Figure 10: Experimental Results on Varying FPP.
Meanwhile, both proposed indices achieve recall rates exceeding
98% across four datasets while maintaining higher QPS than the
baselines, demonstrating the strong retrieval performance of pro-
posed indices under the Bloom filter approximation. In comparison,
all of the baselines reveal a decreased and unchanged QPS under
various recall rates. This is attributed to the large part of the time
it takes to find the neighbors of ℎ-hop of the naive BFS search in
the filter graph, which dominates the whole ANN search process.


## PDF page 11

Approximate Nearest Neighbor Search with Graph Range Filters
(a) QPS.
(b) Recall Rate.
Figure 11: Experimental Results on Varying Graph Range 𝑟.
Performance on Varying Datasets. Comparing the performance
of the algorithms on different datasets (horizontal comparison of
Fig. 9), we observe that DLH and DLH-M on SIFT, DEEP and YFCC
show a much steeper improvement in the QPS-recall trade-off (e.g.,
QPS> 103 with recall > 96%). In contrast, the results on GIST de-
crease more steadily. This is attributed to the higher dimensionality
of vectors in the GIST dataset, which intensifies the computational
overhead of vector similarity calculations. Regardless of the choice
of 𝑏, these computations consistently dominate larger portion of
time while the search process. Consequently, this leads to a modest
yet steady decline in QPS for the GIST dataset.
Performance on Varying Sparsity of Filter Graphs. When
varying the sparsity of the filter graph via 𝑝(vertical comparison
of Fig. 9), we observe a slight QPS decrease for the baselines. This
is because, with a larger 𝑝(i.e., larger average degrees in the filter
graph), all of the baselines spend more time finding all vectors that
satisfy the graph range filter. In contrast, although larger average
degrees of the filter graphs may lead to larger labeling sets in
DLH and DLH-M, the Bloom filters utilize a similar number of bits
for compressed representations of these sets, thus guaranteeing
similar trends for the proposed algorithms. This validates the well
extensibility of DLH and DLH-M for more dense filter graphs.
Performance on Varying FPP of Bloom Filters. Fig. 10 reports
the QPS and recall rates of DLH and DLH-M on SIFT. With FPP
of Bloom filters varying from 0.001 to 0.015, the QPS of both DLH
and DLH-M stay stable, while the recall rates slightly decrease (still
over 99%). This is because a larger FPP can lead to false positives
of in-range queries, thus resulting in slight decrease in the recall
rate. The decrease is acceptable, indicating the effectiveness of the
employed Bloom filters for hash compression.
Performance on Varying Graph Range 𝑟. Fig. 11 presents the
experimental results on varying the graph range 𝑟. With ℎincreases
from 3 to 6, the recall rates stay stable, validating the advancement
of DLH and DLH-M. The QPS continues to grow as 𝑟approaches
5, but stabilizes beyond this point. This trend occurs because in-
creasing 𝑟improves the proportion of candidate vectors that satisfy
the graph range constraint, thereby enhancing the QPS. However,
when 𝑟reaches 5, the vectors meeting the constraint encompass
nearly all nodes within the filter graph. Consequently, the QPS for
(a) QPS.
(b) Recall Rate.
Figure 12: Search performance of scalability test.
𝑟= 5 and 𝑟= 6 converges to a similar level. This observation holds
for both DLH and DLH-M, which achieve comparable performance.
5.3
Experiments on Scalability Test
As shown in Fig. 12, to test the scalability of the proposed indices,
we increase the total number of vectors from 2×106 to 10×106, and
report the QPS and recall rate of DLH and DLH-M under 𝑏= 100
and 𝑏= 250. As shown in Fig. 12a, with the dataset size increasing,
the QPS of the proposed indices keep stable. This is attributed to the
design of DLH that keeps a stable time cost for in-range queries and
stable error rate of Bloom filters. Meanwhile, both DLH and DLH-M
achieve slightly lower QPS when 𝑏increases, as more candidates
are considered to test whether they satisfy the graph filter. Besides,
DLH-M keeps identical recall rates to DLH, because DLH-M ensures
exact output to DLH.
5.4
Experiments on Index Construction
To evaluate the storage performance of the proposed indices, we
report the index size of DAL+HNSW (in Sec. 3.2) and DLH+HNSW
(in Sec. 3) against the original ANN index, i.e., HNSW, under differ-
ent sparsity 𝑝for the filter graphs. We omit the results for DLH-M
as it has identical results to DLH.
Overall Performance. Figure 13 illustrates the index sizes of the
three evaluated indices. DAL incurs the highest storage overhead,
requiring 1.41× to 1.76× more space than DLH and HNSW, respec-
tively. This significant footprint stems from the inherently complex
structure of filter graphs, which necessitate the construction of
extensive labeling sets, particularly for large-scale datasets. In con-
trast, DLH consumes only 1.29× the storage of the original HNSW
index. This modest increase is primarily due to the integration of
Bloom filters for set compression, confirming that DLH achieves
superior space efficiency while maintaining high performance.
Performance on Varying 𝑝. With 𝑝increasing from 0.00025 to
0.0003 (i.e., the reported results from Fig. 13a to Fig. 13c), the extra
index size of DAL and DLH increases from 500MB and 218MB
to 678MB and 296MB, respectively. The size of DLH keeps slight
compared to the HNSW index size of 630MB. This validates the
flexibility of DLH under filter graphs with different sparsity.


## PDF page 12

Qian Tao, Yuntao Jiang, Yongxin Tong, and Yu Sun
(a) SIFT(𝑝= 0.00025)
(b) SIFT(𝑝= 0.00027)
(c) SIFT(𝑝= 0.00032)
Figure 13: Indexing construction space overhead on the SIFT dataset.
Performance on Varying 𝑀. When varying the maximum degree
𝑀in the HNSW (i.e., various 𝑀in Fig. 13a, Fig. 13b and Fig. 13c),
the index size of HNSW (i.e., the blue bars) increases from 630MB
to 752MB (the blue bar in Fig. 13). Meanwhile, since the index size
of DAL and DLH is independent of the original index, the index
size keeps stable under varied 𝑀.
5.5
Summary
We draw the following observations from the evaluation.
• Both proposed indices achieve recall rates exceeding 98.5%
while delivering significantly higher QPS than the baselines,
demonstrating their strong performance.
• The proposed indices exhibit stable and robust performance
across varying parameters, including the FPP of the Bloom
filter and the graph range in the filter graph.
• The indices incur less than 1.29× the storage overhead of
the original vector indices, indicating good space efficiency.
6
RELATED WORKS
Attributed Vector Database. Similarity search on vectors has been
widely adopted in both academics and industry [1, 8, 35, 43, 46].
The high dimensionality of the vectors in the database causes chal-
lenges in the similarity search process, leading to a research direc-
tion of approximate vector similarity search. Recent efforts have
been proposed to achieve efficient approximate similarity search
from various perspectives, including hashing compression [32, 42],
clustering-based pruning [6], quantization [14, 16, 20, 34]. Among
them, graph-based indices emerge as the preference due to their
significant performance [13, 28, 33, 44, 47, 49].
More recent attentions have focused on the attributed vector
databases [22, 27], i.e., vectors are associated with certain attributes,
and the retrieved similar vectors should satisfy given filters. Com-
mon attributes and filters include numerical attributes and the range
filters [26, 48, 51], and category attributes and hit filters [3, 17, 30].
Other studies support arbitrary attributes and filters [36, 38].
Among these works, those designed for numerical and category
attributes cannot be applied to the ANNGR problem in this paper
due to the complicated graph range distributions for different query
vectors. Solutions to arbitrary attributes fit for the ANNGR problem,
but are limited to the performance bottleneck of finding the ℎ-hop
range neighborhoods.
Index Design for Graph RAGs. Recent studies have focused
on RAGs with graph-structured relationships [7, 9]. This demand
motivates a related research line of index design for graph RAGs.
For example, [18] and [9] map the adjacent information and the
k-hop in the graph into vectors, respectively. The vectors are then
indexed and retrieved as the regular vectors in the vector data-
base. Other works [19, 23, 25] seek to convert the graph structures
into natural language, such that the retrieved context from RAGs
contains enriched information from graphs.
These works aim at enriching the graph structure information
of RAGs in format of vectors or texts. In comparison, our paper
focuses on the index design for a more specific and practical 𝑘-NN
search problem from the perspective of vector database, which is
orthogonal to these works.
7
CONCLUSION
This paper investigates the filtered ANN problem with graph range
filters, which aims to retrieve the top-𝑘vectors that are both highly
similar to the query and satisfy structural constraints in the filter
graph. We propose DLH, a post-filtering framework that constructs
a lightweight, distance-aware labeling index with Bloom filter-
based hashing compression, enabling efficient query processing
with low overhead. Observing the asymmetry in in-range queries,
we further design an improved index, namely DLH-M, which ex-
ploits memoization of intermediate hashing results to reduce re-
dundant computations and improve query efficiency. Extensive
experiments on diverse real-world datasets demonstrate that our
approaches outperform state-of-the-art baselines, achieving up to
a 70.4× improvement in QPS while maintaining high recall (above
98.5%) with modest space overhead making our approach a solution
for practical systems that require both high-performance similarity
search and flexible filtering capabilities.
REFERENCES
[1] 2025. Vector index in Qdrant.
https://qdrant.tech/documentation/concepts/
indexing/#vector-index
[2] Takuya Akiba, Yoichi Iwata, and Yuichi Yoshida. 2013. Fast exact shortest-path
distance queries on large networks by pruned landmark labeling. In SIGMOD.
[3] Anas Ait Aomar, Karima Echihabi, Marco Arnaboldi, Ioannis Alagiannis, Damien
Hilloulin, and Manal Cherkaoui. 2025. RWalks: Random Walks as Attribute


## PDF page 13

Approximate Nearest Neighbor Search with Graph Range Filters
Diffusers for Filtered Vector Search. SIGMOD (2025).
[4] Artem Babenko and Victor S. Lempitsky. 2016. Efficient Indexing of Billion-Scale
Datasets of Deep Descriptors. In CVPR.
[5] Burton H. Bloom. 1970. Space/Time Trade-offs in Hash Coding with Allowable
Errors. CACM (1970).
[6] Qi Chen, Bing Zhao, Haidong Wang, Mingqin Li, Chuanjie Liu, Zengzhong Li,
Mao Yang, and Jingdong Wang. 2021. SPANN: Highly-efficient Billion-scale
Approximate Nearest Neighborhood Search. In NeurIPS.
[7] Prateek Chhikara, Dev Khant, Saket Aryan, Taranjeet Singh, and Deshraj Yadav.
2025. Mem0: Building Production-Ready AI Agents with Scalable Long-Term
Memory. CoRR (2025).
[8] Matthijs Douze, Alexandr Guzhva, Chengqi Deng, Jeff Johnson, Gergely Szilvasy,
Pierre-Emmanuel Mazaré, Maria Lomeli, Lucas Hosseini, and Hervé Jégou. 2024.
The Faiss library. CoRR (2024).
[9] Darren Edge, Ha Trinh, Newman Cheng, Joshua Bradley, Alex Chao, Apurva
Mody, Steven Truitt, and Jonathan Larson. 2024. From Local to Global: A Graph
RAG Approach to Query-Focused Summarization. CoRR (2024).
[10] P. Erdös and A. Rényi. [n.d.]. On Random Graphs I. Publicationes Mathematicae
Debrecen ([n. d.]), 290.
[11] Wenqi Fan, Yujuan Ding, Liangbo Ning, Shijie Wang, Hengyun Li, Dawei Yin,
Tat-Seng Chua, and Qing Li. 2024. A Survey on RAG Meeting LLMs: Towards
Retrieval-Augmented Large Language Models. In KDD.
[12] Cong Fu, Chao Xiang, Changxu Wang, and Deng Cai. 2019. Fast Approximate
Nearest Neighbor Search With The Navigating Spreading-out Graph. VLDB
(2019).
[13] Cong Fu, Chao Xiang, Changxu Wang, and Deng Cai. 2019. Fast Approximate
Nearest Neighbor Search With The Navigating Spreading-out Graph. VLDB
(2019).
[14] Jianyang Gao and Cheng Long. 2024. RaBitQ: Quantizing High-Dimensional
Vectors with a Theoretical Error Bound for Approximate Nearest Neighbor
Search. SIGMOD (2024).
[15] Yunfan Gao, Yun Xiong, Xinyu Gao, Kangxiang Jia, Jinliu Pan, Yuxi Bi, Yi Dai,
Jiawei Sun, Qianyu Guo, Meng Wang, and Haofen Wang. 2023.
Retrieval-
Augmented Generation for Large Language Models: A Survey. CoRR (2023).
[16] Tiezheng Ge, Kaiming He, Qifa Ke, and Jian Sun. 2014. Optimized Product
Quantization. TPAMI (2014).
[17] Siddharth Gollapudi, Neel Karia, Varun Sivashankar, Ravishankar Krishnaswamy,
Nikit Begwani, Swapnil Raz, Yiyong Lin, Yin Zhang, Neelam Mahapatro, Premku-
mar Srinivasan, Amit Singh, and Harsha Vardhan Simhadri. 2023. Filtered-
DiskANN: Graph Algorithms for Approximate Nearest Neighbor Search with
Filters. In WWW.
[18] Xiaoxin He, Yijun Tian, Yifei Sun, Nitesh V. Chawla, Thomas Laurent, Yann Le-
Cun, Xavier Bresson, and Bryan Hooi. 2024. G-Retriever: Retrieval-Augmented
Generation for Textual Graph Understanding and Question Answering. In
NeurIPS.
[19] Yongfeng Huang, Yanyang Li, Yichong Xu, Lin Zhang, Ruyi Gan, Jiaxing Zhang,
and Liwei Wang. 2023. MVP-Tuning: Multi-View Knowledge Retrieval with
Prompt Tuning for Commonsense Reasoning. In ACL.
[20] Hervé Jégou, Matthijs Douze, and Cordelia Schmid. 2011. Product Quantization
for Nearest Neighbor Search. TPAMI (2011).
[21] Md. Rezaul Karim, Felix Hermsen, Sisay Adugna Chala, Paola de Perthuis, and
Avikarsha Mandal. 2024. Scalable Semi-Supervised Graph Learning Techniques
for Anti Money Laundering. IEEE Access (2024).
[22] Mocheng Li, Xiao Yan, Baotong Lu, Yue Zhang, James Cheng, and Chenhao Ma.
2025. Attribute Filtering in Approximate Nearest Neighbor Search: An In-depth
Experimental Study. CoRR (2025).
[23] Shiyang Li, Yifan Gao, Haoming Jiang, Qingyu Yin, Zheng Li, Xifeng Yan, Chao
Zhang, and Bing Yin. 2023. Graph Reasoning for Question Answering with
Triplet Retrieval. In ACL.
[24] Wen Li, Ying Zhang, Yifang Sun, Wei Wang, Mingjie Li, Wenjie Zhang, and
Xuemin Lin. 2020. Approximate Nearest Neighbor Search on High Dimensional
Data - Experiments, Analyses, and Improvement. TKDE (2020).
[25] Zhuoyang Li, Liran Deng, Hui Liu, Qiaoqiao Liu, and Junzhao Du. 2024. UniOQA:
A Unified Framework for Knowledge Graph Question Answering with Large
Language Models. CoRR (2024).
[26] Anqi Liang, Pengcheng Zhang, Bin Yao, Zhongpu Chen, Yitong Song, and
Guangxu Cheng. 2024. UNIFY: Unified Index for Range Filtered Approximate
Nearest Neighbors Search. VLDB (2024).
[27] Yanjun Lin, Kai Zhang, Zhenying He, Yinan Jing, and X. Sean Wang. 2025. Survey
of Filtered Approximate Nearest Neighbor Search over the Vector-Scalar Hybrid
Data. CoRR (2025).
[28] Dawei Liu, Bolong Zheng, Ziyang Yue, Fuhao Ruan, Xiaofang Zhou, and Chris-
tian S. Jensen. 2025. Wolverine: Highly Efficient Monotonic Search Path Repair
for Graph-based ANN Index Updates. VLDB (2025).
[29] Shige Liu, Zhifang Zeng, Li Chen, Adil Ainihaer, Arun Ramasami, Songting Chen,
Yu Xu, Mingxi Wu, and Jianguo Wang. 2025. TigerVector: Supporting Vector
Search in Graph Databases for Advanced RAGs. In SIGMOD.
[30] Jiarui Luo, Miao Qiao, Chaoji Zuo, and Dong Deng. 2025. Tag-Filtered Approxi-
mate Nearest Neighbor Search. In ICDE.
[31] Lailong Luo, Deke Guo, Richard T. B. Ma, Ori Rottenstreich, and Xueshan Luo.
2019. Optimizing Bloom Filter: Challenges, Solutions, and Comparisons. IEEE
Commun. Surv. Tutor. (2019).
[32] Qin Lv, William Josephson, Zhe Wang, Moses Charikar, and Kai Li. 2007. Multi-
Probe LSH: Efficient Indexing for High-Dimensional Similarity Search. In VLDB.
[33] Yury A. Malkov and Dmitry A. Yashunin. 2020. Efficient and Robust Approximate
Nearest Neighbor Search Using Hierarchical Navigable Small World Graphs.
TPAMI (2020).
[34] Julieta Martinez, Shobhit Zakhmi, Holger H. Hoos, and James J. Little. 2018.
LSQ++: Lower Running Time and Higher Recall in Multi-codebook Quantization.
In ECCV.
[35] Zhaojie Niu, Xinhui Tian, Xindong Peng, and Xing Chen. 2025. BlendHouse: A
Cloud-Native Vector Database System in ByteHouse. In ICDE.
[36] Liana Patel, Peter Kraft, Carlos Guestrin, and Matei Zaharia. 2024. ACORN: Per-
formant and Predicate-Agnostic Search Over Vector Embeddings and Structured
Data. SIGMOD (2024).
[37] Diego Sanmartin. 2024. KG-RAG: Bridging the Gap Between Knowledge and
Creativity. CoRR (2024).
[38] Gaurav Sehgal and Semih Salihoglu. 2025. NaviX: A Native Vector Index Design
for Graph DBMSs With Robust Predicate-Agnostic Search Performance. VLDB
(2025).
[39] Teng Shi, Jun Xu, Xiao Zhang, Xiaoxue Zang, Kai Zheng, Yang Song, and Han Li.
2025. Retrieval Augmented Generation with Collaborative Filtering for Person-
alized Text Generation. In SIGIR.
[40] Liang Su, Mingyang Zhang, Kangxiang Jia, Tengfei Liu, Weiqiang Wang, Yun
Xiong, Xixi Wu, Xinyu Gao, Yongrui Fu, and Jiawei Zhang. 2025. Towards
Explainable Transaction Risk Analysis With Dual Graph Retrieval Augmented
Generation. In CIKM.
[41] Bart Thomee, David A. Shamma, Gerald Friedland, Benjamin Elizalde, Karl Ni,
Douglas Poland, Damian Borth, and Li-Jia Li. 2016. YFCC100M: the new data in
multimedia research. CACM (2016).
[42] Yao Tian, Xi Zhao, and Xiaofang Zhou. 2024. DB-LSH 2.0: Locality-Sensitive
Hashing With Query-Based Dynamic Bucketing. TKDE (2024).
[43] Jianguo Wang, Xiaomeng Yi, Rentong Guo, Hai Jin, Peng Xu, Shengjun Li, Xi-
angyu Wang, Xiangzhou Guo, Chengming Li, Xiaohai Xu, Kun Yu, Yuxing Yuan,
Yinghao Zou, Jiquan Long, Yudong Cai, Zhenxiang Li, Zhifeng Zhang, Yihua
Mo, Jun Gu, Ruiyi Jiang, Yi Wei, and Charles Xie. 2021. Milvus: A Purpose-Built
Vector Data Management System. In SIGMOD, Guoliang Li, Zhanhuai Li, Stratos
Idreos, and Divesh Srivastava (Eds.).
[44] Mengzhao Wang, Xiaoliang Xu, Qiang Yue, and Yuxiang Wang. 2021. A Com-
prehensive Survey and Experimental Comparison of Graph-Based Approximate
Nearest Neighbor Search. VLDB (2021).
[45] Yuxiang Wang, Ziyuan He, Yongxin Tong, Zimu Zhou, and Yiman Zhong. 2025.
Timestamp Approximate Nearest Neighbor Search Over High-Dimensional Vec-
tor Data. In ICDE.
[46] Chuangxian Wei, Bin Wu, Sheng Wang, Renjie Lou, Chaoqun Zhan, Feifei Li,
and Yuanzhe Cai. 2020. AnalyticDB-V: A Hybrid Analytical Engine Towards
Query Fusion for Structured and Unstructured Data. VLDB (2020).
[47] Jiadong Xie, Jeffrey Xu Yu, and Yingfan Liu. 2025. Graph-Based K-Nearest
Neighbor Search Revisited. TODS (2025).
[48] Yuexuan Xu, Jianyang Gao, Yutong Gou, Cheng Long, and Christian S. Jensen.
2024. iRangeGraph: Improvising Range-dedicated Graphs for Range-filtering
Nearest Neighbor Search. SIGMOD (2024).
[49] Shuo Yang, Jiadong Xie, Yingfan Liu, Jeffrey Xu Yu, Xiyue Gao, Qianru Wang,
Yanguo Peng, and Jiangtao Cui. 2025. Revisiting the Index Construction of
Proximity Graph-Based Approximate Nearest Neighbor Search. VLDB (2025).
[50] Mengdan Zhu, Senhao Cheng, Guangji Bai, Yifei Zhang, and Liang Zhao. 2025.
Cross-modal RAG: Sub-dimensional Retrieval-Augmented Text-to-Image Gener-
ation. CoRR (2025).
[51] Chaoji Zuo, Miao Qiao, Wenchao Zhou, Feifei Li, and Dong Deng. 2024. SeRF: Seg-
ment Graph for Range-Filtering Approximate Nearest Neighbor Search. SIGMOD
(2024).


