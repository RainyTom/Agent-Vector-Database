# 2601.01291v3 原始 PDF 文本提取

来源：https://arxiv.org/pdf/2601.01291v3

提取日期：2026-10-18。由 PyMuPDF 提取，未经 MinerU 版式转换；公式和表格需回看 PDF。

## PDF page 1

Curator: Efficient Vector Search with Low-Selectivity Filters
Yicheng Jin
Duke University
Durham, NC, USA
yicheng.jin@duke.edu
Yongji Wu
University of California, Berkeley1
Berkeley, CA, USA
wuyongji317@gmail.com
Wenjun Hu
Yale University
New Haven, CT, USA
wenjun.hu@cantab.net
Bruce M. Maggs
Duke University
Durham, NC, USA
bmm@cs.duke.edu
Jun Yang
Duke University
Durham, NC, USA
junyang@cs.duke.edu
Xiao Zhang
Cisco ThousandEyes1
Durham, NC, USA
xz234@alumni.duke.edu
Danyang Zhuo
Duke University
Durham, NC, USA
danyang@cs.duke.edu
Abstract
Embedding-based dense retrieval has become the cornerstone of
many critical applications, where approximate nearest neighbor
search (ANNS) queries are often combined with filters on labels such
as dates and price ranges. Graph-based indexes achieve state-of-the-
art performance on unfiltered ANNS but encounter connectivity
breakdown on low-selectivity filtered queries, where qualifying
vectors become sparse and the graph structure among them frag-
ments. Recent research proposes specialized graph indexes that
address this issue by expanding graph degree, which incurs prohib-
itively high construction costs. Given these inherent limitations of
graph-based methods, we argue for a dual-index architecture and
present Curator, a partition-based index that complements existing
graph-based approaches for low-selectivity filtered ANNS. Cura-
tor builds specialized indexes for different labels within a shared
clustering tree, where each index adapts to the distribution of its
qualifying vectors to ensure efficient search while sharing structure
to minimize memory overhead. The system also supports incre-
mental updates and handles arbitrary complex predicates beyond
single-label filters by efficiently constructing temporary indexes
on the fly. Our evaluation demonstrates that integrating Curator
with state-of-the-art graph indexes reduces low-selectivity query
latency by up to 20.9× compared to pre-filtering fallback, while
increasing construction time and memory footprint by only 5.5%
and 4.3%, respectively.
1
Introduction
The rise of deep learning has transformed information retrieval by
enabling the representation of unstructured data as high-dimensional
vectors that capture semantic relationships [21, 27, 36]. Approxi-
mate nearest neighbor search (ANNS) over these vector embeddings
has become a fundamental primitive powering modern applications
from web search [29, 50] to recommendation systems [8, 28, 53]
and retrieval-augmented generation [22, 31].
1Yongji Wu and Xiao Zhang were with Duke University at the time this work was
conducted.
Pure semantic similarity, however, often proves insufficient for
real-world applications. Modern systems increasingly require fil-
tered ANNS, where vector similarity search is constrained by struc-
tured predicates over metadata attributes. For instance, e-commerce
platforms must find semantically similar products within specific
price ranges and categories, while enterprise search systems need
to respect user access permissions and document types. This com-
bination of semantic similarity and structured filtering has driven
significant interest from both industry [34, 35, 42, 44, 45] and re-
search communities [5, 12, 32, 43, 46, 52, 55, 56].
Among the various challenges in filtered ANNS, handling queries
with diverse selectivity—the fraction of vectors qualifying the filter
conditions—is particularly difficult. The difficulty varies dramati-
cally across the selectivity spectrum, with each regime requiring
different algorithmic strategies and presenting distinct performance
bottlenecks.
1.1
Why Existing Approaches Fail at Low
Selectivity
Existing approaches to filtered ANNS can be broadly categorized by
their underlying index structures: flat indexes (brute-force search),
graph-based indexes, and partition-based indexes. While each index
type has found success in specific scenarios, they all encounter
limitations when handling low-selectivity queries. Fig. 1 illustrates
these challenges across different approaches.
Graph connectivity breakdown. As shown in Fig. 1(a), filtered
search on graph-based indexes exposes a critical trade-off: as se-
lectivity decreases, the sub-graph of qualifying vectors becomes
increasingly sparse and disconnected. This connectivity breakdown
forces algorithms to choose between two unsatisfactory options:
visit all neighbors of each node, which is expensive since most
neighbors are unqualified, or skip unqualified neighbors entirely,
which is both incomplete—as some qualifying vectors become un-
reachable—and inefficient, since graph search requires sufficient
connectivity for optimal performance.
arXiv:2601.01291v3  [cs.DB]  10 Jan 2026


## PDF page 2

Yicheng Jin, Yongji Wu, Wenjun Hu, Bruce M. Maggs, Jun Yang, Xiao Zhang, and Danyang Zhuo
(a) Vanilla Graph Index
(b) Specialized Graph Index
(c) Vanilla Partition Index
(d) Curator
Figure 1: Comparison of different approaches to filtered
ANNS. Blue points represent qualifying vectors, gray points
represent non-qualifying vectors. (a) Vanilla graph-based
approach suffers from connectivity breakdown at low selec-
tivity. (b) Specialized graph-based approach requires increas-
ing graph degree significantly to maintain connectivity. (c)
Vanilla partition-based approach suffers from granularity
mismatch at low selectivity. (d) Curator adopts a hierarchi-
cal partitioning approach that buffers qualifying vectors in
sparse regions at higher levels of the tree, adapting clustering
granularity to filter selectivity.
Densification becomes prohibitive. Recent approaches attempt
to address connectivity issues through graph densification, as illus-
trated in Fig. 1(b). Both ACORN [32] and Filtered DiskANN [12]
build dense graphs to ensure connectivity among qualifying vec-
tors. However, the required graph degree grows rapidly as selec-
tivity decreases. If each node needs 𝑚qualified neighbors and
selectivity is 𝑠, then the total degree must be approximately 𝑚/𝑠.
This relationship makes densification prohibitively expensive for
low-selectivity queries, as our empirical evaluation demonstrates
significant increases in construction time and memory usage. Al-
ternative approaches like UNG [5] attempt different strategies but
face significant limitations, as detailed in §2.
Pre-filtering scales poorly. When densification becomes imprac-
tical, many systems fall back to pre-filtering: first identifying all
qualifying vectors, then performing brute-force search over this sub-
set. While this approach guarantees completeness, it scales poorly
with the absolute number of qualifying vectors. On large-scale
datasets, low-selectivity filters can still match tens of thousands of
vectors, making brute-force search computationally expensive.
Partition granularity mismatch. Given these limitations of
graph-based approaches, one might consider partition-based alter-
natives. Indexes like IVF (Inverted File) cluster vectors and reduce
10
3
10
2
10
1
Filter Selectivity
10
1
100
101
102
Query Latency (ms)
Curator
Pre-Filtering
ACORN-10
ACORN-40
Figure 2: Performance comparison between Curator and the
state-of-the-art solution (ACORN + pre-filtering) across var-
ied selectivity levels in the low-selectivity regime. ACORN-𝛾
(𝛾= 10, 40) denotes ACORN with different graph degrees, with
larger 𝛾indicating denser index graphs. Curator closes the
performance gap of ACORN at low selectivity with minimal
overhead. Detailed experiment setup is provided in §6.4.
search scope by examining only nearby clusters, and they are not
vulnerable to connectivity breakdown. However, naively applying
them incurs another issue: partitions built for all indexed vectors
are too fine-grained for filtered search. As shown in Fig. 1(c), when
selectivity is low, each cluster contains only a few qualifying vec-
tors, thus the search must visit significantly more clusters compared
to unfiltered search, leading to substantial overhead for distance
computations against cluster centroids.
1.2
Our Approach: Adaptive Hierarchical
Partitioning
The limitations across existing approaches suggest the need for a
specialized index dedicated to low-selectivity filters. This raises a
natural question: should we design a dedicated index or improve
existing specialized indexes to handle a wider range of selectivity?
Given the fundamental connectivity challenges that graph-based
indexes face at low selectivity, we design Curator to complement
existing graph-based approaches through a dual-index architecture
rather than attempting to extend their performance into the low-
selectivity regime. Graph-based methods excel at high-selectivity
scenarios, while Curator specializes in the low-selectivity regime
where traditional approaches struggle. This complementary design
enables the two indexes to work together, achieving better overall
performance across the entire selectivity spectrum. The key require-
ment for this approach to be cost-effective is that Curator must
maintain low memory and construction overhead.
Following this complementary design philosophy, this paper
introduces Curator, a partition-based index that employs adaptive
hierarchical clustering to achieve efficient low-selectivity search.
As a partition-based approach, Curator deliberately accepts slower
search performance than graph-based indexes at high selectivity,
where graph connectivity remains strong, in exchange for robust
performance at low selectivity where graph-based approaches suf-
fer from connectivity breakdown. As illustrated in Fig. 1(d), Curator
uses hierarchical partitioning where qualifying vectors in sparse


## PDF page 3

