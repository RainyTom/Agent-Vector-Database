# 2505.03212v2 原始 PDF 文本提取

来源：https://arxiv.org/pdf/2505.03212v2

提取日期：2026-10-18；PyMuPDF，非 MinerU；公式和表格需回看 PDF。

## PDF page 1

Elastic Index Selection for Label-Hybrid AKNN Search
Mingyu Yang
HKUST (GZ) & HKUST
myang250@connect.hkust-gz.edu.cn
Wenxuan Xia
HKUST (GZ)
wxia248@connect.hkust-gz.edu.cn
Wentao Li
University of Leicester
wl226@leicester.ac.uk
Raymond Chi-Wing Wong
HKUST
raywong@cse.ust.hk
Wei Wang∗
HKUST (GZ) & HKUST
weiwcs@ust.hk
ABSTRACT
Real-world vector embeddings often carry additional label at-
tributes, such as keywords and tags. In this context, label-hybrid
approximate 𝑘-nearest neighbor (AKNN) search retrieves the
top-𝑘approximate nearest vectors to a query, subject to the con-
straint that their labels fully contain the query-label set. A naive so-
lution builds a separate index for every query-label set, but the expo-
nential growth of such sets makes this approach storage-prohibitive.
To overcome this, we propose selectively indexing only a subset
of query-label sets while still ensuring efficient processing for all
queries. This is made possible by a key insight into label contain-
ment: an index built for a label set 𝐿can also serve any query
whose label set 𝐿′ is a superset of 𝐿, with query cost bounded by
the elastic factor: the ratio between the number of vectors match-
ing 𝐿and those matching 𝐿′. We formalize the index-selection task
as a constrained optimization problem that chooses which label
sets to index to satisfy space and query efficiency constraints. We
prove the problem is NP-complete and propose efficient greedy algo-
rithms for its efficiency- and space-constrained variants. Extensive
experiments on real-world datasets show that our method achieves
10×–800× speedups over state-of-the-art techniques. Moreover, our
approach is index-agnostic and can be seamlessly integrated into
existing vector database systems.
PVLDB Reference Format:
Mingyu Yang, Wenxuan Xia, Wentao Li, Raymond Chi-Wing Wong, Wei
Wang. Elastic Index Selection for Label-Hybrid AKNN Search. PVLDB,
19(4): XXX-XXX, 2025.
doi:XX.XX/XXX.XX
PVLDB Artifact Availability:
The source code, data, and/or other artifacts have been made available at
https://github.com/mingyu-hkustgz/LabelANN.
1
INTRODUCTION
The 𝑘-nearest neighbor (KNN) search over high-dimensional
vectors is a core operation in modern data systems, underpinning
a wide range of applications such as recommendation systems [48],
data mining [8], face recognition [56], product search [56], and
∗Corresponding author.
This work is licensed under the Creative Commons BY-NC-ND 4.0 International
License. Visit https://creativecommons.org/licenses/by-nc-nd/4.0/ to view a copy of
this license. For any use beyond those covered by this license, obtain permission by
emailing info@vldb.org. Copyright is held by the owner/author(s). Publication rights
licensed to the VLDB Endowment.
Proceedings of the VLDB Endowment, Vol. 19, No. 4 ISSN 2150-8097.
doi:XX.XX/XXX.XX
Item #1
Brand:  A
Size:      L
Color:   blue
Year:     2025
x1
Item #2
Brand:  B
Size:     S
Color:  red
Year:    2024
x2
Item #3
Brand:  A
Size:     M
Color:  yellow
Year:    2024
x3
Item #4
Brand: A
Size:     L
Color:  white
Year:    2025
x4
An item that is most similar to
the photo
-Filters: "brand=A" and
"year=2025"
q
Customer
Vector Space
x1
x2
x3
x4
q
√
Figure 1: Example of label-hybrid AKNN search in an online shop-
ping scenario (𝑘= 1). Each item is represented by an embedding
vector 𝑥𝑖(𝑖∈[1, 4]) and associated with label attributes such as
brand and year. A customer submits a reference photo 𝑞together
with a query-label set 𝐿𝑞. The task is to find the item most similar to
𝑞that also satisfies the label constraint. Thus, 𝑥2 and 𝑥3 are discarded
for label mismatch, and 𝑥4 is returned as the nearest neighbor.
retrieval-augmented generation (RAG) for large language mod-
els [38]. In real-world deployments, vector embeddings are often
associated with label attributes, such as product brands, keywords,
and geolocations. For example, in an e-commerce setting, a user
may search for items similar to a given photo while specifying label
constraints like brand name and release year.
In this context, we introduce the problem of label-hybrid 𝑘-
nearest neighbor (KNN) search, illustrated in Fig. 1. Unlike tradi-
tional KNN search, this variant considers both the vector embed-
dings and their associated label attributes. Formally, each entry
in the database 𝑆consists of a vector embedding and a label set
𝐿. Given a query vector 𝑞and the label constraint (expressed as a
query-label set 𝐿𝑞), the goal is to retrieve the top-𝑘nearest neigh-
bors of 𝑞in 𝑆, where each returned vector with label set 𝐿must
contain all labels in the query-label set 𝐿𝑞, i.e., 𝐿𝑞⊆𝐿. However, due
to the curse of dimensionality [32], computing exact label-hybrid
KNN in high-dimensional spaces is computationally expensive. Re-
cent work therefore studies the label-hybrid approximate KNN
(AKNN) problem, which efficiently returns the top-𝑘approximate
neighbors satisfying the label constraint, trading a small amount of
accuracy for substantial performance gains [6, 46].
Existing Solutions. To support label-hybrid AKNN search, ex-
isting approaches often employ graph-based indexes [12, 13, 21,
23, 29, 34, 39, 40, 43, 47, 51, 54, 63] combined with filter-based
search strategies, due to their good search efficiency. Specifically,
the graph-based index is constructed by adding base vectors in data-
base 𝑆as nodes and connecting each node to its nearby neighbors
to form edges, which are carefully selected to support efficient nav-
igation. At query time, on top of the graph-based index, filter-based
arXiv:2505.03212v2  [cs.DB]  11 Dec 2025

## PDF page 2

Empty[400]
C[200]
ABC[100]
B[200]
AB[400]
BC[200]
AC[100]
A[400]
Empty[2000]
C[600]
ABC[100]
B[900]
AB[500]
BC[200]
AC[200]
A[1000]
ABC[100]
AB[400]
AC[100]
A[400]
(a) Number of Vectors
Associated with Label Set 
(b) Vector Groups Required
for Query-Label Set {A}
(c) Number of Vectors for
Each Query-Label Set
Figure 2: Motivating example where database 𝑆and queries draw
labels from the alphabet {𝐴, 𝐵,𝐶}. Base vectors in 𝑆are grouped
by their associated label sets 𝐿, each group denoted as {𝐿}[count].
Arrows connect a group 𝐿to groups with its minimal supersets. For
instance, Fig. 2(a) shows 400 vectors labeled only {𝐴}, written as
𝐴[400]. Given a AKNN query with 𝐿𝑞= {𝐴}, Fig. 2(b) highlights
all relevant groups—those whose label sets are supersets of {𝐴}—
namely {𝐴,𝐴𝐵,𝐴𝐶,𝐴𝐵𝐶}, totaling 1, 000 matching vectors that must
be indexed under 𝐿𝑞= {𝐴}. Fig. 2(c) summarizes the index sizes
required for each query-label set. Supporting all possible query-
label sets in the workload requires indexing 5, 400 vectors—about
2.75× the original dataset size.
search strategies [21] are applied to enforce label constraints and
retrieve the final results. In particular, two strategies—PreFiltering
and PostFiltering—are integrated into the graph-based index. The
PreFiltering strategy prunes nodes (i.e., base vectors) that do not
satisfy the query label constraints, removing both the nodes and
their connections to neighbors during the search. In contrast, the
PostFiltering strategy retains all nodes and connections, but ex-
cludes any label-mismatched nodes from being added to the results.
A base vector matches the query if its label set contains the
query-label set, and the selectivity of a query is the number of its
label-matched vectors. The performance of both strategies drops
sharply when selectivity is low. (1) PreFiltering suffers from reduced
accuracy as removing many label-mismatched nodes sparsifies the
graph, making it difficult to reach the target vectors. (2) While
PostFiltering retains all nodes, it still requires computing distances
between the query and many mismatched vectors, causing inef-
ficiency. To address these challenges, existing solutions such as
Milvus [50], ADB [56], VBASE [65], and CHASE [41] dynamically
select between PreFiltering and PostFiltering based on the query
workload and cost estimation. Yet, the limitations of these search
strategies persist. Some heuristic approaches, including NHQ [51]
and HQANN [57], propose fusion distances that combine both vec-
tor similarity and label matching between the query and base vec-
tors. These methods require manual tuning of vector–label weight
parameters and still perform well below the state-of-the-art.
State-of-the-Art.
To
support
label-hybrid
AKNN
search,
ACORN [46] and UNG [6] represent the state-of-the-art. ACORN
extends the PreFiltering strategy to address connectivity issues
under low selectivity by introducing a parameter 𝛾that increases
the graph density—each node has 𝛾times more outgoing edges
than in a standard index. This denser graph improves robustness
when filtering out label-mismatched nodes during query process-
ing. However, ACORN ignores label information during index
construction, leading to potential result incompleteness.
To address this, UNG exploits label containment during graph
construction. It partitions base vectors into groups by their label
Table 1: We compare our method with existing solutions along three
dimensions. Search Performance measures accuracy and efficiency,
validated experimentally. Index Flexibility captures the ability to
support label-hybrid AKNN search without dependence on a specific
index structure. Space–Performance Trade-offs assess how well the
method balances indexing cost and storage efficiency.
Feature
ELI (Our)
NHQ
UNG
ACORN
Filtered
Search Performance
★★★
★
★↑
★↑
★
Index Flexibility
✓
×
×
×
✓
Space–Performance Trade-offs
✓
×
×
×
×
sets and builds a subgraph for each group. Subgraphs for a label set
𝐿𝑞are connected to those of its minimal supersets via cross-group
edges, ensuring that any vector with a superset of 𝐿𝑞is reachable
from the group of 𝐿𝑞, thereby guaranteeing completeness. However,
both ACORN and UNG are limited to graph-based indexes and thus
lack index flexibility. Their performance also degrades when query
selectivity is low. Experiments show significant deterioration as the
size of the alphabet grows, and neither method can flexibly adjust
the index structure to meet tight storage constraints.
Motivation. Given an alphabet Σ from which both the database 𝑆
and queries draw labels, we may face an exponential explosion of
possible label sets. Specifically, let L be the set of query-label sets
in the workload (a subset of the power set of Σ). A naive strategy
for AKNN queries is to build a separate index for each query-label
set 𝐿𝑞∈L, where the index for 𝐿𝑞stores all vectors whose label
sets contain 𝐿𝑞. Under this scheme, a vector with label set 𝐿must
be inserted into every index whose query-label set is a subset of
𝐿. As illustrated in Fig. 2, a vector labeled {𝐴𝐵𝐶} must appear in
eight indexes: {∅,𝐴, 𝐵,𝐶,𝐴𝐵,𝐴𝐶, 𝐵𝐶,𝐴𝐵𝐶}, where ∅denotes the
top index with no label constraint (𝐿𝑞= ∅). This replication creates
substantial overhead: empirical studies show that for |Σ| = 6–10,
the total index entries can be 64×–1024× larger than the original
database, leading to prohibitive indexing and storage costs.
Our Solution. Indexing every label set in L is impractical due
to the sheer number of indexes and the resulting storage cost. A
natural question arises: can we index only a subset of L while still
enabling efficient label-hybrid AKNN search? This is non-trivial—if
a query-label set 𝐿∈L is not indexed, queries with label set 𝐿may
be inefficient to process. Fortunately, label containment provides a
key insight: an index built for 𝐿can also serve any query whose
label set 𝐿′ is a superset of 𝐿. We further show that the query la-
tency is bounded by an elastic factor—the ratio of vectors matching
𝐿′ to those matching 𝐿. Note that a higher elastic factor implies
better search efficiency. For example, in Fig. 2(c), all vectors match-
ing {𝐴𝐵} are contained in those matching {𝐴}, with an overlap
ratio (or elastic factor) of at least 0.5. Thus, the index on {𝐴} can
efficiently answer queries for {𝐴𝐵}. The index on {𝐵} has an even
higher overlap with {𝐴𝐵}, yielding greater efficiency. We therefore
formalize the problem of index selection as choosing a subset of
L to index while bounding both space usage and the elastic factor.
We first show that the decision version of the index-selection
problem is NP-complete. We then formulate two optimization vari-
ants: (1) an efficiency-constrained version that minimizes index
space while guaranteeing a lower bound on the elastic factor (search
2

## PDF page 3

efficiency), and (2) a space-constrained version that maximizes
the elastic factor under a fixed space budget. We propose a greedy
algorithm for the first variant and extend it to the second, and
we further describe how to update indexes in dynamic settings.
Overall, our methods selectively index label sets to control space
while maintaining high elastic factors for efficient label-hybrid
AKNN search. Compared with existing approaches (see Table 1),
our solution provides greater flexibility, higher search efficiency,
and superior space–performance trade-offs.
Contribution. We summarize our main contributions as follows:
Problem Analysis (§ 3). We identify the limitations of existing label-
hybrid AKNN solutions: limited flexibility in the underlying indexes
and suboptimal query performance. To address these issues, we
measure search performance using the elastic factor, which quanti-
fies how an index built on a query-label set 𝐿can support queries
over its supersets, motivating our novel index-selection problem.
Index Selection Problem Formulation (§ 3). We formally define the
decision version of the index-selection problem: given a query
workload with label sets L, can we choose a subset of L so that
the total index space stays within a specified bound and the elastic
factor exceeds a given threshold? We prove this problem is NP-
complete, showing that exact solutions are intractable.
Index Selection Problem Solutions (§ 4). Building on the decision ver-
sion, we define two practical optimization variants of the index
selection problem: (1) the efficiency-constrained variant, which
minimizes space given a lower bound on the elastic factor; and (2)
the space-constrained variant, which maximizes the elastic factor
under a space limit. These formulations enable users to balance
space and query efficiency. We design a greedy algorithm for the
first variant and extend it to solve the second.
Extensive Experiments (§ 5). We evaluate our algorithm on multiple
datasets with diverse label distributions associated with the base
vectors. Experiments show that our approach attains near-optimal
search efficiency with only a 100% space overhead and outperforms
competitors on large-scale datasets with large alphabets, achieving
10×–800× speedups over state-of-the-art baselines.
Due to space constraints, some proofs and experiments are omit-
ted, which can be found in our appendix.
2
PRELIMINARY
We formally define the label-hybrid approximate𝑘-nearest neighbor
search problem in § 2.1. We then review existing solutions in § 2.2.
We list the commonly used notations in the table 2.
2.1
Problem Statement
Each entry (𝑥𝑖, 𝐿𝑖) in the dataset 𝑆consists of a 𝑑-dimensional base
vector 𝑥𝑖∈R𝑑and an associated label set 𝐿𝑖, which may be empty,
where all possible labels are from the alphabet Σ. A label-hybrid
search is denoted by a query (𝑞, 𝐿𝑞), where 𝑞is the query vector
and 𝐿𝑞is the query-label set. When 𝐿𝑞is specified, we first filter
the dataset 𝑆to obtain a candidate subset 𝑆(𝐿𝑞) ⊆𝑆, defined as:
𝑆(𝐿𝑞) = {(𝑥𝑖, 𝐿𝑖) ∈𝑆| 𝐿𝑞⊆𝐿𝑖}, i.e., the label-matched vectors
whose label sets contain all labels in 𝐿𝑞. The label-hybrid 𝑘-nearest
neighbor search is performed over this candidate subset. Formally,
Table 2: Summary of Notations
Notation
Description
𝑆
Set of vectors, each with an associated label set
𝐿𝑖, 𝐿𝑞
Base and query label sets
𝑆(𝐿𝑞)
Subset of 𝑆whose label sets contain 𝐿𝑞
|𝐿|
Cardinality of label set 𝐿
L
Collection of all label sets
| L|
Number of label sets in L
I
Collection of indexes
L
The selected indexes from L
Σ
Label alphabet (all possible labels)
𝑁
Cardinality of 𝑆
𝑞
Query vector
𝑒
Elastic factor
𝑑
Dimensionality of vectors in 𝑆
𝛿(𝑢, 𝑣)
Distance (or similarity) between 𝑢and 𝑣
𝐼
AKNN search index (e.g., HNSW)
𝜏
Space budget
𝑐
Elastic-factor threshold
Definition 2.1 (Label-Hybrid KNN Search). Given a dataset 𝑆and
a query (𝑞, 𝐿𝑞), label-hybrid KNN search returns a set 𝑆′ ⊆𝑆(𝐿𝑞) of
𝑘entries such that for every (𝑥𝑖, 𝐿𝑖) ∈𝑆′ and every (𝑥𝑗, 𝐿𝑗) ∈𝑆(𝐿𝑞),
𝛿(𝑥𝑖,𝑞) ≤𝛿(𝑥𝑗,𝑞), where 𝛿(·, ·) denotes the vector distance (We
use the Euclidean distance as an example).
Exact 𝑘-nearest neighbor search often suffers from the curse
of dimensionality [32], causing traditional methods [4, 5, 45] that
perform well in low-dimensional spaces to degrade greatly in high-
dimensional settings. As a result, approximate 𝑘-nearest neighbor
(AKNN) search has been extensively studied [1, 9, 12, 13, 16–20, 34,
36, 39, 43, 53, 55, 59, 61, 66] for its ability to improve efficiency at
the cost of some accuracy. This challenge also arises in the context
of label-hybrid search. Therefore, we focus on the label-hybrid
approximate 𝑘-nearest neighbor (AKNN) problem. To evaluate the
result quality, we use recall as the metric, defined as recall = | ˆ𝑆∩
𝑆′|/|𝑆′|, where 𝑆′ is the exact result set and ˆ𝑆is the approximate
result set with | ˆ𝑆| = 𝑘. An effective label-hybrid AKNN solution
should balance high search efficiency with high recall.
Remark. Label-hybrid AKNN search can be viewed as a special
case of filtered nearest-neighbor search [21], where our filtering
requires the label set 𝐿𝑖of a base vector to contain the query-label
set 𝐿𝑞. Other variants—such as set intersection (𝐿𝑞∩𝐿𝑖≠∅) or set
equality (𝐿𝑞= 𝐿𝑖)—can be addressed using the solutions developed
for our studied AKNN search problem. A detailed analysis appears
in our appendix.
2.2
Existing Solutions
To address the label-hybrid AKNN search problem, existing ap-
proaches typically combine different types of AKNN indexes
with filter-based search strategies. Among these, graph-based in-
dexes [13, 29, 34, 40, 43, 47] are widely adopted due to their state-
of-the-art search efficiency. In such indexes, base vectors in the
dataset 𝑆are treated as nodes in a graph, where each node is care-
fully connected to its nearby vectors, enabling efficient navigation.
3

