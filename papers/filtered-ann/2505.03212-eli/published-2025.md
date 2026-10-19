# PVLDB 19(4):617–630 (2025) 原始 PDF 文本提取

来源：https://www.vldb.org/pvldb/vol19/p617-yang.pdf

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
approximate 푘-nearest neighbor (AKNN) search retrieves the
top-푘approximate nearest vectors to a query, subject to the con-
straint that their labels fully contain the query-label set. A naive so-
lution builds a separate index for every query-label set, but the expo-
nential growth of such sets makes this approach storage-prohibitive.
To overcome this, we propose selectively indexing only a subset
of query-label sets while still ensuring eﬃcient processing for all
queries. This is made possible by a key insight into label contain-
ment: an index built for a label set 퐿can also serve any query
whose label set 퐿′ is a superset of 퐿, with query cost bounded by
the elastic factor—the ratio between the number of vectors match-
ing 퐿and those matching 퐿′. We formalize the index-selection task
as a constrained optimization problem that chooses which label
sets to index to satisfy space and query eﬃciency constraints. We
prove the problem is NP-complete and propose eﬃcient greedy algo-
rithms for its eﬃciency- and space-constrained variants. Extensive
experiments on real-world datasets show that our method achieves
10↑–800↑speedups over state-of-the-art techniques. Moreover, our
approach is index-agnostic and can be seamlessly integrated into
existing vector database systems.
PVLDB Reference Format:
Mingyu Yang, Wenxuan Xia, Wentao Li, Raymond Chi-Wing Wong,
and Wei Wang. Elastic Index Selection for Label-Hybrid AKNN Search.
PVLDB, 19(4): 617 - 630, 2025.
doi:10.14778/3785297.3785304
PVLDB Artifact Availability:
The source code, data, and/or other artifacts have been made available at
https://github.com/mingyu-hkustgz/LabelANN.
1
INTRODUCTION
The 푘-nearest neighbor (KNN) search over high-dimensional
vectors is a core operation in modern data systems, underpinning
a wide range of applications such as recommendation systems [42],
data mining [6], face recognition [48], product search [48], and
∗Corresponding author.
This work is licensed under the Creative Commons BY-NC-ND 4.0 International
License. Visit https://creativecommons.org/licenses/by-nc-nd/4.0/ to view a copy of
this license. For any use beyond those covered by this license, obtain permission by
emailing info@vldb.org. Copyright is held by the owner/author(s). Publication rights
licensed to the VLDB Endowment.
Proceedings of the VLDB Endowment, Vol. 19, No. 4 ISSN 2150-8097.
doi:10.14778/3785297.3785304
Item #1
Brand:  A
Size:      L
Color:   blue
Year:     2025
x1
Item #2
Brand:  B
Size:     S
Color:  red
Year:    2024
x2
Item #3
Brand:  A
Size:     M
Color:  yellow
Year:    2024
x3
Item #4
Brand: A
Size:     L
Color:  white
Year:    2025
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
ping scenario (푘= 1). Each item is represented by an embedding
vector 푥푖(푖∈[1, 4]) and associated with label attributes such as
brand and year. A customer submits a reference photo 푞together
with a query-label set 퐿푞. The task is to ﬁnd the item most similar to
푞that also satisﬁes the label constraint. Thus, 푥2 and 푥3 are discarded
for label mismatch, and 푥4 is returned as the nearest neighbor.
retrieval-augmented generation (RAG) for large language mod-
els [32]. In real-world deployments, vector embeddings are often
associated with label attributes, such as product brands, keywords,
and geolocations. For example, in an e-commerce setting, a user
may search for items similar to a given photo while specifying label
constraints like brand name and release year.
In this context, we introduce the problem of label-hybrid 푘-
nearest neighbor (KNN) search, illustrated in Fig. 1. Unlike tradi-
tional KNN search, this variant considers both the vector embed-
dings and their associated label attributes. Formally, each entry
in the database 푆consists of a vector embedding and a label set
퐿. Given a query vector 푞and the label constraint (expressed as a
query-label set 퐿푞), the goal is to retrieve the top-푘nearest neigh-
bors of 푞in 푆, where each returned vector with label set 퐿must
contain all labels in the query-label set 퐿푞, i.e., 퐿푞⊆퐿. However, due
to the curse of dimensionality [27], computing exact label-hybrid
KNN in high-dimensional spaces is computationally expensive. Re-
cent work therefore studies the label-hybrid approximate KNN
(AKNN) problem, which eﬃciently returns the top-푘approximate
neighbors satisfying the label constraint, trading a small amount of
accuracy for substantial performance gains [5, 40].
Existing Solutions. To support label-hybrid AKNN search, ex-
isting approaches often employ graph-based indexes [10, 11, 18,
19, 25, 29, 33, 34, 37, 41, 44, 46, 55] combined with ﬁlter-based
search strategies, due to their good search eﬃciency. Speciﬁcally,
the graph-based index is constructed by adding base vectors in data-
base 푆as nodes and connecting each node to its nearby neighbors
to form edges, which are carefully selected to support eﬃcient nav-
igation. At query time, on top of the graph-based index, ﬁlter-based
617

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
Figure 2: Motivating example where database 푆and queries draw
labels from the alphabet {퐴, 퐵,퐶}. Base vectors in 푆are grouped
by their associated label sets 퐿, each group denoted as {퐿}[count].
Arrows connect a group 퐿to groups with its minimal supersets. For
instance, Fig. 2(a) shows 400 vectors labeled only {퐴}, written as
퐴[400]. Given an AKNN query with 퐿푞= {퐴}, Fig. 2(b) highlights
all relevant groups—those whose label sets are supersets of {퐴}—
namely {퐴,퐴퐵,퐴퐶,퐴퐵퐶}, totaling 1, 000 matching vectors that must
be indexed under 퐿푞= {퐴}. Fig. 2(c) summarizes the index sizes
required for each query-label set. Supporting all possible query-
label sets in the workload requires indexing 5, 400 vectors—about
2.75↑the original dataset size.
search strategies [18] are applied to enforce label constraints and
retrieve the ﬁnal results. In particular, two strategies—PreFiltering
and PostFiltering—are integrated into the graph-based index. The
PreFiltering strategy prunes nodes (i.e., base vectors) that do not
satisfy the query label constraints, removing both the nodes and
their connections to neighbors during the search. In contrast, the
PostFiltering strategy retains all nodes and connections, but ex-
cludes any label-mismatched nodes from being added to the results.
A base vector matches the query if its label set contains the
query-label set, and the selectivity of a query is the number of its
label-matched vectors. The performance of both strategies drops
sharply when selectivity is low. (1) PreFiltering suﬀers from reduced
accuracy as removing many label-mismatched nodes sparsiﬁes the
graph, making it diﬃcult to reach the target vectors. (2) While
PostFiltering retains all nodes, it still requires computing distances
between the query and many mismatched vectors, causing inef-
ﬁciency. To address these challenges, existing solutions such as
Milvus [43], ADB [48], VBASE [57], and CHASE [35] dynamically
select between PreFiltering and PostFiltering based on the query
workload and cost estimation. Yet, the limitations of these search
strategies persist. Some heuristic approaches, including NHQ [44]
and HQANN [49], propose fusion distances that combine both vec-
tor similarity and label matching between the query and base vec-
tors. These methods require manual tuning of vector–label weight
parameters and still perform well below the state-of-the-art.
State-of-the-Art.
To
support
label-hybrid
AKNN
search,
ACORN [40] and UNG [5] represent the state-of-the-art. ACORN
extends the PreFiltering strategy to address connectivity issues
under low selectivity by introducing a parameter 훾that increases
the graph density—each node has 훾times more outgoing edges
than in a standard index. This denser graph improves robustness
when ﬁltering out label-mismatched nodes during query process-
ing. However, ACORN ignores label information during index
construction, leading to potential result incompleteness.
To address this, UNG exploits label containment during graph
construction. It partitions base vectors into groups by their label
Table 1: We compare our method with existing solutions along three
dimensions. Search Performance measures accuracy and eﬃciency,
validated experimentally. Index Flexibility captures the ability to
support label-hybrid AKNN search without dependence on a speciﬁc
index structure. Space–Performance Trade-oﬀs assess how well the
method balances indexing cost and storage eﬃciency.
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
↑
↑
↑
✓
Space–Performance Trade-oﬀs
✓
↑
↑
↑
↑
sets and builds a subgraph for each group. Subgraphs for a label set
퐿푞are connected to those of its minimal supersets via cross-group
edges, ensuring that any vector with a superset of 퐿푞is reachable
from the group of 퐿푞, thereby guaranteeing completeness. However,
both ACORN and UNG are limited to graph-based indexes and thus
lack index ﬂexibility. Their performance also degrades when query
selectivity is low. Experiments show signiﬁcant deterioration as the
size of the alphabet grows, and neither method can ﬂexibly adjust
the index structure to meet tight storage constraints.
Motivation. Given an alphabet Σ from which both the database 푆
and queries draw labels, we may face an exponential explosion of
possible label sets. Speciﬁcally, let L be the set of query-label sets
in the workload (a subset of the power set of Σ). A naive strategy
for AKNN queries is to build a separate index for each query-label
set 퐿푞∈L, where the index for 퐿푞stores all vectors whose label
sets contain 퐿푞. Under this scheme, a vector with label set 퐿must
be inserted into every index whose query-label set is a subset of
퐿. As illustrated in Fig. 2, a vector labeled {퐴퐵퐶} must appear in
eight indexes: {∅,퐴, 퐵,퐶,퐴퐵,퐴퐶, 퐵퐶,퐴퐵퐶}, where ∅denotes the
top index with no label constraint (퐿푞= ∅). This replication creates
substantial overhead: empirical studies show that for |Σ| = 6–10,
the total index entries can be 64↑–1024↑larger than the original
database, leading to prohibitive indexing and storage costs.
Our Solution. Indexing every label set in L is impractical due
to the sheer number of indexes and the resulting storage cost. A
natural question arises: can we index only a subset of L while still
enabling eﬃcient label-hybrid AKNN search? This is non-trivial—if
a query-label set 퐿∈L is not indexed, queries with label set 퐿may
be ineﬃcient to process. Fortunately, label containment provides a
key insight: an index built for 퐿can also serve any query whose
label set 퐿′ is a superset of 퐿. We further show that the query la-
tency is bounded by an elastic factor—the ratio of vectors matching
퐿′ to those matching 퐿. Note that a higher elastic factor implies
better search eﬃciency. For example, in Fig. 2(c), all vectors match-
ing {퐴퐵} are contained in those matching {퐴}, with an overlap
ratio (or elastic factor) of at least 0.5. Thus, the index on {퐴} can
eﬃciently answer queries for {퐴퐵}. The index on {퐵} has an even
higher overlap with {퐴퐵}, yielding greater eﬃciency. We therefore
formalize the problem of index selection as choosing a subset of
L to index while bounding both space usage and the elastic factor.
We ﬁrst show that the decision version of the index-selection
problem is NP-complete. We then formulate two optimization vari-
ants: (1) an eﬃciency-constrained version that minimizes index
space while guaranteeing a lower bound on the elastic factor (search
618

## PDF page 3

eﬃciency), and (2) a space-constrained version that maximizes
the elastic factor under a ﬁxed space budget. We propose a greedy
algorithm for the ﬁrst variant and extend it to the second, and
we further describe how to update indexes in dynamic settings.
Overall, our methods selectively index label sets to control space
while maintaining high elastic factors for eﬃcient label-hybrid
AKNN search. Compared with existing approaches (see Table 1),
our solution provides greater ﬂexibility, higher search eﬃciency,
and superior space–performance trade-oﬀs.
Contribution. We summarize our main contributions as follows:
Problem Analysis (§ 3). We identify the limitations of existing label-
hybrid AKNN solutions: limited ﬂexibility in the underlying indexes
and suboptimal query performance. To address these issues, we
measure search performance using the elastic factor, which quanti-
ﬁes how an index built on a query-label set 퐿can support queries
over its supersets, motivating our novel index-selection problem.
Index Selection Problem Formulation (§ 3). We formally deﬁne the
decision version of the index-selection problem: given a query
workload with label sets L, can we choose a subset of L so that
the total index space stays within a speciﬁed bound and the elastic
factor exceeds a given threshold? We prove this problem is NP-
complete, showing that exact solutions are intractable.
Index Selection Problem Solutions (§ 4). Building on the decision ver-
sion, we deﬁne two practical optimization variants of the index
selection problem: (1) the eﬃciency-constrained variant, which
minimizes space given a lower bound on the elastic factor; and (2)
the space-constrained variant, which maximizes the elastic factor
under a space limit. These formulations enable users to balance
space and query eﬃciency. We design a greedy algorithm for the
ﬁrst variant and extend it to solve the second.
Extensive Experiments (§ 5). We evaluate our algorithm on multiple
datasets with diverse label distributions associated with the base
vectors. Experiments show that our approach attains near-optimal
search eﬃciency with only a 100% space overhead and outperforms
competitors on large-scale datasets with large alphabets, achieving
10↑–800↑speedups over state-of-the-art baselines.
Due to space constraints, some proofs and experiments are omit-
ted, which can be found in our technical report [54].
2
PRELIMINARY
We formally deﬁne the label-hybrid approximate푘-nearest neighbor
search problem in § 2.1. We then review existing solutions in § 2.2.
We list the commonly used notations in the Table 2.
2.1
Problem Statement
Each entry (푥푖, 퐿푖) in the dataset 푆consists of a 푑-dimensional base
vector 푥푖∈R푑and an associated label set 퐿푖, which may be empty,
where all possible labels are from the alphabet Σ. A label-hybrid
search is denoted by a query (푞, 퐿푞), where 푞is the query vector
and 퐿푞is the query-label set. When 퐿푞is speciﬁed, we ﬁrst ﬁlter
the dataset 푆to obtain a candidate subset 푆(퐿푞) ⊆푆, deﬁned as:
푆(퐿푞) = {(푥푖, 퐿푖) ∈푆| 퐿푞⊆퐿푖}, i.e., the label-matched vectors
whose label sets contain all labels in 퐿푞. The label-hybrid 푘-nearest
neighbor search is performed over this candidate subset. Formally,
Table 2: Summary of Notations
Notation
Description
푆
Set of vectors, each with an associated label set
퐿푖, 퐿푞
Base and query label sets
푆(퐿푞)
Subset of 푆whose label sets contain 퐿푞
|퐿|
Cardinality of label set 퐿
L
Collection of all label sets
|L|
Number of label sets in L
I
Collection of indexes
L
The selected indexes from L
Σ
Label alphabet (all possible labels)
푁
Cardinality of 푆
푞
Query vector
푒
Elastic factor
푑
Dimensionality of vectors in 푆
훿(푢, 푣)
Distance (or similarity) between 푢and 푣
퐼
AKNN search index (e.g., HNSW)
휏
Space budget
푐
Elastic-factor threshold
Deﬁnition 2.1 (Label-Hybrid KNN Search). Given a dataset 푆and a
query (푞, 퐿푞), label-hybrid KNN search returns a set 푆′ ⊆푆(퐿푞) of 푘
entries such that for every (푥푖, 퐿푖) ∈푆′ and every (푥푗, 퐿푗) ∈푆(퐿푞),
훿(푥푖,푞) ≤훿(푥푗,푞), where 훿(·, ·) denotes the vector distance (We
use the Euclidean distance as an example).
Exact 푘-nearest neighbor search often suﬀers from the curse
of dimensionality [27], causing traditional methods [3, 4, 39] that
perform well in low-dimensional spaces to degrade greatly in high-
dimensional settings. As a result, approximate 푘-nearest neighbor
(AKNN) search has been extensively studied [1, 7, 10, 11, 13–17, 29,
30, 33, 37, 45, 47, 51, 53, 58] for its ability to improve eﬃciency at
the cost of some accuracy. This challenge also arises in the context
of label-hybrid search. Therefore, we focus on the label-hybrid
approximate 푘-nearest neighbor (AKNN) problem. To evaluate the
result quality, we use recall as the metric, deﬁned as recall = | ˆ푆∩
푆′|/|푆′|, where 푆′ is the exact result set and ˆ푆is the approximate
result set with | ˆ푆| = 푘. An eﬀective label-hybrid AKNN solution
should balance high search eﬃciency with high recall.
Remark. Label-hybrid AKNN search can be viewed as a special
case of ﬁltered nearest-neighbor search [18], where our ﬁltering
requires the label set 퐿푖of a base vector to contain the query-label
set 퐿푞. Other variants—such as set intersection (퐿푞∩퐿푖≠∅) or set
equality (퐿푞= 퐿푖)—can be addressed using the solutions developed
for our studied AKNN search problem. A detailed analysis appears
in our technical report [54].
2.2
Existing Solutions
To address the label-hybrid AKNN search problem, existing ap-
proaches typically combine diﬀerent types of AKNN indexes
with ﬁlter-based search strategies. Among these, graph-based in-
dexes [11, 25, 29, 34, 37, 41] are widely adopted due to their state-
of-the-art search eﬃciency. In such indexes, base vectors in the
dataset 푆are treated as nodes in a graph, where each node is care-
fully connected to its nearby vectors, enabling eﬃcient navigation.
619

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
Query Vector 2
Query Vector 1
Figure 3: The Example of Filter-Based Search
Graph Index. When a graph is used as an index, search starts
from a designated entry node and iteratively explores neighbors
that are progressively closer to the query until the top-1 result
is found. A key index design component is the edge-occlusion
strategy, which guides the search toward the query while keeping
the graph sparse and the node degree bounded by a constant [11,
28]. For AKNN search—where more than one result is required—
beam search is typically employed. It maintains the top-푚closest
candidates during traversal, where푚is the beam width. Under ideal
indexing conditions (i.e., high index quality and the query present
in the dataset) [11, 28], only one extra expansion step on the graph
is needed to retrieve the next neighbor, yielding an overall search
complexity logarithmic in the dataset size.
Filter-Based Search. On top of the graph index, we introduce
two ﬁlter-based search strategies—PreFiltering and PostFiltering—
to support label-hybrid AKNN search. Both strategies maintain
the original index structure and require only label-based ﬁltering
during graph traversal. (1) PreFiltering strategy: Given a query
(푞, 퐿푞), PreFiltering prunes unmatched nodes and their neighbors
during traversal. As shown in Fig. 3, suppose nodes 푥4 and 푥5 do
not satisfy the label constraint in query 1. In this case, PreFiltering
removes the outgoing edges of 푥5, thereby making the true nearest
neighbor 푥6 unreachable from the entry node 푥1. (2) PostFiltering
strategy: This method allows traversal through unmatched nodes
but excludes them from the result set. For instance, when processing
query 2 in Fig. 3, PostFiltering visits nodes 푥1,푥2,푥3,푥4, and 푥7.
Although 푥4 is label-unmatched, its outgoing edge is still used for
routing, enabling the algorithm to eventually reach and return 푥7.
For top-푘queries, PostFiltering traverses incrementally, visiting
successive nearest neighbors until 푘matching results are found.
However, if most nodes are label-mismatched, the algorithm may
still need to examine up to 푂(푁) = 푂(|푆|) nodes to obtain 푘
results, because these ﬁltered-out nodes must still be visited for
PostFiltering. PreFiltering faces a similar challenge—under low se-
lectivity, it may fail to reach any valid neighbors from the entry
node, making its correctness and eﬃciency diﬃcult to guarantee.
3
ELASTIC INDEX SELECTION PROBLEM
This section formally deﬁnes the index selection problem, aiming to
overcome the exhaustive cost in indexing space and time caused by
exponential label combinations. In particular, we ﬁrst introduce the
elastic factor, which captures how an index built on a query-label
set can be reused for its supersets. We then formally deﬁne the index
selection problem as a decision problem: determining whether it
is possible to select a subset of label sets for indexing such that
both space usage and query eﬃciency remain within given bounds.
Finally, we prove that the problem is computationally hard.
3.1
Elastic Factor for Index-Sharing
Given an alphabet Σ, one could in principle build an index for
every label set in the power set of Σ, but the number of such sets
is exponential. Fortunately, index sharing can dramatically reduce
the required indexes. Speciﬁcally, an index built on a query-label
set 퐿2 can serve as a substitute for another query-label set 퐿1 if
it contains all base vectors associated with 퐿1. This holds when
퐿2 ⊆퐿1, because every vector matching 퐿1 also matches its subset
퐿2. For example, the group labeled {퐴} includes all entries from its
superset group {퐴퐵}, since any vector labeled {퐴퐵} also contains
label {퐴}. Thus, a query with label set {퐴퐵} can be answered using
an index built on {퐴}, allowing indexes for some query-label sets
to be omitted and reducing storage costs using label containment.
Elastic Factor. We next analyze the query eﬃciency provided by
shared indexes. For simplicity, assume we already have a subset of
indexes I = {퐼1, . . . , 퐼푚} built on a selected subset of possible query-
label sets (we discuss how to choose this subset later). Each index
퐼푖∈I contains the base vectors in 푆that match some query-label
set 퐿푖; we sometimes use 퐼푖to refer directly to these label-matched
vectors. An index can answer a query with label set 퐿푞only if it con-
tains all base vectors matching 퐿푞, following the label-containment
rule described earlier. Intuitively, if the set 푆(퐿푞) of vectors match-
ing the query-label set 퐿푞is fully contained in some index 퐼∈I
and overlaps heavily with 퐼, query eﬃciency is high because few
label-mismatched vectors remain. This intuition motivates the elas-
tic factor: the maximum overlap ratio, across all indexes in I that
can answer query 푞, between 푆(퐿푞) and the index used.
Deﬁnition 3.1 (Elastic Factor). Given a label-hybrid dataset 푆, a
query (푞, 퐿푞), and an index set I = {퐼1, . . . , 퐼푚} where each 퐼푖⊆푆,
the elastic factor of I with respect to 퐿푞is deﬁned as:
푒(푆(퐿푞), I) =
max
푆(퐿푞)⊆퐼푖
|푆(퐿푞)|
|퐼푖|
.
We only use indexes 퐼푖satisfying 푆(퐿푞) ⊆퐼푖, as they contain all
vectors matching 퐿푞and can thus answer query 푞without omission.
The elastic factor is the maximum overlap ratio |푆(퐿푞)|/|퐼푖| between
푆(퐿푞) and any such index 퐼푖∈I. When I contains a single index 퐼푖,
we also denote the elastic factor between 퐿푞and 퐼푖as 푒(푆(퐿푞), 퐼푖).
To guarantee a non-zero elastic factor, we include a top index 퐼⊤
built over all base vectors in 푆without label ﬁltering, ensuring
푆(퐿푞) ⊆퐼⊤. The elastic factor lies in (0, 1]: it is 1 when an index
is built exactly on the query-label set 퐿푞and decreases toward 0
when the chosen index contains more label-mismatched vectors.
Example 1. Figure 4(a) lists the vector groups with their associated
label sets in the database 푆. Three vectors have no labels (Empty[3])
and three have the label set {퐴퐵퐶}[3]. Figure 4(b) shows the number
of vectors matched by each possible query–label set. For example,
a query with label set 퐿푞= {퐴} matches all vectors labeled {퐴},
{퐴퐵}, {퐴퐶}, or {퐴퐵퐶}, totaling 10 vectors. Building index 퐼2 on
these 10 vectors (label set {퐴}) supports not only 퐿푞= {퐴} but also
queries such as 퐿푞= {퐴퐵}, since it contains all required base vectors.
Figure 4(c) illustrates index sharing with an elastic factor of 0.3. Index
퐼2 can answer the query 퐿푞= {퐴퐵퐶} because the overlap ratio is
3/10 = 0.3, whereas index 퐼1 (label set ∅) cannot, as its overlap ratio
3/17 is below the threshold.
620

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
(a): Number of Vectors
Associated with Label Set
I1:{Empty, A, B, C, AC}
I2:{A, AB, AC, ABC}
I3:{B, AB, BC, ABC}
I4:{C, AC, BC, ABC}
I5:{AB, ABC}
I6:{AC, ABC}
I7:{BC, ABC}
I8:{ABC}
(c): The Index Sharing with
an Elastic Factor of 0.3
(b): Number of Vectors Matched
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
푒= 0.001
푒= 0.01
푒= 0.1
푒= 1
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
Figure 5: The eﬀect of the elastic factor on query eﬃciency (푘= 10:
left, 푘= 20: right). We randomly generate label sets for both base
and query vectors and build HNSW indexes on the SIFT100M dataset.
Queries are grouped by elastic factor—0.001, 0.01, 0.1, and 1—with
푒= 1 serving as the optimal baseline.
Connection to Query Eﬃciency. In the extreme, one could per-
form ﬁlter-based search (e.g., PostFiltering) on the top index 퐼⊤,
which contains all entries and whose label set (∅) is a subset of every
query-label set. However, this is ineﬃcient for queries with non-
empty 퐿푞because the search must traverse many label-mismatched
nodes in the graph index. The ineﬃciency arises because the elastic
factor of 퐼⊤is typically very low for such queries. To raise the elastic
factor, additional indexes must be added to I. The key question then
becomes: when the elastic factor of I for a query 푞is at least 푐, what
is the resulting query cost?
To answer this, we ﬁrst analyze the expected cost of KNN search
(ignoring label constraints). To retrieve the 푘nearest neighbors, we
ﬁrst use the index to locate the top-1 result and then incrementally
continue the search from each previously discovered neighbor until
all 푘results are obtained (e.g., extending from top-1 to top-2, and
so on). Thus, the eﬃciency of KNN search depends on the expected
number of steps required to obtain 푘valid results. When using
an index built over the entire dataset 푆(as in PostFiltering), the
expected number of steps E(푟) can grow to 푂(푁) under low query
selectivity, where 푁= |푆|, explaining its ineﬃciency. In contrast, if
the query is answered using an index with a constant elastic factor
푐, the expected number of steps to obtain 푘results is bounded by
푂(푘/푐) [40, 52]. This bound holds because, in graph-based indexes,
retrieving the next nearest-neighbor/result typically requires only
one additional expansion step after locating the current result,
under ideal conditions (e.g., slow growth or when the query is
contained in the dataset). Extending this reasoning to our label-
hybrid AKNN search, we still ﬁrst locate the top-1 nearest neighbor
and then continue the search until all 푘results satisfying the label
constraint are retrieved. We show that, apart from the cost of ﬁnding
the ﬁrst neighbor, the additional cost of retrieving the remaining
results remains 푂(푘/푐), as formalized in Lemma 3.2.
Lemma 3.2. Given a dataset 푆, a query (푞, 퐿푞), and a selected index
set I, let 푂(퐶) denote the expected time to retrieve the top-1 neighbor
from a graph index. If the elastic factor satisﬁes 푒!푆(퐿푞), I" ≥푐for
some constant 푐∈(0, 1], then the expected time to obtain the top-푘
label-hybrid AKNN results with PostFiltering is 푂
#
퐶+ 푘
푐
$
.
Proof sketch. We make two assumptions. (i) After incurring a
cost of 푂(퐶) to locate the top-1 nearest neighbor, each successive
nearest neighbor can be retrieved in amortized constant time. Note
that we do not require the visited neighbors to satisfy the query’s
label constraints, and the property holds for most graph indexes.
(ii) Conditioned on distance ordering, at least a fraction 푐of the
visited nearest neighbors satisfy the query’s label constraints, guar-
anteed by 푒(·, ·) ≥푐. Since PostFiltering outputs a vector only if it
matches the query label set, more than 푘neighbors may need to be
examined. Let푇denote the number of (steps to examine) neighbors
until 푘matches are found. 푇follows a negative-hypergeometric (or
geometric) distribution with success probability at least 푐, which
yields E[푇] ≤푘/푐. Including the cost 푂(퐶) for locating the ﬁrst
neighbor, the total expected cost is bounded by 푂(퐶+ 푘/푐).
Remark on Lemma 3.2. The above result provides an expected
bound under two standard assumptions: (1) locating the top-1 near-
est neighbor costs 푂(퐶), and enumerating additional neighbors re-
quires amortized constant work; (2) Label-matched and mismatched
vectors are assumed to be uniformly distributed in the graph and in-
dependent of the labels, ensuring that at least a 푐-fraction of visited
vectors satisfy the label constraint (as guaranteed by the elastic fac-
tor). This bound is not a worst-case guarantee, as these assumptions
may not always hold for graph indexes such as HNSW. Neverthe-
less, the elastic factor 푐remains a useful control knob for expected
performance, because the additional time cost scales directly with
푘/푐. Our empirical studies (Fig. 5) conﬁrm this monotonic relation-
ship: larger elastic factors yield higher query throughput (Qps) at
ﬁxed recall, and the observed performance drop as 푐decreases.
Empirical studies. To further validate Lemma 3.2, we measure the
practical eﬃciency of PostFiltering search under varying elastic
factors on the 100M subset of the SIFT1B dataset [2], denoted
SIFT100M. In the classical PostFiltering strategy, the elastic fac-
tor is ﬁxed as it uses only the top index containing all base vectors.
Here, we add more indexes to I to vary the elastic factor and report
results in Fig. 5. As shown, higher elastic factors yield better eﬃ-
ciency (measured in Qps), consistent with the lower time complexity
predicted by theory. When the factor reaches 1, search achieves
optimal eﬃciency—equivalent to indexing only the label-matched
data. Eﬃciency decreases sublinearly as the elastic factor drops: for
example, reducing the factor to 1
10 lowers throughput by only 2↑
at 98% recall with 푘=10. This occurs because the added cost scales
621

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
Figure 6: The Example of the EIS Problem
with 푘, while the top-1 search still runs in 푂(퐶), usually 푂(log 푁).
The gap widens for larger 푘(e.g., a 3↑slowdown at 푘=20 and 98%
recall), but in most applications 푘is small relative to 푁. Thus, as
long as the elastic factor remains constant, the overall time cost
stays bounded.
3.2
Problem Deﬁnition
We have analyzed the impact of the elastic factor on search eﬃ-
ciency for a given index set I. When using I, the expected query cost
is directly tied to the elastic factor of I for each query. If the elastic
factor for every query in the workload is guaranteed to exceed a
constant lower bound 푐, then the AKNN search cost is bounded by
an additional 푂(푘/푐). This establishes a clear relationship between
index cost (the total size of indexes in I) and query eﬃciency (cap-
tured by the elastic factor). Building on this, among all subsets of
indexes that can correctly answer the query workload, we seek a
subset I that is both small and yields a large elastic factor, ensuring
strong query performance. This motivates the formal deﬁnition of
the Elastic Index Selection (EIS) problem.
Deﬁnition 3.3 (Fixed Eﬃciency Index Selection (EIS)).
Input A label-hybrid dataset 푆, a query workload with label
sets L = {퐿1, . . . , 퐿푛}, and a universal index set I =
{퐼1, . . . , 퐼푛}, where each index 퐼푖= 푆(퐿푖) is built over the
base vectors matching the query-label set 퐿푖with cost
|퐼푖|. Let 휏and 푐be non-negative real numbers.
Output Decide whether there exists a subset I ⊆I such that
푒!푆(퐿푖), I" ≥푐for every 퐿푖∈L and the total cost satis-
ﬁes %
퐼∈I |퐼| ≤휏.
The EIS problem is a decision problem: determine whether a
subset of indexes from the universal set I can be chosen such that
(1) the total space usage is within a user-speciﬁed bound 휏, and
(2) the elastic factor—which governs query eﬃciency—is at least a
given threshold 푐. In Section 4, we present the optimization version,
where the goal is either to minimize space cost while satisfying a
required elastic-factor threshold or to maximize the elastic factor
subject to a space budget.
Example 2. The EIS problem under diﬀerent elastic-factor thresh-
olds, 푐= 0.5 and 푐= 0.3 is in Fig. 6. When 푐= 0.5, the top index 퐼⊤
(built on the empty label set) can serve queries with 퐿푞= {퐴} and
퐿푞= ∅, since their overlaps exceed 0.5. It cannot, however, support
퐿푞= {퐵}, whose overlap is only 0.45. With a more relaxed threshold
of 푐= 0.3, 퐼⊤also supports 퐿푞= {퐵} and 퐿푞= {퐶}, but it still cannot
answer 퐿푞= {퐴퐵}, whose overlap is 0.25, unless 푐< 0.25. Finally,
when 푐= 0, the top index can serve all possible queries.
Remark on Query Workload. In the problem deﬁnition, we as-
sume a query workload with query-label sets L푞= {퐿1, . . . , 퐿푛}.
This assumption is justiﬁed for two reasons: (1) It generalizes the
traditional setting where L푞includes all subsets of the alphabet Σ
(i.e., the power set). (2) It enables a workload-driven approach: L푞
can reﬂect historical query patterns, allowing index selection and
optimization to match realistic query behavior.
Although the total number of possible query-label sets can reach
2|Σ| in the worst case, the eﬀective number is typically far smaller
due to mutual exclusivity among certain labels. For example, a prod-
uct has only one brand name, making brand labels mutually exclu-
sive. Our formulation explicitly captures such cases, ensuring that
incompatible label combinations—like diﬀerent brand names—do
not co-occur in L푞. We also assume the workload always includes
the empty set ∅, representing queries with no label constraints and
thus matching all base vectors. For simplicity, we treat the cost of
this top index as zero, i.e., |퐼top| = 0.
Remark on Index Size/Cost. The cost of each index 퐼푖(built for
query-label set 퐿푖) reﬂects the space it occupies. For graph-based
indexes, this cost can be approximated by the number of vectors it
contains, since node degree is typically bounded by a constant [11,
28]. In practice, each node maintains 푀edges to support eﬃcient
memory access, where 푀is a user-deﬁned parameter [36]. Thus,
the overall space usage of an index can be estimated as the product
of its vector count and 푀. In our study, however, we measure index
size/cost |퐼| simply by the number of vectors it contains.
Index Selection at Query Time. At query time, a label set 퐿푞may
be served by multiple indexes in the selected set I. For example, a
query with 퐿푞= {퐴퐵} can be answered by the index for {퐴} or
for {퐵} if both are present in I. In such cases, our method selects
the index that yields the maximum elastic factor with 푆(퐿푞). This
choice improves query eﬃciency because greater overlap means
fewer label-mismatched vectors in the index, reducing the cost of
scanning irrelevant entries.
3.3
Problem Hardness
We prove that the EIS problem is NP-complete in Theorem 3.4,
underscoring the computational challenges inherent in solving it.
Theorem 3.4. The EIS problem is NP-complete.
Proof sketch. We show EIS is NP-complete by reducing from
the 3-Set Cover (3-SC) problem [8, 31]. EIS is in NP as validity is
veriﬁable in polynomial time. Given a 3-SC instance with elements
U, subsets S, and budget 푘, we construct an EIS instance with a
universal index set I containing: (1) an element index 퐼푢푖for each
푢푖∈U; (2) a subset index 퐼푠푗for each 푠푗∈S; (3) a top index 퐼⊤
covering all base vectors; and (4) a bottom index 퐼all.
Let푏> 3. We set the index sizes to |퐼푢푖| = 퐴= 푏+1, |퐼푠푗| = 퐵= 2푏,
|퐼⊤| = 푁> 4푏, and |퐼all| = 푏. We set the elastic factor 푐= 2푏/푁and
the cost budget 휏= 푘· 퐵. The construction ensures the following
overlap properties: (1) 퐼⊤covers every subset index 퐼푠푗with ratio
퐵/푁= 푐, but fails to cover element indexes 퐼푢푖suﬃciently (ratio
퐴/푁< 푐). (2) A subset index 퐼푠푗covers an element index 퐼푢푖with
ratio 퐴/퐵> 푐if and only if 푢푖∈푠푗. (3) To enforce cost constraints,
we introduce duplicate elements 푢′
푖such that covering a speciﬁc
element 푢푖directly via element indexes requires selecting both 퐼푢푖
622