Curator : Efficient Vector Search with Low-Selectivity Filters
regions are buffered at higher levels of the tree, adapting clustering
granularity to the local density of qualifying vectors. The system
constructs specialized indexes for different filter predicates, all em-
bedded within a shared hierarchical clustering tree that maintains
low overhead through structural sharing.
Curator supports both label-based filtering—where vectors are
associated with categorical labels known in advance and queries
return the nearest vectors matching a given label—and arbitrary
complex predicates involving multiple attribute types and logical
operators. The core challenge with complex predicates is that they
are typically not indexed directly due to the vast number of pos-
sible combinations. Prior works resort to inefficient multi-stage
filtering or inflexible special handling of a small subset of predicate
types in index design. Curator addresses this by dynamically con-
structing temporary indexes that mirror the shared tree structure
with minimal overhead, enabling efficient evaluation of complex
predicates without requiring pre-built specialized indexes for every
combination.
To validate this complementary design, we evaluate Curator
against ACORN (the state-of-the-art graph-based index) using a
semi-synthetic dataset constructed from YFCC-10M with 20 log-
arithmically distributed selectivity levels in [0.001, 0.2], enabling
systematic coverage of the low-selectivity regime. As shown in
Fig. 2, brute-force search (pre-filtering fallback) exhibits linear scal-
ing, while ACORN shows sharp performance degradation at low
selectivity. Curator fills this gap with minimal construction and
memory overhead. The results show that increasing graph degree
(from ACORN-10 to ACORN-40) offers limited performance im-
provement at low selectivity, confirming that densifying graphs is
not an effective solution and making Curator an ideal complemen-
tary approach.
1.3
Contributions
This paper makes the following key contributions:
• Adaptive partitioning for low-selectivity ANNS: We
design Curator, a hierarchical partition-based index that
adapts clustering granularity to the distribution of qualified
vectors, achieving high performance for low-selectivity
queries while maintaining minimal memory overhead.
• Efficient complex predicate support: We present a novel
algorithm for handling arbitrary predicates by dynamically
constructing temporary indexes with minimal overhead.
• Comprehensive system design: We develop efficient
algorithms including batch construction, incremental up-
dates, and specialized search procedures for both single-
label filters and complex predicates, ensuring search effi-
ciency and completeness while adapting to dynamic changes
in data distributions.
• Extensive experimental validation: We conduct compre-
hensive experiments on real-world datasets demonstrating
that Curator reduces low-selectivity query latency by up to
20.9× compared to the state-of-the-art solution (ACORN +
pre-filtering), while increasing construction time and mem-
ory footprint by only 5.5% and 4.3%, respectively, confirm-
ing the effectiveness of our complementary approach.
2
Background
2.1
Problem Formulation
We formulate the problem of filtered ANNS as follows. We start
with single-label search and then generalize to complex predicate
search:
Single-label search. Let L = {𝑙1,𝑙2, . . . ,𝑙| L|} represent a finite
universe of labels, and consider a dataset 𝑆= {(𝑥, 𝐿𝑥) | 𝐿𝑥⊆L}
where each object consists of a vector 𝑥and an associated set of
labels 𝐿𝑥. Single-label search involves queries that check for the
existence of a single label in the label set: a query (𝑥𝑞,𝑙,𝑘) requests
the 𝑘nearest neighbors of the query vector 𝑥𝑞within the subset of
vectors where 𝑙∈𝐿𝑥.
Complex predicate search. Consider a more general dataset
𝑆= {(𝑥,𝐴𝑥)} where each object consists of a vector 𝑥and associ-
ated attributes 𝐴𝑥including categorical labels, numerical values,
timestamps, and other structured data. Complex predicate search
involves arbitrary filter predicates 𝜎combining range queries, com-
posite conditions across multiple attributes, and complex logical
operators. A query (𝑥𝑞, 𝜎,𝑘) requests the 𝑘nearest neighbors of the
query vector 𝑥𝑞within the subset P𝜎of vectors qualifying predi-
cate 𝜎. Complex predicate search subsumes single-label search and
additionally supports AND queries (𝑥𝑞, 𝐿𝑞,𝑘) where 𝐿𝑞⊆𝐿𝑥, and
OR queries (𝑥𝑞, 𝐿𝑞,𝑘) where 𝐿𝑞∩𝐿𝑥≠∅.
For both search types, the selectivity of filter 𝜎is defined as
|P𝜎|/|𝑆|, which fundamentally determines query complexity. We
measure search quality using Recall@K = |𝑅∩𝑅∗|
𝑘
, where 𝑅and 𝑅∗
denote the retrieved results and ground truth respectively.
2.2
Filtered ANNS Approaches
Below, we review existing methods of filtered ANNS organized
by how filtering is integrated. This taxonomy is orthogonal to the
underlying index type, focusing instead on how adaptations are ap-
plied to search algorithms and index structures. For each approach,
we first discuss its application to single-label search scenarios and
then examine its support for complex predicate search.
Metadata filtering. This approach integrates filtering with no
or minimal modifications to search algorithms and index struc-
tures, utilizing a single shared index. It includes three subtypes:
(1) Pre-filtering [42, 44, 45] retrieves all qualified vectors from a
scalar index and performs brute-force search, since most vector
indexes cannot efficiently support searching subsets of vectors. (2)
Post-filtering [42, 45] filters intermediate results from unfiltered
ANNS, though determining sufficiently large 𝑘to ensure enough
results remain after filtering is challenging and overshoot leads to
inefficiency. (3) Inline filtering [19, 23, 34, 52] excludes unqualified
vectors during search, eliminating intermediate 𝑘selection but re-
quiring explicit algorithmic support. This approach can support
arbitrary complex predicates.
Per-label indexing. This approach constructs dedicated vector
indexes for each label and directs queries to the corresponding
index without changing search algorithms [20]. While it delivers
optimal search performance for single-label queries by eliminating
query-time filtering, it incurs significant construction and memory
overhead since each vector may be indexed multiple times across


## PDF page 4

Yicheng Jin, Yongji Wu, Wenjun Hu, Bruce M. Maggs, Jun Yang, Xiao Zhang, and Danyang Zhuo
different per-label indexes. Extending this to per-predicate indexing
for complex predicate search is generally impossible due to the
combinatorial explosion of possible predicates.
Specialized indexes. Recent research has developed specialized
indexes for filtered ANNS modifying both index construction and
search algorithms, with most work focusing on graph-based ap-
proaches. These methods can be categorized into three main strate-
gies:
(1) Fused distance. These methods [43, 46] employ hybrid distance
metrics that integrate both attribute and vector similarity through-
out index construction and query processing. They are limited to
equality-based attribute matching and require queries to specify
values of all attributes for attribute similarity computation. This
severely restricts query expressiveness, making them unsuitable
for the problem settings we are considering.
(2) Graph densification. Filtered DiskANN [12] and ACORN [32]
follow a two-phase approach: dense graph construction then com-
pression. Filtered DiskANN builds dense graphs by merging per-
label indexes, while ACORN directly builds large-degree graphs.
For compression, Filtered DiskANN prunes edges while preserving
connectivity, whereas ACORN removes edges between nodes reach-
able as two-hop neighbors, compensating by visiting both direct
and two-hop neighbors during search. Both face the fundamental
connectivity challenge that the required graph degree scales with
1/selectivity. When this scaling becomes prohibitive, ACORN falls
back to pre-filtering.
(3) Subgraph stitching. UNG [5] constructs separate graph indexes
for each unique label set and connects them using cross-group edges.
During query processing, it traverses subgraphs corresponding to
matching label sets, with connectivity among label sets guaranteed
by cross-group edge construction to ensure completeness. How-
ever, this approach often becomes inefficient when label set groups
become highly fragmented with few nodes per group, which fre-
quently occurs in real datasets. In such cases, the graph structure
degrades as most edges become cross-group connections that prior-
itize completeness over search efficiency. This leads to significant
regression in search performance and long construction times for
the stitching process in our evaluation.
These specialized approaches exhibit varying support for com-
plex predicates due to their tight integration of filtering with index
structures. Filtered DiskANN supports only OR queries, and UNG
supports only AND queries, while ACORN extends beyond these
limitations to handle general complex predicates.
3
Curator Overview
This section presents Curator’s architecture and design principles.
Fig. 3 illustrates Curator’s core design: a single shared base index
and a set of per-label indexes embedded within it. This strategy al-
lows per-label indexes to share the base index’s spatial partitioning
while adapting their granularity to their unique label distributions,
addressing the fundamental limitations of existing approaches for
low-selectivity queries outlined in §2.
Base Index
Unﬁltered Vector Distribution
Per-Label Index
Filtered Label Distribution
Per-Label Indexes
Bloom Filter
Encode
x2
x4
x5
Vector Store
2
4
5
Buffers
Curator Index
Compact Encoding of Per-Label Indexes
Figure 3: Overview of Curator. The base index represents the
finest-grained partitioning on unfiltered vector distribution,
while the per-label indexes are embedded within the base
index, adapting to the unique distribution of their respective
labels. Per-label indexes are compactly encoded using buffers
and Bloom filters.
3.1
Base Index
The base index 𝑇is a standard hierarchical k-means clustering
tree [26] that partitions all vectors in the dataset. The tree recur-
sively partitions the vector space using k-means clustering until
reaching sufficiently fine granularity, i.e., when a node contains
fewer vectors than the predefined leaf capacity. The structure of
the base index thus adapts to the distribution of all indexed vectors,
which we refer to as the vector distribution. Within the base in-
dex, each node stores a centroid that represents the corresponding
cluster during search, while leaf nodes store the actual vector data
within the cluster. This base index serves as the shared partitioning
structure for embedded per-label indexes.
We choose hierarchical k-means as the base index for two key
reasons. First, as a partition-based index, it does not suffer from
the inherent connectivity issues that plague graph-based indexes
at low selectivity, as partitions remain well-defined regardless of
label sparsity. Second, the hierarchical structure naturally embeds
varied partitioning granularities for different filters within a single
tree, allowing per-label indexes to adapt their clustering depth to
label-specific distributions. Regarding search performance, hierar-
chical k-means is typically slower than graph-based indexes like
HNSW but comparable to IVF for unfiltered search. This perfor-
mance positioning is confirmed in our evaluation (Fig. 8), where
Curator traversing per-label clustering trees achieves performance
comparable to per-label IVF.
3.2
Per-Label Indexes
Per-label indexes are embedded within the base index to adapt to
the unique distribution of their respective labels. Unlike traditional
approaches that build separate indexes for each label, Curator’s
per-label indexes reuse the base index’s spatial partitioning while


## PDF page 5