## PDF page 4

x4
x5
x2
x6
x7
x1
x3
Entry Node
PostFiltering
Case
PreFiltering
Case
Query Vector 2
Query Vector 1
Figure 3: The Example of Filter-Based Search
Graph Index. When a graph is used as an index, search starts
from a designated entry node and iteratively explores neighbors
that are progressively closer to the query until the top-1 result
is found. A key index design component is the edge-occlusion
strategy, which guides the search toward the query while keeping
the graph sparse and the node degree bounded by a constant [13,
33]. For AKNN search—where more than one result is required—
beam search is typically employed. It maintains the top-𝑚closest
candidates during traversal, where𝑚is the beam width. Under ideal
indexing conditions (i.e., high index quality and the query present
in the dataset) [13, 33], only one extra expansion step on the graph
is needed to retrieve the next neighbor, yielding an overall search
complexity logarithmic in the dataset size.
Filter-Based Search. On top of the graph index, we introduce
two filter-based search strategies—PreFiltering and PostFiltering—
to support label-hybrid AKNN search. Both strategies maintain
the original index structure and require only label-based filtering
during graph traversal. (1) PreFiltering strategy: Given a query
(𝑞, 𝐿𝑞), PreFiltering prunes unmatched nodes and their neighbors
during traversal. As shown in Fig. 3, suppose nodes 𝑥4 and 𝑥5 do
not satisfy the label constraint in query 1. In this case, PreFiltering
removes the outgoing edges of 𝑥5, thereby making the true nearest
neighbor 𝑥6 unreachable from the entry node 𝑥1. (2) PostFiltering
strategy: This method allows traversal through unmatched nodes
but excludes them from the result set. For instance, when processing
query 2 in Fig. 3, PostFiltering visits nodes 𝑥1,𝑥2,𝑥3,𝑥4, and 𝑥7.
Although 𝑥4 is label-unmatched, its outgoing edge is still used for
routing, enabling the algorithm to eventually reach and return 𝑥7.
For top-𝑘queries, PostFiltering traverses incrementally, visiting
successive nearest neighbors until 𝑘matching results are found.
However, if most nodes are label-mismatched, the algorithm may
still need to examine up to 𝑂(𝑁) = 𝑂(|𝑆|) nodes to obtain 𝑘
results, because these filtered-out nodes must still be visited for
PostFiltering. PreFiltering faces a similar challenge—under low se-
lectivity, it may fail to reach any valid neighbors from the entry
node, making its correctness and efficiency difficult to guarantee.
3
ELASTIC INDEX SELECTION PROBLEM
This section formally defines the index selection problem, aiming to
overcome the exhaustive cost in indexing space and time caused by
exponential label combinations. In particular, we first introduce the
elastic factor, which captures how an index built on a query-label
set can be reused for its supersets. We then formally define the index
selection problem as a decision problem: determining whether it
is possible to select a subset of label sets for indexing such that
both space usage and query efficiency remain within given bounds.
Finally, we prove that the problem is computationally hard.
3.1
Elastic Factor for Index-Sharing
Given an alphabet Σ, one could in principle build an index for
every label set in the power set of Σ, but the number of such sets
is exponential. Fortunately, index sharing can dramatically reduce
the required indexes. Specifically, an index built on a query-label
set 𝐿2 can serve as a substitute for another query-label set 𝐿1 if
it contains all base vectors associated with 𝐿1. This holds when
𝐿2 ⊆𝐿1, because every vector matching 𝐿1 also matches its subset
𝐿2. For example, the group labeled {𝐴} includes all entries from its
superset group {𝐴𝐵}, since any vector labeled {𝐴𝐵} also contains
label {𝐴}. Thus, a query with label set {𝐴𝐵} can be answered using
an index built on {𝐴}, allowing indexes for some query-label sets
to be omitted and reducing storage costs using label containment.
Elastic Factor. We next analyze the query efficiency provided by
shared indexes. For simplicity, assume we already have a subset of
indexes I = {𝐼1, . . . , 𝐼𝑚} built on a selected subset of possible query-
label sets (we discuss how to choose this subset later). Each index
𝐼𝑖∈I contains the base vectors in 𝑆that match some query-label
set 𝐿𝑖; we sometimes use 𝐼𝑖to refer directly to these label-matched
vectors. An index can answer a query with label set 𝐿𝑞only if it con-
tains all base vectors matching 𝐿𝑞, following the label-containment
rule described earlier. Intuitively, if the set 𝑆(𝐿𝑞) of vectors match-
ing the query-label set 𝐿𝑞is fully contained in some index 𝐼∈I
and overlaps heavily with 𝐼, query efficiency is high because few
label-mismatched vectors remain. This intuition motivates the elas-
tic factor: the maximum overlap ratio, across all indexes in I that
can answer query 𝑞, between 𝑆(𝐿𝑞) and the index used.
Definition 3.1 (Elastic Factor). Given a label-hybrid dataset 𝑆, a
query (𝑞, 𝐿𝑞), and an index set I = {𝐼1, . . . , 𝐼𝑚} where each 𝐼𝑖⊆𝑆,
the elastic factor of I with respect to 𝐿𝑞is defined as:
𝑒(𝑆(𝐿𝑞), I) =
max
𝑆(𝐿𝑞)⊆𝐼𝑖
|𝑆(𝐿𝑞)|
|𝐼𝑖|
.
We only use indexes 𝐼𝑖satisfying 𝑆(𝐿𝑞) ⊆𝐼𝑖, as they contain all
vectors matching 𝐿𝑞and can thus answer query 𝑞without omission.
The elastic factor is the maximum overlap ratio |𝑆(𝐿𝑞)|/|𝐼𝑖| between
𝑆(𝐿𝑞) and any such index 𝐼𝑖∈I. When I contains a single index 𝐼𝑖,
we also denote the elastic factor between 𝐿𝑞and 𝐼𝑖as 𝑒(𝑆(𝐿𝑞), 𝐼𝑖).
To guarantee a non-zero elastic factor, we include a top index 𝐼⊤
built over all base vectors in 𝑆without label filtering, ensuring
𝑆(𝐿𝑞) ⊆𝐼⊤. The elastic factor lies in (0, 1]: it is 1 when an index
is built exactly on the query-label set 𝐿𝑞and decreases toward 0
when the chosen index contains more label-mismatched vectors.
Example 1. Figure 4(a) lists the vector groups with their associated
label sets in the database 𝑆. Three vectors have no labels (Empty[3])
and three have the label set {𝐴𝐵𝐶}[3]. Figure 4(b) shows the number
of vectors matched by each possible query–label set. For example, a
query with label set 𝐿𝑞= {𝐴} matches all vectors labeled {𝐴}, {𝐴𝐵},
{𝐴𝐶}, or {𝐴𝐵𝐶}, totaling 10 vectors. Building index 𝐼2 on these 10
vectors (label set {𝐴}) supports not only 𝐿𝑞= {𝐴} but also queries such
as 𝐿𝑞= {𝐴𝐵}, since it contains all required base vectors. Figure 4(c)
illustrates index sharing with an elastic factor of 0.3. Index 𝐼2 can
answer the query 𝐿𝑞= {𝐴𝐵𝐶} because the overlap ratio is 3/10 = 0.3,
whereas index 𝐼1 (label set ∅) cannot, as its overlap ratio 3/17 is below
the threshold.
4

## PDF page 5

A[3]
Empty[3]
ABC[3]
B[1]
C[1]
AC[3]
AB[1]
BC[2]
I2:A[10]
I1:Empty[17]
I8:ABC[3]
I3:B[7]
I4:C[9]
I6:AC[6]
I5:AB[4]
I7:BC[5]
(a): Number of Vectors
Associated with Label Set
I1:{Empty, A, B, C, AC}
I2:{A, AB, AC, ABC}
I3:{B, AB, BC, ABC}
I4:{C, AC, BC, ABC}
I5:{AB, ABC}
I6:{AC, ABC}
I7:{BC, ABC}
I8:{ABC}
(c): The Index Sharing with
an Elastic Factor of 0.3
(b): Number of Vectors Matched
by Each Query-Label Set
I2:A[10]
I1:Empty[17]
I8:ABC[3]
I3:B[7]
I4:C[9]
I6:AC[6]
I5:AB[4]
I7:BC[5]
(d): Greedy Result
I2:A[10]
I1:Empty[17]
I8:ABC[3]
I3:B[7]
I4:C[9]
I6:AC[6]
I5:AB[4]
I7:BC[5]
(e): Optimal Result
Figure 4: The Running Example of Elastic Factor and Greedy Methods
𝑒= 0.001
𝑒= 0.01
𝑒= 0.1
𝑒= 1
90
92
94
96
98
100
101
102
103
recall@10(%)
Qps
90
92
94
96
98
100
101
102
103
recall@20(%)
Qps
Figure 5: Effect of the elastic factor on query efficiency (𝑘= 10: left,
𝑘= 20: right). We randomly generate label sets for both base and
query vectors and build HNSW indexes on the SIFT100M dataset.
Queries are grouped by elastic factor—0.001, 0.01, 0.1, and 1—with
𝑒= 1 serving as the optimal baseline.
Connection to Query Efficiency. In the extreme, one could per-
form filter-based search (e.g., PostFiltering) on the top index 𝐼⊤,
which contains all entries and whose label set (∅) is a subset of every
query-label set. However, this is inefficient for queries with non-
empty 𝐿𝑞because the search must traverse many label-mismatched
nodes in the graph index. The inefficiency arises because the elastic
factor of 𝐼⊤is typically very low for such queries. To raise the elastic
factor, additional indexes must be added to I. The key question then
becomes: when the elastic factor of I for a query 𝑞is at least 𝑐, what
is the resulting query cost?
To answer this, we first analyze the expected cost of KNN search
(ignoring label constraints). To retrieve the 𝑘nearest neighbors, we
first use the index to locate the top-1 result and then incrementally
continue the search from each previously discovered neighbor until
all 𝑘results are obtained (e.g., extending from top-1 to top-2, and
so on). Thus, the efficiency of KNN search depends on the expected
number of steps required to obtain 𝑘valid results. When using
an index built over the entire dataset 𝑆(as in PostFiltering), the
expected number of steps E(𝑟) can grow to 𝑂(𝑁) under low query
selectivity, where 𝑁= |𝑆|, explaining its inefficiency. In contrast, if
the query is answered using an index with a constant elastic factor
𝑐, the expected number of steps to obtain 𝑘results is bounded by
𝑂(𝑘/𝑐) [46, 60]. This bound holds because, in graph-based indexes,
retrieving the next nearest-neighbor/result typically requires only
one additional expansion step after locating the current result,
under ideal conditions (e.g., slow growth or when the query is
contained in the dataset). Extending this reasoning to our label-
hybrid AKNN search, we still first locate the top-1 nearest neighbor
and then continue the search until all 𝑘results satisfying the label
constraint are retrieved. We show that, apart from the cost of finding
the first neighbor, the additional cost of retrieving the remaining
results remains 𝑂(𝑘/𝑐), as formalized in Lemma 3.2.
Lemma 3.2. Given a dataset 𝑆, a query (𝑞, 𝐿𝑞), and a selected index
set I, let 𝑂(𝐶) denote the expected time to retrieve the top-1 neighbor
from a graph index. If the elastic factor satisfies 𝑒𝑆(𝐿𝑞), I ≥𝑐for
some constant 𝑐∈(0, 1], then the expected time to obtain the top-𝑘
label-hybrid AKNN results with PostFiltering is 𝑂