## PDF page 7

and 퐼푢′
푖(total cost 2퐴= 2푏+ 2). However, covering 푢푖via a relevant
subset index 퐼푠푗costs only 퐵= 2푏. Since 2퐴> 퐵, it is strictly
cheaper to cover elements using subset indexes. The detailed proof
is available in our technical report.
The EIS problem is a decision problem with a yes/no answer. In
practice, however, one often seeks to optimize either space usage or
query time, as these are the key factors in deploying AKNN search.
We therefore study two optimization variants of EIS: (1) Eﬃciency-
constrained variant that minimizes total space while enforcing
a lower bound on the elastic factor and (2) Space-constrained
variant that maximizes the elastic factor subject to a given space
budget. Because EIS is NP-complete, both variants are NP-hard. We
next show how to address these variants.
4
PROBLEM SOLUTION
We ﬁrst solve the eﬃciency-constrained EIS problem, then extend
it to the space-constrained variant, and conclude with a discussion.
4.1
Method for Eﬃciency-constrained Variant
This optimization version of the EIS problem resembles Deﬁni-
tion 3.3, but aims to compute the following output:
Output A subset I ⊆I such that 푒(푆(퐿푖), I) ≥푐for all 퐿푖∈L,
and the total cost %
퐼∈I |퐼| is minimized.
Since this variant of the EIS problem is NP-hard, we employ
a greedy algorithm to obtain an approximate solution. The basic
idea is as follows. Given a collection of indexes I (corresponding
to a query workload with label sets L), each index 퐼∈I has
an associated space cost |퐼|, measured by the number of matched
vectors it contains. We deﬁne a beneﬁt function for each index and
iteratively select the index with the highest beneﬁt. By deﬁnition of
the elastic factor, adding more indexes improves it and eventually
covers all label sets in L (if all are selected). Yet, to minimize total
cost, selection must be made carefully to ensure solution quality.
A key preprocessing is that we always include the top index 퐼⊤in
the solution. As noted in § 3.2, this guarantees that every query
has at least one index covering all of its matched vectors, ensuring
correctness. Thus, the answer set I is initialized with 퐼⊤. The beneﬁt
of adding a candidate index 퐼∈I \ I to the current solution I is
deﬁned as 퐵(퐼, I), which is deﬁned below.
Deﬁnition 4.1. Let C ⊂I be the set of indexes already covered
by the selected set I with overlap ratio at least 푐, where 푐is the
elastic–factor threshold: C =
&
퐼푖∈I
'' ∃퐼푗∈I : (퐼푖⊆퐼푗) ∧
!|퐼푖|/|퐼푗| ≥푐"(
. Let C′ be the set of indexes newly covered by adding
퐼, also with overlap ratio at least 푐: C′ =
&
퐼푖∈I \ C : (퐼푖⊆퐼) ∧
!|퐼푖|/|퐼| ≥푐"(
. The beneﬁt of adding 퐼to I is 퐵(퐼, I) = %
퐼푖∈C′ |퐼푖|
|퐼| .
The beneﬁt captures two aspects: (i) the number of previously
uncovered indexes that the candidate index 퐼can cover (with over-
lap ratio at least 푐, i.e., the set C′), and (ii) the actual overlap ratio
between 퐼and each newly covered index 퐼푖∈C′. In other words,
the beneﬁt is the total overlap ratio of 퐼with all indexes in C′.
Algorithm. We implement the greedy method in Algorithm 1. First,
we add the top index 퐼⊤to the answer set I (Line 1). At each iteration,
we select the next candidate index—each candidate 퐼corresponds
to a query–label set 퐿푞in the workload—based on its beneﬁt score
Algorithm 1: Greedy Algorithm
Input: The Universal Index Set I, The Elastic Factor 푐
Output: The Selected Index Set I
1 I ←{퐼⊤};
2 while Stop Condition Not Met do
3
퐼←퐼∈I \ I such that 퐵(퐼, I) is maximum;
4
I ←I ∪{퐼};
5
if For all 퐿푞∈L, 푒(푆(퐿푞), I) ≥푐) then Break;
6 return I;
// Selected Index Set
Table 3: The Beneﬁts of Each Candidate Index in Each Round
Init Round
Second Round
Third Round
퐼1
49 / 17 = 2.88
퐼2
23 / 10 = 2.30
7 / 10 = 0.70
0 / 10 = 0.00
퐼3
19 / 7 = 2.71
12 / 7 = 1.71
5 / 7 = 0.71
퐼4
23 / 9 = 2.55
14 / 9 = 1.55
5 / 9 = 0.55
퐼5
7 / 4 = 1.75
7 / 4 = 1.75
퐼6
9 / 6 = 1.50
3 / 6 = 0.50
0 / 6 = 0.00
퐼7
8 / 5 = 1.60
8 / 5 = 1.60
5 / 5 = 1.00
퐼8
3 / 3 = 1.00
3 / 3 = 1.00
0 / 3 = 0.00
(Lines 3–4). The process stops when all label sets in L are covered,
yielding a valid solution (Line 5).
Example 3. Using the example dataset in Fig. 4, we evaluate the
beneﬁt of diﬀerent index choices, with Table 3 reporting the beneﬁt ra-
tio for each candidate index. Note that the top index 퐼1 = 퐼⊤is always
selected ﬁrst, regardless of its beneﬁt. Selecting 퐼1 covers the vectors
in 퐼2, 퐼3, 퐼4, 퐼6, and itself (with overlap ratio at least the elastic–factor
threshold 푐= 0.3), for a total of 17 + 10 + 7 + 9 + 6 = 49 vectors at
a cost of 17, yielding a beneﬁt of 49/17 ′ 2.88. After 퐼1 is selected,
the beneﬁts of the remaining candidates change. Initially, 퐼2 covers
퐼2, 퐼5, 퐼6, and 퐼8, i.e., 23 vectors at a cost of 10, for a beneﬁt of 2.3.
In the second round, as 퐼1 already covers 퐼2 and 퐼6, 퐼2 can now cover
only 퐼5 and 퐼8, reducing its beneﬁt to 0.7. The greedy algorithm next
selects the index with the highest updated beneﬁt (Fig. 4(d)), which
is 퐼5. At this point, 퐼1 and 퐼5 cover all vectors except those in 퐼7. The
third round selects 퐼7, with a cost of 5. The greedy solution, therefore,
has a total cost of 17 + 4 + 5 = 26, which is not optimal. As shown in
Fig. 4(e), the optimal solution instead selects 퐼3 in the second round.
Although 퐼3 covers only 퐼5, 퐼7, and 퐼8 with a lower beneﬁt of 1.71 (due
to overlap with 퐼1), it achieves full coverage using just two indexes,
퐼1 and 퐼3, for a total cost of 17 + 7 = 24, outperforming the greedy
approach.
Implementation Details and Cost Analysis. To make the greedy
method practical, we ﬁrst create an index in I for each query–label
set in L. In theory, there can be up to 2|Σ| query–label sets—and
thus the same number of indexes—in the worst case. However,
given a query workload L, we only need to compute the sizes of
푂
#
min
#
2|Σ|, |I|
$$
indexes during preprocessing, where |I| = |L|
When the labels in the alphabet Σ follow a power–law distribu-
tion (as in many real-world datasets), this preprocessing cost is
empirically a small fraction of the total runtime. For other label
distributions, however, the overhead can be substantial. To handle
such cases, following [23], we estimate the size of some index 퐼in
623