Curator : Efficient Vector Search with Low-Selectivity Filters
adjusting granularity to label-specific vector densities. This hier-
archical storage strategy ensures that all vector data resides exclu-
sively in the base index, avoiding the costly vector duplication that
would occur if per-label indexes stored their own copies of vector
data.
Per-label indexes are compactly encoded using two key data
structures: buffers that store vector identifiers at leaf nodes, and
Bloom filters that define the structure of per-label indexes.
Vector Identifiers and Buffers. To avoid vector duplication,
per-label indexes store buffers—sorted lists of vector identifiers—at
leaf nodes rather than actual vectors. The buffer capacity 𝐵𝑚𝑎𝑥
controls leaf node granularity. These identifiers are constructed
hierarchically to encode each vector’s tree location: each node is
assigned a node identifier as a sequence of branch indices from the
root, and each vector’s identifier combines its containing leaf’s
node identifier with its index within that leaf. These two compo-
nents are bit-packed into a single integer with zero padding for
uniform length. This construction ensures each node identifier rep-
resents a vector ID subspace containing all vectors with that prefix,
so sorting vector identifiers groups vectors from the same sub-tree
contiguously (Fig. 5). This property enables efficient construction
of temporary per-predicate indexes (§4.2), batch construction of
per-label indexes (§5.2), and buffer split/merge operations during
updates (§5.1).
Bloom Filters. The base index contains three distinct node types
with respect to each per-label index: internal nodes requiring fur-
ther partitioning, leaf nodes containing buffers with qualified vec-
tors, and external nodes not included in the per-label index. During
search, Curator must efficiently determine the boundaries of each
per-label index—consisting of leaf nodes and external nodes—to
know when to backtrack. While leaf nodes are easily identified
by the presence of buffers, distinguishing between internal and
external nodes is challenging since both lack buffers.
A straightforward approach would maintain an exact set at each
node 𝑛representing the per-label indexes containing it, denoted
as T𝑛= {𝑙| 𝑛∈𝑇𝑙}. During search, checking membership 𝑙∈T𝑛
would determine whether the current node is inside 𝑇𝑙. However,
storing T𝑛exactly becomes costly when the number of unique la-
bels in the dataset is large. Therefore, Curator approximates T𝑛
using Bloom filters B𝑛at each node in the base index. Bloom filters
are space-efficient probabilistic data structures that support mem-
bership queries with bounded false positive rate 𝑝. Importantly,
Bloom filters have no false negatives, ensuring that when 𝑙∉B𝑛,
the search algorithm has definitively reached an external node and
can safely backtrack. While false positives may cause occasional
unnecessary exploration of nodes outside the per-label index, they
do not affect search correctness since external nodes do not contain
buffers and thus never contribute to the result set.
4
Search Algorithms
This section presents the core search algorithms that enable Cu-
rator to deliver efficient search when the filter selectivity is low.
We distinguish two query types: single-label search and complex
predicates that involve more attribute types and logical operators.
The embedded per-label index structure ensures both efficiency
by avoiding visits to unqualified vectors and adapting structure
Table 1: Notations used in the paper
Notation
Description
P
Set of indexed vectors
L
Finite universe of labels
P𝑙
Vectors associated with label 𝑙
L𝑥
Labels associated with vector 𝑥
𝑇,𝑇𝑙
Index and per-label index for label 𝑙
P𝑛
Cluster represented by tree node 𝑛
𝜇𝑛
Centroid of node 𝑛
P𝑛,𝑙
Buffer for label 𝑙at node 𝑛
B𝑛
Bloom filter at node 𝑛
𝐶𝑛
Child nodes of node 𝑛
𝐵𝑚𝑎𝑥
Buffer capacity
Algorithm 1: Single-Label Search
Input
:Index 𝑇, query vector 𝑥𝑞, label 𝑙, beam width 𝑏,
result set size ef
Output:𝑘approximate nearest neighbors of 𝑥𝑞
1 Initialize Q ←BeamSearch(𝑇.root,𝑏)
// Frontier
2 Initialize R ←∅
// Result set
3 while Q ≠∅do
4
𝑛←arg min𝑛′∈Q 𝑆(𝑛′,𝑥𝑞)
5
Q ←Q \ {𝑛}
6
if 𝑙∉B𝑛then
//
1 Outside 𝑇𝑙
7
continue
8
else if P𝑛,𝑙≠∅then
//
2 Leaf node
9
R′ ←R ∪P𝑛,𝑙
10
if |R′| > ef then
11
R′ ←top-ef closest vectors in R′
12
if R′ = R then
13
break
14
R ←R′
15
else
//
3 Internal node
16
Q ←Q ∪𝐶𝑛
17 return top-𝑘closest vectors in R
to label distribution, and completeness by covering all qualifying
vectors within a connected clustering tree. For single-label filters,
Curator traverses the corresponding pre-built per-label index; for
complex predicates, it first constructs a temporary per-predicate in-
dex with minimal overhead and then applies the exact same search
algorithm.
4.1
Single-Label Search
Single-label search finds qualifying vectors by scanning clusters
closest to the query vector within the space partitioning represented
by the per-label index (Fig. 4). Algorithm 1 implements this in two
phases: beam search for robust initialization that traverses to the
neighborhood of the query vector, followed by best-first search for
efficient exploration of that neighborhood.
Phase 1: Beam Search. Algorithm 1 begins with beam search to
initialize the search frontier Q with tree nodes representing nearest


## PDF page 6

Yicheng Jin, Yongji Wu, Wenjun Hu, Bruce M. Maggs, Jun Yang, Xiao Zhang, and Danyang Zhuo
xq
Per-Label Index
Hierarchical Clustering
Figure 4: Single-label search on Curator. Highlighted clusters
and tree nodes are visited by the search algorithm.
clusters. This phase traverses from the root until reaching leaf nodes
of the per-label index, maintaining the top-𝑏closest nodes at each
level. Without beam search, best-first search starting from the root
would be misguided by the imprecision of high-level nodes, causing
the algorithm to become trapped in suboptimal subtrees without
exploring subtrees whose centroids are relatively far from the query
vector but still contain nearby vectors within their boundaries.
Exploring multiple promising paths simultaneously mitigates this
centroid imprecision by increasing the likelihood that the actual
nearest leaf nodes are covered by the beam search.
Phase 2: Best-First Search. Using the frontier initialized by beam
search, the second phase performs best-first search, systematically
exploring the neighborhood guided by heuristic scores. The al-
gorithm processes nodes based on their type with regard to the
per-label index𝑇𝑙: leaf nodes containing buffers update the result set
with their qualified vectors. For nodes without buffers, the Bloom
filter B𝑛determines whether the node is an internal node requiring
expansion or an external node outside 𝑇𝑙where the search should
backtrack. During exploration, the search maintains a dynamic re-
sult set R containing the ef closest vectors discovered so far. Each
time a buffer is visited, the algorithm updates R and checks for
convergence—if the result set remains unchanged (i.e., no vector
in the buffer is closer than the worst member of R), the search
terminates. Larger ef values widen the exploration radius, trading
more work for higher recall. In the limit, when ef is sufficiently
large, the traversal visits the entire per-label index encompassing
all qualifying vectors, thus guaranteeing completeness. We found
this adaptive termination criterion accommodates varying query
difficulties more effectively than approaches that use a fixed com-
putational budget.
Heuristic Function. Both search phases employ the same heuris-
tic function to score candidate nodes: 𝑆(𝑛,𝑥𝑞) =


𝜇𝑛−𝑥𝑞


 −𝛼·
1
| P𝑛|
Í
𝑥∈P𝑛∥𝑥−𝜇𝑛∥. This heuristic estimates the minimum query-
to-cluster distance by combining centroid distance with cluster
radius. The 𝛼coefficient encourages exploring large, distant clus-
ters to avoid local optima.
4.2
Complex Predicate Search
The search algorithm described in the previous section supports
only single-label categorical filters, which limits query expressive-
ness in two key ways: (1) it only supports categorical attributes,
preventing range queries on numerical attributes, and (2) it does
not support logical operators like AND, OR, or NOT to compose
multiple filtering conditions.
Approach Overview. To address these limitations, we extend the
system to support arbitrary complex predicates while maintaining
the performance characteristics of single-label search. The key in-
sight is that any complex predicate ultimately identifies a subset
of vectors that qualify the given conditions. Instead of modifying
the existing search infrastructure, we treat this subset as if it were
associated with a virtual label, allowing us to reuse the same hi-
erarchical partitioning and search algorithms. By constructing a
temporary index containing only these qualifying vectors, we can
apply the same search algorithm used for single-label filters.
The challenge lies in efficiently constructing this temporary in-
dex without expensive operations. A naive approach would require
traversing the index for each qualified vector to find its correspond-
ing position in the temporary index, which involves distance com-
putations and tree traversals.
Our approach exploits a key property of the vector identifier
construction described in §3: given a list of qualified vectors sorted
by their IDs, vectors from the same sub-tree are grouped contigu-
ously. This property enables efficient temporary index construction
through binary search operations. Given a sorted list of qualifying
vector identifiers, we can recursively partition them into sub-trees
by finding the boundaries between different child nodes using bi-
nary search, following the same hierarchical structure as the main
index.
The sorted list of vector IDs, which we expect as the input for
temporary index construction, is typically produced by an external
relational database. This list could be represented as a posting list
from an inverted index or a compressed bitmap. Evaluating attribute
predicates and composing bitmaps inside the database is far cheaper
than vector distance computation, so we treat this sorted list as
the algorithm input. For further optimization, the costs of filter
evaluation and temporary index construction can be reduced by
only performing these operations in the sub-trees projected to be
searched by the algorithm, rather than evaluating the predicate
over all vectors and constructing the complete temporary index
upfront.
Temporary Index Construction. Algorithm 2 presents the tem-
porary index construction procedure. The algorithm exploits the
sorted vector ID property through a recursive tree building process
that mirrors the structure of the main index 𝑇. The BuildSubtree
function constructs each node 𝑛𝜎in the temporary index 𝑇𝜎by
processing a corresponding range P𝜎[𝑙: 𝑟] of sorted qualified vec-
tor IDs. The algorithm operates as follows for each node: (1) Leaf
condition: If the number of qualified vectors in the current range
does not exceed the buffer capacity 𝐵𝑚𝑎𝑥, the algorithm creates a
leaf node and terminates recursion for this sub-tree. (2) Hierarchi-
cal partitioning: Otherwise, it iterates through each child 𝑐of the
corresponding main index node 𝑛. For each child, binary search op-
erations locate the subset of qualified vectors whose IDs fall within
the child’s vector ID range [𝑐.min,𝑐.max). This partitioning step
leverages the contiguous property of sorted vector IDs to efficiently
split the qualified vector list. After partitioning, the construction
algorithm is then applied recursively to build the sub-tree for each
child.


## PDF page 7