𝐶+ 𝑘
𝑐

.
Proof sketch. We make two assumptions. (i) After incurring a
cost of 𝑂(𝐶) to locate the top-1 nearest neighbor, each successive
nearest neighbor can be retrieved in amortized constant time. Note
that we do not require the visited neighbors to satisfy the query’s
label constraints, and the property holds for most graph indexes.
(ii) Conditioned on distance ordering, at least a fraction 𝑐of the
visited nearest neighbors satisfy the query’s label constraints, guar-
anteed by 𝑒(·, ·) ≥𝑐. Since PostFiltering outputs a vector only if it
matches the query label set, more than 𝑘neighbors may need to be
examined. Let𝑇denote the number of (steps to examine) neighbors
until 𝑘matches are found. 𝑇follows a negative-hypergeometric (or
geometric) distribution with success probability at least 𝑐, which
yields E[𝑇] ≤𝑘/𝑐. Including the cost 𝑂(𝐶) for locating the first
neighbor, the total expected cost is bounded by 𝑂(𝐶+ 𝑘/𝑐).
Remark on Lemma 3.2. The above result provides an expected
bound under two standard assumptions: (1) locating the top-1 near-
est neighbor costs 𝑂(𝐶), and enumerating additional neighbors re-
quires amortized constant work; (2) Label-matched and mismatched
vectors are assumed to be uniformly distributed in the graph and in-
dependent of the labels, ensuring that at least a 𝑐-fraction of visited
vectors satisfy the label constraint (as guaranteed by the elastic fac-
tor). This bound is not a worst-case guarantee, as these assumptions
may not always hold for graph indexes such as HNSW. Neverthe-
less, the elastic factor 𝑐remains a useful control knob for expected
performance, because the additional time cost scales directly with
𝑘/𝑐. Our empirical studies (Fig. 5) confirm this monotonic relation-
ship: larger elastic factors yield higher query throughput (Qps) at
fixed recall, and the observed performance drop as 𝑐decreases.
Empirical studies. To further validate Lemma 3.2, we measure the
practical efficiency of PostFiltering search under varying elastic
factors on the 100M subset of the SIFT1B dataset [2], denoted
SIFT100M. In the classical PostFiltering strategy, the elastic fac-
tor is fixed as it uses only the top index containing all base vectors.
Here, we add more indexes to I to vary the elastic factor and report
results in Fig. 5. As shown, higher elastic factors yield better effi-
ciency (measured in Qps), consistent with the lower time complexity
predicted by theory. When the factor reaches 1, search achieves
optimal efficiency—equivalent to indexing only the label-matched
data. Efficiency decreases sublinearly as the elastic factor drops: for
example, reducing the factor to 1
10 lowers throughput by only 2×
at 98% recall with 𝑘=10. This occurs because the added cost scales
5

## PDF page 6

Empty[2000]
C[600]
ABC[100]
B[900]
AB[500]
BC[200]
AC[200]
A[1000]
Elastic Factor=0.5
Empty[2000]
C[600]
ABC[100]
B[900]
AB[500]
BC[200]
AC[200]
A[1000]
Elastic Factor=0.3
Figure 6: Example of the EIS Problem
with 𝑘, while the top-1 search still runs in 𝑂(𝐶), usually 𝑂(log 𝑁).
The gap widens for larger 𝑘(e.g., a 3× slowdown at 𝑘=20 and 98%
recall), but in most applications 𝑘is small relative to 𝑁. Thus, as
long as the elastic factor remains constant, the overall time cost
stays bounded.
3.2
Problem Definition
We have analyzed the impact of the elastic factor on search effi-
ciency for a given index set I. When using I, the expected query cost
is directly tied to the elastic factor of I for each query. If the elastic
factor for every query in the workload is guaranteed to exceed a
constant lower bound 𝑐, then the AKNN search cost is bounded by
an additional 𝑂(𝑘/𝑐). This establishes a clear relationship between
index cost (the total size of indexes in I) and query efficiency (cap-
tured by the elastic factor). Building on this, among all subsets of
indexes that can correctly answer the query workload, we seek a
subset I that is both small and yields a large elastic factor, ensuring
strong query performance. This motivates the formal definition of
the Elastic Index Selection (EIS) problem.
Definition 3.3 (Fixed Efficiency Index Selection (EIS)).
Input A label-hybrid dataset 𝑆, a query workload with label
sets L = {𝐿1, . . . , 𝐿𝑛}, and a universal index set I =
{𝐼1, . . . , 𝐼𝑛}, where each index 𝐼𝑖= 𝑆(𝐿𝑖) is built over the
base vectors matching the query-label set 𝐿𝑖with cost
|𝐼𝑖|. Let 𝜏and 𝑐be non-negative real numbers.
Output Decide whether there exists a subset I ⊆I such that
𝑒𝑆(𝐿𝑖), I ≥𝑐for every 𝐿𝑖∈L and the total cost satis-
fies Í
𝐼∈I |𝐼| ≤𝜏.
The EIS problem is a decision problem: determine whether a
subset of indexes from the universal set I can be chosen such that
(1) the total space usage is within a user-specified bound 𝜏, and
(2) the elastic factor—which governs query efficiency—is at least a
given threshold 𝑐. In Section 4, we present the optimization version,
where the goal is either to minimize space cost while satisfying a
required elastic-factor threshold or to maximize the elastic factor
subject to a space budget.
Example 2. The EIS problem under different elastic-factor thresh-
olds, 𝑐= 0.5 and 𝑐= 0.3 is in Fig. 6. When 𝑐= 0.5, the top index 𝐼⊤
(built on the empty label set) can serve queries with 𝐿𝑞= {𝐴} and
𝐿𝑞= ∅, since their overlaps exceed 0.5. It cannot, however, support
𝐿𝑞= {𝐵}, whose overlap is only 0.45. With a more relaxed threshold
of 𝑐= 0.3, 𝐼⊤also supports 𝐿𝑞= {𝐵} and 𝐿𝑞= {𝐶}, but it still cannot
answer 𝐿𝑞= {𝐴𝐵}, whose overlap is 0.25, unless 𝑐< 0.25. Finally,
when 𝑐= 0, the top index can serve all possible queries.
Remark on Query Workload. In the problem definition, we as-
sume a query workload with query-label sets L𝑞= {𝐿1, . . . , 𝐿𝑛}.
This assumption is justified for two reasons: (1) It generalizes the
traditional setting where L𝑞includes all subsets of the alphabet Σ
(i.e., the power set). (2) It enables a workload-driven approach: L𝑞
can reflect historical query patterns, allowing index selection and
optimization to match realistic query behavior.
Although the total number of possible query-label sets can reach
2|Σ| in the worst case, the effective number is typically far smaller
due to mutual exclusivity among certain labels. For example, a prod-
uct has only one brand name, making brand labels mutually exclu-
sive. Our formulation explicitly captures such cases, ensuring that
incompatible label combinations—like different brand names—do
not co-occur in L𝑞. We also assume the workload always includes
the empty set ∅, representing queries with no label constraints and
thus matching all base vectors. For simplicity, we treat the cost of
this top index as zero, i.e., |𝐼top| = 0.
Remark on Index Size/Cost. The cost of each index 𝐼𝑖(built for
query-label set 𝐿𝑖) reflects the space it occupies. For graph-based
indexes, this cost can be approximated by the number of vectors it
contains, since node degree is typically bounded by a constant [13,
33]. In practice, each node maintains 𝑀edges to support efficient
memory access, where 𝑀is a user-defined parameter [42]. Thus,
the overall space usage of an index can be estimated as the product
of its vector count and 𝑀. In our study, however, we measure index
size/cost |𝐼| simply by the number of vectors it contains.
Index Selection at Query Time. At query time, a label set 𝐿𝑞may
be served by multiple indexes in the selected set I. For example, a
query with 𝐿𝑞= {𝐴𝐵} can be answered by the index for {𝐴} or
for {𝐵} if both are present in I. In such cases, our method selects
the index that yields the maximum elastic factor with 𝑆(𝐿𝑞). This
choice improves query efficiency because greater overlap means
fewer label-mismatched vectors in the index, reducing the cost of
scanning irrelevant entries.
3.3
Problem Hardness
We prove that the EIS problem is NP-complete in Theorem 3.4,
underscoring the computational challenges inherent in solving it.
Theorem 3.4. The EIS problem is NP-complete.
Proof sketch. We show EIS is NP-complete by reducing from
the 3-Set Cover (3-SC) problem [10, 37]. EIS is in NP as validity is
verifiable in polynomial time. Given a 3-SC instance with elements
U, subsets S, and budget 𝑘, we construct an EIS instance with a
universal index set I containing: (1) an element index 𝐼𝑢𝑖for each
𝑢𝑖∈U; (2) a subset index 𝐼𝑠𝑗for each 𝑠𝑗∈S; (3) a top index 𝐼⊤
covering all base vectors; and (4) a bottom index 𝐼all.
Let𝑏> 3. We set the index sizes to |𝐼𝑢𝑖| = 𝐴= 𝑏+1, |𝐼𝑠𝑗| = 𝐵= 2𝑏,
|𝐼⊤| = 𝑁> 4𝑏, and |𝐼all| = 𝑏. We set the elastic factor 𝑐= 2𝑏/𝑁and
the cost budget 𝜏= 𝑘· 𝐵. The construction ensures the following
overlap properties: (1) 𝐼⊤covers every subset index 𝐼𝑠𝑗with ratio
𝐵/𝑁= 𝑐, but fails to cover element indexes 𝐼𝑢𝑖sufficiently (ratio
𝐴/𝑁< 𝑐). (2) A subset index 𝐼𝑠𝑗covers an element index 𝐼𝑢𝑖with
ratio 𝐴/𝐵> 𝑐if and only if 𝑢𝑖∈𝑠𝑗. (3) To enforce cost constraints,
we introduce duplicate elements 𝑢′
𝑖such that covering a specific
element 𝑢𝑖directly via element indexes requires selecting both 𝐼𝑢𝑖
6

## PDF page 7

and 𝐼𝑢′
𝑖(total cost 2𝐴= 2𝑏+ 2). However, covering 𝑢𝑖via a relevant
subset index 𝐼𝑠𝑗costs only 𝐵= 2𝑏. Since 2𝐴> 𝐵, it is strictly
cheaper to cover elements using subset indexes. The detailed proof
is available in our appendix.
The EIS problem is a decision problem with a yes/no answer. In
practice, however, one often seeks to optimize either space usage or
query time, as these are the key factors in deploying AKNN search.
We therefore study two optimization variants of EIS: (1) Efficiency-
constrained variant that minimizes total space while enforcing
a lower bound on the elastic factor and (2) Space-constrained
variant that maximizes the elastic factor subject to a given space
budget. Because EIS is NP-complete, both variants are NP-hard. We
next show how to address these variants.
4
PROBLEM SOLUTION
We first solve the efficiency-constrained EIS problem, then extend
it to the space-constrained variant, and conclude with a discussion.
4.1
Method for Efficiency-constrained Variant
This optimization version of the EIS problem resembles Defini-
tion 3.3, but aims to compute the following output:
Output A subset I ⊆I such that 𝑒(𝑆(𝐿𝑖), I) ≥𝑐for all 𝐿𝑖∈L,
and the total cost Í
𝐼∈I |𝐼| is minimized.
Since this variant of the EIS problem is NP-hard, we employ
a greedy algorithm to obtain an approximate solution. The basic
idea is as follows. Given a collection of indexes I (corresponding
to a query workload with label sets L), each index 𝐼∈I has
an associated space cost |𝐼|, measured by the number of matched
vectors it contains. We define a benefit function for each index and
iteratively select the index with the highest benefit. By definition of
the elastic factor, adding more indexes improves it and eventually
covers all label sets in L (if all are selected). Yet, to minimize total
cost, selection must be made carefully to ensure solution quality.
A key preprocessing is that we always include the top index 𝐼⊤in
the solution. As noted in § 3.2, this guarantees that every query
has at least one index covering all of its matched vectors, ensuring
correctness. Thus the answer set I is initialized with 𝐼⊤. The benefit
of adding a candidate index 𝐼∈I \ I to the current solution I is
defined as 𝐵(𝐼, I), which is defined below.
Definition 4.1. Let C ⊂I be the set of indexes already covered
by the selected set I with overlap ratio at least 𝑐, where 𝑐is the
elastic–factor threshold: C =

𝐼𝑖∈I
 ∃𝐼𝑗∈I : (𝐼𝑖⊆𝐼𝑗) ∧
|𝐼𝑖|/|𝐼𝑗| ≥𝑐	
. Let C′ be the set of indexes newly covered by
adding 𝐼, also with overlap ratio at least 𝑐: C′ =

𝐼𝑖∈I \ C : (𝐼𝑖⊆
𝐼)∧|𝐼𝑖|/|𝐼| ≥𝑐	
. The benefit of adding 𝐼to I is 𝐵(𝐼, I) = Í
𝐼𝑖∈C′ |𝐼𝑖|
|𝐼| .
The benefit captures two aspects: (i) the number of previously
uncovered indexes that the candidate index 𝐼can cover (with over-
lap ratio at least 𝑐, i.e., the set C′), and (ii) the actual overlap ratio
between 𝐼and each newly covered index 𝐼𝑖∈C′. In other words,
the benefit is the total overlap ratio of 𝐼with all indexes in C′.
Algorithm. We implement the greedy method in Algorithm 1. First,
we add the top index 𝐼⊤to the answer set I (Line 1). At each iteration,
we select the next candidate index—each candidate 𝐼corresponds
to a query–label set 𝐿𝑞in the workload—based on its benefit score
Algorithm 1: Greedy Algorithm
Input: The Universal Index Set I, The Elastic Factor 𝑐
Output: The Selected Index Set I
1 I ←{𝐼⊤};
2 while Stop Condition Not Met do
3
𝐼←𝐼∈I \ I such that 𝐵(𝐼, I) is maximum;
4
I ←I ∪{𝐼};
5
if For all 𝐿𝑞∈L, 𝑒(𝑆(𝐿𝑞), I) ≥𝑐) then Break;
6 return I;
// Selected Index Set
Table 3: The Benefits of Each Candidate Index in Each Round
Init Round
Second Round
Third Round
𝐼1
49 / 17 = 2.88
𝐼2
23 / 10 = 2.30
7 / 10 = 0.70
0 / 10 = 0.00
𝐼3
19 / 7 = 2.71
12 / 7 = 1.71
5 / 7 = 0.71
𝐼4
23 / 9 = 2.55
14 / 9 = 1.55
5 / 9 = 0.55
𝐼5
7 / 4 = 1.75
7 / 4 = 1.75
𝐼6
9 / 6 = 1.50
3 / 6 = 0.50
0 / 6 = 0.00
𝐼7
8 / 5 = 1.60
8 / 5 = 1.60
5 / 5 = 1.00
𝐼8
3 / 3 = 1.00
3 / 3 = 1.00
0 / 3 = 0.00
(Lines 3–4). The process stops when all label sets in L are covered,
yielding a valid solution (Line 5).
Example 3. Using the example dataset in Fig. 4, we evaluate the
benefit of different index choices, with Table 3 reporting the benefit
ratio for each candidate index. Note that the top index 𝐼1 = 𝐼⊤is always
selected first, regardless of its benefit. Selecting 𝐼1 covers the vectors
in 𝐼2, 𝐼3, 𝐼4, 𝐼6, and itself (with overlap ratio at least the elastic–factor
threshold 𝑐= 0.3), for a total of 17 + 10 + 7 + 9 + 6 = 49 vectors at
a cost of 17, yielding a benefit of 49/17 ≈2.88. After 𝐼1 is selected,
the benefits of the remaining candidates change. Initially, 𝐼2 covers 𝐼2,
𝐼5, 𝐼6, and 𝐼8, i.e., 23 vectors at a cost of 10, for a benefit of 2.3. In the
second round, as 𝐼1 already covers 𝐼2 and 𝐼6, 𝐼2 can now cover only 𝐼5
and 𝐼8, reducing its benefit to 0.7. The greedy algorithm next selects
the index with the highest updated benefit (Fig. 4(d)), which is 𝐼5. At
this point, 𝐼1 and 𝐼5 cover all vectors except those in 𝐼7. The third round
selects 𝐼7, with a cost of 5. The greedy solution therefore has a total
cost of 17 + 4 + 5 = 26, which is not optimal. As shown in Fig. 4(e),
the optimal solution instead selects 𝐼3 in the second round. Although
𝐼3 covers only 𝐼5, 𝐼7, and 𝐼8 with a lower benefit of 1.71 (due to overlap
with 𝐼1), it achieves full coverage using just two indexes, 𝐼1 and 𝐼3, for
a total cost of 17 + 7 = 24, outperforming the greedy approach.
Implementation Details and Cost Analysis. To make the greedy
method practical, we first create an index in I for each query–label
set in L. In theory, there can be up to 2|Σ| query–label sets—and
thus the same number of indexes—in the worst case. However,
given a query workload L, we only need to compute the sizes of
𝑂