## PDF page 8

I under a query–label set 퐿푞by sampling or advanced cardinal-
ity–estimation techniques [24], without fully materializing 퐼. Once
index sizes are approximated, we can eﬃciently compute the ben-
eﬁt of each candidate and apply the greedy algorithm. To further
improve performance, we maintain a max-heap to track the index
with the highest beneﬁt at each iteration. As selecting an index can
aﬀect the beneﬁts of up to |I| related indexes, we check whether
the heap’s top entry has become stale; if so, we update its beneﬁt
and reinsert it. This strategy keeps the practical runtime well below
푂(|I|2) — the cost incurred by the greedy method.
4.2
Method for Space-Constrained Variant
In the previous section, we addressed the eﬃciency-constrained
index-selection problem. We now turn to selecting indexes that
maximize query eﬃciency while satisfying a space constraint. This
optimization variant of the EIS problem follows Deﬁnition 3.3, but
focuses on computing the following output:
Output A subset I ⊆I such that the total cost %
퐼∈I |퐼| ≤휏, and
the minimum elastic factor min(푒(푆(퐿푞),푒(I)) over all
퐿푞∈L is maximized.
Insight. We observe that the elastic–factor threshold 푐is mono-
tone for the EIS problem. For example, if an index set I satisﬁes a
threshold of 0.5 for a query workload L, it also satisﬁes any smaller
threshold 푐′ < 0.5. This holds because the threshold 푐bounds the
overlap between the indexes in I and any label set in L, requiring
the overlap to be at least 푐. Thus, if the stricter bound 푐is met, any
looser bound 푐′ is automatically satisﬁed.
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
eﬃciency–constrained
counterpart and apply a binary search to ﬁnd a set I whose
elastic factor exceeds a given 푐while keeping the total cost within
the budget 휏. We perform the binary search over 푐∈(0, 1] in
decreasing order. At each step, for a candidate value of 푐, we
invoke Algorithm 1 to check if the returned index set I has total
cost at most 휏. The search stops once such a set is found, yielding
the maximum achievable elastic factor under the space constraint.
Overall, this procedure requires 푂
#
log 1
휀
$
calls to Algorithm 1,
where 휀is the resolution for the binary search (set to 0.001 in
our paper). In practice, the overhead of combining binary search
with the greedy method is negligible—typically under 1% of the
index-construction time, or about 1–2 seconds even for large
workloads L.
4.3
Discussions
Limitation. For either eﬃciency- or space-constrained EIS prob-
lem, our greedy methods output a subset of indexes I from the uni-
versal index set I, where each index corresponds to a query–label
set in the workload L. The eﬃciency of our approach for AKNN
search stems from controlling either space or query time through
index sharing based on label-containment relationships. In extreme
cases, however, index sharing may be ineﬀective. For example, if
the query workload consists of label sets that are completely dis-
joint, there is no containment relationship between them. We must
then either create a separate index for each disjoint query–label set
(resulting in high space cost) or build a single index covering all
vectors in the database (resulting in a small elastic factor). Fortu-
nately, in real-world scenarios, when query–label sets are disjoint,
their base vectors are typically disjoint as well, so building an index
for each query–label set remains aﬀordable, requiring only 푂(푁)
space. By contrast, algorithms such as UNG, ACORN, and Filter-
Di-.ANN are unaﬀected by this limitation as they do not rely on
index sharing and may perform better in such cases.
Extensions to Dynamic Scenarios. In real-world deployments,
both the database vectors and the query workload may evolve over
time. We describe how our method accommodates such updates.
Database Vector Dynamics. Vectors can be inserted into or deleted
from the database. We focus on insertions, since deletions can be
handled by marking a vector as ignored and continuing the search
until all results are found [10, 29, 36]. When new vectors arrive, we
insert each into every index 퐼푖(built on query–label set 퐿푖) whose
labels match the vector. For queries, we continue using the original
selected index set I, but periodically check for data changes and
update the indexes once 10–20% new vectors have been added.
Query Workload Dynamics. When new label sets 퐿푞appear in the
query workload, we ﬁrst collect all matching vectors for each 퐿푞. If
fewer than 4, 000 (a threshold determined by experiments) are col-
lected, we scan them directly to answer the new query. Otherwise,
we select indexes from the set I built on label sets that are subsets of
퐿푞. Note that the top index 퐼⊤is always available (as ∅is a subset of
any query-label set). If multiple indexes qualify, we choose the one
with the maximum overlap with the collected vectors to answer the
query. We selectively create an index for the new label set in the
query workload (e.g., when the number of collected vectors exceeds
4,000), but adjust set I only periodically to maintain eﬃciency.
Index Updates. For both types of dynamics, we postpone structural
updates to the current selected index set I and process them in
batches. (1) For the eﬃciency-constrained variant: We maintain
the current set I as long as it satisﬁes the elastic–factor constraint.
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
conﬁrm the eﬃciency of this update strategy.
5
EXPERIMENT
5.1
Experiment Settings
Datasets. We evaluate on 8 publicly available datasets (see Ta-
ble 4) that are standard benchmarks for AKNN search, as well as
datasets generated by state-of-the-art embedding models such as
MSMARCO, OpenAI-1536 [53], TripClick, and Arxiv [26]. To exam-
ine scalability, we also sample 100M vectors (denoted as DEEP) from
624

## PDF page 9

UNG
ELI-0.2
ELI-2.0
ACORN-1
ACORN-훾
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
Figure 7: The Eﬃciency Comparison Across Methods
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
the Deep1B [12, 53]1. For label annotations, datasets PAPER, LAION,
TripClick, OpenAI-1536, and Arxiv supply real labels or metadata.
For others, we synthesize labels following prior work [5, 18]. Label
sources are grouped into three categories:
(1) Real labels (Paper, TripClick, Arxiv): For Paper, each vector is
labeled by the paper’s venue, research ﬁeld, and institution type. For
TripClick, we use the 28 most frequent clinic categories, consistent
with prior works [5, 40]. For Arxiv, we assign the main subject
categories of each vector as its label.
(2) Metadata-derived labels (LAION, OpenAI-1536): Using vector-
associated metadata (e.g., text or images), we follow the procedure
of [40] to construct a candidate label set and then assign to each
vector its top-3 most relevant labels.
(3) Synthetic labels (SIFT, MSMARCO, DEEP): Since keywords
and tags in real systems typically follow heavy-tailed distributions,
prior work models label frequency using Zipf (power-law) distri-
butions [5, 18]. We reuse publicly available code to generate labels
1www.tensorﬂow.org/datasets/catalog/deep1b
for both base and query vectors under this distribution. In sub-
sequent experiments, we also evaluate alternative distributions
(Uniform, Poisson, and Multinomial) and vary the alphabet size
|Σ| ∈{8, 12, 24, 32} to assess robustness [54].
Metrics. The evaluation covers both eﬃciency and accuracy. For
the search eﬃciency metric, we use Queries Per Second(Qps), which
indicates the number of queries processed by the algorithm per
second, as it is most commonly used in benchmarks. For the search
accuracy, we use recall deﬁned in § 2.1 as the metric to align with
the baselines [5, 40]. All results are reported as averages. We also
report the average selectivity of a query workload. For each query
(푞, 퐿푞), the selectivity is deﬁned as |푆(퐿푞)|/|푆|, i.e., the fraction
of base vectors whose label sets contain 퐿푞. We compute the av-
erage selectivity (denoted Ave. Select) across all test queries in the
workload and annotate it at the top of each ﬁgure to highlight this
value.
Algorithms. We compare the following algorithms:
• ELI-0.2: Our greedy method for the eﬃciency-constrained variant,
where 0.2 denotes the elastic–factor threshold.
• ELI-2.0: Our method for the space-constrained variant, where 2.0
indicates the space bound (at most twice the original dataset size).
• UNG: Uniﬁed Navigating Graph for AKNN search [5].
• ACORN-1: ANN Constraint-Optimized Retrieval Network with
low construction overhead [40].
• ACORN-훾: ANN Constraint-Optimized Retrieval Network for
high-eﬃciency search [40].
• Brute-Force: Scan all label-matched vectors to ﬁnd the results.
625

## PDF page 10

Implementation Details. All code was implemented in C++ and
compiled with GCC 9.4.0 using the -Ofast optimization ﬂag. Ex-
periments were conducted on a workstation equipped with Intel(R)
Xeon(R) Platinum 8352V CPUs @ 2.10 GHz and 512 GB of memory.
We employed multi-threading (144 threads) for index construction
and a single thread for search evaluation. For the underlying index,
we used HNSW [37] with parameters 푀= 16 and efconstruct =
200. For ELI-0.2 (< 1.0), we applied the index-selection method to
achieve a ﬁxed elastic factor of 0.2. For ELI-2.0 (> 1.0), we used the
ﬁxed-space method, allowing at most double the original database
size to maximize eﬃciency. For other baselines, such as ACORN
and UNG, we adopted the default parameters from their respective
papers: 훼= 1.2, 퐿= 100 for UNG; 훾= 80, 푀= 128, 푀퐵= 128
for ACORN-훾on the LAION and TripClick datasets; and 훾= 12,
푀= 32, 푀퐵= 64 for the remaining datasets.
5.2
Experiment Results
Exp-1: Comparison of Query Eﬃciency. We begin by evaluating
query eﬃciency (Qps) across diﬀerent methods. Fig. 7 compares
UNG, ACORN-1, ACORN-훾, and our methods (ELI-0.2, ELI-2.0) un-
der varying alphabet sizes |Σ| and label distributions. Here, Real,
Meta, and Zipf denote the real, meta, and synthetic Zipf-based
label distributions, respectively. Points toward the upper right in-
dicate better performance. Our methods achieve the best trade-oﬀ
between search eﬃciency and accuracy. For example, at 95% recall
with |Σ| = 12, ELI-0.2 delivers a 4↑speedup over UNG on SIFT and
a 10↑speedup on MSMARCO. Performance remains stable across
diﬀerent |Σ| values and real label distributions, whereas UNG’s
retrieval eﬃciency drops sharply as |Σ| grows.
Thanks to the elastic factor, ELI-0.2 sustains nearly 12↑higher
query speed than UNG when |Σ| = 32 on the OpenAI-1536 dataset.
In contrast, ACORN plateaus in accuracy for large |Σ| due to lim-
itations in its PreFiltering strategy. For example, on Fig. 8(a), as
|Σ| increases, ACORN accuracy declines and UNG eﬃciency de-
grades, while ELI-0.2 maintains consistently high eﬃciency. Al-
though larger |Σ| lowers average query selectivity, our design keeps
runtime stable. A modest gap appears between ELI-0.2 and ELI-2.0.
For example, on Fig. 8(b), under a Zipf distribution, the elastic fac-
tor of ELI-2.0 is typically below 0.2 for large |Σ|, so ELI-0.2 often
performs slightly better due to its higher elastic-factor threshold.
Exp-2: Eﬀect of Query Selectivity. To further compare methods,
we vary query selectivity and measure its impact on query eﬃciency.
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
form all other methods across diﬀerent recall levels. These results
validate the elastic-factor cost model: with suﬃciently large 푐(e.g.,
푐≥0.2), the additional computation scales as 푘/푐. We also compare
with brute-force search, which becomes competitive when very
Table 5: The Comparison of Indexing Time (s)
UNG
ACORN-1
ACORN-훾
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
ACORN-훾
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
full set I, verifying the eﬀectiveness of index sharing in reducing in-
dex count. (2) Signiﬁcant reduction in indexed size. The bottom
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
the cmdeﬃciency comparison in Fig. 7. (1) Indexing time. Table 5
shows that our methods (especially ELI-2.0) are far more eﬃcient
than UNG and ACORN-훾. Speciﬁcally, UNG requires nearly 20↑
the indexing time on SIFT and 5↑on MSMARCO, while ACORN-훾
averages about 2↑the indexing time of our methods. Although
626

## PDF page 11

UNG
ELI-0.2
ELI-2.0
ACORN-1
ACORN-훾
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
Figure 8: The Eﬀect of the Query Selectivity (on Arxiv)
/'-


 0)/+)%*