Curator : Efficient Vector Search with Low-Selectivity Filters
Algorithm 2: Temporary Index Construction
Input: Sorted qualified vector IDs P𝜎, main index 𝑇
Output: Temporary hierarchical index 𝑇𝜎
1 𝑇𝜎←InitializeIndex()
2 𝑇𝜎.root ←BuildSubtree(P𝜎,𝑇.root, 0, |P𝜎|)
3 return 𝑇𝜎
4 Function BuildSubtree(P𝜎, 𝑛, 𝑙, 𝑟)
/* Build sub-tree rooted at 𝑛for P𝜎[𝑙: 𝑟]
*/
5
𝑛𝜎←InitializeNode()
6
if 𝑟−𝑙≤𝐵𝑚𝑎𝑥then
//
1 Leaf node of 𝑇𝜎
7
return 𝑛𝜎
8
foreach 𝑐∈𝐶𝑛do
//
2 Split P𝜎[𝑙: 𝑟]
/* 𝑐covers vector ID range [𝑐.min,𝑐.max) */
9
𝑙𝑐←BinarySearch(P𝜎,𝑐.min,𝑙,𝑟)
10
𝑟𝑐←BinarySearch(P𝜎,𝑐.max,𝑙,𝑟)
11
if 𝑙𝑐≤𝑟𝑐then
12
𝑐𝜎←BuildSubtree(P𝜎,𝑐,𝑙𝑐,𝑟𝑐)
13
𝑛𝜎.AddChild(𝑐𝜎)
14
return 𝑛𝜎
11*
10*
0**
000
010
100
101
Qualiﬁed vector IDs (sorted)
Temporary Index Tσ
Partitioning of Qualiﬁed VIDs
000
010
100
101
110
100
101
110
110
[000,010]
[100,101]
Buffer 
contents
[110]
L1 
idx
L2 
idx
Vector 
idx
Vector Identiﬁer (VID)
Leaf NID + Vector Index in Leaf
…
Ln 
idx
Node Identiﬁer (NID)
Parent NID + Branch Index
1**
***
10*
11*
00*
01*
0**
1**
Base Index T
Partitioning of VID Space
NID represents a
VID subspace
0
1
0
1
0
1
Global VID 
space
Figure 5: Temporary index construction for complex predi-
cates. The sorted qualified vector IDs are recursively parti-
tioned following the base index structure, enabling efficient
construction of a temporary index.
Fig. 5 illustrates this process using a simplified scenario where
each node has two children and both leaf capacity and buffer
capacity are 2, allowing vector IDs to be represented as 3-digit
binary numbers. The left panel shows the base index with vec-
tor ID ranges annotated for each node, such as [000-111] for the
root and [000-011], [100-111] for its children, corresponding to
the [𝑐.min,𝑐.max) ranges referenced in the algorithm. The middle
panel demonstrates the recursive partitioning process: starting with
sorted qualified vector IDs {000, 010, 100, 101, 110}, binary search
finds the partition boundary at vector ID 100 (the start of the right
sub-tree’s range [100-111]), splitting the list into groups {000, 010}
and {100, 101, 110}. The partitioning continues recursively until the
leaf condition is met—for instance, the right sub-tree is further split
into {100, 101} and {110}, with the latter qualifying the leaf condi-
tion (≤2 vectors). The right panel shows the resulting temporary
index where the qualified vector IDs are distributed into buffers in
leaf nodes.
The temporary index maintains a lightweight structure where
each node stores only: (1) a pointer to the corresponding node in
the base index, (2) the range offsets [𝑙,𝑟) in the qualified vector ID
list, and (3) pointers to child nodes. This design minimizes memory
overhead while preserving the hierarchical structure necessary for
efficient search.
Search Process. Once the temporary index is constructed, the
search algorithm proceeds identically to the single-label search
case. While there are minor layout differences between the per-label
indexes embedded in the main index and the constructed temporary
index, they are conceptually equivalent, thus the difference does
not affect the correctness or efficiency of the search algorithm.
Pre-indexing Filters. Despite finding that the overhead of tem-
porary index construction is minimal in our evaluation, in certain
scenarios it may be desirable to further eliminate this overhead by
pre-constructing indexes for filter predicates, particularly when
some filter predicates are frequently encountered. The simplest ap-
proach is caching the constructed temporary indexes as a side prod-
uct of complex predicate search, which requires minimal additional
implementation effort. Alternatively, we can permanently integrate
the temporary indexes into the main index structure to save mem-
ory and reuse update routines. This integration process involves
assigning a virtual label to the predicate to reference the temporary
index in future queries, distributing the qualifying vectors as buffers
across the appropriate nodes in the hierarchy, and updating the
Bloom filters accordingly. The integration is straightforward due to
the one-to-one mapping between nodes of the temporary index and
the main index. Both approaches achieve nearly identical search
performance in our evaluation, suggesting that the choice between
them can be made based on application-specific considerations.
Curator treats pre-construction or caching of temporary indexes
as a pluggable policy. Deployments may supply custom logic or
rely on standard workload analysis tools and query optimizers to
identify frequent, high-payoff predicates within resource budgets,
preserving compatibility with existing index management pipelines.
5
Index Maintenance
Curator maintains both a base index fitted to the overall vector
distribution and multiple per-label indexes adapted to their respec-
tive label distributions. As data evolves, the system must efficiently
update these structures while preserving search performance. This
section describes the incremental update mechanisms and rebuild-
ing strategies that maintain index quality with minimal overhead.
5.1
Incremental Updates
Curator supports efficient incremental updates to both vectors and
labels, each requiring different maintenance strategies.
Label Updates. Label updates modify per-label indexes through
buffer operations and Bloom filter maintenance, as illustrated in


## PDF page 8

Yicheng Jin, Yongji Wu, Wenjun Hu, Bruce M. Maggs, Jun Yang, Xiao Zhang, and Danyang Zhuo
❶Locate closest leaf
❷Update buffer
4
5
2
buffer underfull
❸Update per-label index
5
2
5
4
2
remove
2
4
5
2
4
5
2
4
5
buffer overfull
append
split
merge
Figure 6: Label update operations in Curator. Top: label inser-
tion triggers buffer overflow, causing vectors to be flushed to
child nodes. Bottom: label deletion creates underfull buffers
in child nodes that merge into the parent node.
Fig. 6. Throughout this process, Curator maintains two index invari-
ants that ensure both search performance and correctness: buffer
capacity limits and Bloom filter correctness.
Curator maintains buffer capacity limits using a strategy similar
to buffer trees [2]. When a buffer overflows due to label insertion,
vectors are flushed to child nodes at the next level (Fig. 6 top).
Conversely, when sibling buffers’ combined size falls below ca-
pacity due to deletions, they merge into the parent node (Fig. 6
bottom). These operations occur recursively and avoid distance
computations by leveraging the root-to-leaf path encoded in vec-
tor identifiers. Efficiency is further enhanced by the contiguous
grouping property (§3), where vectors belonging to the same child
are always grouped contiguously within buffers, enabling efficient
partitioning via array slicing.
Curator maintains Bloom filter correctness when buffer oper-
ations cause structural changes to per-label indexes. For label in-
sertions that expand the per-label index to new nodes, the label is
simply added to the Bloom filter at those nodes. For buffer merging
that removes nodes from per-label indexes, the Bloom filter of par-
ent node 𝑝must be recomputed recursively based on its children
𝐶𝑝, since Bloom filters cannot remove elements:
B𝑝= {𝑙| 𝑙∈L, P𝑝,𝑙≠∅} ∪
Ø
𝑐∈𝐶𝑝
B𝑐
(1)
Here, the first term represents labels with buffers stored at 𝑝(per-
label trees where 𝑝is a leaf node), while the second aggregates
child Bloom filters (per-label trees where 𝑝is an internal node).
Updates propagate recursively from modified nodes upward until
no further changes occur.
Vector Updates. Unlike label updates, vector updates modify only
the base index by finding the nearest leaf node through greedy
search and adding or removing vector identifiers there. For dele-
tions, the root-to-leaf path encoded in vector identifiers eliminates
the need for greedy search, as the target leaf node can be directly
identified from the vector’s encoded path. Vector updates could
use a similar split/merge strategy as label updates, but this would
require updating multiple per-label indexes affected by the struc-
tural changes. This impact is typically minimal since few per-label
indexes reach the leaf nodes of the base index due to label spar-
sity, but Curator instead enforces leaf capacity through periodic
rebuilding (§5.2) for simplicity.
5.2
Index Rebuilding
While incremental updates handle individual changes efficiently,
accumulated updates can cause bucket imbalances that degrade
search performance [49]. To address this, index rebuilding involves
reconstructing either the entire base index or a sub-tree, followed by
batch construction of per-label indexes within the affected region.
Curator offers two rebuilding strategies that provide different
trade-offs between overhead and freshness. Global rebuilding recon-
structs the entire base index using hierarchical k-means clustering,
followed by batch construction of all per-label indexes. The batch
construction of per-label indexes follows the same recursive par-
titioning procedure as temporary index construction for complex
predicates (§4.2). This provides optimal index quality but incurs
higher overhead to maintain the same level of freshness. In contrast,
local rebuilding targets only sub-trees with significant updates, re-
ducing overhead while maintaining freshness where needed. To
determine when local rebuilding is necessary, Curator tracks up-
date statistics at each node: total vectors |P𝑛| and updates since last
rebuild 𝑈𝑛. When the update ratio 𝑈𝑛/|P𝑛| exceeds a predefined
threshold, the sub-tree enters a rebuilding queue for batch process-
ing at defined intervals or on manual request, with new sub-trees
atomically swapped in and counters reset.
6
Evaluation
This section empirically evaluates our method across various di-
mensions. Overall, our results demonstrate:
• Curator achieves competitive search latency with mini-
mal build time and index overhead compared to other ap-
proaches.
• Curator can be efficiently integrated into existing graph-
based indexes with minimal overhead to address their per-
formance issues at low selectivity. Supplementing ACORN
with Curator improves search performance by up to 20.9×
with merely 5.5% and 4.3% overhead in construction time
and memory footprint, respectively.
• Curator efficiently supports incremental updates of both
vectors and labels, as well as low-selectivity queries with
arbitrary complex predicates.
6.1
Experimental Setup
Environment. Curator and the baselines are implemented in C++
and compiled using GCC 9.4.0 with -O3 and AVX512 optimization.
Our implementation builds upon the FAISS library [19] infrastruc-
ture. All evaluations run on a server equipped with an Intel Xeon
Gold 5215 @2.5GHz processor and 256GB of RAM, running Linux
Ubuntu 20.04 LTS. Unless otherwise specified, all experiments are
conducted in a single-threaded environment.
Datasets. We use the following real-world datasets for evaluation
and provide their statistics in Table 2. Each dataset is randomly
divided into training and test sets, with the test set containing
10,000 vectors. (1) The YFCC-10M dataset is a 10M vector subset
of the original YFCC100M dataset [41], which serves as the stan-
dard benchmark in the NeurIPS’23 Big-ANN competition [39]. This
dataset features a hybrid of vector and scalar data, with images
embedded using the CLIP model [36] and annotated with labels


## PDF page 9