min

2|Σ|, |I|

indexes during preprocessing, where |I| = |L|
When the labels in the alphabet Σ follow a power–law distribu-
tion (as in many real-world datasets), this preprocessing cost is
empirically a small fraction of the total runtime. For other label
distributions, however, the overhead can be substantial. To handle
such cases, following [27], we estimate the size of some index 𝐼in
I under a query–label set 𝐿𝑞by sampling or advanced cardinal-
ity–estimation techniques [28], without fully materializing 𝐼. Once
7

## PDF page 8

index sizes are approximated, we can efficiently compute the ben-
efit of each candidate and apply the greedy algorithm. To further
improve performance, we maintain a max-heap to track the index
with the highest benefit at each iteration. As selecting an index can
affect the benefits of up to |I| related indexes, we check whether
the heap’s top entry has become stale; if so, we update its benefit
and reinsert it. This strategy keeps the practical runtime well below
𝑂(|I|2) — the cost incurred by the greedy method.
4.2
Method for Space-Constrained Variant
In the previous section, we addressed the efficiency-constrained
index-selection problem. We now turn to selecting indexes that
maximize query efficiency while satisfying a space constraint. This
optimization variant of the EIS problem follows Definition 3.3, but
focuses on computing the following output:
Output A subset I ⊆I such that the total cost Í
𝐼∈I |𝐼| ≤𝜏, and
the minimum elastic factor min(𝑒(𝑆(𝐿𝑞),𝑒(I)) over all
𝐿𝑞∈L is maximized.
Insight. We observe that the elastic–factor threshold 𝑐is mono-
tone for the EIS problem. For example, if an index set I satisfies a
threshold of 0.5 for a query workload L, it also satisfies any smaller
threshold 𝑐′ < 0.5. This holds because the threshold 𝑐bounds the
overlap between the indexes in I and any label set in L, requiring
the overlap to be at least 𝑐. Thus, if the stricter bound 𝑐is met, any
looser bound 𝑐′ is automatically satisfied.
Method.
Leveraging
this
property,
we
reduce
the
space–constrained
problem
to
its
efficiency–constrained
counterpart and apply a binary search to find a set I whose
elastic factor exceeds a given 𝑐while keeping the total cost within
the budget 𝜏. We perform the binary search over 𝑐∈(0, 1] in
decreasing order. At each step, for a candidate value of 𝑐, we
invoke Algorithm 1 to check if the returned index set I has total
cost at most 𝜏. The search stops once such a set is found, yielding
the maximum achievable elastic factor under the space constraint.
Overall, this procedure requires 𝑂log 1
𝜀
 calls to Algorithm 1,
where 𝜀is the resolution for the binary search (set to 0.001 in
our paper). In practice, the overhead of combining binary search
with the greedy method is negligible—typically under 1% of the
index-construction time, or about 1–2 seconds even for large
workloads L.
4.3
Discussions
Limitation. For either efficiency- or space-constrained EIS prob-
lem, our greedy methods output a subset of indexes I from the uni-
versal index set I, where each index corresponds to a query–label
set in the workload L. The efficiency of our approach for AKNN
search stems from controlling either space or query time through
index sharing based on label-containment relationships. In extreme
cases, however, index sharing may be ineffective. For example, if
the query workload consists of label sets that are completely dis-
joint, there is no containment relationship between them. We must
then either create a separate index for each disjoint query–label set
(resulting in high space cost) or build a single index covering all
vectors in the database (resulting in a small elastic factor). Fortu-
nately, in real-world scenarios, when query–label sets are disjoint,
their base vectors are typically disjoint as well, so building an index
for each query–label set remains affordable, requiring only 𝑂(𝑁)
space. By contrast, algorithms such as UNG, ACORN, and Filter-
DiskANN are unaffected by this limitation as they do not rely on
index sharing and may perform better in such cases.
Extensions to Dynamic Scenarios. In real-world deployments,
both the database vectors and the query workload may evolve over
time. We describe how our method accommodates such updates.
Database vector dynamics. Vectors can be inserted into or deleted
from the database. We focus on insertions, since deletions can be
handled by marking a vector as ignored and continuing the search
until all results are found [12, 34, 42]. When new vectors arrive, we
insert each into every index 𝐼𝑖(built on query–label set 𝐿𝑖) whose
labels match the vector. For queries, we continue using the original
selected index set I, but periodically check for data changes and
update the indexes once 10–20% new vectors have been added.
Query workload dynamics. When new label sets 𝐿𝑞appear in the
query workload, we first collect all matching vectors for each 𝐿𝑞. If
fewer than 4, 000 (a threshold determined by experiments) are col-
lected, we scan them directly to answer the new query. Otherwise,
we select indexes from the set I built on label sets that are subsets of
𝐿𝑞. Note that the top index 𝐼⊤is always available (as ∅is a subset of
any query-label set). If multiple indexes qualify, we choose the one
with the maximum overlap with the collected vectors to answer the
query. We selectively create an index for the new label set in the
query workload (e.g., when the number of collected vectors exceeds
4,000), but adjust set I only periodically to maintain efficiency.
Index updates. For both types of dynamics, we postpone structural
updates to the current selected index set I and process them in
batches. (1) For the efficiency-constrained variant: We maintain
the current set I as long as it satisfies the elastic–factor constraint.
If vector insertions or newly created indexes (from new queries)
violate this constraint, we reapply Algorithm 1 to add the necessary
indexes to enlarge I. (2) For the space-constrained variant: We use
the existing I and its current elastic factor as the starting point.
If additional space is available, we attempt to increase the target
elastic factor. Otherwise, we lower the target, prune redundant
indexes to free space, and continue the binary-search process.
Throughout these updates, we support non-blocking operations:
The index set need not be rebuilt from scratch, and incoming queries
are routed dynamically to the index with the highest elastic fac-
tor, ensuring uninterrupted service. Empirical results in Section 5
confirm the efficiency of this update strategy.
5
EXPERIMENT
5.1
Experiment Settings
Datasets. We evaluate on 8 publicly available datasets (see Ta-
ble 4) that are standard benchmarks for AKNN search, as well as
datasets generated by state-of-the-art embedding models such as
MSMARCO, OpenAI-1536 [61], TripClick, and Arxiv [31]. To exam-
ine scalability, we also sample 100M vectors (denoted as DEEP) from
the Deep1B [15, 61]1. For label annotations, datasets PAPER, LAION,
TripClick, OpenAI-1536, and Arxiv supply real labels or metadata.
1www.tensorflow.org/datasets/catalog/deep1b
8

## PDF page 9

UNG
ELI-0.2
ELI-2.0
ACORN-1
ACORN-𝛾
Brute-Force
80
90
100
102
103
104
recall@10(%)
Qps
Ave. Select 18% (Zipf,|Σ|=12)
70
80
90
100
102
103
104
recall@10(%)
Qps
Ave. Select 8.9% (Zipf,|Σ|=32)
(a) SIFT
70
80
90
100
102
103
recall@10(%)
Qps
Ave. Select 18% (Zipf,|Σ|=12)
70
80
90
100
102
103
recall@10(%)
Qps
Ave. Select 9% (Zipf,|Σ|=32)
(b) MSMARC
70
80
90
100
101
102
103
recall@10(%)
Qps
Ave. Select 13% (Meta,|Σ|=32)
70
80
90
100
101
102
103
recall@10(%)
Qps
Ave. Select 9% (Zipf,|Σ|=32)
(c) OpenAI-1536
70
80
90
100
101
102
103
104
recall@10(%)
Qps
Ave. Select 14% (Meta,|Σ|=32)
70
80
90
100
101
102
103
recall@10(%)
Qps
Ave. Select 9% (Zipf,|Σ|=32)
(d) LAION
92
94
96
98
100
102
103
104
recall@10(%)
Qps
Ave Select 8.3% (Real,|Σ|=7)
(e) PAPER
70
80
90
100
100
102
recall@10(%)
Qps
Ave. Select 4.5% (Real,|Σ|=28)
(f) TripClick
80
90
100
102
103
recall@10(%)
Qps
Ave Select 15% (Real,|Σ|=37)
(g) Arxiv
80
90
100
101
102
103
recall@10(%)
Qps
Ave. Select 18% (Zipf,|Σ|=12)
(h) DEEP
Figure 7: The Efficiency Comparison Across Methods
Table 4: The Statistics of Datasets
Dataset
Dimension
Size
Query Size
Type
Label
SIFT
128
1,000,000
10000
Image
Synthetic
MSMARCO
1024
1,000,000
1000
Text
Synthetic
LAION
512
1,182,243
1000
Image
Meta
OpenAI-1536
1536
999,000
1000
Text
Meta
PAPER
200
2,029,997
10000
Text
Real
TripClick
768
1,055,976
1000
Text
Real
Arxiv
4000
100,000
1000
Text
Real
DEEP
96
100,000,000
1000
Image
Synthetic
For others, we synthesize labels following prior work [6, 21]. Label
sources are grouped into three categories:
(1) Real labels (Paper, TripClick, Arxiv): For Paper, each vector is
labeled by the paper’s venue, research field, and institution type. For
TripClick, we use the 28 most frequent clinic categories, consistent
with prior works [6, 46]. For Arxiv, we assign the main subject
categories of each vector as its label.
(2) Metadata-derived labels (LAION, OpenAI-1536): Using vector-
associated metadata (e.g., text or images), we follow the procedure
of [46] to construct a candidate label set and then assign to each
vector its top-3 most relevant labels.
(3) Synthetic labels (SIFT, MSMARCO, DEEP): Since keywords
and tags in real systems typically follow heavy-tailed distributions,
prior work models label frequency using Zipf (power-law) distri-
butions [6, 21]. We reuse publicly available code to generate labels
for both base and query vectors under this distribution. In sub-
sequent experiments, we also evaluate alternative distributions
(Uniform, Poisson, and Multinomial) and vary the alphabet size
|Σ| ∈{8, 12, 24, 32} to assess robustness [62].
Metrics. The evaluation covers both efficiency and accuracy. For
the search efficiency metric, we use Queries Per Second(Qps), which
indicates the number of queries processed by the algorithm per
second, as it is most commonly used in benchmarks. For the search
accuracy, we use recall defined in § 2.1 as the metric to align with
the baselines [6, 46]. All results are reported as averages. We also
report the average selectivity of a query workload. For each query
(𝑞, 𝐿𝑞), the selectivity is defined as |𝑆(𝐿𝑞)|/|𝑆|, i.e., the fraction
of base vectors whose label sets contain 𝐿𝑞. We compute the av-
erage selectivity (denoted Ave. Select) across all test queries in the
workload and annotate it at the top of each figure to highlight this
value.
Algorithms. We compare the following algorithms:
• ELI-0.2: Our greedy method for the efficiency-constrained variant,
where 0.2 denotes the elastic–factor threshold.
• ELI-2.0: Our method for the space-constrained variant, where 2.0
indicates the space bound (at most twice the original dataset size).
• UNG: Unified Navigating Graph for AKNN search [6].
• ACORN-1: ANN Constraint-Optimized Retrieval Network with
low construction overhead [46].
• ACORN-𝛾: ANN Constraint-Optimized Retrieval Network for
high-efficiency search [46].
• Brute-Force: Scan all label-matched vectors to find the results.
9

## PDF page 10

Implementation Details. All code was implemented in C++ and
compiled with GCC 9.4.0 using the -Ofast optimization flag. Ex-
periments were conducted on a workstation equipped with Intel(R)
Xeon(R) Platinum 8352V CPUs @ 2.10 GHz and 512 GB of memory.
We employed multi-threading (144 threads) for index construction
and a single thread for search evaluation. For the underlying index,
we used HNSW [43] with parameters 𝑀= 16 and efconstruct =
200. For ELI-0.2 (< 1.0), we applied the index-selection method to
achieve a fixed elastic factor of 0.2. For ELI-2.0 (> 1.0), we used the
fixed-space method, allowing at most double the original database
size to maximize efficiency. For other baselines, such as ACORN
and UNG, we adopted the default parameters from their respective
papers: 𝛼= 1.2, 𝐿= 100 for UNG; 𝛾= 80, 𝑀= 128, 𝑀𝐵= 128
for ACORN-𝛾on the LAION and TripClick datasets; and 𝛾= 12,
𝑀= 32, 𝑀𝐵= 64 for the remaining datasets.
5.2
Experiment Results
Exp-1: Comparison of Query Efficiency. We begin by evaluating
query efficiency (Qps) across different methods. Fig. 7 compares
UNG, ACORN-1, ACORN-𝛾, and our methods (ELI-0.2, ELI-2.0) un-
der varying alphabet sizes |Σ| and label distributions. Here, Real,
Meta, and Zipf denote the real, meta, and synthetic Zipf-based
label distributions, respectively. Points toward the upper right in-
dicate better performance. Our methods achieve the best trade-off
between search efficiency and accuracy. For example, at 95% recall
with |Σ| = 12, ELI-0.2 delivers a 4× speedup over UNG on SIFT and
a 10× speedup on MSMARCO. Performance remains stable across
different |Σ| values and real label distributions, whereas UNG’s
retrieval efficiency drops sharply as |Σ| grows.
Thanks to the elastic factor, ELI-0.2 sustains nearly 12× higher
query speed than UNG when |Σ| = 32 on the OpenAI-1536 dataset.
In contrast, ACORN plateaus in accuracy for large |Σ| due to lim-
itations in its PreFiltering strategy. For example, on Fig. 8(a), as
|Σ| increases, ACORN accuracy declines and UNG efficiency de-
grades, while ELI-0.2 maintains consistently high efficiency. Al-
though larger |Σ| lowers average query selectivity, our design keeps
runtime stable. A modest gap appears between ELI-0.2 and ELI-2.0.
For example, on Fig. 8(b), under a Zipf distribution, the elastic fac-
tor of ELI-2.0 is typically below 0.2 for large |Σ|, so ELI-0.2 often
performs slightly better due to its higher elastic-factor threshold.
Exp-2: Effect of Query Selectivity. To further compare methods,
we vary query selectivity and measure its impact on query efficiency.
We evaluate on Arxiv with four settings: No Filter (100% selectivity),
30%, 10%, and 5% selectivity, as shown in Fig. 8. In the No-Filter
setting, the problem reduces to a standard AKNN search without la-
bel constraints. Here, ELI-0.2 and ELI-2.0 match the performance of
HNSW-PostFiltering (i.e., HNSW with the PostFiltering strategy)
and outperform other baselines. At 30% selectivity, ELI-2.0 consis-
tently surpasses UNG and ACORN across the full recall range, while
HNSW-PostFiltering performs nearly the same as ELI-0.2. When se-
lectivity drops to 10% and 5%, the advantage of our methods widens:
they maintain high Qps even at high recall and consistently outper-
form all other methods across different recall levels. These results
validate the elastic-factor cost model: with sufficiently large 𝑐(e.g.,
𝑐≥0.2), the additional computation scales as 𝑘/𝑐. We also compare
with brute-force search, which becomes competitive when very
Table 5: The Comparison of Indexing Time (s)
UNG
ACORN-1
ACORN-𝛾
ELI-0.2
ELI-2.0
SIFT
405
7
62
34
25
MSMARCO
459
29
290
148
87
PAPER
65
18
128
39
64
LAION
27
105
3238
121
63
TripClick
283
137
4289
127
81
OpenAI-1536
218
47
568
409
166
Arxiv
116
9
50
23
25
DEEP
-
912
11328
8864
3991
Table 6: The Comparison of Index Size (Mb)
Base
UNG
ACORN-1
ACORN-𝛾
ELI-0.2
ELI-2.0
SIFT
488
186
442
485
634
382
MSMARCO
3906
233
442
485
634
382
PAPER
1548
372
898
986
699
650
LAION
2309
204
2038
1820
871
405
TripClick
3093
192
1820
1625
814
540
OpenAI-1536
5853
261
442
485
782
342
Arxiv
1525
20
80
85
26
30
DEEP
36621
-
44262
48591
85293
37902
few vectors match the labels (e.g., fewer than 4, 000). In practice,
we therefore build indexes only for query–label sets containing at
least about 4, 000 vectors and use brute force otherwise.
Exp-3: Index Selection Statistics. Recall that our methods aim
to select a subset of indexes I from the universal index set I. To
illustrate how the selection works, we report both the number of
indexes chosen and the total number of vectors covered by the
indexes selected by our methods (ELI-0.2 and ELI-2.0). We evaluate
on real datasets (OpenAI-1536, Laion, TripClick, Arxiv, and Paper).
For Paper, we also vary the label distribution (e.g., Poisson) as an
additional test. The results are presented in Fig. 9. The top panel
of Fig. 9 presents the total number of candidate indexes |I| as well
as the number selected by ELI-0.2 and ELI-2.0. The bottom panel
shows the total size of all candidate indexes (i.e., total number of
vectors across indexes) and the size of the indexes actually selected
by ELI-0.2 and ELI-2.0.
From Fig. 9, we observe: (1) Compact label-set selection. Both
methods select up to an order of magnitude fewer indexes than the
full set I, verifying the effectiveness of index sharing in reducing in-
dex count. (2) Significant reduction in indexed size. The bottom
panel shows that ELI-0.2 indexes only a small fraction of the total
base vectors, while ELI-2.0 indexes roughly twice the number of
base vectors. (3) Robustness across distributions. Under various
label distributions for Paper (Uniform, Poisson, Multinomial, and
Zipf), our methods always achieve substantial reductions in both
the number of selected indexes and their total size.
Exp-4: Indexing Time and Space. We compare indexing time
in Table 5 and index size in Table 6. For datasets with real labels,
results are reported under their native label distributions. For syn-
thetic datasets, we report results with |Σ| = 32, consistent with
the cmdefficiency comparison in Fig. 7. (1) Indexing time. Table 5
shows that our methods (especially ELI-2.0) are far more efficient
than UNG and ACORN-𝛾. Specifically, UNG requires nearly 20×
the indexing time on SIFT and 5× on MSMARCO, while ACORN-𝛾
averages about 2× the indexing time of our methods. Although
10

## PDF page 11

UNG
ELI-0.2
ELI-2.0
ACORN-1
ACORN-𝛾
HNSW-PostFiltering
Brute-Force
40
60
80
100
101
102
103
recall@10(%)
Qps
No Filter 100% Selectivity
70
80
90
100
102
103
recall@10(%)
Qps
30% Selectivity
70
80
90
100
102
103
recall@10(%)
Qps
10% Selectivity
80
90
100
101
102
103
recall@10(%)
Qps
5% Selectivity
Figure 8: The Effect of the Query Selectivity (on Arxiv)
OpenAI-1536
LAION
TripClick
Arxiv
PAPER
Poisson
Uniform
Multi-Normial
Zipf, | |=12
Zipf,| |=32
10
100
1K
Count (Index)
256
298
268
12
36
1K
794
794
238
648
198
216
145
10
4
9
77
77
69
253
140
138
42
11
4
19
17
16
39
137
Statistics of Index Count
All Index
ELI-0.2 Index
ELI-2.0 Index
OpenAI-1536
LAION
TripClick
Arxiv
PAPER
Poisson
Uniform
Multi-Normial
Zipf, | |=12
Zipf,| |=32
1M
10M
100M
Count (Vector)
8M
9.5M
27.4M
301.9K
16.2M
74.8M
42.6M
30.4M
14M
26.4M
5.4M
5.6M
4M
170.4K
4.1M
2.7M
10.9M
8M
5.7M
8.4M
Statistics of Vector Count
All Vector
ELI-0.2 Vector
ELI-2.0 Vector
Figure 9: The Statistics of Index Selection by Our Methods
ACORN-1 builds indexes quickly, its omission of label information
results in poorer search efficiency. (2) Index size. Table 6 reports
index sizes, where Base denotes the size of the raw vectors. On the
OpenAI-1536 dataset, all methods require only about one-tenth of
the total storage space. Our methods achieve the best search per-
formance with comparable index size. In particular, both ACORN
and ELI-2.0 use roughly twice the space of the original vectors, yet
ELI-2.0 achieves strong search performance with much smaller ad-
ditional space. UNG saves the most space, but in high-dimensional
settings its index size is only a small fraction of the base vector
dataset, leading to lower search efficiency.
Exp-5: Varying Label Distributions. Label distributions influ-
ence label-hybrid AKNN search, so we evaluate performance under
different distributions—Zipf, Uniform, Poisson, and Multinomial—
with |Σ| = 12. Experiments are conducted on the Paper dataset, and
results are shown in Fig. 10. (1) Stable performance. Our methods
perform consistently across all tested distributions. In contrast, the
search accuracy of other methods varies significantly with the dis-
tribution: under Uniform and Multinomial settings, ACORN fails
to reach 80% recall, while UNG and our methods maintain high
accuracy. (2) High efficiency. Across all distributions, our methods
remain highly competitive. They achieve recall comparable to UNG
at high thresholds and deliver 3×–5× better search efficiency.
Exp-6: Index Update Evaluation. We evaluate the effectiveness
of our index-update strategy on the Arxiv and SIFT datasets. Each
dataset is partitioned into two halves. The first 50% of the data,
with label-set size |Σ| = 12, is used to construct the initial index set
I. The remaining 50%, featuring an expanded label set of |Σ| = 24,
simulates the insertion of both new vectors (50% more vectors) and
an enlarged query workload (12 additional labels).
Query efficiency. Fig. 11 reports query efficiency. ELI-0.2 and ELI-
2.0 denote the methods rebuilt from scratch using the full dataset,
whereas ELI′-0.2 and ELI′-2.0 represent our batch-update strategy,
which first builds indexes on the initial 50% of data and then in-
crementally updates them. The results show that periodic updates
incur less than a 5% drop in query efficiency compared with full
re-computation, demonstrating that the batch update mechanism
preserves practical performance.
Update cost. Figure 12 compares the update cost of ELI′-0.2 and ELI′-
2.0 against rebuilding from scratch. The left panel shows update
time: the rightmost bar corresponds to full re-computation on 100%
of the data, while the three bars to the left represent incremental
insertions of 10%, 20%, and 50% new vectors into the initial index
built on the first 50% of the data. Update time naturally increases
with the number of inserted vectors but remains well below the cost
of rebuilding, even when 50% of the data is added. The right panel
reports space usage (in units of updated vectors). As more vectors
are inserted, space usage grows slightly but remains smaller than
that of full re-computation. Notably, both our methods, especially
ELI′-2.0, use significantly less space than their rebuilt counterparts.
Overall, these experiments show that our incremental update
strategy achieves significantly lower update time and space over-
head compared with rebuilding from scratch(Fig. 12), while incur-
ring only negligible loss in query efficiency (Fig. 11).
6
RELATED WORK
Attribute-Hybrid AKNN Search. When vectors carry label at-
tributes (as defined in this paper) or numerical attributes (e.g., [58]),
an attribute-hybrid AKNN search can be defined to return ap-
proximate 𝑘-nearest neighbors whose attributes satisfy given con-
straints. Such searches can be handled using either attribute-free
or attribute-dependent solutions.
Attribute-Free Solutions. These methods ignore attributes during in-
dex construction and consider them only at query time. Specifically,
the PreFiltering and PostFiltering search strategies are widely used,
each performing differently under varying query workloads. Sys-
tems such as ADB [56], VBASE [65], CHASE [41], and Milvus [50]
11

## PDF page 12

UNG
ELI-0.2
ELI-2.0
ACORN-1
ACORN-𝛾
80
90
100
102
103
104
recall@10(%)
Qps
Ave. Select 18% Zipf
70
80
90
100
102
103
recall@10(%)
Qps
Ave. Select 6.8% Uniform
80
90
100
102
103
104
recall@10(%)
Qps
Ave. Select 20% Poisson
70
80
90
100
102
103
recall@10(%)
Qps
Ave. Select 5.3% multinormal
Figure 10: Varying Label Distributions
ELI-0.2
ELI-2.0
ELI′-0.2
ELI′-2.0
85
90
95
100
103
104
recall@10(%)
Qps
(a) SIFT
92
94
96
98
100
102
102.5
recall@10(%)
Qps
(b) Arxiv
Figure 11: Query Performance Drop Caused by Incremental Updates
10% Update
20% Update
50% Update
Rebuild
0
5K
10K
15K
20K
25K
30K
Index Times (ms)
Comparison of Incremental Update Time
ELI-0.2 Update
ELI-2.0 Update
10% Update
20% Update
50% Update
Rebuild
0
50K
100K
150K
200K
250K
Index Vector
Comparison of Incremental Update Vector
ELI-0.2 Update
ELI-2.0 Update
Figure 12: The Time and Space Cost of Incremental Updates
choose between these strategies using cost models. ACORN also
follows the PreFiltering strategy but builds a denser graph index to
mitigate its connectivity issues. Because these approaches ignore
attributes during index construction, their search accuracy and
efficiency lag behind methods that incorporate attributes directly.
Attribute-Dependent Solutions. These methods build indexes by con-
sidering the attribute type. (1) For attribute-hybrid AKNN search
with numerical attributes, recent work [11, 58, 60] organizes query-
specified numeric intervals using segment trees, while SeRF [68]
further compresses indexes across overlapping intervals. (2) For
attribute-hybrid AKNN search with label attributes, UNG [6] ex-
ploits label-set inclusion, adding cross-group edges so the graph
index searches only filtered vectors. NHQ [51] incorporates at-
tribute similarity into vector similarity, requiring manual tuning of
vector and attribute weights.
Data Cube Computation. Existing studies on OLAP and data-cube
computation select group-by aggregates over the subset lattice of di-
mensions, exploiting the fact that coarser aggregates (supersets) can
be derived from finer-grained materializations (subsets) [24, 27, 44].
Our setting shares this lattice structure and the reuse principle: an
index built for a query-label set can serve queries with superset
labels via label containment. However, key differences remain: (1)
Cube computation relies on algebraic properties of aggregates (e.g.,
distributive or algebraic measures) to derive superset results exactly
from subsets, whereas our reuse is containment-based, yielding a
query cost bounded by 𝑂(𝐶+ 𝑘/𝑐) on graph-based indexes, where
𝑐is the elastic factor and 𝑘/𝑐is the additional cost. (2) Cube/view
selection minimizes aggregate-query latency subject to storage lim-
its, while our Elastic Index Selection (EIS) problem selects a subset
of query-label sets to index so as to satisfy both space budgets and
workload-wide elastic-factor coverage, ensuring predictable AKNN
efficiency under label constraints. (3) Although both problems are
NP-hard and typically approached with greedy heuristics, our ben-
efit function and feasibility constraints are tailored to vector-search
semantics and the label-containment relationship.
Frequent Itemset Mining. Our work is conceptually related to Fre-
quent Itemset Mining (FIM), where labels correspond to items and
label sets to transactions [25, 26, 64]. Both exploit the anti-monotone
property of support (the Apriori principle), with classic algorithms
such as Apriori and FP-Growth widely used for FIM [25, 26]. The
objectives, however, differ fundamentally. FIM finds all itemsets
whose support exceeds a threshold for pattern discovery, whereas
our ELI task is an optimization problem: selecting a subset of query-
label sets on which to build indexes. This selection is guided not
by absolute support but by a novel elastic factor modeling query
efficiency. Thus, while FIM outputs frequent patterns, our method
produces a concrete indexing plan for AKNN search, making the
formulation and goals distinct.
7
CONCLUSION
This paper investigates the label-hybrid AKNN search problem and
shows that an index built for a query–label set remains effective for
all its supersets with bounded query time. Building on this insight,
we formalize the Elastic Index Selection (EIS) problem, prove that
EIS is NP-complete, and introduce two optimization variants solved
by efficient greedy algorithms. Extensive experiments demonstrate
the effectiveness and superiority of our approach. As future work,
we plan to explore hardware acceleration for further speedups.
ACKNOWLEDGMENTS
We are grateful to the anonymous reviewers for their constructive
comments. Wentao Li is supported by NSFC (Grant No.62302417).
Raymond Chi-Wing Wong is supported by PRP/004/25FX. Wei
Wang is supported by HKUST(GZ)-IEIP-RoP (G01RF000256),
Guangdong Provincial Key Lab of Integrated Communication,
Sensing and Computation for Ubiquitous Internet of Things
(No.2023B1212010007, SL2023A03J00934), Guangzhou Municipal
Science and Technology Project (No. 2023A03J0003, 2023A03J0013,
and 2024A03J0621). This work is also supported by Ant Group.
12

## PDF page 13

REFERENCES
[1] Fabien André, Anne-Marie Kermarrec, and Nicolas Le Scouarnec. 2015. Cache
locality is not enough: High-Performance Nearest Neighbor Search with Product
Quantization Fast Scan. Proceedings of the VLDB Endowment 9, 4 (2015).
[2] Martin Aumüller, Erik Bernhardsson, and Alexander Faithfull. 2020. ANN-
Benchmarks: A benchmarking tool for approximate nearest neighbor algorithms.
Information Systems 87 (2020), 101374.
[3] Artem Babenko and Victor S. Lempitsky. 2015. The Inverted Multi-Index. IEEE
Trans. Pattern Anal. Mach. Intell. 37, 6 (2015), 1247–1260.
[4] Norbert Beckmann, Hans-Peter Kriegel, Ralf Schneider, and Bernhard Seeger.
1990. The R*-tree: An efficient and robust access method for points and rectangles.
In Proceedings of the 1990 ACM SIGMOD international conference on Management
of data. 322–331.
[5] Alina Beygelzimer, Sham Kakade, and John Langford. 2006. Cover trees for
nearest neighbor. In Proceedings of the 23rd international conference on Machine
learning. 97–104.
[6] Yuzheng Cai, Jiayang Shi, Yizhuo Chen, and Weiguo Zheng. 2024. Navigating La-
bels and Vectors: A Unified Approach to Filtered Approximate Nearest Neighbor
Search. Proceedings of the ACM on Management of Data 2, 6 (2024), 1–27.
[7] Moses S Charikar. 2002. Similarity estimation techniques from rounding algo-
rithms. In Proceedings of the thiry-fourth annual ACM symposium on Theory of
computing. 380–388.
[8] Thomas Cover and Peter Hart. 1967. Nearest neighbor pattern classification.
IEEE transactions on information theory 13, 1 (1967), 21–27.
[9] Mayur Datar, Nicole Immorlica, Piotr Indyk, and Vahab S Mirrokni. 2004. Locality-
sensitive hashing scheme based on p-stable distributions. In Proceedings of the
twentieth annual symposium on Computational geometry. 253–262.
[10] Rong-chii Duh and Martin Fürer. 1997. Approximation of k-set cover by semi-
local optimization. In Proceedings of the twenty-ninth annual ACM symposium on
Theory of computing. 256–264.
[11] Joshua Engels, Benjamin Landrum, Shangdi Yu, Laxman Dhulipala, and Julian
Shun. 2024. Approximate Nearest Neighbor Search with Window Filters. ICML
2024 (2024).
[12] Cong Fu, Changxu Wang, and Deng Cai. 2022. High Dimensional Similarity
Search With Satellite System Graph: Efficiency, Scalability, and Unindexed Query
Compatibility. IEEE Trans. Pattern Anal. Mach. Intell. 44, 8 (2022), 4139–4150.
[13] Cong Fu, Chao Xiang, Changxu Wang, and Deng Cai. 2019. Fast Approximate
Nearest Neighbor Search With The Navigating Spreading-out Graph. Proc. VLDB
Endow. 12, 5 (2019), 461–474.
[14] Junhao Gan, Jianlin Feng, Qiong Fang, and Wilfred Ng. 2012. Locality-sensitive
hashing scheme based on dynamic collision counting. In Proceedings of the 2012
ACM SIGMOD international conference on management of data. 541–552.
[15] Jianyang Gao, Yutong Gou, Yuexuan Xu, Yongyi Yang, Cheng Long, and Raymond
Chi-Wing Wong. 2024. Practical and Asymptotically Optimal Quantization of
High-Dimensional Vectors in Euclidean Space for Approximate Nearest Neighbor
Search. arXiv preprint arXiv:2409.09913 (2024).
[16] Jianyang Gao and Cheng Long. 2023. High-Dimensional Approximate Nearest
Neighbor Search: with Reliable and Efficient Distance Comparison Operations.
Proc. ACM Manag. Data 1, 2 (2023), 137:1–137:27. https://doi.org/10.1145/3589282
[17] Jianyang Gao and Cheng Long. 2024. RaBitQ: Quantizing High-Dimensional
Vectors with a Theoretical Error Bound for Approximate Nearest Neighbor
Search. Proceedings of the ACM on Management of Data 2, 3 (2024), 1–27.
[18] Tiezheng Ge, Kaiming He, Qifa Ke, and Jian Sun. 2014. Optimized Product
Quantization. IEEE Trans. Pattern Anal. Mach. Intell. 36, 4 (2014), 744–755.
[19] Aristides Gionis, Piotr Indyk, Rajeev Motwani, et al. 1999. Similarity search in
high dimensions via hashing. In Vldb, Vol. 99. 518–529.
[20] Michel X Goemans and David P Williamson. 1995. Improved approximation
algorithms for maximum cut and satisfiability problems using semidefinite pro-
gramming. Journal of the ACM (JACM) 42, 6 (1995), 1115–1145.
[21] Siddharth Gollapudi, Neel Karia, Varun Sivashankar, Ravishankar Krishnaswamy,
Nikit Begwani, Swapnil Raz, Yiyong Lin, Yin Zhang, Neelam Mahapatro, Premku-
mar Srinivasan, et al. 2023. Filtered-diskann: Graph algorithms for approximate
nearest neighbor search with filters. In Proceedings of the ACM Web Conference
2023. 3406–3416.
[22] Yunchao Gong, Svetlana Lazebnik, Albert Gordo, and Florent Perronnin. 2013.
Iterative Quantization: A Procrustean Approach to Learning Binary Codes for
Large-Scale Image Retrieval. IEEE Trans. Pattern Anal. Mach. Intell. 35, 12 (2013),
2916–2929. https://doi.org/10.1109/TPAMI.2012.193
[23] Yutong Gou, Jianyang Gao, Yuexuan Xu, and Cheng Long. 2025. SymphonyQG:
Towards Symphonious Integration of Quantization and Graph for Approximate
Nearest Neighbor Search. Proceedings of the ACM on Management of Data 3, 1
(2025), 1–26.
[24] Jim Gray, Surajit Chaudhuri, Adam Bosworth, Andrew Layman, Don Reichart,
Murali Venkatrao, Frank Pellow, and Hamid Pirahesh. 1997. Data cube: A re-
lational aggregation operator generalizing group-by, cross-tab, and sub-totals.
Data mining and knowledge discovery 1, 1 (1997), 29–53.
[25] Jiawei Han, Jian Pei, and Hanghang Tong. 2022. Data mining: concepts and
techniques. Morgan kaufmann.
[26] Jiawei Han, Jian Pei, and Yiwen Yin. 2000. Mining frequent patterns without
candidate generation. ACM sigmod record 29, 2 (2000), 1–12.
[27] Venky Harinarayan, Anand Rajaraman, and Jeffrey D Ullman. 1996. Implement-
ing data cubes efficiently. Acm Sigmod Record 25, 2 (1996), 205–216.
[28] Hazar Harmouch and Felix Naumann. 2017. Cardinality estimation: An experi-
mental survey. Proceedings of the VLDB Endowment 11, 4 (2017), 499–512.
[29] Ben Harwood and Tom Drummond. 2016. Fanng: Fast approximate nearest
neighbour graphs. In Proceedings of the IEEE Conference on Computer Vision and
Pattern Recognition. 5713–5722.
[30] Qiang Huang, Jianlin Feng, Yikai Zhang, Qiong Fang, and Wilfred Ng. 2015.
Query-aware locality-sensitive hashing for approximate nearest neighbor search.
Proceedings of the VLDB Endowment 9, 1 (2015), 1–12.
[31] Patrick Iff, Paul Bruegger, Marcin Chrapek, Maciej Besta, and Torsten Hoefler.
2025. Benchmarking Filtered Approximate Nearest Neighbor Search Algorithms
on Transformer-based Embedding Vectors.
arXiv:2507.21989 [cs.DB] https:
//arxiv.org/abs/2507.21989
[32] Piotr Indyk and Rajeev Motwani. 1998. Approximate nearest neighbors: towards
removing the curse of dimensionality. In Proceedings of the thirtieth annual ACM
symposium on Theory of computing. 604–613.
[33] Piotr Indyk and Haike Xu. 2023. Worst-case performance of popular approximate
nearest neighbor search implementations: Guarantees and limitations. Advances
in Neural Information Processing Systems 36 (2023), 66239–66256.
[34] Suhas Jayaram Subramanya, Fnu Devvrit, Harsha Vardhan Simhadri, Ravishankar
Krishnawamy, and Rohan Kadekodi. 2019. Diskann: Fast accurate billion-point
nearest neighbor search on a single node. Advances in Neural Information
Processing Systems 32 (2019).
[35] Hervé Jégou, Matthijs Douze, and Cordelia Schmid. 2011. Product Quantization
for Nearest Neighbor Search. IEEE Trans. Pattern Anal. Mach. Intell. 33, 1 (2011),
117–128.
[36] Yannis Kalantidis and Yannis Avrithis. 2014. Locally optimized product quan-
tization for approximate nearest neighbor search. In Proceedings of the IEEE
conference on computer vision and pattern recognition. 2321–2328.
[37] Richard M Karp. 2009. Reducibility among combinatorial problems. In 50 Years
of Integer Programming 1958-2008: from the Early Years to the State-of-the-Art.
Springer, 219–241.
[38] Patrick Lewis, Ethan Perez, Aleksandra Piktus, Fabio Petroni, Vladimir
Karpukhin, Naman Goyal, Heinrich Küttler, Mike Lewis, Wen-tau Yih, Tim Rock-
täschel, et al. 2020. Retrieval-augmented generation for knowledge-intensive nlp
tasks. Advances in Neural Information Processing Systems 33 (2020), 9459–9474.
[39] Wen Li, Ying Zhang, Yifang Sun, Wei Wang, Mingjie Li, Wenjie Zhang, and
Xuemin Lin. 2020. Approximate Nearest Neighbor Search on High Dimensional
Data - Experiments, Analyses, and Improvement. IEEE Trans. Knowl. Data Eng.
32, 8 (2020), 1475–1488.
[40] Kejing Lu, Mineichi Kudo, Chuan Xiao, and Yoshiharu Ishikawa. 2021. HVS: Hi-
erarchical Graph Structure Based on Voronoi Diagrams for Solving Approximate
Nearest Neighbor Search. Proc. VLDB Endow. 15, 2 (2021), 246–258.
[41] Rui Ma, Kai Zhang, Zhenying He, Yinan Jing, X Sean Wang, and Zhenqiang Chen.
2025. CHASE: A Native Relational Database for Hybrid Queries on Structured
and Unstructured Data. arXiv preprint arXiv:2501.05006 (2025).
[42] Yu A Malkov and Dmitry A Yashunin. [n.d.]. hnswlib. https://github.com/nmslib/
hnswlib.
[43] Yury A. Malkov and Dmitry A. Yashunin. 2020. Efficient and Robust Approximate
Nearest Neighbor Search Using Hierarchical Navigable Small World Graphs. IEEE
Trans. Pattern Anal. Mach. Intell. 42, 4 (2020), 824–836.
[44] Imene Mami and Zohra Bellahsene. 2012. A survey of view selection methods.
Acm Sigmod Record 41, 1 (2012), 20–29.
[45] Gonzalo Navarro. 2002. Searching in metric spaces by spatial approximation.
The VLDB Journal 11 (2002), 28–46.
[46] Liana Patel, Peter Kraft, Carlos Guestrin, and Matei Zaharia. 2024. Acorn: Per-
formant and predicate-agnostic search over vector embeddings and structured
data. Proceedings of the ACM on Management of Data 2, 3 (2024), 1–27.
[47] Yun Peng, Byron Choi, Tsz Nam Chan, Jianye Yang, and Jianliang Xu. 2023.
Efficient Approximate Nearest Neighbor Search in Multi-dimensional Databases.
Proc. ACM Manag. Data 1, 1 (2023), 54:1–54:27. https://doi.org/10.1145/3588908
[48] J Ben Schafer, Dan Frankowski, Jon Herlocker, and Shilad Sen. 2007. Collaborative
filtering recommender systems. In The adaptive web: methods and strategies of
web personalization. Springer, 291–324.
[49] Yifang Sun, Wei Wang, Jianbin Qin, Ying Zhang, and Xuemin Lin. 2014. SRS:
Solving c-Approximate Nearest Neighbor Queries in High Dimensional Euclidean
Space with a Tiny Index. Proc. VLDB Endow. 8, 1 (2014), 1–12.
[50] Jianguo Wang, Xiaomeng Yi, Rentong Guo, Hai Jin, Peng Xu, Shengjun Li, Xi-
angyu Wang, Xiangzhou Guo, Chengming Li, Xiaohai Xu, et al. 2021. Milvus:
A purpose-built vector data management system. In Proceedings of the 2021
International Conference on Management of Data. 2614–2627.
[51] Mengzhao Wang, Lingwei Lv, Xiaoliang Xu, Yuxiang Wang, Qiang Yue, and
Jiongkang Ni. 2022. Navigable proximity graph-driven native hybrid queries
with structured and unstructured constraints. arXiv preprint arXiv:2203.13601
(2022).
13

## PDF page 14

[52] Mengzhao Wang, Haotian Wu, Xiangyu Ke, Yunjun Gao, Yifan Zhu, and Wenchao
Zhou. 2025. Accelerating Graph Indexing for ANNS on Modern CPUs. arXiv
preprint arXiv:2502.18113 (2025).
[53] Mengzhao Wang, Weizhi Xu, Xiaomeng Yi, Songlin Wu, Zhangyang Peng, Xi-
angyu Ke, Yunjun Gao, Xiaoliang Xu, Rentong Guo, and Charles Xie. 2024.
Starling: An I/O-Efficient Disk-Resident Graph Index Framework for High-
Dimensional Vector Similarity Search on Data Segment. Proc. ACM Manag.
Data 2, 1 (2024), V2mod014:1–V2mod014:27. https://doi.org/10.1145/3639269
[54] Mengzhao Wang, Xiaoliang Xu, Qiang Yue, and Yuxiang Wang. 2021. A com-
prehensive survey and experimental comparison of graph-based approximate
nearest neighbor search. Proceedings of the VLDB Endowment 14, 11 (2021),
1964–1978.
[55] Runhui Wang and Dong Deng. 2020. DeltaPQ: lossless product quantization code
compression for high dimensional similarity search. Proceedings of the VLDB
Endowment 13, 13 (2020), 3603–3616.
[56] Chuangxian Wei, Bin Wu, Sheng Wang, Renjie Lou, Chaoqun Zhan, Feifei Li,
and Yuanzhe Cai. 2020. AnalyticDB-V: a hybrid analytical engine towards query
fusion for structured and unstructured data. Proceedings of the VLDB Endowment
13, 12 (2020), 3152–3165.
[57] Wei Wu, Junlin He, Yu Qiao, Guoheng Fu, Li Liu, and Jin Yu. 2022. HQANN:
Efficient and robust similarity search for hybrid queries with structured and
unstructured constraints. In Proceedings of the 31st ACM International Conference
on Information & Knowledge Management. 4580–4584.
[58] Yuexuan Xu, Jianyang Gao, Yutong Gou, Cheng Long, and Christian S Jensen.
2024. iRangeGraph: Improvising Range-dedicated Graphs for Range-filtering
Nearest Neighbor Search. arXiv preprint arXiv:2409.02571 (2024).
[59] Mingyu Yang, Wentao Li, Jiabao Jin, Xiaoyao Zhong, Xiangyu Wang, Zhitao Shen,
Wei Jia, and Wei Wang. 2024. Effective and General Distance Computation for
Approximate Nearest Neighbor Search. arXiv preprint arXiv:2404.16322 (2024).
[60] Mingyu Yang, Wentao Li, Zhitao Shen, Chuan Xiao, and Wei Wang. 2025. ESG:
Elastic Graphs for Range-Filtering Approximate k-Nearest Neighbor Search.
arXiv preprint arXiv:2504.04018 (2025).
[61] Mingyu Yang, Wentao Li, and Wei Wang. 2024. Fast High-dimensional Approx-
imate Nearest Neighbor Search with Efficient Index Time and Space. arXiv
preprint arXiv:2411.06158 (2024).
[62] Mingyu Yang, Wenxuan Xia, Wentao Li, Raymond Chi-Wing Wong, and Wei
Wang. 2025. Technical Report. https://github.com/mingyu-hkustgz/LabelANN/
blob/master/technical_report.pdf
[63] Ziqi Yin, Jianyang Gao, Pasquale Balsebre, Gao Cong, and Cheng Long. 2025.
DEG: Efficient Hybrid Vector Search Using the Dynamic Edge Navigation Graph.
Proceedings of the ACM on Management of Data 3, 1 (2025), 1–28.
[64] Mohammed Javeed Zaki. 2002. Scalable algorithms for association mining. IEEE
transactions on knowledge and data engineering 12, 3 (2002), 372–390.
[65] Qianxi Zhang, Shuotao Xu, Qi Chen, Guoxin Sui, Jiadong Xie, Zhizhen Cai,
Yaoqi Chen, Yinxuan He, Yuqing Yang, Fan Yang, et al. 2023. {VBASE}: Unifying
Online Vector Similarity Search and Relational Queries via Relaxed Monotonicity.
In 17th USENIX Symposium on Operating Systems Design and Implementation
(OSDI 23). 377–395.
[66] Bolong Zheng, Xi Zhao, Lianggui Weng, Nguyen Quoc Viet Hung, Hang Liu,
and Christian S. Jensen. 2020. PM-LSH: A Fast and Accurate LSH Framework
for High-Dimensional Approximate NN Search. Proc. VLDB Endow. 13, 5 (2020),
643–655.
[67] Xiaoyao Zhong, Haotian Li, Jiabao Jin, Mingyu Yang, Deming Chu, Xiangyu
Wang, Zhitao Shen, Wei Jia, George Gu, Yi Xie, et al. 2025. VSAG: An Optimized
Search Framework for Graph-based Approximate Nearest Neighbor Search. arXiv
preprint arXiv:2503.17911 (2025).
[68] Chaoji Zuo, Miao Qiao, Wenchao Zhou, Feifei Li, and Dong Deng. 2024. SeRF:
Segment Graph for Range-Filtering Approximate Nearest Neighbor Search. Pro-
ceedings of the ACM on Management of Data 2, 1 (2024), 1–26.
14

## PDF page 15

A APPENDIX
A.1 Proofs
Proof of Lemma 3.2. We analyze the expected search time of the
PostFiltering strategy on a graph index by decomposing it into two
phases: (1) the initial search to locate the first nearest neighbor
(top-1 result), and (2) iterative expansion to collect 𝑘neighbors
satisfying the query’s label constraint.
Phase 1: Initial Search Let 𝐶denote the expected cost of finding
the top-1 neighbor of query 𝑞on the underlying graph index. This
abstracts the entry cost of popular structures such as HNSW or NSG,
typically analyzed as𝑂(log 𝑁) under standard assumptions [13, 46],
where 𝑁is the number of indexed points.
Phase 2: Iterative Expansion After the first match, PostFiltering in-
spects candidates in increasing distance order until 𝑘items sat-
isfying 𝐿𝑞are found. Efficiency depends on the probability that
a candidate satisfies the label constraint. We assume (i) label at-
tributes are independent of vector positions, and (ii) label-matched
points are uniformly distributed in the space and the number is
larger than 𝑘. If the elastic factor satisfies 𝑒𝑆(𝐿𝑞), I ≥𝑐, then at
least a 𝑐-fraction of candidates within the selected index set I match
𝐿𝑞. Under these assumptions, the probability that any visited candi-
date matches 𝐿𝑞is 𝑝≥𝑐. Finding 𝑘matches is therefore a sequence
of Bernoulli trials with success probability 𝑝without replacement,
so the number of trials 𝑇follows a negative-hypergeometric distri-
bution with expectation E[𝑇] = 𝑘𝑁+1
𝑝𝑁+1 < 𝑘/𝑐. Modern graph-based
indexes [13, 23, 34, 42] retrieve the next neighbor in 𝑂(1) amortized
time because each node maintains a bounded-degree neighbor list.
Hence, the expected cost of this phase is 𝑂(𝑘/𝑐).
Total Cost Summing both phases, the expected time to obtain
the top-𝑘label-hybrid AKNN results is 𝑂

𝐶+ 𝑘
𝑐

. This matches
and generalizes prior analyses of post-filtering strategies, such as
HNSW-based filtered search [46], within our elastic-factor frame-
work. As with HNSW and NSG, the bound captures expected com-
plexity and does not guarantee worst-case recall, but it aligns with
recent NSG analyses [60] that report the same 𝑂(𝐶+ 𝑘/𝑐) cost
under similar conditions.
Proof of Theorem 3.4. Given a dataset 𝑆, a query workload with la-
bel sets L = {𝐿1, . . . , 𝐿𝑛}, and a universal index set I = {𝐼1, . . . , 𝐼𝑛}
(where each 𝐼𝑖∈I is built on a label set 𝐿𝑖∈L), the EIS prob-
lem asks whether there exists a subset I ⊆I such that for every
query–label set 𝐿𝑞∈L we can find an index 𝐼∈I with elastic
factor 𝑒(𝑆(𝐿𝑞), 𝐼) greater than a threshold 𝑐, while the total cost
satisfies Í
𝐼∈I |𝐼| ≤𝜏.
The problem is in NP as we can verify in polynomial time that
(1) the total cost Í
𝐼∈I |𝐼| does not exceed the budget 𝜏, and (2)
each query–label set is covered by some selected index with elastic
factor at least 𝑐. To show NP-completeness, we reduce from the
NP-complete 3-Set Cover (3-SC) problem [10, 37].
Definition 7.1 (3-Set Cover (3-SC)).
Input A universal set
U = {𝑢1,𝑢2, . . . ,𝑢𝑝} of 𝑝elements and a collection
S = {𝑠1,𝑠2, . . . ,𝑠𝑙} of 𝑙subsets, where each 𝑠𝑖has size at
most 3 and Ð𝑙
𝑖=1 𝑠𝑖= U. Let the selection budget be 𝑘.
Output Determine if there exists a subset S ⊆S of size at most
𝑘such that Ð
𝑠𝑖∈S 𝑠𝑖= U.
I:u'1[11]
I:u'2[11]
I:u'l[11]
I:u'3[11]
I:u1[11]
I:s1[20]
I:s2[20]
I:sp[20]
Empty[N]
I:u2[11]
I:u3[11]
I:ul[11]
Cover Relationship
s1s2u'1[1]
s2u'2[1]
slu'p[1]
s1slu'3[1]
s1s2u1[1]
s1[8]
s2[4]
sl[6]
Empty
s2u2[1]
s1slu3[1]
slup[1]
(b) EIS Problem Instance
(d) Query Label Set and Vector Label
u1
u2
u3
ul
s1
s2
sp
element unit
Set Mapping
Set Mapping
(a) 3-SC Example
element unit
All[10]
All[10]
 I:top:{I:top, I:s1, I:s2 ,..., I:sl }
 I:si:{I:uj ,..., I:u'j}(up to 3 Element unit and I:all)
 I:uj:{I:uj, I:all}    I:all:{I:all}
(c): Set Cover Relationship
Under Elastic Factor = 20/N 
Figure 13: Proof of NP-completeness. Set 𝐷= 𝑏= 10, so 𝐴= 𝑏+1 = 11,
𝐵= 2𝑏= 20, and 𝑁= 50 > 4𝑏. Then 𝑐=
𝐵
𝑁
=
20
50 = 0.4. Thus
we have 10 vectors in 𝐼all and 50 vectors in 𝐼⊤. Each element index
𝐼𝑢𝑖is duplicated with a dummy index 𝐼𝑢′
𝑖, and each subset index
𝐼𝑠𝑗connects to at most three element indexes 𝐼𝑢𝑖or 𝐼𝑢′
𝑖. Moreover,
|𝐼𝑠𝑗|/|𝐼⊤| = 0.4 = 𝑐, |𝐼𝑢𝑖|/|𝐼⊤| = 11
50 = 0.22 < 𝑐, |𝐼𝑢𝑖|/|𝐼𝑠𝑖| = 11
20 > 𝑐.
Construction. Given a 3-SC instance (U, S,𝑘), we create a corre-
sponding EIS instance (see Fig. 13). Let the alphabet be Σ = U ∪S,
and define four types of indexes as follows: (1) for each element
𝑢𝑖∈U, an element index 𝐼𝑢𝑖of size |𝐼𝑢𝑖| = 𝐴; (2) for each subset
𝑠𝑗∈S, a subset index 𝐼𝑠𝑗of size |𝐼𝑠𝑗| = 𝐵; together with two
special indexes: (3) a top index 𝐼⊤(Empty[N] in Fig. 13) containing
all base vectors, with size |𝐼⊤| = 𝑁; and (4) a bottom index 𝐼all
(All[D] in Fig. 13) containing vectors matching all labels in Σ, with
size |𝐼all| = 𝐷. The top index 𝐼⊤, required for AKNN search, always
exists, and we omit its cost for simplicity. The costs 𝐴, 𝐵, 𝐷and the
elastic-factor threshold 𝑐are set to satisfy the following conditions.
(1) Each subset index 𝐼𝑠𝑗covers an element index 𝐼𝑢𝑖with overlap
ratio at least 𝑐(i.e., 𝐼𝑢𝑖⊆𝐼𝑠𝑗, |𝐼𝑢𝑖|/|𝐼𝑠𝑗| ≥𝑐) if and only if 𝑢𝑖∈𝑠𝑗.
(2) The top index 𝐼⊤covers every subset index 𝐼𝑠𝑗with overlap ratio
𝑐, while its overlap ratio with any element index 𝐼𝑢𝑖is less than 𝑐.
(3) To cover element/label set 𝑢𝑖, selecting the element index 𝐼𝑢𝑖
incurs higher cost than selecting any subset index 𝐼𝑠𝑗, where𝑢𝑖∈𝑠𝑗.
Feasibility. We next show how to satisfy the above conditions. •
Set the cost of 𝐼all to 𝐷= 𝑏> 3 (as in Fig. 13(b), where 𝑏= 10
and 𝐼all contains vectors covering all labels in Σ). • Let 𝐴= 𝑏+ 1,
𝐵= 2𝑏, and set the elastic–factor threshold to 𝑐= 2𝑏
𝑁with 𝑁> 4𝑏.
If 𝑁≤4𝑏, we pad 𝐼⊤with vectors carrying no labels to increase 𝑁
(as in Fig. 13(b), where 𝑁> 40). • Each element index 𝐼𝑢𝑖is built
from the 𝑏vectors in 𝐼all plus one additional vector whose label set
is 𝑠1 · · ·𝑠𝑥𝑢𝑖, where 𝑠1, . . . ,𝑠𝑥are the subsets containing 𝑢𝑖. Thus
|𝐼𝑢𝑖| = 𝑏+1 = 𝐴, created on the query–label set 𝐿𝑞= {𝑠1, . . . ,𝑠𝑥,𝑢𝑖}.
For this additional vector, we also create another vector with label
set 𝑠1 · · ·𝑠𝑥𝑢′
𝑖, where 𝑢′
𝑖is a duplicate label equivalent in meaning
to 𝑢𝑖(i.e., if 𝑢𝑖∈𝑠𝑗then 𝑢′
𝑖∈𝑠𝑗). Accordingly, we introduce another
element index 𝐼𝑢′
𝑖of size 𝑏+ 1, consisting of the 𝑏vectors from 𝐼all
plus one vector with labels 𝑠1 · · ·𝑠𝑥𝑢′
𝑖, created on the query–label
set 𝐿𝑞= {𝑠1, . . . ,𝑠𝑥,𝑢′
𝑖}. • Each subset index 𝐼𝑠𝑗is created on the
query–label set 𝐿𝑞= {𝑠𝑗}. Since each subset 𝑠𝑗contains at most
three elements 𝑢𝑖(and their duplicates 𝑢′
𝑖), it covers at most 3 × 2
vectors of the form 𝑠1 · · ·𝑠𝑥𝑢𝑖or 𝑠1 · · ·𝑠𝑥𝑢′
𝑖, together with the 𝑏
vectors in 𝐼all. We then append up to 2𝑏−6 extra vectors labeled 𝑠𝑗
so that the total number of vectors matching 𝑠𝑗equals 𝐵= 2𝑏.
In
summary,
the
query
workload
is
L
=
{{𝑠1}, . . . , {𝑠𝑙}, {𝑠1 · · ·𝑠𝑥𝑢𝑖}, . . . , {𝑠1 · · ·𝑠𝑥𝑢′
𝑖}, ∅, Σ}, and the corre-
sponding universal index set is I = {𝐼𝑠1, . . . , 𝐼𝑠𝑙, 𝐼𝑢1, . . . , 𝐼𝑢𝑝, 𝐼⊤, 𝐼all}.
15

## PDF page 16

Conditional Checking. Note that 𝑐= 2𝑏
𝑁< 2𝑏
4𝑏= 1
2. For Condition
(1), when 𝑢𝑖∈𝑠𝑗, the overlap between 𝐼𝑢𝑖and 𝐼𝑠𝑗equals |𝐼𝑢𝑖|, so
the overlap ratio is
|𝐼𝑢𝑖|
|𝐼𝑠𝑗| = 𝐴
𝐵= 𝑏+1
2𝑏
> 1
2 > 𝑐. For Condition (2),
for each 𝑠𝑗, the overlap ratio with 𝐼⊤is
|𝐼𝑠𝑗|
|𝐼⊤| = 𝐵
𝑁= 2𝑏
𝑁= 𝑐. For
each 𝑢𝑖, it is
|𝐼𝑢𝑖|
|𝐼⊤| = 𝐴
𝑁= 𝑏+1
𝑁. Since 2𝑏> 𝑏+ 1 and 𝑏> 3, we
have 𝑏+1
𝑁
< 𝑐, satisfying the condition. For Condition (3), to cover
vectors containing label 𝑢𝑖, selecting a subset index 𝐼𝑠𝑗with 𝑢𝑖∈𝑠𝑗
costs 𝐵= 2𝑏. Selecting the element index 𝐼𝑢𝑖also requires selecting
𝐼𝑢′
𝑖(otherwise, the vector with label set 𝑠1 · · ·𝑠𝑥𝑢′
𝑖would not be
included), for a total cost of 2𝐴= 2(𝑏+ 1). As 2𝑏< 2(𝑏+ 1),
choosing 𝐼𝑠𝑗is cheaper, so this condition holds.
Correctness. Note that 𝐼⊤must always be selected but incurs zero
cost, while 𝐼all need not be selected since it is already covered by
other indexes. We set the space budget 𝜏= 𝑘· 𝐵, which permits
selecting at most 𝑘subset indexes (by Condition (3)).
3-SC ⇒EIS. If the 3-SC instance admits a cover S ⊆S with |S| ≤𝑘,
select the corresponding subset indexes {𝐼𝑠𝑗: 𝑠𝑗∈S}. Each 𝐼𝑠𝑗is
created on the query–label set 𝑠𝑗and covers all vectors whose label
set is𝑢𝑖for every𝑢𝑖∈𝑠𝑗. The overlap ratio is at least 𝑐, so the elastic
factor between 𝐼𝑠𝑗and both 𝑠𝑗and 𝑢𝑖is at least 𝑐. This solves EIS
because (1) the top index 𝐼⊤covers the query-label sets S (as well as
∅) in L with elasticity factor𝑐, and (2) the index built on the selected
S covers the query-label sets {{𝑠1 · · ·𝑠𝑥𝑢𝑖}, . . . , {𝑠1 · · ·𝑠𝑥𝑢′
𝑖} (as well
as Σ, since |𝐼all|/|𝐼𝑠𝑗|| = 𝑏/2𝑏> 𝑐) in L with elasticity factor 𝑐.
3-SC ⇐EIS. Conversely, suppose the EIS instance admits a feasible
selection I. By Condition (3), any 𝐼𝑢𝑖∈I can be replaced by some
𝐼𝑠𝑗containing 𝑢𝑖without violating constraints or increasing total
cost. Thus we may assume the solution consists solely of subset
indexes after such replacements. Let S = {𝑠𝑗: 𝐼𝑠𝑗∈I}. Because
every 𝑢𝑖(in the query workload U ⊆L) must be covered by some
subset index (with overlap ratio at least 𝑐), we have Ð
𝑠𝑗∈S 𝑠𝑗= U.
The budget enforces |S| ≤𝜏/𝐵= 𝑘, yielding a valid 3-SC solution.
A.2 EXTRA DISCUSSIONS
Other Types of Label Constraints. Our work primarily addresses
the label-containment constraint, where a vector’s label set must
contain the query label set. Other label constraints can also be
supported:
(1) Label equality. Here, we require base vectors whose label set
𝐿𝑖exactly matches the query label set 𝐿𝑞. To handle AKNN search
under this constraint, we group base vectors by their label sets to
form a label-set inverted list. An index is then built for each group,
and the overall dataset cardinality remains unchanged. For example,
an index for 𝐿𝑞= {𝐴} is built over base vectors 𝑥2 and 𝑥5, both
labeled {𝐴}. When using a graph-based index—the most efficient
structure to date—the total space complexity is 𝑂(𝑁𝑀), where 𝑀
is the maximum node degree constrained by edge occlusion.
(2) Label overlap. Here we require that a vector’s label set intersects
the query label set, i.e., 𝐿𝑖∩𝐿𝑞≠∅. To support AKNN search under
this type of label constraint, we decompose the query into |𝐿𝑞|
subqueries, each with a single-label containment constraint. For
example, a label-overlap query with label set 𝐿𝑞= {𝐴𝐵} can be
answered by merging the results of label-containment queries with
UNG
ELI-0.2
ELI-2.0
ACORN-1
ACORN-𝛾
80
90
100
102
103
recall@10(%)
Qps
Label Overlap(Zipf,|Σ|=12)
(a) SIFT
70
80
90
100
101
102
103
104
recall@10(%)
Qps
Label Overlap(Real,|Σ|=7)
(b) PAPER
Figure 14: The Test of Label-Overlap Constraints
label sets 𝐿𝑞= {𝐴} and 𝐿𝑞= {𝐵} and taking their union. These
extensions show that our approach naturally generalizes beyond
simple label containment, while we focus on the containment case
throughout this paper.
Vector Similarity Search. Vector similarity search is closely re-
lated to AKNN search and has been extensively studied. Most prior
work targets approximate search [19], since exact search is prohib-
itively expensive in high-dimensional spaces [32]. Among these
methods, 𝑐-approximate nearest neighbor search returns results
within a factor 𝑐of the true distance and can be solved in sublinear
time using locality-sensitive hashing (LSH) [9, 14, 19, 20, 30, 49, 66].
For the AKNN search problem in this paper, graph-based vector
indexes [12, 13, 23, 34, 40, 43, 47, 53] represent the current state
of the art and are significantly more efficient than LSH according
to benchmarks [39]. In addition, inverted-list indexes [3, 35] and
quantization-based methods [15, 17, 18, 22, 35, 36, 61] are widely
used for AKNN search. Inverted indexes provide low space over-
head, while quantization techniques speed up distance computa-
tions [52, 59]. In this paper, we employ the widely adopted HNSW
algorithm [42] as the underlying index for each vector group, al-
though other optimized AKNN libraries [34, 67] can be used as
drop-in replacements.
A.3 Extra Experiments
Exp-7: Test of Label-Overlap Constraints. We previously dis-
cussed how our method for AKNN search with label-containment
constraints can be extended to handle label-overlap constraints.
Specifically, a query with label overlap is decomposed into multiple
single-label containment sub-queries; We solve each sub-query in-
dependently and then merge the results. We evaluate this approach
on both synthetic SIFT data with a Zipf distribution and the real-
world Paper dataset, as shown in Fig. 14. The figure shows that,
despite requiring multiple search calls for single-label containment
sub-queries, our methods ELI-0.2 and ELI-2.0 achieve search per-
formance comparable to ACORN-𝛾, while delivering 2×–10× the
efficiency of ACORN and UNG. Since our current framework does
not treat overlap queries as part of the optimization workload, we
plan to explore extensions of our greedy method to support such
queries and, more broadly, other complex label constraints.
Exp-8: Scalability to Large Datasets. To validate the scalability
of our algorithm on large-scale data, we adopt the DEEP dataset
containing 100M vectors of 96 dimensions. Labels follow a Zipf dis-
tribution with varying query selectivities, and the results are shown
16

## PDF page 17

ELI-0.2
ELI-2.0
ACORN-1
ACORN-𝛾
70
80
90
100
102
103
recall@10(%)
Qps
Ave. Select 24% (Zipf,|Σ|=8)
80
90
100
101
102
103
recall@10(%)
Qps
Ave. Select 18% (Zipf,|Σ|=12)
(a) DEEP (High-Selectivity)
70
80
90
100
101
102
recall@10(%)
Qps
Ave. Select 11% (Zipf,|Σ|=24)
80
85
90
95
101
102
103
recall@10(%)
Qps
Ave. Select 9% (Zipf,|Σ|=32)
(b) DEEP (Low-Selectivity)
Figure 15: The Test of Scalability
DEEP(| |=8)
DEEP(| |=12)
DEEP(| |=24)
DEEP(| |=32)
1B
Count (Points)
540.3M
691.9M
1.1B
1.3B
202.4M
262M
454.4M
531.9M
Comparison of Selected Points
All Points
ELI-0.2
ELI-2.0
DEEP(| |=8)
DEEP(| |=12)
DEEP(| |=24)
DEEP(| |=32)
100
1K
10K
0.1M
Count (Sets)
233
1.2K
8.1K
13.6K
71
394
2.6K
4.7K
71
203
263
351
Comparison of Selected Sets
All Sets
ELI-0.2
ELI-2.0
Figure 16: The Index Select Statistic of Large-Scale Data
in Fig. 15. Because brute-force search achieves only 0.2–0.6 Qps,
we omit it from the figure. Moreover, UNG fails to scale, encoun-
tering a core dump during index construction on the DEEP100M
dataset. From Fig. 15, ACORN exhibits 4× worse performance than
our ELI-0.2 method at 90% recall and 3× worse than ELI-2.0. Our
methods maintain consistent performance as the alphabet size |Σ|
increases, whereas ACORN fails to reach 90% recall for large |Σ|.
We further analyze index-selection statistics of our methods on
this dataset by varying the alphabet size, with results shown in
Fig. 16. ELI-2.0 indexes only an additional 100 million vectors with
351 selected index sets—about 1/12 of all base vectors. In contrast,
ELI-0.2 selects more indexes, covering roughly 400 million vectors
(about one-third of the 1.2 billion candidates). This larger index
set pays off: ELI-0.2 achieves 4×–6× higher query efficiency than
ELI-2.0. These results underscore the trade-off between index size
and query efficiency in real-world large-scale scenarios.
Exp-9: Varying Alphabet Size. In Exp-1 we varied the alphabet
size over a limited range; here we further test the search efficiency
of different methods—particularly UNG, ACORN, and our ELI-0.2—
under much larger alphabets. We increase |Σ| from 64 to 128, 256,
and 512 to evaluate UNG and ELI-0.2. The ACORN method fails
to achieve 80% recall at these scales and is therefore omitted from
Fig. 17. Results show that our method achieves nearly an 800×
speedup in search efficiency at 99% recall when |Σ| = 512. This
UNG
ELI-0.2
80
90
100
101
102
103
104
recall@10(%)
Qps
Ave. Select 6.3% (Zipf,|Σ|=64)
80
90
100
101
102
103
104
recall@10(%)
Qps
Ave. Select 4.7% (Zipf,|Σ|=128)
(a) SIFT |Σ|=64,128
80
90
100
101
102
103
104
recall@10(%)
Qps
Ave. Select 3.7% (Zipf,|Σ|=256)
80
90
100
101
102
103
104
recall@10(%)
Qps
Ave. Select 3.5% (Zipf,|Σ|=512)
(b) SIFT |Σ|=256,512
Figure 17: Varying the Alphabet Size |Σ|
UNG
ELI-0.2
ELI-2.0
ACORN-1
ACORN-𝛾
70
80
90
100
103
recall@10(%)
QPS
thread@4
70
80
90
100
103
104
recall@10(%)
QPS
thread@8
(a) MSMARCO-Fewer Processes
70
80
90
100
103
104
recall@10(%)
QPS
thread@16
70
80
90
100
103
104
recall@10(%)
QPS
thread@32
(b) MSMARCO-More Processes
Figure 18: Varying the Thread Number (on MSMARCO)
ELI-2.0
ELI-0.2
ELI-0.5
ELI-Opt
90
95
102
103
recall@10(%)
QPS
contain@32
(a) MSMARC
90
95
102
103
recall@10(%)
QPS
contain@32
(b) OpenAI-1536
Figure 19: The Comparison with the Exhaustive Method
improvement arises because the fixed-efficiency design of ELI-0.2
exploits the lower selectivity of large alphabets. In contrast, UNG
suffers substantial degradation in search efficiency as |Σ| grows. We
also compare index-build times. For |Σ| = 512, ELI-0.2 builds the
17

## PDF page 18

ELI-0.2
ELI-2.0
1.0
1.001
1.002
1.003
102.5
103
103.5
Ratio
Qps
Ave Select 18% (Zipf,|Σ|=12)
1.0
1.001
1.002
103
104
Ratio
Qps
Ave Select 11% (Zipf,|Σ|=24)
(a) SIFT
1.0 1.00031.0006
1.001
102.5
103
Ratio
Qps
Ave Select 18% (Zipf,|Σ|=12)
1.0
1.0003
1.0006
1.001
102.6
102.8
103
Ratio
Qps
Ave Select 11% (Zipf,|Σ|=24)
(b) Arxiv
Figure 20: The Effect of the Distance Ratio
ELI-0.2
ELI-2.0
92
94
96
98
100
103
104
recall@10(%)
Qps
Ave Select 18% (Zipf,|Σ|=12)
94
96
98
103
104
recall@10(%)
Qps
Ave Select 11% (Zipf,|Σ|=24)
(a) SIFT
90
95
100
102.5
103
recall@10(%)
Qps
Ave Select 18% (Zipf,|Σ|=12)
94
96
98
100
102.6
102.8
recall@10(%)
Qps
Ave Select 11% (Zipf,|Σ|=24)
(b) Arxiv
Figure 21: The Test of Maximum Inner Product Search
index in 155 seconds, whereas UNG requires 2,091 seconds—over
13× longer—demonstrating the superior scalability of ELI-0.2.
Exp-10: Varying Thread Number. During search, we apply par-
allelism to improve performance. To evaluate scalability, we vary
the number of threads from 4 to 32 with |Σ| = 12 and report results
on the MSMARCO dataset (other datasets show similar trends). Fig-
ure 18 presents the results. Our methods achieve the highest search
performance across all thread settings, demonstrating robustness in
multi-threaded environments. Moreover, our architecture invokes
only a single sub-index for each query, making it well suited for
distributed systems—a direction we plan to explore in future work.
Exp-11: Comparison with the Exhaustive Method. Because
our method selects a subset of indexes from the universal set corre-
sponding to the query workload, we compare it with an exhaustive
method that builds indexes for all possible query–label sets (i.e.,
selects the entire universal index set). We also introduce ELI-0.5, an
efficiency-constrained variant of EIS with the elastic-factor thresh-
old increased to 0.5. Figure 19 shows the search efficiency of ELI-
2.0, ELI-0.2, ELI-0.5, and the exhaustive method (ELI-Opt) under
a Zipf distribution with |Σ| = 32 on the MSMARCO and OpenAI-
1536 datasets. The results indicate that ELI-0.2 and ELI-0.5 achieve
near-optimal search efficiency at high recall, closely matching ELI-
Opt. ELI-2.0 delivers roughly 3× lower Qps than the exhaustive
method but requires far less space. In terms of index size, ELI-2.0,
ELI-0.2, ELI-0.5, and ELI-Opt consume 383 MB, 634 MB, 812 MB,
and 1,280 MB, respectively. Excluding the mandatory 192 MB top
index, ELI-2.0 uses only about one-sixth of the space of the exhaus-
tive approach while maintaining acceptable search performance.
Moreover, ELI-0.5 requires only about half the space required by
ELI-Opt to achieve nearly identical search efficiency, highlighting
the advantage of our methods in efficiency-oriented scenarios.
Exp-12: Effect of Distance Ratio. To further assess search per-
formance, we measure the distance ratio [7, 14, 66]: the ratio of
the average distance between a query and the returned objects
to the average distance between the query and its ground-truth
𝑘nearest neighbors. A ratio of 1.0 indicates that the exact 𝑘NN
results are found; larger ratios allow more flexibility in the returned
objects. We report Qps at varying distance ratios on the SIFT and
Arxiv datasets, with results shown in Fig. 20. Our methods require
more time (i.e., achieve lower Qps) to reach very small distance ra-
tios, but performance improves as the ratio increases. Notably, our
methods often achieve an average distance ratio near 1.001, substan-
tially outperforming prior LSH-based approaches [9, 14, 30, 49, 66],
demonstrating our superiority from another perspective.
Exp-13: Test of Maximum Inner Product Search. Our default
experiments use Euclidean distance to measure similarity between
queries and base vectors. Here we replace Euclidean distance with
the inner product and evaluate whether our method still performs
well. This setting corresponds to Maximum Inner Product Search
(MIPS). We test this extension on the SIFT and Arxiv datasets, with
results shown in Fig. 21. As illustrated, MIPS delivers computational
performance comparable to the Euclidean metric under our frame-
work, maintaining high Qps even at high recall. These findings
indicate that our method is broadly applicable across common vec-
tor similarity measures and align with prior evidence [2, 23] that
graph-based indexes (e.g., HNSW) sustain high search efficiency
across different similarity metrics.
18