05)4

.)11.-
!-)(.0,
3+2).0,)$+
#)/(

#)/(

	

		


.3-2-&'5





















	








	
















2$2)12)%1.(-&'5.3-2
++-&'5
	-&'5
	-&'5
/'-


 0)/+)%*
05)4

.)11.-
!-)(.0,
3+2).0,)$+
#)/(

#)/(



	

		
.3-2"'%2.0




	







	












	






	




2$2)12)%1.("'%2.0.3-2
++"'%2.0
	"'%2.0
	"'%2.0
Figure 9: The Statistics of Index Selection by Our Methods
ACORN-1 builds indexes quickly, its omission of label information
results in poorer search eﬃciency. (2) Index size. Table 6 reports
index sizes, where Base denotes the size of the raw vectors. On the
OpenAI-1536 dataset, all methods require only about one-tenth of
the total storage space. Our methods achieve the best search per-
formance with a comparable index size. In particular, both ACORN
and ELI-2.0 use roughly twice the space of the original vectors, yet
ELI-2.0 achieves strong search performance with much smaller ad-
ditional space. UNG saves the most space, but in high-dimensional
settings, its index size is only a small fraction of the base vector
dataset, leading to lower search eﬃciency.
Exp-5: Varying Label Distributions. Label distributions inﬂu-
ence label-hybrid AKNN search, so we evaluate performance under
diﬀerent distributions—Zipf, Uniform, Poisson, and Multinomial—
with |Σ| = 12. Experiments are conducted on the Paper dataset, and
results are shown in Fig. 10. (1) Stable performance. Our methods
perform consistently across all tested distributions. In contrast, the
search accuracy of other methods varies signiﬁcantly with the dis-
tribution: under Uniform and Multinomial settings, ACORN fails
to reach 80% recall, while UNG and our methods maintain high
accuracy. (2) High eﬃciency. Across all distributions, our methods
remain highly competitive. They achieve recall comparable to UNG
at high thresholds and deliver 3↑–5↑better search eﬃciency.
Exp-6: Index Update Evaluation. We evaluate the eﬀectiveness
of our index-update strategy on the Arxiv and SIFT datasets. Each
dataset is partitioned into two halves. The ﬁrst 50% of the data,
with label-set size |Σ| = 12, is used to construct the initial index set
I. The remaining 50%, featuring an expanded label set of |Σ| = 24,
simulates the insertion of both new vectors (50% more vectors) and
an enlarged query workload (12 additional labels).
Query Eﬃciency. Fig. 11 reports query eﬃciency. ELI-0.2 and ELI-
2.0 denote the methods rebuilt from scratch using the full dataset,
whereas ELI′-0.2 and ELI′-2.0 represent our batch-update strategy,
which ﬁrst builds indexes on the initial 50% of data and then in-
crementally updates them. The results show that periodic updates
incur less than a 5% drop in query eﬃciency compared with full
re-computation, demonstrating that the batch update mechanism
preserves practical performance.
Update Cost. Figure 12 compares the update cost of ELI′-0.2 and
ELI′-2.0 against rebuilding from scratch. The left panel shows up-
date time: the rightmost bar corresponds to full re-computation on
100% of the data, while the three bars to the left represent incremen-
tal insertions of 10%, 20%, and 50% new vectors into the initial index
built on the ﬁrst 50% of the data. Update time naturally increases
with the number of inserted vectors but remains well below the cost
of rebuilding, even when 50% of the data is added. The right panel
reports space usage (in units of updated vectors). As more vectors
are inserted, space usage grows slightly but remains smaller than
that of full re-computation. Notably, both our methods, especially
ELI′-2.0, use signiﬁcantly less space than their rebuilt counterparts.
Overall, these experiments show that our incremental update
strategy achieves signiﬁcantly lower update time and space over-
head compared with rebuilding from scratch(Fig. 12), while incur-
ring only a negligible loss in query eﬃciency (Fig. 11).
6
RELATED WORK
Attribute-Hybrid AKNN Search. When vectors carry label at-
tributes (as deﬁned in this paper) or numerical attributes (e.g., [50]),
an attribute-hybrid AKNN search can be deﬁned to return ap-
proximate 푘-nearest neighbors whose attributes satisfy given con-
straints. Such searches can be handled using either attribute-free
or attribute-dependent solutions.
Attribute-Free Solutions. These methods ignore attributes during in-
dex construction and consider them only at query time. Speciﬁcally,
the PreFiltering and PostFiltering search strategies are widely used,
each performing diﬀerently under varying query workloads. Sys-
tems such as ADB [48], VBASE [57], CHASE [35], and Milvus [43]
627

## PDF page 12

UNG
ELI-0.2
ELI-2.0
ACORN-1
ACORN-훾
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

	"%
	"%

	"%
&
	



	



	


	
 '$$
!"#$! ! # %"%
	"%
	"%

	"%
	"%

	"%
&
	

	

		


	
		

	
 '%!#
!"#$! ! # %"%%!#
	"%
	"%
Figure 12: The Time and Space Cost of Incremental Updates
choose between these strategies using cost models. ACORN also
follows the PreFiltering strategy but builds a denser graph index to
mitigate its connectivity issues. Because these approaches ignore
attributes during index construction, their search accuracy and
eﬃciency lag behind methods that incorporate attributes directly.
Attribute-Dependent Solutions. These methods build indexes by con-
sidering the attribute type. (1) For attribute-hybrid AKNN search
with numerical attributes, recent work [9, 50, 52] organizes query-
speciﬁed numeric intervals using segment trees, while SeRF [59]
further compresses indexes across overlapping intervals. (2) For
attribute-hybrid AKNN search with label attributes, UNG [5] ex-
ploits label-set inclusion, adding cross-group edges so the graph
index searches only ﬁltered vectors. NHQ [44] incorporates at-
tribute similarity into vector similarity, requiring manual tuning of
vector and attribute weights.
Data Cube Computation. Existing studies on OLAP and data-cube
computation select group-by aggregates over the subset lattice of di-
mensions, exploiting the fact that coarser aggregates (supersets) can
be derived from ﬁner-grained materializations (subsets) [20, 23, 38].
Our setting shares this lattice structure and the reuse principle: an
index built for a query-label set can serve queries with superset
labels via label containment. However, key diﬀerences remain: (1)
Cube computation relies on algebraic properties of aggregates (e.g.,
distributive or algebraic measures) to derive superset results exactly
from subsets, whereas our reuse is containment-based, yielding a
query cost bounded by 푂(퐶+ 푘/푐) on graph-based indexes, where
푐is the elastic factor and 푘/푐is the additional cost. (2) Cube/view
selection minimizes aggregate-query latency subject to storage lim-
its, while our Elastic Index Selection (EIS) problem selects a subset
of query-label sets to index so as to satisfy both space budgets and
workload-wide elastic-factor coverage, ensuring predictable AKNN
eﬃciency under label constraints. (3) Although both problems are
NP-hard and typically approached with greedy heuristics, our ben-
eﬁt function and feasibility constraints are tailored to vector-search
semantics and the label-containment relationship.
Frequent Itemset Mining. Our work is conceptually related to Fre-
quent Itemset Mining (FIM), where labels correspond to items and
label sets to transactions [21, 22, 56]. Both exploit the anti-monotone
property of support (the Apriori principle), with classic algorithms
such as Apriori and FP-Growth widely used for FIM [21, 22]. The
objectives, however, diﬀer fundamentally. FIM ﬁnds all itemsets
whose support exceeds a threshold for pattern discovery, whereas
our ELI task is an optimization problem: selecting a subset of query-
label sets on which to build indexes. This selection is guided not
by absolute support but by a novel elastic factor modeling query
eﬃciency. Thus, while FIM outputs frequent patterns, our method
produces a concrete indexing plan for AKNN search, making the
formulation and goals distinct.
7
CONCLUSION
This paper investigates the label-hybrid AKNN search problem and
shows that an index built for a query–label set remains eﬀective for
all its supersets with bounded query time. Building on this insight,
we formalize the Elastic Index Selection (EIS) problem, prove that
EIS is NP-complete, and introduce two optimization variants solved
by eﬃcient greedy algorithms. Extensive experiments demonstrate
the eﬀectiveness and superiority of our approach. As future work,
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
628

## PDF page 13

REFERENCES
[1] Fabien André, Anne-Marie Kermarrec, and Nicolas Le Scouarnec. 2015. Cache
locality is not enough: High-Performance Nearest Neighbor Search with Product
Quantization Fast Scan. Proceedings of the VLDB Endowment 9, 4 (2015).
[2] Martin Aumüller, Erik Bernhardsson, and Alexander Faithfull. 2020. ANN-
Benchmarks: A benchmarking tool for approximate nearest neighbor algorithms.
Information Systems 87 (2020), 101374.
[3] Norbert Beckmann, Hans-Peter Kriegel, Ralf Schneider, and Bernhard Seeger.
1990. The R*-tree: An eﬃcient and robust access method for points and rectangles.
In Proceedings of the 1990 ACM SIGMOD international conference on Management
of data. 322–331.
[4] Alina Beygelzimer, Sham Kakade, and John Langford. 2006. Cover trees for
nearest neighbor. In Proceedings of the 23rd international conference on Machine
learning. 97–104.
[5] Yuzheng Cai, Jiayang Shi, Yizhuo Chen, and Weiguo Zheng. 2024. Navigating La-
bels and Vectors: A Uniﬁed Approach to Filtered Approximate Nearest Neighbor
Search. Proceedings of the ACM on Management of Data 2, 6 (2024), 1–27.
[6] Thomas Cover and Peter Hart. 1967. Nearest neighbor pattern classiﬁcation.
IEEE transactions on information theory 13, 1 (1967), 21–27.
[7] Mayur Datar, Nicole Immorlica, Piotr Indyk, and Vahab S Mirrokni. 2004. Locality-
sensitive hashing scheme based on p-stable distributions. In Proceedings of the
twentieth annual symposium on Computational geometry. 253–262.
[8] Rong-chii Duh and Martin Fürer. 1997. Approximation of k-set cover by semi-
local optimization. In Proceedings of the twenty-ninth annual ACM symposium on
Theory of computing. 256–264.
[9] Joshua Engels, Benjamin Landrum, Shangdi Yu, Laxman Dhulipala, and Julian
Shun. 2024. Approximate Nearest Neighbor Search with Window Filters. ICML
2024 (2024).
[10] Cong Fu, Changxu Wang, and Deng Cai. 2022. High Dimensional Similarity
Search With Satellite System Graph: Eﬃciency, Scalability, and Unindexed Query
Compatibility. IEEE Trans. Pattern Anal. Mach. Intell. 44, 8 (2022), 4139–4150.
[11] Cong Fu, Chao Xiang, Changxu Wang, and Deng Cai. 2019. Fast Approximate
Nearest Neighbor Search With The Navigating Spreading-out Graph. Proc. VLDB
Endow. 12, 5 (2019), 461–474.
[12] Jianyang Gao, Yutong Gou, Yuexuan Xu, Yongyi Yang, Cheng Long, and Raymond
Chi-Wing Wong. 2024. Practical and Asymptotically Optimal Quantization of
High-Dimensional Vectors in Euclidean Space for Approximate Nearest Neighbor
Search. arXiv preprint arXiv:2409.09913 (2024).
[13] Jianyang Gao and Cheng Long. 2023. High-Dimensional Approximate Nearest
Neighbor Search: with Reliable and Eﬃcient Distance Comparison Operations.
Proc. ACM Manag. Data 1, 2 (2023), 137:1–137:27. https://doi.org/10.1145/3589282
[14] Jianyang Gao and Cheng Long. 2024. RaBitQ: Quantizing High-Dimensional
Vectors with a Theoretical Error Bound for Approximate Nearest Neighbor
Search. Proceedings of the ACM on Management of Data 2, 3 (2024), 1–27.
[15] Tiezheng Ge, Kaiming He, Qifa Ke, and Jian Sun. 2014. Optimized Product
Quantization. IEEE Trans. Pattern Anal. Mach. Intell. 36, 4 (2014), 744–755.
[16] Aristides Gionis, Piotr Indyk, Rajeev Motwani, et al. 1999. Similarity search in
high dimensions via hashing. In Vldb, Vol. 99. 518–529.
[17] Michel X Goemans and David P Williamson. 1995. Improved approximation
algorithms for maximum cut and satisﬁability problems using semideﬁnite pro-
gramming. Journal of the ACM (JACM) 42, 6 (1995), 1115–1145.
[18] Siddharth Gollapudi, Neel Karia, Varun Sivashankar, Ravishankar Krishnaswamy,
Nikit Begwani, Swapnil Raz, Yiyong Lin, Yin Zhang, Neelam Mahapatro, Premku-
mar Srinivasan, et al. 2023. Filtered-diskann: Graph algorithms for approximate
nearest neighbor search with ﬁlters. In Proceedings of the ACM Web Conference
2023. 3406–3416.
[19] Yutong Gou, Jianyang Gao, Yuexuan Xu, and Cheng Long. 2025. SymphonyQG:
Towards Symphonious Integration of Quantization and Graph for Approximate
Nearest Neighbor Search. Proceedings of the ACM on Management of Data 3, 1
(2025), 1–26.
[20] Jim Gray, Surajit Chaudhuri, Adam Bosworth, Andrew Layman, Don Reichart,
Murali Venkatrao, Frank Pellow, and Hamid Pirahesh. 1997. Data cube: A re-
lational aggregation operator generalizing group-by, cross-tab, and sub-totals.
Data mining and knowledge discovery 1, 1 (1997), 29–53.
[21] Jiawei Han, Jian Pei, and Hanghang Tong. 2022. Data mining: concepts and
techniques. Morgan kaufmann.
[22] Jiawei Han, Jian Pei, and Yiwen Yin. 2000. Mining frequent patterns without
candidate generation. ACM sigmod record 29, 2 (2000), 1–12.
[23] Venky Harinarayan, Anand Rajaraman, and Jeﬀrey D Ullman. 1996. Implement-
ing data cubes eﬃciently. Acm Sigmod Record 25, 2 (1996), 205–216.
[24] Hazar Harmouch and Felix Naumann. 2017. Cardinality estimation: An experi-
mental survey. Proceedings of the VLDB Endowment 11, 4 (2017), 499–512.
[25] Ben Harwood and Tom Drummond. 2016. Fanng: Fast approximate nearest
neighbour graphs. In Proceedings of the IEEE Conference on Computer Vision and
Pattern Recognition. 5713–5722.
[26] Patrick Iﬀ, Paul Bruegger, Marcin Chrapek, Maciej Besta, and Torsten Hoeﬂer.
2025. Benchmarking Filtered Approximate Nearest Neighbor Search Algorithms
on Transformer-based Embedding Vectors.
arXiv:2507.21989 [cs.DB] https:
//arxiv.org/abs/2507.21989
[27] Piotr Indyk and Rajeev Motwani. 1998. Approximate nearest neighbors: towards
removing the curse of dimensionality. In Proceedings of the thirtieth annual ACM
symposium on Theory of computing. 604–613.
[28] Piotr Indyk and Haike Xu. 2023. Worst-case performance of popular approximate
nearest neighbor search implementations: Guarantees and limitations. Advances
in Neural Information Processing Systems 36 (2023), 66239–66256.
[29] Suhas Jayaram Subramanya, Fnu Devvrit, Harsha Vardhan Simhadri, Ravishankar
Krishnawamy, and Rohan Kadekodi. 2019. Diskann: Fast accurate billion-point
nearest neighbor search on a single node. Advances in Neural Information
Processing Systems 32 (2019).
[30] Yannis Kalantidis and Yannis Avrithis. 2014. Locally optimized product quan-
tization for approximate nearest neighbor search. In Proceedings of the IEEE
conference on computer vision and pattern recognition. 2321–2328.
[31] Richard M Karp. 2009. Reducibility among combinatorial problems. In 50 Years
of Integer Programming 1958-2008: from the Early Years to the State-of-the-Art.
Springer, 219–241.
[32] Patrick Lewis, Ethan Perez, Aleksandra Piktus, Fabio Petroni, Vladimir
Karpukhin, Naman Goyal, Heinrich Küttler, Mike Lewis, Wen-tau Yih, Tim Rock-
täschel, et al. 2020. Retrieval-augmented generation for knowledge-intensive nlp
tasks. Advances in Neural Information Processing Systems 33 (2020), 9459–9474.
[33] Wen Li, Ying Zhang, Yifang Sun, Wei Wang, Mingjie Li, Wenjie Zhang, and
Xuemin Lin. 2020. Approximate Nearest Neighbor Search on High Dimensional
Data - Experiments, Analyses, and Improvement. IEEE Trans. Knowl. Data Eng.
32, 8 (2020), 1475–1488.
[34] Kejing Lu, Mineichi Kudo, Chuan Xiao, and Yoshiharu Ishikawa. 2021. HVS: Hi-
erarchical Graph Structure Based on Voronoi Diagrams for Solving Approximate
Nearest Neighbor Search. Proc. VLDB Endow. 15, 2 (2021), 246–258.
[35] Rui Ma, Kai Zhang, Zhenying He, Yinan Jing, X Sean Wang, and Zhenqiang Chen.
2025. CHASE: A Native Relational Database for Hybrid Queries on Structured
and Unstructured Data. arXiv preprint arXiv:2501.05006 (2025).
[36] Yu A Malkov and Dmitry A Yashunin. [n.d.]. hnswlib. https://github.com/nmslib/
hnswlib.
[37] Yury A. Malkov and Dmitry A. Yashunin. 2020. Eﬃcient and Robust Approximate
Nearest Neighbor Search Using Hierarchical Navigable Small World Graphs. IEEE
Trans. Pattern Anal. Mach. Intell. 42, 4 (2020), 824–836.
[38] Imene Mami and Zohra Bellahsene. 2012. A survey of view selection methods.
Acm Sigmod Record 41, 1 (2012), 20–29.
[39] Gonzalo Navarro. 2002. Searching in metric spaces by spatial approximation.
The VLDB Journal 11 (2002), 28–46.
[40] Liana Patel, Peter Kraft, Carlos Guestrin, and Matei Zaharia. 2024. Acorn: Per-
formant and predicate-agnostic search over vector embeddings and structured
data. Proceedings of the ACM on Management of Data 2, 3 (2024), 1–27.
[41] Yun Peng, Byron Choi, Tsz Nam Chan, Jianye Yang, and Jianliang Xu. 2023.
Eﬃcient Approximate Nearest Neighbor Search in Multi-dimensional Databases.
Proc. ACM Manag. Data 1, 1 (2023), 54:1–54:27. https://doi.org/10.1145/3588908
[42] J Ben Schafer, Dan Frankowski, Jon Herlocker, and Shilad Sen. 2007. Collaborative
ﬁltering recommender systems. In The adaptive web: methods and strategies of
web personalization. Springer, 291–324.
[43] Jianguo Wang, Xiaomeng Yi, Rentong Guo, Hai Jin, Peng Xu, Shengjun Li, Xi-
angyu Wang, Xiangzhou Guo, Chengming Li, Xiaohai Xu, et al. 2021. Milvus:
A purpose-built vector data management system. In Proceedings of the 2021
International Conference on Management of Data. 2614–2627.
[44] Mengzhao Wang, Lingwei Lv, Xiaoliang Xu, Yuxiang Wang, Qiang Yue, and
Jiongkang Ni. 2022. Navigable proximity graph-driven native hybrid queries
with structured and unstructured constraints. arXiv preprint arXiv:2203.13601
(2022).
[45] Mengzhao Wang, Weizhi Xu, Xiaomeng Yi, Songlin Wu, Zhangyang Peng, Xi-
angyu Ke, Yunjun Gao, Xiaoliang Xu, Rentong Guo, and Charles Xie. 2024.
Starling: An I/O-Eﬃcient Disk-Resident Graph Index Framework for High-
Dimensional Vector Similarity Search on Data Segment. Proc. ACM Manag.
Data 2, 1 (2024), V2mod014:1–V2mod014:27. https://doi.org/10.1145/3639269
[46] Mengzhao Wang, Xiaoliang Xu, Qiang Yue, and Yuxiang Wang. 2021. A com-
prehensive survey and experimental comparison of graph-based approximate
nearest neighbor search. Proceedings of the VLDB Endowment 14, 11 (2021),
1964–1978.
[47] Runhui Wang and Dong Deng. 2020. DeltaPQ: lossless product quantization code
compression for high dimensional similarity search. Proceedings of the VLDB
Endowment 13, 13 (2020), 3603–3616.
[48] Chuangxian Wei, Bin Wu, Sheng Wang, Renjie Lou, Chaoqun Zhan, Feifei Li,
and Yuanzhe Cai. 2020. AnalyticDB-V: a hybrid analytical engine towards query
fusion for structured and unstructured data. Proceedings of the VLDB Endowment
13, 12 (2020), 3152–3165.
[49] Wei Wu, Junlin He, Yu Qiao, Guoheng Fu, Li Liu, and Jin Yu. 2022. HQANN:
Eﬃcient and robust similarity search for hybrid queries with structured and
unstructured constraints. In Proceedings of the 31st ACM International Conference
on Information & Knowledge Management. 4580–4584.
629

## PDF page 14

[50] Yuexuan Xu, Jianyang Gao, Yutong Gou, Cheng Long, and Christian S Jensen.
2024. iRangeGraph: Improvising Range-dedicated Graphs for Range-ﬁltering
Nearest Neighbor Search. arXiv preprint arXiv:2409.02571 (2024).
[51] Mingyu Yang, Wentao Li, Jiabao Jin, Xiaoyao Zhong, Xiangyu Wang, Zhitao Shen,
Wei Jia, and Wei Wang. 2024. Eﬀective and General Distance Computation for
Approximate Nearest Neighbor Search. arXiv preprint arXiv:2404.16322 (2024).
[52] Mingyu Yang, Wentao Li, Zhitao Shen, Chuan Xiao, and Wei Wang. 2025. ESG:
Elastic Graphs for Range-Filtering Approximate k-Nearest Neighbor Search.
arXiv preprint arXiv:2504.04018 (2025).
[53] Mingyu Yang, Wentao Li, and Wei Wang. 2024. Fast High-dimensional Approx-
imate Nearest Neighbor Search with Eﬃcient Index Time and Space. arXiv
preprint arXiv:2411.06158 (2024).
[54] Mingyu Yang, Wenxuan Xia, Wentao Li, Raymond Chi-Wing Wong, and Wei
Wang. 2025. Technical Report. https://github.com/mingyu-hkustgz/LabelANN/
blob/master/technical_report.pdf
[55] Ziqi Yin, Jianyang Gao, Pasquale Balsebre, Gao Cong, and Cheng Long. 2025.
DEG: Eﬃcient Hybrid Vector Search Using the Dynamic Edge Navigation Graph.
Proceedings of the ACM on Management of Data 3, 1 (2025), 1–28.
[56] Mohammed Javeed Zaki. 2002. Scalable algorithms for association mining. IEEE
transactions on knowledge and data engineering 12, 3 (2002), 372–390.
[57] Qianxi Zhang, Shuotao Xu, Qi Chen, Guoxin Sui, Jiadong Xie, Zhizhen Cai,
Yaoqi Chen, Yinxuan He, Yuqing Yang, Fan Yang, et al. 2023. {VBASE}: Unifying
Online Vector Similarity Search and Relational Queries via Relaxed Monotonicity.
In 17th USENIX Symposium on Operating Systems Design and Implementation
(OSDI 23). 377–395.
[58] Bolong Zheng, Xi Zhao, Lianggui Weng, Nguyen Quoc Viet Hung, Hang Liu,
and Christian S. Jensen. 2020. PM-LSH: A Fast and Accurate LSH Framework
for High-Dimensional Approximate NN Search. Proc. VLDB Endow. 13, 5 (2020),
643–655.
[59] Chaoji Zuo, Miao Qiao, Wenchao Zhou, Feifei Li, and Dong Deng. 2024. SeRF:
Segment Graph for Range-Filtering Approximate Nearest Neighbor Search. Pro-
ceedings of the ACM on Management of Data 2, 1 (2024), 1–26.
630