Curator : Efficient Vector Search with Low-Selectivity Filters
Table 2: Characteristics of Evaluation Datasets
YFCC-10M
arXiv
Source Data
Image
Text
# Vectors
10M
2M
Vector Dimension
192
384
Unique Labels
1000
100
Labels per Vector
5.53
9.93
representing objects, locations, etc. We use the 1000 most popular
labels to ensure all baselines consume a manageable amount of
memory. (2) The arXiv dataset [7] contains metadata from 2.3M
arXiv papers, including titles, authors, abstracts, and paper cate-
gories (e.g., cs.DB denotes the field of database systems). We embed
paper abstracts using the all-MiniLM-L6-v2 [14] model and use
the 100 most popular categories as labels.
Baselines. We evaluate the performance of Curator against the
following indexes: IVF [19], HNSW [24], Parlay-IVF [20], Filtered
DiskANN [12], and ACORN [32]. IVF and HNSW represent state-
of-the-art partition-based and graph-based indexes, respectively.
For both IVF and HNSW, we develop variants of per-label indexing
and inline filtering to facilitate filtered search. These variants are
referred to as Per-label IVF/HNSW and Shared IVF/HNSW, abbre-
viated as P-IVF/HNSW and S-IVF/HNSW, respectively, throughout
the paper. Parlay-IVF is the top open source entry in the filtered
search track of the Big-ANN competition, which constructs per-
label graph-based indexes with shared vector storage. Due to ex-
cessive memory requirements, we omit evaluation of Per-label IVF
and Per-label HNSW baselines on the YFCC-10M dataset. We also
exclude NHQ [43] from our evaluation as it requires specifying the
presence or absence of all labels in filter predicates, and UNG [5]
due to its poor performance on our datasets (as discussed in §2).
In addition to these standalone vector indexes, we evaluate
pgvector [33], a PostgreSQL extension that allows users to create
vector-typed columns and build vector indexes on them, integrat-
ing vector search with SQL-based predicate filtering. We evaluate
two pgvector-based baselines: Pg-IVF and Pg-HNSW, which use
IVF-Flat and HNSW as the underlying vector indexes, respectively.
Vectors and labels are stored in PostgreSQL tables with labels repre-
sented as INT[] arrays indexed using GIN for efficient containment
queries. We use UNLOGGED tables to reduce index construction time.
For Pg-IVF, we scan all vectors in the 𝑛𝑝𝑟𝑜𝑏𝑒buckets closest to
the query vector and return the top-𝑘qualified vectors. Distances
are computed for both qualified and unqualified vectors. Pg-HNSW
employs iterative search that resumes exploration from the next ef
best candidates after each search iteration converges, terminating
after finding 𝑘qualified vectors or scanning at most 𝑇𝑚𝑎𝑥vectors.
To generate recall-throughput curves, we sweep 𝑛𝑝𝑟𝑜𝑏𝑒for Pg-IVF
and 𝑇𝑚𝑎𝑥for Pg-HNSW.
In all experiments, we store vectors and compute distances in
float32 precision without scalar or product quantization [17], and
conduct parameter sweeps to determine the Pareto-optimal configu-
rations for each index. For experiments displaying the performance
of only a single configuration per index, we choose the config-
uration that achieves 90% recall with the lowest search latency.
We list the parameter space of each baseline below: (1) Per-label
IVF: 𝑛𝑙𝑖𝑠𝑡∈{10, 20, 40}, 𝑛𝑝𝑟𝑜𝑏𝑒∈{1, 2, 4, 8}; (2) Per-label HNSW:
𝑒𝑓𝑐∈{16, 32, 64, 128}, 𝑀∈{8, 16, 32}, 𝑒𝑓∈{8, 16, 32, 64, 128};
(3) Shared IVF: 𝑛𝑙𝑖𝑠𝑡∈{1000, 2000, 4000, 8000, 16000}, 𝑛𝑝𝑟𝑜𝑏𝑒∈
{8, 16, 32, 64, 128}; (4) Shared HNSW: 𝑒𝑓𝑐∈{16, 32, 64, 128}, 𝑀∈
{16, 32, 64}, 𝑒𝑓∈{8, 16, 32, 64, 128}; (5) Parlay-IVF: 𝑀∈{8, 16, 32},
𝑒𝑓
∈{8, 16, 32, 64, 128}; (6) Filtered DiskANN: 𝑒𝑓𝑐∈{64, 128,
256, 512}, 𝑀∈{64, 128, 256, 512}, 𝑒𝑓∈{64, 128, 256, 512, 1024},
𝛼= 1.2; (7) ACORN-𝛾: 𝑀∈{16, 32, 64}, 𝛾∈{10, 20, 40}, 𝑀𝛽∈
{𝑀, 2𝑀, 4𝑀},𝑒𝑓∈{8, 16, 32, 64, 128}; (8) ACORN-1: 𝑀∈{16, 32, 64},
𝛾= 1, 𝑀𝛽= 2𝑀, 𝑒𝑓∈{8, 16, 32, 64, 128}; (9) Curator: 𝑛𝑙𝑖𝑠𝑡∈
{16, 32}, 𝐵𝑚𝑎𝑥∈{64, 128, 256} (buffer and leaf capacity), 𝑒𝑓∈
{64, 128, 256, 512, 1024}, 𝑏= 4 (beam size); (10) Pg-IVF: 𝑛𝑙𝑖𝑠𝑡∈
{1000, 2000, 4000, 8000, 16000},𝑛𝑝𝑟𝑜𝑏𝑒∈{8, 16, 32, 64, 128, 256, 512};
(11) Pg-HNSW: 𝑒𝑓𝑐∈{16, 32, 64, 128}, 𝑀∈{16, 32, 64}, 𝑒𝑓= 128,
𝑇𝑚𝑎𝑥∈{5 × 103, 1 × 104, 2 × 104, 4 × 104, 8 × 104}.
6.2
Query Performance
Single-Label Search. Fig. 7-8 show the recall-throughput trade-
offs across varied filter selectivity levels. Overall, per-label indexing
approaches achieve the best search performance when feasible:
Parlay-IVF consistently delivers optimal results by sharing vector
data among per-label indexes, while Per-Label HNSW and IVF’s
performance on arXiv do not scale to the 10M-scale YFCC-10M
dataset due to excessive memory overhead. However, these ap-
proaches come with prohibitive memory and construction costs, as
demonstrated in our analysis in the subsequent sections, making
them impractical for many real-world deployments.
For specialized indexes, performance degrades significantly as
selectivity decreases due to graph connectivity issues. Filtered
DiskANN’s performance drops most severely, suffering from its
less efficient compression strategy, while ACORN-1 slightly outper-
forms ACORN-𝛾at very low selectivity levels. Metadata filtering
baselines generally exhibit lower query throughput but show inter-
esting behavior at high selectivity levels: Shared HNSW achieves
better performance than specialized indexes at the 99th percentile
selectivity (0.34) on YFCC-10M, demonstrating that the query-time
overhead of specialized indexes can offset the benefits of fewer
unnecessary distance computations. The pgvector baselines gen-
erally achieve the lowest query throughput across all evaluated
approaches (except Filtered DiskANN on YFCC-10M at high recall
regimes), which can be attributed to executor overhead and disk I/O
costs that in-memory indexes do not incur. Notably, Pg-IVF exhibits
an unusual pattern in low selectivity query groups: at high search
budgets, the recall-throughput curve flattens. This occurs because
the query planner estimates high cost for vector index scans and
opts for pre-filtering instead.
Curator consistently achieves competitive performance across all
selectivity levels, particularly excelling at low selectivity where spe-
cialized indexes and metadata filtering baselines struggle. For exam-
ple, Shared HNSW and pgvector are dominated by pre-filtering at
low selectivity, while Curator achieves higher throughput than pre-
filtering at near-perfect recall and offers a flexible recall-throughput
trade-off. Additionally, Curator matches Per-Label IVF’s search per-
formance on arXiv while requiring significantly lower memory


## PDF page 10

Yicheng Jin, Yongji Wu, Wenjun Hu, Bruce M. Maggs, Jun Yang, Xiao Zhang, and Danyang Zhuo
0.6
0.8
1.0
101
102
103
104
QPS
1p Sel (0.0013)
0.6
0.8
1.0
101
102
103
104
25p Sel (0.0016)
0.6
0.8
1.0
Recall@10
101
102
103
104
50p Sel (0.0022)
0.6
0.8
1.0
101
102
103
104
75p Sel (0.0033)
0.6
0.8
1.0
101
102
103
104
99p Sel (0.3387)
Parlay IVF
Filtered DiskANN
Shared HNSW
Shared IVF
Pg-HNSW
Pg-IVF
ACORN-1
ACORN-
Pre-Filtering
Curator
Figure 7: Trade-offs between recall and query throughput for single-label filters with varied selectivity on YFCC-10M. Each line
represents an algorithm’s performance across various search parameter configurations. Parenthetical numbers in subplot titles
indicate the actual selectivity value for each filter.
0.50
0.75
1.00
101
102
103
104
QPS
1p Sel (0.0083)
0.50
0.75
1.00
101
102
103
104
25p Sel (0.0566)
0.50
0.75
1.00
Recall@10
101
102
103
104
50p Sel (0.0883)
0.50
0.75
1.00
101
102
103
104
75p Sel (0.1252)
0.50
0.75
1.00
101
102
103
104
99p Sel (0.2679)
Per-Label HNSW
Per-Label IVF
Parlay IVF
Filtered DiskANN
Shared HNSW
Shared IVF
Pg-HNSW
Pg-IVF
ACORN-1
ACORN-
Pre-Filtering
Curator
Figure 8: Trade-offs between recall and query throughput for single-label filters with varied selectivity on arXiv. Each line
represents an algorithm’s performance across various search parameter configurations. Parenthetical numbers in subplot titles
indicate the actual selectivity value for each filter.
usage and construction costs, demonstrating the efficiency of its
embedded per-label indexes.
This performance profile reflects Curator’s deliberate design
trade-offs as discussed in §1. Per-label indexing approaches (Parlay-
IVF, Per-Label HNSW/IVF) achieve the best search performance
by eliminating query-time filtering but require excessive mem-
ory overhead and construction costs due to vector duplication, as
demonstrated in §6.3. Similarly, Curator exhibits slower search than
specialized graph-based indexes (ACORN) at high selectivity, which
is expected since Curator’s partition-based approach prioritizes ro-
bustness to sparsity over the precision of graph connectivity. How-
ever, Curator’s performance advantage emerges precisely where
it was designed to excel: at low selectivity where graph connec-
tivity breaks down, Curator maintains efficient search while other
approaches either fail entirely or fall back to expensive pre-filtering.
Complex Predicate Queries. We construct queries with complex
predicates based on the YFCC-10M dataset, evaluating two types
of filter predicates: OR queries (𝑙1 ∨𝑙2, requiring existence of either
label in the vector’s label list) and AND queries (𝑙1 ∧𝑙2, requiring
existence of both labels). Given the average label selectivity of
approximately 1% in YFCC-10M, OR queries have selectivity around
2% while AND queries have selectivity around 0.01%. For each
predicate type, we randomly generate 100 label combinations and
0.8
1.0
Recall@10
10¹
10³
QPS
OR (Sel = 2.0e-02)
0.8
1.0
Recall@10
10
10
0.5
1.0
Recall@10
10
10²
10
AND (Sel = 3.2e-04)
Curator
Curator (Idx)
Shared HNSW
Per-Pred HNSW
Shared IVF
Per-Pred IVF
Pg-HNSW
Pg-IVF
Parlay IVF
ACORN
Pre-Filter
Figure 9: Recall vs query throughput trade-offs for complex
predicate queries. Each line represents an algorithm’s per-
formance across various search parameter configurations.
Average selectivity values are annotated in subplot titles.
AND query results are split into two subplots due to the wide
throughput range. Parlay-IVF only supports AND queries.
select 100 query vectors from the test set, resulting in 10,000 total
queries.


## PDF page 11

Curator : Efficient Vector Search with Low-Selectivity Filters
103
104
105
106
Build Time (s)
10
1
100
101
102
Index Overhead (GB)
YFCC-10M (Sel 
 0.15)
102
103
104
105
Build Time (s)
10
1
100
101
102
arXiv (Sel 
 0.15)
Latency
(R=0.9)
1ms
10ms
100ms
Curator
Pg-I
A-1
S-H
S-I
Pg-H
A-γ
Disk
Curator
S-I
P-I
P-H
A-1
Par
Pg-H
A-γ
Disk
S-H
Par
Pg-I
Figure 10: Trade-off between index overhead, build time and
search latency for evaluated indexes. We report search la-
tency at 0.9 recall on low selectivity queries (≤0.15). For
pgvector baselines, we report on-disk footprint rather than
in-memory overhead. Marker size indicates search latency.
We evaluate Curator’s performance against Shared HNSW, Shared
IVF, Parlay-IVF (AND queries only), ACORN, and pgvector base-
lines (Pg-IVF and Pg-HNSW), which are the only baselines support-
ing complex predicates. Two variants of Curator are evaluated: (1)
predicate not indexed, where Curator constructs a temporary index
structure at query time for qualified vectors and traverses it, and (2)
predicate indexed, where qualified vectors for each predicate are
pre-indexed as virtual labels, eliminating the need for temporary
index construction during search. Per-predicate HNSW and IVF
approaches, which build separate indexes for all possible predi-
cates, serve as references for optimal performance but are generally
impractical due to prohibitive memory and construction costs.
As shown in Fig. 9, Curator with predicate indexed achieves near-
optimal performance compared to the per-predicate approaches,
demonstrating the index quality of Curator’s temporary indexes.
Furthermore, Curator without indexing achieves similar perfor-
mance to the indexed version, significantly outperforming ACORN
on both filter types and showcasing the efficiency of temporary
index construction at query time. ACORN fails to achieve >0.2 recall
on AND queries due to limited connectivity in the graph structure
at low selectivity. For AND queries, pgvector shows only a single
point as the PostgreSQL query planner picks pre-filtering plan due
to the extremely low selectivity.
The results demonstrate that Curator provides a unified solu-
tion for both single-label and complex predicate search: while it
delivers competitive performance on single-label filters across all
selectivity levels, it uniquely excels at complex predicates where
most existing approaches either fail entirely or require prohibitive
memory overhead for per-predicate indexing.
6.3
Resource Efficiency
Performance vs Resource Trade-offs. Fig. 10 provides a compre-
hensive view of the fundamental trade-offs between search perfor-
mance, memory efficiency, and construction cost across all evalu-
ated approaches. This analysis focuses on low-selectivity scenarios
(≤0.15) where the differences between approaches are most pro-
nounced.
The results demonstrate that Curator achieves competitive search
performance on low-selectivity queries while maintaining the low-
est in-memory overhead and build time among all evaluated ap-
proaches. Only two baselines, Parlay-IVF and Per-Label HNSW,
P-HNSWP-IVF
Parlay
DiskANN
ACORN-
ACORN-1
S-HNSWS-IVF
Pg-HNSW
Pg-IVF
Curator
102
103
104
105
106
Construction Time (s)
YFCC-10M
arXiv
Figure 11: Index construction time for the evaluated indexes.
achieve lower search latency than Curator, but at significantly
higher resource costs. Specifically, Parlay-IVF requires 206× and
266× longer build times on YFCC-10M and arXiv respectively, with
2.8× and 5.1× higher memory overhead. Per-Label HNSW (on arXiv)
incurs 108× longer build time and 43× higher memory overhead for
only marginal latency improvements. Pg-IVF’s on-disk footprint is
exceptionally low at 0.95GB on YFCC-10M and 0.31GB on arXiv,
attributed to compact inverted list encoding. However, its search
performance lags behind in-memory approaches due to executor
overhead and disk I/O costs as discussed previously.
This analysis reveals why Curator represents an attractive prac-
tical solution: it delivers performance competitive with the best
approaches while requiring minimal additional resources, making it
suitable for deployment scenarios where memory and construction
time constraints are critical considerations.
Construction Time. Fig. 11 compares the index construction
time of Curator against all baselines. The index construction time
includes the time to optionally train the index structure (required
for Curator and IVF baselines) and to insert all vectors and labels.
Filtered DiskANN, ACORN, Parlay-IVF, and pgvector baselines
are constructed in batch mode, whereas all other indexes are built
incrementally.
Overall, Curator achieves the lowest construction time across all
evaluated approaches, requiring only 727s on YFCC-10M and 144s
on arXiv. Specialized indexes like ACORN-𝛾and Filtered DiskANN
incur dramatically higher construction costs on both datasets, pri-
marily due to the overhead of constructing and compressing the
intermediate dense graph. Specifically, ACORN-𝛾requires 468×
and 482× longer construction time than Curator on YFCC-10M
and arXiv respectively, while Filtered DiskANN requires 1,360×
and 459× longer time. These specialized approaches require signifi-
cantly more construction time than even building separate per-label
indexes: both ACORN-𝛾and Filtered DiskANN take around 4×
longer time to build than Per-Label HNSW on arXiv, highlighting
the substantial overhead of these sophisticated indexing strategies.
Memory Overhead. Index overhead represents the additional
memory required by indexes beyond storing the original vectors
(assuming no compression), calculated by subtracting raw vector
storage size from the total memory footprint. We subtract vector
storage because all indexes maintain at least one copy of vector
data, making this a fair comparison baseline. However, we do not
subtract label/metadata storage since indexes preserve metadata
in vastly different formats: some store plain label lists or inverted


## PDF page 12

Yicheng Jin, Yongji Wu, Wenjun Hu, Bruce M. Maggs, Jun Yang, Xiao Zhang, and Danyang Zhuo
P-HNSW
P-IVF
Parlay
DiskANN
ACORN-
ACORN-1
S-HNSW
S-IVF
Pg-HNSW
Pg-IVF
Curator
0
10
20
30
Index Overhead (GB)
YFCC-10M
arXiv
Figure 12: Index overhead for evaluated indexes. Index over-
head is computed by subtracting the raw vector storage size
(excluding label lists) from the total memory footprint. For
pgvector baselines, we report on-disk footprint.
indexes, while per-label indexing baselines require no metadata
storage at all.
As shown in Fig. 12, Curator and Pg-IVF achieve the lowest in-
dex overhead among all evaluated approaches. Curator requires
3.45GB on YFCC-10M and 0.68GB on arXiv, while Pg-IVF achieves
even lower on-disk footprint at 0.95GB and 0.31GB respectively.
Per-label indexing approaches incur dramatically higher memory
costs due to vector data duplication, with Per-Label HNSW and
Per-Label IVF requiring 43× and 56× more overhead than Curator
on arXiv, respectively. Specialized indexes show more moderate
but still significant overhead increases due to their large graph de-
grees: Filtered DiskANN requires 5.7× and 4.4× more memory than
Curator, while ACORN variants require 2.5–2.8× more overhead
across both datasets.
6.4
Integration with Graph-Based Indexes
In this section, we evaluate how integrating Curator with graph-
based indexes improves search performance at low selectivity with-
out significantly increasing construction costs.
Dataset. For our evaluation, we construct a semi-synthetic dataset
from a randomly sampled 1M subset of the YFCC-10M dataset to
achieve systematic coverage of the low-selectivity regime with suf-
ficient data points at each selectivity level for reliable evaluation.
Specifically, we define 20 selectivity levels distributed on a logarith-
mic scale within the range [0.001, 0.2]. The logarithmic distribution
ensures dense sampling at the lower end of the selectivity spec-
trum where Curator is designed to excel. For each selectivity level,
we generate 10 labels by randomly sampling the corresponding
fraction of vectors from the dataset. We determine the optimal
build configuration for each index through grid search and evaluate
queries under varying search configurations. For each selectivity
level, we report the minimum query latency required to achieve an
average recall of 90%.
Baselines and Integration Strategy. We select ACORN as the
graph-based index as it achieves best performance on our datasets.
To illustrate the trade-offs of densifying graphs, we evaluate two
ACORN-𝛾constructions with 𝛾= 10 and 𝛾= 40, denoted as
ACORN-10 and ACORN-40, respectively. For low-selectivity queries,
we combine ACORN with either pre-filtering or Curator, resulting
in four hybrid indexes in total. In our integration experiments, we
P-HNSW
P-IVF
S-HNSW
S-IVF
Curator
10
3
10
2
10
1
100
Latency (ms)
Label Insertion
YFCC-10M
arXiv
S-HNSW
S-IVF
Curator
10
3
10
2
10
1
100
Latency (ms)
Vector Insertion
Label
Vector
10
3
10
2
10
1
100
Deletion (Curator)
Figure 13: Update performance for the indexes supporting
incremental updates. Per-label indexing baselines do not
support vector insertion. Deletion latency for vectors and
labels is only shown for Curator, as other indexes do not
support deletion.
assume users perform selectivity estimation and choose indexes
based on fixed selectivity thresholds determined through offline
profiling, similar to ACORN’s approach. In practice, this selection
logic can also be handled by external systems such as database
query optimizers, allowing flexible integration strategies tailored
to specific workload patterns.
Results. As shown in Fig. 2, densifying the graph-based index
yields negligible improvements at low selectivity while significantly
raising construction costs, increasing construction time by approxi-
mately 5× in our experiments. Integrating ACORN-10 with Curator
increases construction time and memory footprint by only 5.5%
and 4.3%, respectively, while improving search performance by up
to 20.9× compared to ACORN with pre-filtering at low selectivity.
These results demonstrate that Curator provides an efficient com-
plementary solution for low-selectivity queries without imposing
significant resource overhead.
6.5
Update Performance
Fig. 13 compares update performance of Curator against baselines
supporting incremental updates. Parlay-IVF, Filtered DiskANN, and
ACORN are excluded due to their lack of incremental update sup-
port. Per-label indexing baselines do not support vector insertion,
as vectors are directly inserted into corresponding per-label indexes
during label insertion operations. For deletion operations, only Cu-
rator supports both vector and label deletion, so performance is
shown for Curator only.
While per-label indexing baselines avoid vector insertion costs,
they incur significant overhead when assigning new labels to vec-
tors, as each assignment involves an update to the corresponding
index. In contrast, metadata filtering baselines involve costly dis-
tance computations during vector insertion but achieve fast label
insertion by simply appending labels to metadata. Therefore, for
scenarios where each vector is associated with many labels, the


## PDF page 13

Curator : Efficient Vector Search with Low-Selectivity Filters
0
5000
10000
Number of Labels
0
5
10
Memory Footprint (GB)
2
4
6
8
10
Number of Vectors (M)
10
20
Per-Label HNSW
Per-Label IVF
Parlay IVF
Filtered DiskANN
Shared HNSW
ACORN-1
ACORN-
Curator
Figure 14: Curator exhibits superior memory efficiency as
the number of labels and vectors scales. The light green line
shows the size of vector storage alone.
total update costs for per-label indexing are substantially higher
than those for metadata filtering.
Compared to all baselines, Curator achieves consistently low
latency across all update operations. Vector insertion in Curator is
4.4–58.7× faster than shared index approaches, while label insertion
maintains minimal overhead of 0.002–0.003 ms across both datasets.
Curator’s deletion performance is also efficient, with vector deletion
requiring 0.004–0.006 ms and label deletion requiring 0.005–0.010
ms. This low latency stems from the efficient navigation afforded
by Curator’s tree structure and fast buffer operations.
6.6
Scalability
In this section, we assess Curator’s memory efficiency across vary-
ing numbers of labels and vectors.
Scaling with Number of Labels. For this experiment, we use a 1M
subset of YFCC-10M, generating a series of datasets with varying
numbers of labels where each label is randomly assigned to 1% of
vectors. As shown in Fig. 14 (left), per-label approaches (HNSW
and IVF) exhibit the steepest growth due to vector duplication
across multiple indexes. Parlay-IVF grows second fastest—while it
shares vector storage, each vector is indexed multiple times as the
label count increases, increasing the total number of graph edges.
Shared HNSW and ACORN show moderate growth since their
index structures remain unchanged as labels increase, requiring
only additional label storage. Curator achieves the smallest growth
rate through compact encoding of embedded per-label indexes.
Filtered DiskANN fails beyond 600 labels because the merged graph
becomes increasingly dense, requiring more aggressive pruning to
maintain a fixed maximum degree, which significantly degrades
recall.
Scaling with Number of Vectors. For this experiment, we use
sampled subsets of the YFCC-10M dataset ranging from 2M to 10M
vectors, with average labels per vector held constant. Per-label
HNSW and IVF are excluded from this comparison due to excessive
memory requirements. As shown in Fig. 14 (right), the differences
between graph-based approaches primarily stem from varied graph
degrees: Filtered DiskANN requires significantly more edges to
maintain connectivity after pruning, while Parlay-IVF, ACORN,
and Shared HNSW exhibit similar memory growth rates since each
vector maintains comparable total edge counts (aggregated across
0.25
0.50
0.75
1.00
Query Latency (ms)
0.8
0.9
Recall@10
Curator
Unconstrained
0.25
0.50
0.75
1.00
Query Latency (ms)
0.8
0.9
Recall@10
Original
Shuffled
2
4
6
8
10
Number of Vectors (M)
0.00
0.02
0.04
0.06
% Indexed Vectors
Vectors
Nodes
Figure 15: Ablation studies validating Curator’s design: (a)
Performance impact of structural constraints on per-label
indexes. (b) Performance impact of vector-label correlation.
(c) Tree traversal overhead in Curator’s search algorithm.
all per-label indexes for Parlay-IVF). Curator maintains the lowest
memory footprint due to its compact encoding and partition-based
design.
6.7
Ablation Study
We conduct three ablation studies to validate Curator’s design and
evaluate its robustness across different dataset characteristics.
Structural Constraints. Curator enforces structural sharing by
constraining per-label indexes to follow the base index’s hierar-
chical partitioning, which potentially impacts performance since
per-label indexes cannot freely adapt to label distributions. To eval-
uate this trade-off, we compare Curator against a variant where
per-label indexes are constructed independently with the same
hyper-parameters. Fig. 15(a) shows nearly identical performance be-
tween both versions, demonstrating that structural constraints have
minimal impact on the quality of embedded per-label indexes. This
finding is further supported by the similar performance between
Curator with predicate indexed versus per-predicate indexing in
§6.2.
Vector-Label Correlation. Real-world datasets exhibit varying
degrees of vector-label correlation—the tendency for vectors shar-
ing the same label to cluster together in the embedding space due to
semantic similarity. To assess Curator’s robustness across datasets
with different correlation patterns, we test on a shuffled variant
of YFCC-10M where labels are randomly reassigned, effectively
eliminating vector-label correlation. Fig. 15(b) shows nearly iden-
tical recall-throughput trade-offs on both original and shuffled
datasets, confirming Curator’s robust performance across varied
data distributions. We additionally quantify correlation using the
"Query Correlation" metric from ACORN [32], which compares a
query’s distance to the set of qualified vectors against a random
set of equal size; larger values indicate stronger clustering by label.
On YFCC-10M, the average query correlation is 0.161, whereas on
the label-shuffled dataset it is 0.028, showing the effectiveness of
label shuffling and confirming that the original dataset exhibits
meaningful correlation.
Tree Traversal Overhead. Unlike graph-based indexes where
all distance computations target individual vectors, Curator per-
forms distance computations against both cluster centroids (during
tree traversal) and vectors (during buffer scanning). To quantify
the overhead of tree traversal, we measure the proportion of dis-
tance computations used for tree traversal versus buffer scanning.


## PDF page 14

Yicheng Jin, Yongji Wu, Wenjun Hu, Bruce M. Maggs, Jun Yang, Xiao Zhang, and Danyang Zhuo
Fig. 15(c) shows that traversal overhead decreases from 26.9% to
18.9% as dataset size grows from 2M to 10M vectors, demonstrat-
ing that the overhead remains moderate and diminishes at larger
scales.
7
Related Work
Approximate Nearest Neighbor Search. As vector similarity
search becomes increasingly important across various modern ap-
plications, many efficient vector indexing techniques have been
proposed. Graph-based indexes [9, 10, 13, 15, 16, 24] construct
proximity graphs, where vectors are connected to their nearest
neighbors. At query time, a best-first beam search begins from
a set of seed nodes and navigates through the graph to locate
vectors near the query. Partition-based indexes, which can be fur-
ther categorized into clustering-based [3, 4, 6, 19, 54], hashing-
based [1, 11, 30, 37, 38, 47] and tree-based [25, 26, 40] approaches,
partition the vector space into sub-spaces and represent vectors by
the sub-space to which they belong. During search, the algorithm
navigates the index to identify the sub-spaces closest to the query
vector and examines all vectors within them.
Filtered ANNS. Existing approaches to filtered ANNS follow three
main paradigms. Metadata filtering strategies, widely adopted in
production vector databases [42, 45], use cost models to dynamically
select optimal filtering strategies based on estimated selectivity.
Per-label indexing constructs dedicated vector indexes for each
label to eliminate query-time filtering overhead. While applying
this approach naively incurs prohibitive memory overhead due to
storing multiple copies of vectors, Parlay-IVF [20] addresses this
limitation through shared vector storage across per-label indexes.
To further improve performance beyond these straightforward
approaches, specialized indexes tailor both index construction and
search algorithms for filtered ANNS. Fused distance approaches [43,
46] incorporate attribute similarity into vector distance to bias
search toward qualified vectors, though they limit query expressive-
ness by requiring all attributes to be specified. Graph densification
methods [12, 32] build high-degree graphs to ensure connectivity
among qualifying vectors, then compress them to reduce memory
usage, but face prohibitive construction costs at low selectivity.
Subgraph stitching [5] constructs separate graphs for each unique
label set and adds interconnecting edges, yet suffers from degraded
index quality when these graphs are highly fragmented with few
vectors in each.
Beyond categorical filtering, several works address range filtering
on numeric attributes, where queries specify value ranges rather
than discrete labels. ARKGraph [55] and SeRF [56] merge and com-
press per-range indexes to reduce storage overhead, while iRange-
Graph [48] constructs graph indexes for fewer ranges and dynami-
cally combines them at query time. DIGRA [18] and RangePQ [51]
extend these approaches with dynamic update capabilities. Unlike
Curator’s focus on categorical label filtering, range filtering lever-
ages the total order of numeric attributes to optimize for contiguous
value ranges, representing a complementary but distinct problem
domain.
8
Conclusion
This paper introduced Curator, a partition-based index that comple-
ments existing graph-based approaches for low-selectivity filtered
ANNS. Curator employs hierarchical clustering to build specialized
per-label indexes within a shared clustering tree, adapting cluster-
ing granularity to label distributions while maintaining low over-
head through structural sharing. Curator supports both single-label
search and complex predicate search by dynamically constructing
temporary indexes with minimal overhead. Our evaluation demon-
strates that integrating Curator with state-of-the-art graph indexes
reduces low-selectivity query latency by up to 20.9× compared to
pre-filtering fallback, while increasing construction time and mem-
ory footprint by only 5.5% and 4.3%, respectively. Curator’s source
code is publicly available at https://github.com/hatsu3/curator-v2.
Acknowledgments
We thank the anonymous reviewers for their valuable feedback.
This research was partially supported by NSF AI Institute on Edge
Computing (Athena, grant No. 2112562) and NSF grants (2503010,
2402696, 2238665, 2402823), and by gifts from Adobe, Amazon, Meta,
and IBM.
References
[1] Alexandr Andoni, Piotr Indyk, Thijs Laarhoven, Ilya Razenshteyn, and Ludwig
Schmidt. 2015. Practical and optimal LSH for angular distance. Advances in
Neural Information Processing Systems 28 (2015).
[2] Lars Arge. 1995. The buffer tree: A new technique for optimal I/O-algorithms. In
Workshop on Algorithms and Data structures. Springer, 334–345.
[3] Artem Babenko and Victor Lempitsky. 2015. The inverted multi-index. IEEE
transactions on pattern analysis and machine intelligence 37, 6 (2015), 1247–1260.
[4] Dmitry Baranchuk, Artem Babenko, and Yury Malkov. 2018. Revisiting the
inverted indices for billion-scale approximate nearest neighbors. In Proceedings
of the European Conference on Computer Vision (ECCV). 202–216.
[5] Yuzheng Cai, Jiayang Shi, Yizhuo Chen, and Weiguo Zheng. 2024. Navigating La-
bels and Vectors: A Unified Approach to Filtered Approximate Nearest Neighbor
Search. Proceedings of the ACM on Management of Data 2, 6 (2024), 1–27.
[6] Qi Chen, Bing Zhao, Haidong Wang, Mingqin Li, Chuanjie Liu, Zengzhong
Li, Mao Yang, and Jingdong Wang. 2021. Spann: Highly-efficient billion-scale
approximate nearest neighborhood search. Advances in Neural Information
Processing Systems 34 (2021), 5199–5212.
[7] Colin B Clement, Matthew Bierbaum, Kevin P O’Keeffe, and Alexander A Alemi.
2019. On the use of arxiv as a dataset. arXiv preprint arXiv:1905.00075 (2019).
[8] Paul Covington, Jay Adams, and Emre Sargin. 2016. Deep neural networks
for youtube recommendations. In Proceedings of the 10th ACM conference on
recommender systems. 191–198.
[9] Cong Fu and Deng Cai. 2016. Efanna: An extremely fast approximate nearest
neighbor search algorithm based on knn graph. arXiv preprint arXiv:1609.07228
(2016).
[10] Cong Fu, Chao Xiang, Changxu Wang, and Deng Cai. 2019. Fast approximate
nearest neighbor search with the navigating spreading-out graph. Proceedings of
the VLDB Endowment 12, 5 (2019), 461–474.
[11] Aristides Gionis, Piotr Indyk, and Rajeev Motwani. 1999. Similarity search in
high dimensions via hashing. In Proceedings of the 25th International Conference
on Very Large Data Bases. 518–529.
[12] Siddharth Gollapudi, Neel Karia, Varun Sivashankar, Ravishankar Krishnaswamy,
Nikit Begwani, Swapnil Raz, Yiyong Lin, Yin Zhang, Neelam Mahapatro, Premku-
mar Srinivasan, Amit Singh, and Harsha Vardhan Simhadri. 2023. Filtered-
DiskANN: Graph Algorithms for Approximate Nearest Neighbor Search with
Filters. In Proceedings of the ACM Web Conference 2023. 3406–3416.
[13] Ben Harwood and Tom Drummond. 2016. FANNG: Fast approximate nearest
neighbour graphs. In Proceedings of the IEEE Conference on Computer Vision and
Pattern Recognition. 5713–5722.
[14] Hugging Face. 2021. sentence-transformers/all-MiniLM-L6-v2 - Hugging Face.
https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2.
Accessed:
2023-11-26.
[15] Masajiro Iwasaki and Daisuke Miyazaki. 2018. Optimization of indexing based on
k-nearest neighbor graph for proximity search in high-dimensional data. arXiv
preprint arXiv:1810.07355 (2018).


## PDF page 15

Curator : Efficient Vector Search with Low-Selectivity Filters
[16] Suhas Jayaram Subramanya, Fnu Devvrit, Harsha Vardhan Simhadri, Ravishankar
Krishnaswamy, and Rohan Kadekodi. 2019. Diskann: Fast accurate billion-point
nearest neighbor search on a single node. Advances in Neural Information
Processing Systems 32 (2019).
[17] Herve Jegou, Matthijs Douze, and Cordelia Schmid. 2011. Product quantization
for nearest neighbor search. IEEE Transactions on Pattern Analysis and Machine
Intelligence 33, 1 (2011), 117–128.
[18] Mengxu Jiang, Zhi Yang, Fangyuan Zhang, Guanhao Hou, Jieming Shi, Wenchao
Zhou, Feifei Li, and Sibo Wang. 2025. DIGRA: A Dynamic Graph Indexing for
Approximate Nearest Neighbor Search with Range Filter. Proceedings of the ACM
on Management of Data 3, 3 (2025), 1–26.
[19] Jeff Johnson, Matthijs Douze, and Hervé Jégou. 2019. Billion-scale similarity
search with GPUs. IEEE Transactions on Big Data 7, 3 (2019), 535–547.
[20] Ben Landrum, Magdalen Manohar, Mazin Karjikar, and Laxman Dhulipala. 2023.
ParlayANN’s IVF. https://github.com/harsha-simhadri/big-ann-benchmarks/
tree/main/neurips23/filter/parlayivf.
[21] Quoc Le and Tomas Mikolov. 2014. Distributed representations of sentences and
documents. In Proceedings of the International Conference on Machine Learning.
PMLR, 1188–1196.
[22] Patrick Lewis, Ethan Perez, Aleksandra Piktus, Fabio Petroni, Vladimir
Karpukhin, Naman Goyal, Heinrich Küttler, Mike Lewis, Wen-tau Yih, Tim
Rocktäschel, Sebastian Riedel, and Douwe Kiela. 2020. Retrieval-augmented
generation for knowledge-intensive nlp tasks. Advances in Neural Information
Processing Systems 33 (2020), 9459–9474.
[23] Yury Malkov and Dmitry Yashunin. 2018. hnswlib. https://github.com/nmslib/
hnswlib. Accessed: 2023-11-26.
[24] Yu A Malkov and Dmitry A Yashunin. 2020. Efficient and robust approximate
nearest neighbor search using hierarchical navigable small world graphs. IEEE
transactions on pattern analysis and machine intelligence 42, 4 (2020), 824–836.
[25] Marius Muja and David G Lowe. 2009. Fast approximate nearest neighbors with
automatic algorithm configuration. VISAPP (1) 2, 331-340 (2009), 2.
[26] Marius Muja and David G Lowe. 2014. Scalable nearest neighbor algorithms
for high dimensional data. IEEE transactions on pattern analysis and machine
intelligence 36, 11 (2014), 2227–2240.
[27] Annamalai Narayanan, Mahinthan Chandramohan, Rajasekar Venkatesan, Lihui
Chen, Yang Liu, and Shantanu Jaiswal. 2017. graph2vec: Learning distributed
representations of graphs. arXiv preprint arXiv:1707.05005 (2017).
[28] Maxim Naumov, Dheevatsa Mudigere, Hao-Jun Michael Shi, Jianyu Huang,
Narayanan Sundaraman, Jongsoo Park, Xiaodong Wang, Udit Gupta, Carole-Jean
Wu, Alisson G Azzolini, Dmytro Dzhulgakov, Andrey Mallevich, Ilia Cherniavskii,
Yinghai Lu, Raghuraman Krishnamoorthi, Ansha Yu, Volodymyr Kondratenko,
Stephanie Pereira, Xianjie Chen, Wenlin Chen, Vijay Rao, Bill Jia, Liang Xiong,
and Marat Smelyanskiy. 2019. Deep learning recommendation model for person-
alization and recommendation systems. arXiv preprint arXiv:1906.00091 (2019).
[29] Pandu Nayak. 2019. Understanding searches better than ever before.
https:
//blog.google/products/search/search-language-understanding-bert/ Accessed:
2023-11-26.
[30] Behnam Neyshabur and Nathan Srebro. 2015. On symmetric and asymmetric
lshs for inner product search. In International Conference on Machine Learning.
PMLR, 1926–1934.
[31] OpenAI. 2023. ChatGPT Retrieval Plugin. https://github.com/openai/chatgpt-
retrieval-plugin. Accessed: 2023-11-26.
[32] Liana Patel, Peter Kraft, Carlos Guestrin, and Matei Zaharia. 2024. ACORN: Per-
formant and Predicate-Agnostic Search Over Vector Embeddings and Structured
Data. Proceedings of the ACM on Management of Data 2, 3 (2024), 1–27.
[33] pgvector contributors. 2024. pgvector. https://github.com/pgvector/pgvector.
[34] Pinecone. 2023. Pinecone. https://www.pinecone.io/. Accessed: 2023-11-26.
[35] Qdrant. 2023. Qdrant. https://github.com/qdrant/qdrant. Accessed: 2023-11-26.
[36] Alec Radford, Jong Wook Kim, Chris Hallacy, Aditya Ramesh, Gabriel Goh,
Sandhini Agarwal, Girish Sastry, Amanda Askell, Pamela Mishkin, Jack Clark,
Gretchen Krueger, and Ilya Sutskever. 2021. Learning transferable visual models
from natural language supervision. In Proceedings of the International Conference
on Machine Learning. PMLR, 8748–8763.
[37] Anshumali Shrivastava and Ping Li. 2014. Asymmetric LSH (ALSH) for sublinear
time maximum inner product search (MIPS). Advances in Neural Information
Processing Systems 27 (2014).
[38] Anshumali Shrivastava and Ping Li. 2015. Improved asymmetric locality sensitive
hashing (ALSH) for maximum inner product search (MIPS). In Proceedings of the
Conference on Uncertainty in Artificial Intelligence.
[39] Harsha Simhadri, Martin Aumuller, Dmitry Baranchuk, Matthijs Douze, Amir In-
gber, Edo Liberty, Frank Liu, and George Williams. 2023. NeurIPS’23 Competition
Track: Big-ANN. https://big-ann-benchmarks.com/neurips23.html.
[40] Spotify. 2013. Annoy (Approximate Nearest Neighbors Oh Yeah). https://github.
com/spotify/annoy. Accessed: 2023-11-26.
[41] Bart Thomee, David A Shamma, Gerald Friedland, Benjamin Elizalde, Karl Ni,
Douglas Poland, Damian Borth, and Li-Jia Li. 2016. YFCC100M: The new data in
multimedia research. Commun. ACM 59, 2 (2016), 64–73.
[42] Jianguo Wang, Xiaomeng Yi, Rentong Guo, Hai Jin, Peng Xu, Shengjun Li, Xi-
angyu Wang, Xiangzhou Guo, Chengming Li, Xiaohai Xu, Kun Yu, Yuxing Yuan,
Yinghao Zou, Jiquan Long, Yudong Cai, Zhenxiang Li, Zhifeng Zhang, Yihua Mo,
Jun Gu, Ruiyi Jiang, Yi Wei, and Charles Xie. 2021. Milvus: A purpose-built vec-
tor data management system. In Proceedings of the ACM SIGMOD International
Conference on Management of Data. 2614–2627.
[43] Mengzhao Wang, Lingwei Lv, Xiaoliang Xu, Yuxiang Wang, Qiang Yue, and
Jiongkang Ni. 2022. Navigable proximity graph-driven native hybrid queries
with structured and unstructured constraints. arXiv preprint arXiv:2203.13601
(2022).
[44] Weaviate. 2023. Weaviate. https://github.com/weaviate/weaviate. Accessed:
2023-11-26.
[45] Chuangxian Wei, Bin Wu, Sheng Wang, Renjie Lou, Chaoqun Zhan, Feifei Li,
and Yuanzhe Cai. 2020. Analyticdb-v: A hybrid analytical engine towards query
fusion for structured and unstructured data. Proceedings of the VLDB Endowment
13, 12 (2020), 3152–3165.
[46] Wei Wu, Junlin He, Yu Qiao, Guoheng Fu, Li Liu, and Jin Yu. 2022. HQANN:
Efficient and Robust Similarity Search for Hybrid Queries with Structured and
Unstructured Constraints. In Proceedings of the 31st ACM International Conference
on Information & Knowledge Management. 4580–4584.
[47] Hao Xu, Jingdong Wang, Zhu Li, Gang Zeng, Shipeng Li, and Nenghai Yu. 2011.
Complementary hashing for approximate nearest neighbor search. In 2011 Inter-
national Conference on Computer Vision. IEEE, 1631–1638.
[48] Yuexuan Xu, Jianyang Gao, Yutong Gou, Cheng Long, and Christian S Jensen.
2024. irangegraph: Improvising range-dedicated graphs for range-filtering near-
est neighbor search. Proceedings of the ACM on Management of Data 2, 6 (2024),
1–26.
[49] Yuming Xu, Hengyu Liang, Jin Li, Shuotao Xu, Qi Chen, Qianxi Zhang, Cheng Li,
Ziyue Yang, Fan Yang, Yuqing Yang, Peng Cheng, and Mao Yang. 2023. SPFresh:
Incremental In-Place Update for Billion-Scale Vector Search. In Proceedings of
the 29th Symposium on Operating Systems Principles. 545–561.
[50] Belinda Zeng. 2022. Go beyond the search box: Introducing multisearch. https:
//blog.google/products/search/multisearch/
[51] Fangyuan Zhang, Mengxu Jiang, Guanhao Hou, Jieming Shi, Hua Fan, Wenchao
Zhou, Feifei Li, and Sibo Wang. 2025. Efficient Dynamic Indexing for Range
Filtered Approximate Nearest Neighbor Search. Proceedings of the ACM on
Management of Data 3, 3 (2025), 1–26.
[52] Qianxi Zhang, Shuotao Xu, Qi Chen, Guoxin Sui, Jiadong Xie, Zhizhen Cai, Yaoqi
Chen, Yinxuan He, Yuqing Yang, Fan Yang, Mao Yang, and Lidong Zhou. 2023.
VBASE: Unifying Online Vector Similarity Search and Relational Queries via
Relaxed Monotonicity. In Proceedings of the 17th USENIX Symposium on Operating
Systems Design and Implementation. 377–395.
[53] Shuai Zhang, Lina Yao, Aixin Sun, and Yi Tay. 2019. Deep learning based recom-
mender system: A survey and new perspectives. ACM computing surveys (CSUR)
52, 1 (2019), 1–38.
[54] Ting Zhang, Chao Du, and Jingdong Wang. 2014. Composite quantization for
approximate nearest neighbor search. In International Conference on Machine
Learning. PMLR, 838–846.
[55] Chaoji Zuo and Dong Deng. 2023. ARKGraph: All-Range Approximate K-Nearest-
Neighbor Graph. Proceedings of the VLDB Endowment 16, 10 (2023), 2645–2658.
[56] Chaoji Zuo, Miao Qiao, Wenchao Zhou, Feifei Li, and Dong Deng. 2024. SeRF:
Segment Graph for Range-Filtering Approximate Nearest Neighbor Search. Pro-
ceedings of the ACM on Management of Data 2, 1 (2024), 1–26.


