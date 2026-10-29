## PDF page 1

arXiv:2412.03301v2  [cs.DB]  4 Jul 2025
Approximate Vector Set Search Inspired by Fly
Olfactory Neural System
Yiqi Lia, Sheng Wanga, Zhiyu Chenc, Shangfeng Chena, Zhiyong Penga,b
aSchool of Computer Science, Wuhan University, Wuhan, 430072, Hubei, China
bBig Data Institute, Wuhan University, Wuhan, 430072, Hubei, China
cAmazon.com, Inc., Seattle, 98109, WA, USA
Abstract
Vector set search is an underexplored paradigm of similarity search that fo-
cuses on retrieving vector sets similar to a given query set. This paradigm
captures the intrinsic structural alignment between sets and real-world enti-
ties, enabling finer-grained and more consistent relationship modeling across
a wide range of applications.
However, it faces significantly greater effi-
ciency challenges than traditional single-vector search, primarily due to the
combinatorial explosion of set-to-set comparisons. In this work, we address
both the combinatorial complexity inherent in vector set comparisons and
the curse of dimensionality carried over from single-vector search. To over-
come these obstacles, we propose an efficient algorithm for vector set search,
BioVSS1 (Bio-inspired Vector Set Search). BioVSS draws inspiration from
the fly olfactory neural system, which employs sparse coding principles for
efficient odor recognition in biological neural networks. Based on this biolog-
ical mechanism, our approach quantizes vectors into sparse binary codes and
employs a Bloom filter-based index leveraging set membership properties.
Experimental results show that BioVSS achieves over 50× speedup compared
to brute-force linear scanning on million-scale datasets, while maintaining a
high recall of up to 98.9%, demonstrating its effectiveness for large-scale
vector set search.
Keywords:
Similarity Search, Vector Set Search, Bloom Filter
1This work is an extended version of our paper titled Approximate Vector Set Search:
A Bio-Inspired Approach for High-Dimensional Spaces, presented at the 41st IEEE Inter-
national Conference on Data Engineering (ICDE 2025).
Preprint submitted to x
July 8, 2025


## PDF page 2

1. Introduction
Vector search constitutes a core computational task in numerous fields,
including information retrieval [1], recommender systems [2], and computer
vision [3]. Most existing methods focus on single-vector queries and aim to
address challenges such as the curse of dimensionality [4] and the sparsity of
high-dimensional vector spaces [5]. In this work, we investigate a novel and
challenging problem termed Vector Set Search. Given a query vector set Q,
the goal is to retrieve the top-𝑘most relevant vector sets from a database
D = {V1, V2, . . . , V𝑛}.
This problem extends beyond conventional single-
vector search [6] by involving intra-set vector matching and aggregation,
leading to increased computational complexity.
At present, there are no
efficient solutions available to handle this complexity effectively.
Applications. Vector set search supports a wide range of applications due
to its inherent structural alignment with real-world entities, including:
• Academic Entity Discovery. Academic entity discovery supports efficient
navigation of the rapidly expanding scholarly landscape.
By modeling
academic entities as vector sets—such as representing a paper’s profile [7]
through vectors corresponding to its citations, or characterizing an author’s
profile [8] as a vector set derived from their publications—vector set search
facilitates the identification of related research and the analysis of academic
trends.
• Multi-Modal Query Answering. Multi-modal query answering systems ben-
efit from vector set search to achieve more precise retrieval. As illustrated
in [9], multi-modal inputs (e.g., text, images, and audio) are encoded into
vector sets using a unified multi-modal encoder [10]. Vector set search then
enables cross-modal retrieval by aligning semantic representations across
modalities, thereby enhancing the accuracy of the query results.
• Multi-Vector Ranking for Recommendation. In recommendation systems,
multi-vector ranking leverages vector set representations to model users.
As shown in [11], users are represented as vector sets that encode different
facets of user behavior—such as browsing activities, search histories, and
click interactions. This vector set-based ranking framework enables flexible
matching across diverse user preferences.
Let us delve into scholar search, a characteristic example of vector set
search that we examine as our main experimental focus.
2


## PDF page 3

Vector Set Search
Vector Set Database
SET ID
Vec. ID
Vector
1
0.2
0.3
0.7
...
2
0.2
0.2
0.1
...
1
0.2
0.2
0.5
...
2
0.2
0.1
0.9
...
3
0.2
0.5
0.7
...
Scholar Profiles 
by Publications
Vector Set 
Database
Query Set
 (Set ID:   )
1.0 3.1 4.3
4.2 3.0 2.0
Compute
Pairwise Distance
   Vec.   1      2      3
2
1
Compute Set Distance
Distance 
Aggregation
-
-
-
-
3.0
-
   Vec.   1      2      3
2
1
Vectorizing
Get Top-k Scholars 
Figure 1: An Exemplar Data Flow of Vector Set Search
Example 1: Figure 1 presents a representative data flow of vector set search
in the context of scholar search [8], following the construction of a vector set
database. The database preparation stage involves encoding scholar profiles
as vector sets based on their publication records [12], forming a structured
database of scholars. The vector set search process consists of two primary
phases: distance computation and neighbor retrieval. In the distance compu-
tation phase, pairwise distances (e.g., Euclidean distance) between the query
vector set and each candidate set in the database are computed and subse-
quently aggregated (e.g., via 𝑚𝑎𝑥and 𝑚𝑖𝑛operations) into a set-level dis-
tance. The neighbor retrieval phase then identifies the top-𝑘most similar
scholars according to these aggregated distances. This procedure facilitates
the discovery of relevant scholars to support downstream research tasks.
Motivation. The preceding example highlights an important observation:
the structure of vector sets exhibits strong correspondence with real-world en-
tities, enabling practical and effective applications. With the recent progress
in embedding models [12] capable of generating high-dimensional, semanti-
cally rich representations, the range of applicable scenarios has significantly
broadened. Nonetheless, the computational complexity arising from high-
dimensional vectors and the combinatorial growth of pairwise comparisons
[13] presents notable challenges for building efficient search solutions.
Recent works such as DESSERT [14] have adopted hash table-based tech-
niques to accelerate certain types of non-metric distance calculations. How-
3


## PDF page 4

ever, accurately assessing vector set similarity requires more than speed—it
demands reliable discrimination and consistency across comparisons. These
fundamental properties hinge on the choice of distance metrics (analyzed in
Section 3.2). The Hausdorff distance [15, 16], by design, satisfies these crite-
ria, offering a mathematically sound means for evaluating set-to-set similarity
while preserving essential symmetric properties critical for dependable com-
parisons.
Driven by these foundational needs in set-level similarity measurement,
we emphasize the importance of using metric distances for maintaining eval-
uation consistency. The Hausdorff distance, rooted in classical set theory
[15, 16], inherently supports set-based distance computation. However, its
reliance on exhaustive pairwise distance evaluation renders it computation-
ally expensive, reinforcing the necessity of more efficient approaches for vector
set search.
Challenges. Enabling efficient vector set search in high-dimensional settings
under Hausdorff distance introduces several core challenges.
The first is
the well-known curse of dimensionality [4]. While some recent approaches
attempt to mitigate this issue through hash table construction [14] or by
reducing vector sets to single-vector representations [17], these techniques
are inherently limited to specific non-metric distances, thereby restricting
their suitability for Hausdorff-based computations. The second challenge lies
in the aggregation complexity.
Existing algorithms for Hausdorff distance
search [18, 19, 20] typically exploit geometric properties to reduce the cost
of aggregation.
However, such geometric heuristics become ineffective as
dimensionality increases, limiting their practical value in high-dimensional
scenarios.
Contributions. To overcome the aforementioned challenges, we propose an
approximate top-𝑘vector set search algorithm. Our method is inspired by
locality-sensitive hashing techniques [21, 22], which are in turn motivated
by the biological olfactory circuit [23]. By emulating the mechanisms of the
olfactory circuit, we quantize input vectors into sparse binary codes, thereby
mitigating the curse of dimensionality. On top of this, we design an indexing
structure based on Bloom filters, leveraging their set membership properties
to narrow down the search space and reduce the cost of aggregation. Our
main contributions are outlined below:
• We formulate the first approximate vector set search problem in high-
dimensional spaces using Hausdorff distance, a natural metric for measur-
4


## PDF page 5

ing set similarity (see Section 3).
• We introduce BioVSS, an algorithm that exploits locality-sensitive proper-
ties to accelerate vector set search, and provide formal theoretical analysis
with proofs to establish its correctness (see Section 4).
• We further develop BioVSS++, an improved version of BioVSS that in-
corporates a dual-layer cascaded filtering mechanism combining inverted
index and vector set sketches to minimize unnecessary candidate scans (see
Section 5).
• We perform comprehensive experiments demonstrating that our method
achieves more than 50× speedup over linear scan baselines on million-
scale datasets, while maintaining a recall rate of up to 98.9%, confirming
its practical efficiency (see Section 6).
2. RELATED WORK
Single-Vector Search.
With the advancement of embedding models,
single-vector search has emerged as a widely adopted unified paradigm [24].
Nonetheless, achieving high efficiency remains a significant bottleneck in real-
world applications.
To this end, numerous approximate search strategies
[25] have been introduced to accelerate single-vector retrieval. These strate-
gies are broadly classified into three categories: locality-sensitive hashing
[22, 26, 27], graph-based techniques [28], and space partitioning schemes
[29, 30]. LSH approaches like FlyHash [22] aim to assign similar vectors into
identical hash buckets.
Graph-based solutions, such as HNSW [28], create
navigable proximity graphs to support rapid query traversal. Partitioning-
based methods, including IVFFLAT [24, 31], IndexIVFPQ [32, 33, 31], and
IVFScalarQuantizer [31], utilize k-means clustering [34] to divide the space
and construct inverted indices for faster search. However, these techniques
are tailored specifically for single-vector queries, and are not designed to
accommodate set-based scenarios.
Vector Set Search.
Vector set search has garnered relatively limited at-
tention and remains underexplored.
Earlier studies primarily focused on
low-dimensional vector spaces, approaching vector set search as a set match-
ing problem solvable via algorithms such as the Kuhn-Munkres method [35].
In geospatial research, vector sets representing trajectories were treated as
5


## PDF page 6

point collections and measured using Hausdorff distance [36, 18, 37]. Tech-
niques were proposed to improve efficiency, such as integrating road net-
work structures [38], applying branch-and-bound algorithms [20], and esti-
mating Hausdorff distances for speedup [19]. Nevertheless, these solutions
were designed for low-dimensional contexts and do not translate effectively
to high-dimensional domains. The rise of embedding models allows vector
sets in high-dimensional spaces to express rich semantic information. Some
approaches convert a set of vectors into a single vector [17], enabling the ap-
plication of existing approximate search methods developed for single-vector
queries.
However, such conversion depends on specific distance functions
defined by the corresponding methods. The approach in [14], for instance,
constructs a hash table to accelerate vector set search, but has limitations
when used with certain metrics. Particularly, it lacks theoretical guarantees
for Hausdorff distance when the outer aggregation involves the 𝑚𝑎𝑥func-
tion. Moreover, current methods provide insufficient support for Hausdorff
distance. Our work addresses this gap by focusing on vector set search in
high-dimensional space under the Hausdorff distance metric.
Bio-Inspired Hashing & Bloom Filter.
Biological olfactory systems
across species demonstrate exceptional accuracy in odor recognition [39].
For example, mice (rodents) differentiate odors by interpreting neural sig-
nals in the olfactory bulb [40], while zebrafish (teleosts) detect amino acids
in water through olfactory signaling [41]. Among them, research on flies’
olfactory mechanisms is the most thorough, inspiring the design of sparse
locality-sensitive hashing (LSH) functions [21, 22] via computational model-
ing. Fly-inspired hashing [21], grounded in the fly olfactory system, offers an
LSH technique for single-vector search. In this model, specific odor molecules
activate only a small subset of neurons, resulting in sparse activations that
facilitate robust odor identification. BioHash [22] enhances performance by
learning internal data structures to assign connection weights among pro-
jection neurons, emulating the biological connectivity of the fly’s olfactory
circuit [42]. On a similar note, Bloom filters support efficient set represen-
tation by mapping elements to sparse arrays via hash functions. The binary
Bloom filter [42] employs a bit array with binary states, while the counting
Bloom filter [43] refines this with count-based granularity. This work inte-
grates the fly olfactory circuit’s locality-sensitive behavior with the structural
advantages of Bloom filters. We utilize these parallels to build an efficient
indexing structure.
6


## PDF page 7

3. DEFINITIONS & PRELIMINARIES
This section presents the problem formulation and analyzes why the Haus-
dorff distance is suitable for vector set comparison.
3.1. Problem Definition
Definition 1 (Vector). A vector v = (𝑣1, 𝑣2, ..., 𝑣𝑑) is a tuple of real num-
bers, in which 𝑣𝑖∈R and 𝑑∈N+ represents the dimensionality of the vector.
Definition 2 (Vector Set). A vector set V = {v1, v2, ..., v𝑚} contains a
set of vectors, where 𝑚∈N+ is number of vectors.
Definition 3 (Vector Set Database). A vector set database D =
{V1, V2, ..., V𝑛} is a collection of vector sets, where each V𝑖is a vector set
and 𝑛∈N+ is the number of vector sets.
Definition 4 (Hausdorff Distance). Given two vector sets Q and V, the
Hausdorff distance from Q to V is defined as:
𝐻𝑎𝑢𝑠(Q, V) = max

max
q∈Q min
v∈V 𝑑𝑖𝑠𝑡(q, v), max
v∈V min
q∈Q 𝑑𝑖𝑠𝑡(v, q)

,
where 𝑑𝑖𝑠𝑡(q, v) = ∥q −v∥2 is the Euclidean distance between vectors q ∈R𝑑
and v ∈R𝑑.
To clarify the computational steps involved in calculating the Hausdorff
distance, we present a concrete example below.
Example 2: Figure 2 demonstrates the procedure for computing the Hausdorff
distance. Consider two finite vector sets Q = {q1, q2, q3} and V = {v1, v2}
situated in a high-dimensional space. The Hausdorff distance 𝐻𝑎𝑢𝑠(Q, V)
is obtained via the following operations: (1) compute the maximum over the
minimal distances from each vector in V to all vectors in Q, which yields
maxv∈V minq∈Q 𝑑(v, q) = 2; (2) compute the maximum over the minimal dis-
tances from each vector in Q to all vectors in V: maxq∈Q minv∈V 𝑑(q, v) = 3;
(3) aggregate the two values using the 𝑚𝑎𝑥operator to obtain the final Haus-
dorff distance: 𝐻𝑎𝑢𝑠(Q, V) = 3.
The computation of the Hausdorff distance entails evaluating all pairwise
distances between vectors in the two sets, followed by aggregation as defined
in Definition 4. This leads to a time complexity of 𝑂(𝑚2·𝑑), where 𝑚denotes
the number of vectors in each set and 𝑑indicates the dimensionality. The
7


## PDF page 8

(1) �→�: max(1, 2) = 2;     (2) �→�: max(1, 3, 2) = 3;
(3) Haus(�, �): max(2, 3) = 3.   
1.0
3.1
4.3
4.2
3.0
2.0
��
��
��
��
��
Distance Matrix
��
��
��
��
min
1
3
2
min
1
2 
��
Vector
Set �
Vector
Set �
Figure 2: The Calculation Process of Hausdorff Distance
quadratic growth with respect to 𝑚renders exact computation impractical
for large datasets, particularly when both the set size and dimensionality 𝑑
are high.
To mitigate this computational burden, prior work in single-vector search
has demonstrated the effectiveness of approximation strategies, including
quantization [21, 22, 26] and index pruning [29, 30, 28, 32, 33, 31]. Mo-
tivated by these insights, we formulate the approximate top-𝑘vector set
search problem.
Definition 5 (Approximate Top-𝑘Vector Set Search). Given a vector
set database D = {V1, V2, ..., V𝑛}, a query vector set Q and a Hausdorff
distance function 𝐻𝑎𝑢𝑠(Q, V), the approximate top-k vector set search
is the task of returning R with probability at least 1 −𝛿:
R = {V★
1, V★
2, ..., V★
𝑘} = argmin𝑘
V★
𝑖∈D 𝐻𝑎𝑢𝑠(Q, V★
𝑖),
where argmin𝑘
V𝑖∈D selects 𝑘sets V★
𝑖minimizing 𝐻𝑎𝑢𝑠(Q, V★
𝑖) and V★
𝑘has the
𝑘-th smallest distance from Q. The failure probability is denoted by 𝛿∈[0, 1].
Approximate top-𝑘vector set search achieves higher efficiency by allowing
a minor loss in accuracy, effectively balancing computational speed and result
precision.
3.2. Suitability Analysis of Hausdorff Distance
To demonstrate the advantages of the Hausdorff distance in vector set
comparison, a comparative analysis of three distance measures is conducted:
minimum distance, mean minimum distance, and Hausdorff distance. The
focus is on illustrating the superior precision and symmetry of the Hausdorff
distance.
8


## PDF page 9

Min Distance
Hausdorff Distance
MeanMin Distance
Set Q
Set A
Set B
dist(A, B) = dist(B, A)
Distinguishable(Q to A, B): True
Bidirectional consistency: True
Bidirectional consistency: True
Bidirectional consistency: False
dist(A, B) = dist(B, A)
dist(A, B) ≠dist(B, A)
dist(Q, A) ≠dist(Q, B)
dist(Q, A) = dist(Q, B)
dist(Q, A) ≠dist(Q, B)
Distinguishable(Q to A, B): False Distinguishable(Q to A, B): True
Figure 3: Comparative Analysis of Different Distance Measures
Three vector sets Q, A, and B are used for the precision analysis, each
containing two vectors. For the symmetry analysis, two vector sets Q and
A are employed, where Q contains two vectors and A contains three vectors.
For visualization purposes, all vectors are represented as points in a two-
dimensional space. This representation allows for a clear illustration of the
distance measures’ properties.
The distance measures are defined as follows:
1. Minimum (Min) distance:
𝑑𝑚𝑖𝑛(A, B) =
min
a∈A,b∈B 𝑑(a, b),
2. Mean Minimum (MeanMin) distance:
𝑑𝑚𝑒𝑎𝑛−𝑚𝑖𝑛(A, B) = 1
|A|
∑︁
a∈A
min
b∈B 𝑑(a, b),
3. Hausdorff distance: As defined in Definition 4.
Here, 𝑑(·, ·) denotes the Euclidean distance, and |A| denotes the cardi-
nality of vector set A.
To evaluate precision, the distances between Q and vector sets A and B
are analyzed, as shown in the first row of Figure 3. The specific distance
matrices are presented below:
Q to A
Q1
Q2
A1
1
5
A2
6
3
Q to B
Q1
Q2
B1
1
5
B2
4
1
9


## PDF page 10

The analysis yields the following results:
1. 𝑑𝑚𝑖𝑛(Q, A) = 𝑑𝑚𝑖𝑛(Q, B) = 1
2. 𝑑𝑚𝑒𝑎𝑛−𝑚𝑖𝑛(Q, A) = 2, 𝑑𝑚𝑒𝑎𝑛−𝑚𝑖𝑛(Q, B) = 1
3. 𝑑𝐻(Q, A) = 3, 𝑑𝐻(Q, B) = 2
These results clearly demonstrate the superior precision of the Hausdorff
distance. The minimum distance fails to differentiate between Q’s relation-
ships with A and B. The mean minimum distance and Hausdorff distance
clearly distinguish both the similarities and differences between the vector
sets. While both 𝑑𝑚𝑒𝑎𝑛−𝑚𝑖𝑛and 𝑑𝐻show discriminative power, we further
examine their symmetry properties.
To examine symmetry, a case with two vector sets, Q and A, where Q
contains two vectors and A contains three vectors, is analyzed. The distance
matrix is:
Q to A
Q1
Q2
A1
1
4
A2
4
1
A3
7
3
The analysis yields the following results:
1. 𝑑𝑚𝑖𝑛(Q, A) = 𝑑𝑚𝑖𝑛(A, Q) = 1
2. 𝑑𝑚𝑒𝑎𝑛−𝑚𝑖𝑛(Q, A) = 1, 𝑑𝑚𝑒𝑎𝑛−𝑚𝑖𝑛(A, Q) = 1.67
3. 𝑑𝐻(Q, A) = 𝑑𝐻(A, Q) = 3
These results highlight the perfect symmetry of the Hausdorff distance.
The minimum distance also exhibits symmetry. However, the mean minimum
distance produces different results depending on the direction of comparison.
The Hausdorff distance maintains consistency regardless of the order of vector
set comparison, ensuring reliable and consistent similarity assessments.
The examples demonstrate the superiority of the Hausdorff distance in
vector set comparisons. Its advantages are twofold. First, it provides en-
hanced precision in set differentiation. Second, it maintains consistent sym-
metry regardless of comparison direction. These properties are not present
in other common measures. The Hausdorff distance effectively captures fine-
grained differences while ensuring mathematical consistency. This balance
10


## PDF page 11

Table 1: Summary of Major Notations
Notation
Description
D
Vector Set Database
DH
Sparse Binary Codes of D
T, Q
Vector Set of Target and Query
𝑛, 𝑚, 𝑚𝑞
Cardinality of D, T, and Q
𝐿𝑤𝑡𝑎= 𝐿
Number of Hash Functions / Winner-Takes-All
H
Hash Function (see Definition 7)
𝑘
The Number of Results Returned
𝜎(·)
Min-Max Similarity Function (see Lemma 1)
𝑆𝛼
𝑖𝑗, 𝑆𝛽
𝑖𝑗, S
(i,j)-th Entry of the Similarity Matrix S
𝑠max, 𝑠min
Max. and Min. Real Similarities btw. Vector Sets
ˆS, ˆ𝑠max, ˆ𝑠min
Estimated Values of S, 𝑠max, and 𝑠min
𝛿
Vector Set Search Failure Probability
𝐵★
𝛼, 𝐵𝛽, 𝐵′, 𝐵★
Similarity Bounds of Vector Sets (see Theorem 4)
makes it particularly suitable for complex vector set comparisons. As a re-
sult, it serves as a versatile and reliable metric across various applications.
4. Proposed BioVSS
We propose BioVSS, a search algorithm optimized for the approximate
top-𝑘vector set search task. Inspired by the fly’s olfactory circuitry, BioVSS
exploits its inherent locality-sensitive characteristics to improve search per-
formance. The theoretical basis for the locality-sensitive Hausdorff distance
is laid out in Section 4.2.
4.1. Overview Algorithm
BioVSS consists of two main components: 1) the binary hash encoding
for vector sets, and 2) the search execution in BioVSS.
4.1.1. Binary Hash Encoding for Vector Set
The rationale for employing binary hash encoding lies in harnessing the
similarity-preserving nature of hashing to reduce computational overhead
and improve search efficiency.
We begin by formalizing the unified framework of LSH, which underpins
a range of LSH methods [21, 22, 26, 27], originally introduced in [4].
Definition 6 (Locality-Sensitive Hashing Function [4]). An LSH hash
function ℎ: R𝑑→R is called a similarity-preserving hash for vectors a, b ∈
R𝑑if
P(ℎ(a) = ℎ(b)) = sim(a, b), ∀a, b ∈R𝑑,
11


## PDF page 12

where sim(a, b) ∈[0, 1] is the similarity between vectors q ∈R𝑑and v ∈R𝑑.
Vector
0.5
0.3
0.1
0.6
1
0
Classical 
Hash
0.5
0.3
0.1
0.6
Fly-Based
Hash
0
0
1
0
1
0
0
0
Short Binary Code
Sparse Binary Code
��(�) >> ����(�)
Figure 4: Classical Hashing vs. Fly-Based Hashing
Building upon this unified paradigm, our framework incorporates a fly-
inspired hashing mechanism. As depicted in Figure 4, this method funda-
mentally contrasts with conventional hashing schemes in its approach to di-
mensional transformation. While classical hashing methods commonly apply
dimensionality reduction to produce dense hash codes, the fly-based strategy
draws from the neural projection mechanisms observed in the Drosophila ol-
factory system [21], performing expansion via projections and yielding sparse
binary representations.
The adoption of fly-based hashing is driven by two main considerations.
First, its sparse binary encoding naturally aligns with the set-oriented struc-
ture of Bloom filters, which is critical for the functionality of our framework
(refer to Section 5).
This structural alignment renders it more appropri-
ate than traditional distance-preserving hashing functions. Second, within
fly-inspired methods, we employ BioHash [22] in preference to the original
FlyHash [21], as empirical evaluations indicate that BioHash offers roughly
double the similarity preservation performance compared to FlyHash [22].
Definition 7 (FlyHash [21] & BioHash [22]). A FlyHash or BioHash is a
function H : R𝑑→{0, 1}𝑏that maps a vector v ∈R𝑑to a sparse binary vector
h = H (v) ∈{0, 1}𝑏. The hash code is generated as h = 𝑊𝑇𝐴(𝑊v), where
𝑊∈R𝑏×𝑑is a random projection matrix, and 𝑊𝑇𝐴is the Winner-Take-
All (WTA) operation that sets the 𝐿𝑤𝑡𝑎largest elements to 1 and the rest to 0.
The resulting binary vector h has 𝐿𝑤𝑡𝑎non-zero elements (∥h∥1 = 𝐿𝑤𝑡𝑎≪𝑏),
where 𝐿𝑤𝑡𝑎also represents the number of hash functions composing H defined
in Lemma 6.
As shown in Figure 5(a), FlyHash [21] constructs LSH codes by simulat-
ing the fly’s olfactory neural circuit. It projects input vectors into a higher-
dimensional space using a projection matrix that models the behavior of pro-
jection neurons, thereby increasing representational richness. Winner-take-
all mechanism is then employed to produce sparse binary codes by retaining
12


## PDF page 13

0
0
1
0
0
0
1
0
0.6
2.9
1.5
1.7
0.2
0.6
0.6
0.1
1.6
0.9
1.5
2.1
0.4
0.7
0.9
1.4
0.6
1.8
1.1
1.9
0.4
0.9
1.0
0.4
Matrix 
Projection
Winner-
Take-All
Expanding 
Vector
Vector
Sparse 
Binary Code
0.5
0.3
0.1
0.6
1.6
0.9
1.5
2.1
0.4
0.7
0.9
1.4
1
0
1
0
0
0
0
0
...
Vector Set
Sparse Binary 
Codes of Vector Set
(a) The Process of BioHash 
& FlyHash
(b) The Hashing Process of 
Vector Set
...
1
0
1
0
0
0
0
0
0
0
1
0
0
0
1
0
BioHash
Figure 5: The Hashing Process of BioVSS
only the most active neurons. This approach has been proven to satisfy the
locality-sensitive property [21] (see Definition 6).
Theoretical justifications for this method are provided in Section 4.2,
where we demonstrate that the generated hash codes are well-suited to sup-
port approximate top-𝑘vector set search. As depicted in Figure 5(b), the
vector sets are first quantized into binary hash codes. We now proceed to
describe the hashing procedure in detail.
Algorithm 1 presents the procedure for computing sparse binary repre-
Algorithm 1: Gen Binary Codes(D, 𝐿𝑤𝑡𝑎)
Input: D = {V𝑖}𝑛
𝑖=1: vector set database, 𝐿𝑤𝑡𝑎: # of WTA.
Output: DH: sparse binary codes.
1 Initialization: DH = ∅
2 for 𝑗= 1 to 𝑛do
3
H 𝑗= ∅;
4
for each v ∈V𝑗do
5
h𝑝= 𝑊v;
// Matrix projection
6
h = 𝑊𝑇𝐴(h𝑝, 𝐿𝑤𝑡𝑎);
// Winner-takes-all
7
H 𝑗= H 𝑗∪{h};
8
DH = DH ∪{H 𝑗};
9 return DH;
13


## PDF page 14

Algorithm 2: BioVSS Topk Search(Q, 𝑘, D, DH, 𝐿𝑤𝑡𝑎,𝑐)
Input: Q: query vector set, 𝑘: # of top sets, D = {V𝑖}𝑛
𝑖=1: vector set
database, DH: sparse binary codes, 𝐿𝑤𝑡𝑎: # of WTA, 𝑐: the size of
candidate set.
Output: R: top-𝑘vector sets.
1 Initialization: QH = ∅, C = ∅;
2 for each q ∈Q do
3
h𝑝= 𝑊q;
// Matrix projection
4
h𝑞= 𝑊𝑇𝐴(h𝑝, 𝐿𝑤𝑡𝑎);
// Winner-takes-all
5
QH = QH ∪{h𝑞};
// Select candidates
6 for 𝑗= 1 to 𝑛do
7
𝑑𝐻= 𝐻𝑎𝑢𝑠𝐻(QH, H 𝑗);
8
C = C ∪{(V𝑗, 𝑑𝐻)};
9 F = {(V𝑖, 𝑑𝐻) ∈C | 𝑑𝐻≤𝑑(𝑐)
𝐻}, where 𝑑(𝑐)
𝐻
is the 𝑐-th smallest 𝑑𝐻in C;
// Select top-𝑘results
10 D = ∅;
11 for each (V𝑖, 𝑑𝐻) ∈F do
12
𝑑𝑖= 𝐻𝑎𝑢𝑠(Q, V𝑖);
13
D = D ∪{(V𝑖, 𝑑𝑖)};
14 R = {(V𝑖, 𝑑𝑖) ∈D | 𝑑𝑖≤𝑑(𝑘)
𝑖
}, where 𝑑(𝑘)
𝑖
is the 𝑘-th smallest 𝑑𝑖in D;
15 return R;
sentations for a database of vector sets. The input includes a database D
containing 𝑛vector sets {V𝑖}𝑛
𝑖=1 and a parameter 𝐿𝑤𝑡𝑎, which governs the
WTA operation. The output DH consists of the corresponding sparse binary
codes. The algorithm processes each vector set V𝑗in sequence (line 2), ap-
plying the BioHash transformation (refer to Definition 7) to every vector v
it contains (lines 5–6). This transformation involves computing 𝑊v through
a linear projection, followed WTA operation that retains the top 𝐿𝑤𝑡𝑎entries
as 1 while setting the rest to 0, yielding a binary vector h with exactly 𝐿𝑤𝑡𝑎
active bits. These codes are aggregated on a per-vector-set basis (line 7) and
stored in DH (line 8).
4.1.2. Search Execution in BioVSS
Binary codes offer computational efficiency by enabling rapid bitwise op-
erations, which are natively supported by modern CPU architectures [44].
This results in substantial acceleration compared to conventional floating-
14


## PDF page 15

point arithmetic.
Algorithm 2 presents BioVSS procedure for performing top-𝑘search within
vector set databases. The inputs include query set Q, target result count 𝑘,
database D, its associated sparse binary representations DH, and the WTA
parameter 𝐿𝑤𝑡𝑎. The output R comprises top-𝑘vector sets most similar to
the query. The algorithm first encodes the query set Q into its sparse binary
form QH (lines 2-5). To take advantage of CPU-optimized bit-level com-
putation, a Hamming-based Hausdorff distance 𝐻𝑎𝑢𝑠𝐻is computed (using
ℎ𝑎𝑚𝑚𝑖𝑛𝑔(q, v) [45] in place of 𝑑𝑖𝑠𝑡in Definition 4) between QH and each
binary code set H𝑗in DH (lines 6-8). This step yields a candidate set F of
potentially similar vector sets based on binary code proximity (line 9). In
the refinement phase, the algorithm evaluates the exact Hausdorff distance
(see Definition 4) between Q and each candidate set V𝑖in the original vector
space (lines 10-13). It then selects the 𝑘closest matches (line 14) and returns
the final result set R (line 15).
4.2. Theoretical Analysis of Algorithm Correctness
This section presents a theoretical analysis of the probabilistic guarantees
for result correctness in BioVSS. We define a function constraining similar-
ity measure bounds, followed by upper and lower tail probability bounds for
similarity comparisons. Through several lemmas, we construct our theoreti-
cal framework, culminating in Theorem 4, which establishes the relationship
between the error rate 𝛿and 𝐿= 𝐿𝑤𝑡𝑎.
Assumptions. Our framework assumes all vectors undergo 𝐿2 normaliza-
tion. This allows us to define similarity between vectors q and v as their inner
product: 𝑠𝑖𝑚(q, v) = q𝑇v. We denote the query vector set and another vector
set as Q and V, respectively. To facilitate proof, we define 𝑆𝑖𝑚𝐻𝑎𝑢𝑠(Q, V) =
min(minq∈Q maxv∈V 𝑠𝑖𝑚(q, v), minv∈V maxq∈Q 𝑠𝑖𝑚(q, v)). The similarity met-
ric is equivalent to the Hausdorff distance for 𝐿2-normalized vectors.
4.2.1. Bounds on Min-Max Similarity Scores
we begin by introducing upper and lower bounds for a function 𝜎, which
forms the foundation for our subsequent proofs.
Lemma 1. Consider a matrix S = [𝑠𝑖𝑗] ∈R𝑚𝑞×𝑚containing similarity scores
between a query vector set Q and a target vector set T, where |Q| = 𝑚𝑞and
|T| = 𝑚. Define a function 𝜎: R𝑚𝑞×𝑚→R as 𝜎(S) = min(min𝑖max𝑗𝑠𝑖𝑗, min𝑗max𝑖𝑠𝑖𝑗)
Then, 𝜎(S) satisfies the following bounds: min𝑖,𝑗𝑠𝑖𝑗≤𝜎(S) ≤max𝑖,𝑗𝑠𝑖𝑗.
15


## PDF page 16

Proof. Let 𝑎= min𝑖max𝑗𝑠𝑖𝑗and 𝑏= min𝑗max𝑖𝑠𝑖𝑗. Then 𝜎(S) = min(𝑎, 𝑏).
For the lower bound: ∀𝑖, 𝑗: 𝑠𝑖𝑗≤max𝑗𝑠𝑖𝑗⇒min𝑖,𝑗𝑠𝑖𝑗≤min𝑖max𝑗𝑠𝑖𝑗=
𝑎. Similarly, min𝑖,𝑗𝑠𝑖𝑗≤𝑏. Therefore, min𝑖,𝑗𝑠𝑖𝑗≤min(𝑎, 𝑏) = 𝜎(S).
For the upper bound: ∀𝑖: max𝑗𝑠𝑖𝑗≤max𝑖,𝑗𝑠𝑖𝑗⇒𝑎= min𝑖max𝑗𝑠𝑖𝑗≤
max𝑖,𝑗𝑠𝑖𝑗. Similarly, 𝑏≤max𝑖,𝑗𝑠𝑖𝑗. Therefore, 𝜎(S) = min(𝑎, 𝑏) ≤max𝑖,𝑗𝑠𝑖𝑗.
Thus, we have min𝑖,𝑗𝑠𝑖𝑗≤𝜎(S) ≤max𝑖,𝑗𝑠𝑖𝑗.
□
4.2.2. Upper Tail Probability Bound
we now establish the upper tail probability bound for our relevance score
function, which forms the foundation for our subsequent proofs.
Lemma 2. Consider a similarity matrix S ∈R𝑚𝑞×𝑚between a query vector
set and a target vector set. Define the maximum similarity score in S as
𝑠max = max𝑖,𝑗𝑠𝑖𝑗. Given an estimated similarity matrix ˆS of S and a threshold
𝜏1 ∈(𝑠max, 1), we define Δ1 = 𝜏1 −𝑠max. Then, the following inequality holds:
Pr
h
𝜎(ˆS) ≥𝑠max + Δ1
i
≤𝑚𝑞𝑚𝛾𝐿,
for 𝛾=

𝑠max(1−𝜏1)
𝜏1(1−𝑠max)
𝜏1 
1−𝑠max
1−𝜏1

. Here, 𝜎(·) is the operator defined in Lemma
1, and 𝐿∈Z+ represents the number of hash functions.
Proof. Applying a generic Chernoff bound to 𝜎(ˆS) yields the following bounds
for any 𝑡> 0:
Pr[𝜎(ˆS) ≥𝜏1] = Pr
h
𝑒𝑡𝜎(ˆS) ≥𝑒𝑡𝜏1
i
≤
E
h
𝑒𝑡𝜎(ˆS)i
𝑒𝑡𝜏1
.
Given an LSH family (see Definition 7), the estimated maximum simi-
larity ˆ𝑠max follows a scaled binomial distribution with parameters 𝑠max and
𝐿−1, i.e., ˆ𝑠max ∼𝐿−1B (𝑠max, 𝐿). Substituting the binomial moment gener-
ating function into the expression and using Lemma 1, we can bound the
numerator:
E
h
𝑒𝑡𝜎(ˆS)i
≤E
h
𝑒𝑡max1≤𝑖≤𝑚𝑞,1≤𝑗≤𝑚ˆ𝑆𝑖𝑗i
≤𝑚𝑞𝑚E
h
𝑒𝑡ˆ𝑠max
i
= 𝑚𝑞𝑚

1 −𝑠max + 𝑠max𝑒
𝑡
𝐿
 𝐿
.
Combining the Chernoff bound and the numerator bound yields: Pr[𝜎(ˆS) ≥
𝜏1] ≤𝑚𝑞𝑚𝑒−𝑡𝜏1

1 −𝑠max + 𝑠max𝑒
𝑡
𝐿
 𝐿
.
16


## PDF page 17

To find the tightest upper tail probability bound, we can determine the
infimum by setting the derivative of the bound for 𝑡equal to zero. Then, we
get: 𝑡★= 𝐿ln

𝜏1(1−𝑠max)
𝑠max(1−𝜏1)

.
Since 𝜏1 ∈(𝑠max, 1), the numerator of the fraction inside the logarithm is
greater than the denominator, ensuring that 𝑡★> 0. This result allows us to
obtain the tightest bound.
Substituting 𝑡= 𝑡★into the bound, we obtain:
Pr[𝜎(ˆs) ≥𝜏1] ≤𝑚𝑞𝑚
𝜏1 (1 −𝑠max)
𝑠max(1 −𝜏1)
−𝜏1 1 −𝑠max
1 −𝜏1
 𝐿
.
Thus we have: 𝛾=

𝑠max(1−𝜏1)
𝜏1(1−𝑠max)
𝜏1 
1−𝑠max
1−𝜏1

.
□
4.2.3. Lower Tail Probability Bound
we now establish the lower tail probability bound for our relevance score
function, which forms the foundation for our subsequent proofs.
Lemma 3. Consider a similarity matrix S ∈R𝑚𝑞×𝑚between a query vector
set and a target vector set. Define the minimum similarity score in S as
𝑠min = min𝑖,𝑗𝑠𝑖𝑗. Given an estimated similarity matrix ˆS of S and a threshold
𝜏2 ∈(0, 𝑠min), we define Δ2 = 𝑠min −𝜏2. Then, the following inequality holds:
Pr [𝜎(ˆs) ≤𝑠min −Δ2] ≤𝑚𝑞𝑚𝜉𝐿,
for 𝛾=

𝑠min(1−𝜏2)
𝜏2(1−𝑠min)
𝜏2 
1−𝑠min
1−𝜏2

. Here, 𝜎(·) is the operator defined in Lemma
1, and 𝐿∈Z+ represents the number of hash functions.
Proof. Applying a generic Chernoff bound to 𝜎(ˆS) yields the following in-
equality for any 𝑡< 0:
Pr[𝜎(ˆS) ≤𝜏2] = Pr
h
𝑒𝑡𝜎(ˆS) ≥𝑒𝑡𝜏2
i
≤
E
h
𝑒𝑡𝜎(ˆS)i
𝑒𝑡𝜏2
.
Given an LSH family (see Definition 7), the estimated minimum similarity
ˆ𝑠min follows a scaled binomial distribution with parameters 𝑠min and 𝐿−1, i.e.,
ˆ𝑠min ∼𝐿−1B (𝑠min, 𝐿). Substituting the binomial moment generating func-
tion into the expression and using Lemma 1, we can bound the numerator:
E
h
𝑒𝑡𝜎(ˆS)i
≤E
h
𝑒𝑡min1≤𝑖≤𝑚𝑞,1≤𝑗≤𝑚ˆ𝑆𝑖𝑗i
≤𝑚𝑞𝑚E
h
𝑒𝑡ˆ𝑠min
i
= 𝑚𝑞𝑚

1 −𝑠min + 𝑠min𝑒
𝑡
𝐿
 𝐿
.
17


## PDF page 18

Combining the Chernoff bound and the numerator bound yields: Pr[𝜎(ˆS) ≤
𝜏2] ≤𝑚𝑞𝑚𝑒−𝑡𝜏2

1 −𝑠min + 𝑠min𝑒
𝑡
𝐿
 𝐿
.
To find the tightest upper bound, we can determine the infimum by set-
ting the derivative of the upper bound with respect to 𝑡equal to zero. Then,
we obtain: 𝑡★= 𝐿ln

𝜏2(1−𝑠min)
𝑠min(1−𝜏2)

.
Since 𝜏2 ∈(0, 𝑠min), the numerator of the fraction inside the logarithm is
greater than the denominator, ensuring that 𝑡★< 0. This result allows us to
obtain the tightest upper bound.
Similar to Lemma 2, we get: 𝜉=

𝑠min(1−𝜏2)
𝜏2(1−𝑠min)
𝜏2 
1−𝑠min
1−𝜏2

.
□
4.2.4. Probabilistic Correctness for Search Results
this section presents a theorem establishing the probabilistic guarantees
to the approximate top-𝑘vector set search problem.
Theorem 4. Let V★
𝛼be one of the top-𝑘vector sets that minimize the Haus-
dorff distance 𝐻𝑎𝑢𝑠(Q, V), i.e., V★
𝛼∈argmin𝑘
V∈D 𝐻𝑎𝑢𝑠(Q, V), and V𝛽be
any other vector set in the database D. By applying Lemma 1, we obtain the
following upper and lower bounds for 𝐻𝑎𝑢𝑠(Q, V★
𝛼) and 𝐻𝑎𝑢𝑠(Q, V𝛽):
𝐵★
𝛼= 1 −min
𝑖,𝑗𝑆𝛼
𝑖𝑗= 1 −𝑠𝛼,min,
𝐵𝛽= 1 −max
𝑖,𝑗𝑆𝛽
𝑖𝑗= 1 −𝑠𝛽,max,
where 𝑆𝛼
𝑖𝑗and 𝑆𝛽
𝑖𝑗denote the element-wise similarity matrices between each
vector in the query set Q and each vector in V★
𝛼and V𝛽.
Let 𝐵′ = min𝛽𝐵𝛽be the minimum value of 𝐵𝛽over any vector set V𝛽not
in the top-𝑘results, and let 𝐵★= max𝛼𝐵★
𝛼be the maximum value of 𝐵★
𝛼over
any top-𝑘vector set V★
𝛼. Define Δ1 + Δ2 as follows:
𝐵𝛽−𝐵★
𝛼= 𝑠𝛼,min −𝑠𝛽,max ≥𝐵′ −𝐵★= 2(Δ1 + Δ2).
If Δ1 > 0 and Δ2 > 0, a hash structure with 𝐿= 𝑂 log   𝑛𝑚𝑞𝑚
𝛿
 solves the
approximate top-𝑘vector set search problem (Definition 5) with probability
at least 1 −𝛿, where 𝑛= |D|, 𝑚𝑞= |Q|, 𝑚= |V𝑖| (Assume 𝑚is invariant
across all V𝑖), and 𝛿is the failure probability.
Proof. We aim to determine the value of 𝐿that satisfies both tail bounds
and combine them to obtain the final 𝐿.
18


## PDF page 19

Upper Tail Probability Bound.
For V𝛽∉argmin𝑘
V∈D 𝐻𝑎𝑢𝑠(Q, V),
Lemma 2 yields:
Pr
h
𝜎(ˆS𝛽) ≥𝑠𝛽,max + Δ1
i
≤𝑚𝑞𝑚𝛾𝐿
𝛽,
where 𝛾𝛽=
 𝑠𝛽,max(1−𝜏1)
𝜏1(1−𝑠𝛽,max)
𝜏1  1−𝑠𝛽,max
1−𝜏1

.
To simplify the analysis, we consider 𝛾max = maxV𝛽𝛾𝛽, allowing us to
express all bounds using the same 𝛾max. To ensure that the probability of
the bound holding for all 𝑛−𝑘non-top-𝑘sets is 𝛿
2, we choose 𝐿such that
𝐿≥log 2(𝑛−𝑘)𝑚𝑞𝑚
𝛿

log
1
𝛾max
−1
. Consequently,
Pr
h
𝜎(ˆS𝛽) ≥𝑠𝛽,max + Δ1
i
≤𝑚𝑞𝑚𝛾𝐿
𝛽≤𝑚𝑞𝑚𝛾𝐿
max
≤𝑚𝑞𝑚𝛾
log 2(𝑛−𝑘)𝑚𝑞𝑚
𝛿

log
1
𝛾max
−1
max
=
𝛿
2(𝑛−𝑘) .
Lower Tail Probability Bound. For V★
𝛼∈argmin𝑘
V∈D 𝐻𝑎𝑢𝑠(Q, V), Lemma
3 yields:
Pr
h
𝜎(ˆS𝛼) ≤𝑠𝛼,min −Δ2
i
≤𝑚𝑞𝑚𝜉𝐿
𝛼,
where 𝜉𝛼=
 𝑠𝛼,min(1−𝜏2)
𝜏2(1−𝑠𝛼,min)
𝜏2  1−𝑠𝛼,min
1−𝜏2

.
Following a similar approach as for the upper bound, we ensure that the
probability of the bound holding for all top-𝑘sets is 𝛿
2 by selecting 𝐿such
that 𝐿≥log 2𝑘𝑚𝑞𝑚
𝛿

log
1
𝜉max
−1
, where 𝜉max = max𝛼𝜉𝛼. As a result,
Pr
h
𝜎(ˆS𝛼) ≤𝑠𝛼,min −Δ2
i
≤𝑚𝑞𝑚𝜉𝐿
𝛼≤𝑚𝑞𝑚𝜉𝐿
max
≤𝑚𝑞𝑚𝜉
log 2𝑘𝑚𝑞𝑚
𝛿

log
1
𝜉max
−1
max
= 𝛿
2𝑘.
Combining the Bounds. Let 𝐿= max
 
log 2(𝑛−𝑘)𝑚𝑞𝑚
𝛿
log

1
𝛾max
 ,
log 2𝑘𝑚𝑞𝑚
𝛿
log

1
𝜉max

!
, we calcu-
late the final 𝐿by combining the upper and lower tail probability bounds.
19


## PDF page 20

Let 𝟙be an indicator random variable that equals 1 when all the upper and
lower tail probability bounds are satisfied, and 0 otherwise. The probabil-
ity of solving the approximate top-𝑘vector set problem is equivalent to the
probability that:
Pr

∀𝛼,𝛽

ˆ𝐻 Q, V★
𝛼
 −ˆ𝐻 Q, V𝛽
 < 0

= Pr

∀𝛼,𝛽

min

min
q∈Q max
v∈V★𝛼
(𝑠𝑖𝑚(q, v)), min
v∈V★𝛼
max
q∈Q (𝑠𝑖𝑚(q, v))

−min

min
q∈Q max
v∈V𝛽(𝑠𝑖𝑚(q, v)), min
v∈V𝛽max
q∈Q (𝑠𝑖𝑚(q, v))

> 0

= Pr

∀𝛼,𝛽

min

min
1≤𝑖≤𝑚𝑞max
1≤𝑗≤𝑚𝑆𝛼
𝑖𝑗, min
1≤𝑗≤𝑚max
1≤𝑖≤𝑚𝑞𝑆𝛼
𝑖𝑗

−min

min
1≤𝑖≤𝑚𝑞max
1≤𝑗≤𝑚𝑆𝛽
𝑖𝑗, min
1≤𝑗≤𝑚max
1≤𝑖≤𝑚𝑞𝑆𝛽
𝑖𝑗

> 0

≥Pr

∀𝛼,𝛽

min

min
1≤𝑖≤𝑚𝑞max
1≤𝑗≤𝑚𝑆𝛼
𝑖𝑗, min
1≤𝑗≤𝑚max
1≤𝑖≤𝑚𝑞𝑆𝛼
𝑖𝑗

−min

min
1≤𝑖≤𝑚𝑞max
1≤𝑗≤𝑚𝑆𝛽
𝑖𝑗, min
1≤𝑗≤𝑚max
1≤𝑖≤𝑚𝑞𝑆𝛽
𝑖𝑗

> 0 | 𝟙= 1

Pr(𝟙= 1)
≥Pr

∀𝛼,𝛽

min 𝑆𝛼
𝑖𝑗−Δ2 −(max 𝑆𝛽
𝑖𝑗+ Δ1) > 0 | 𝟙= 1

Pr(𝟙= 1)
= Pr

∀𝛼,𝛽

min 𝑆𝛼
𝑖𝑗−max 𝑆𝛽
𝑖𝑗> Δ1 + Δ2 | 𝟙= 1

Pr(𝟙= 1)
= Pr  ∀𝛼,𝛽
 𝐵𝛽−𝐵★
𝛼> Δ1 + Δ2 | 𝟙= 1 Pr(𝟙= 1)
≥Pr  ∀𝛼,𝛽
 𝐵′ −𝐵★> Δ1 + Δ2 | 𝟙= 1 Pr(𝟙= 1)
≥Pr  ∀𝛼,𝛽(2(Δ1 + Δ2) > Δ1 + Δ2 | 𝟙= 1) Pr(𝟙= 1)
= 1 ∗Pr(𝟙= 1) ≥1 −((𝑛−𝑘) ∗
𝛿
2(𝑛−𝑘) + 𝛿
2𝑘∗𝑘)
= 1 −𝛿.
Therefore, with this choice of 𝐿, the algorithm effectively solves the
approximate top-𝑘vector set search problem.
By eliminating the data-
dependent correlation terms 𝛾max and 𝜉max, and considering the practical
scenario where 𝑛≫𝑘, then 𝐿= 𝑂 log   𝑛𝑚𝑞𝑚
𝛿
.
□
4.3. Performance Discussion
The time complexity of BioVSS is 𝑂(𝑛𝑚2𝐿/𝑤), where 𝑛is the number of
vector sets, 𝑚is the number of vectors per set, 𝐿is the binary vector length,
and 𝑤is the machine word size (typically 32 or 64 bits). The factor 𝐿/𝑤
represents the number of machine words needed to store each binary vector,
20


## PDF page 21

allowing for parallel processing of 𝑤bits. This results in improved efficiency
compared to real-number operations.
Despite this optimization, BioVSS still requires an exhaustive scan. To
address this limitation, we propose enhancing BioVSS with a filter. Next, we
will detail this method.
5. Enhancing BioVSS via Cascade Filter
We introduce BioVSS++, an improved approach that overcomes the lin-
ear scan bottleneck of BioVSS by incorporating a Bio-Inspired Dual-Layer
Cascade Filter (BioFilter). The core idea behind BioVSS++ lies in utilizing
the structural parallels between BioHash and Bloom filters, both of which
rely on sparse representations for encoding vectors and managing set mem-
bership. In the following, we elaborate on the construction of the filter and
the procedure for executing the search.
5.1. Filter Construction
The bio-inspired dual-layer cascade filter serves as the core mechanism
enabling efficient search within BioVSS++. BioFilter is composed of two
primary elements: an inverted index built upon count Bloom filters, and
vector set sketches constructed using binary Bloom filters. The inverted in-
dex utilizes count Bloom filters to generate inverted lists, where vector sets
are organized according to their respective count values. For the second-level
filtering, the vector set sketches make use of binary Bloom filters, allowing
for efficient candidate scanning based on Hamming distance. This two-tiered
filtering strategy in BioFilter effectively boosts search performance by pro-
gressively narrowing down the candidate pool through sequential refinement.
5.1.1. Inverted Index Based on Count Bloom Filter
The inverted index constitutes the first layer of BioFilter. It improves
search efficiency by indexing vector sets in an inverted manner, thereby re-
ducing the number of candidate items.
To overcome the linear scanning bottleneck present in Algorithm 1, we
utilize count Bloom filters to manage vector sets, which serve as the founda-
tion for building the inverted index.
Definition 8 (Count Bloom Filter [43]). Let V = {v1, v2, ..., v𝑚} be a
vector set. A count Bloom filter C = (𝑐1, 𝑐2, ..., 𝑐𝑏) for V is an array
21


## PDF page 22

Insert into Blooms
Sparse Binary 
Codes of Set
BBF
Vector Set Database
Insert into Blooms
1
2
...
8
Inverted Index
Key : Bloom Location
(Set ID, Count Value)
Sketches of  Sets
 Get 
CBF & BBF
Input Query
Filtering By 
Inverted Index
Filtering By 
Sketchs
Refinement 
Get Top-k 
Results
Query
Build a Dual-Layer Cascade Filter & Query
CBF
...
Set ID2
Set ID1
Set ID1
...
Set ID2
01010001
10001101
CBF: Count Bloom Filter
BBF: Binary Bloom Filter
CBFs of Vector Sets
Set ID2
Set ID1
...
BBFs of Vector Sets
...
Set ID2
Set ID1
0
0
1
0
0
0
1
0
1
0
1
0
0
0
0
0
0
0
1
0
0
0
1
0
1
0
3
0
0
0
2
0
1
0
1
0
0
0
1
0
Insert
(3, 7) 
(2, 9) (1, 5) 
(1, 6) (4, 5) 
Figure 6: Filter Construction and Query Execution in BioVSS++
of 𝑏counters, where each counter 𝑐𝑖represents the sum of the 𝑖-th bits of
the sparse binary codes of vectors in V. Formally, 𝑐𝑖= Í𝑚
𝑗=1 H (v𝑗)𝑖, where
H (v𝑗)𝑖is the 𝑖-th bit of the binary code of v𝑗.
The count Bloom filter encodes each binary code’s frequency across all
vectors in the set. Next, we define the inverted index, which is built upon
the count Bloom filters.
Definition 9 (Inverted Index Based on Count Bloom Filter). Let
D = {V1, V2, ..., V𝑛} be a database of vector sets. A inverted index based
on count bloom filter I is a data structure that maps each bit position to
a sorted list of tuples. For each position 𝑖∈{1, 2, ..., 𝑏}, I[𝑖] contains a list
of tuples ( 𝑗, 𝑐( 𝑗)
𝑖
), where 𝑗is the index of the vector set V𝑗∈D, and 𝑐( 𝑗)
𝑖
is
the 𝑖-th count value in the count Bloom filter for V𝑗. The list is sorted in
descending order based on 𝑐( 𝑗)
𝑖
values.
The inverted index plays a critical role in enabling efficient vector set
search. As illustrated in Figure 6, each vector set is first inserted into a count
Bloom filter. Inverted lists are then constructed and ranked in descending
order according to the count values at each filter position. This strategy
leverages the intuition that higher count values imply a greater probability of
22


## PDF page 23

Algorithm 3: Gen Count Bloom Filter(D, 𝐿𝑤𝑡𝑎, 𝑏)
Input: D = {V𝑖}𝑛
𝑖=1: vector set database, 𝐿𝑤𝑡𝑎: # of WTA, 𝑏: the length of
bloom filters
Output: C: count Bloom filters
// Generate binary codes:
Algorithm 1
1 DH = Gen Binary Codes(D, 𝐿𝑤𝑡𝑎);
// Construct Count Bloom Filters
2 C = {C( 𝑗)}𝑛
𝑗=1, where C( 𝑗) ∈N𝑏;
3 for 𝑗= 1 to 𝑛do
4
C( 𝑗) = Í
h∈H 𝑗h, where H 𝑗∈DH;
5 return C;
collisions at the same positions, suggesting stronger similarity among vector
sets.
Algorithm 3 outlines the procedure for generating count Bloom filters
across all vector sets. The inputs include a database D containing 𝑛vec-
tor sets, the winner-takes-all parameter 𝐿𝑤𝑡𝑎, and the desired length 𝑏of
the Bloom filters. Initially, sparse binary codes for the dataset are gener-
ated using the Gen Binary Codes procedure defined in Algorithm 1 (line 1).
Subsequently, a count Bloom filter C is computed for each vector set by ag-
gregating the binary codes of its constituent vectors. Specifically, each count
Bloom filter C( 𝑗) is produced by summing the binary vectors in H𝑗(lines
2–4), followed by returning the complete set of filters (line 5).
Algorithm 4 presents the method for constructing the inverted index.
This algorithm takes the same input as Algorithm 3, and outputs the inverted
index I. First, count Bloom filters for all entries in the dataset are generated
using Algorithm 3 (line 1). For each position 𝑖in the filters, an inverted list
I𝑖is created, storing pairs of set indices and their corresponding count values
(lines 2–6). Finally, each list is sorted in descending order based on these
count values (lines 7–8). This index structure effectively narrows down the
search space by filtering candidates early in the process.
5.1.2. Vector Set Sketches Based on Binary Bloom Filter
Vector set sketches constitute the second layer of BioFilter, offering a
unified binary representation for each vector set. These sketches are gen-
erated using binary Bloom filters, where a bitwise OR operation is applied
across the binary codes of all vectors within a set.
Due to their binary structure, these sketches allow efficient similarity esti-
23


## PDF page 24

Algorithm 4: Build Inverted Index(D, 𝐿𝑤𝑡𝑎, 𝑏)
Input: D = {V𝑖}𝑛
𝑖=1: vector set database, 𝐿𝑤𝑡𝑎: # of WTA, 𝑏: the length of
bloom filters
Output: I: inverted index
// Generate Count Bloom Filters:
Algorithm 3
1 C = Gen Count Bloom Filter(D, 𝐿𝑤𝑡𝑎, 𝑏);
// Build Inverted Index
2 Initialize I = {I𝑖}𝑏
𝑖=1, where I𝑖= ∅for 𝑖= 1, . . . , 𝑏;
3 for 𝑗= 1 to 𝑛do
4
for 𝑖= 1 to 𝑏do
5
𝑐( 𝑗)
𝑖
= C( 𝑗) [𝑖];
6
I𝑖= I𝑖∪{( 𝑗, 𝑐( 𝑗)
𝑖
)};
7 for 𝑖= 1 to 𝑏do
8
Sort I𝑖in descending order by 𝑐( 𝑗)
𝑖
;
9 return I;
mation via Hamming distance, utilizing fast XOR and popcount instructions
supported by modern CPU architectures [44]. This design significantly re-
duces computational overhead by eliminating the need for costly aggregation
steps inherent in Hausdorff distance calculations.
To better understand the operation of the building set sketches, we first
present the binary Bloom filter:
Definition 10 (Binary Bloom Filter [43]). Let V = {v1, v2, ..., v𝑚} be
a set of vectors. The binary Bloom filter B for V is a binary array of
length 𝑏, obtained by performing a bitwise OR operation on the binary codes
all vectors in V. Formally, B = H (v1) ∨H (v2) ∨... ∨H (v𝑚), where H (v𝑖)
is the binary vector of v𝑖and ∨denotes the bitwise OR operation.
As illustrated in Figure 6, each vector set is first inserted into a binary
Bloom filter. This binary Bloom filter is then converted into a corresponding
vector set sketch.
Algorithm 5 details the procedure for constructing set sketches from a
vector set database. The input includes a database D containing 𝑛vector
sets, the winner-takes-all parameter 𝐿𝑤𝑡𝑎, and the binary code length 𝑏. The
output is a set of sketches S, providing compact encodings for each vector
set. The algorithm starts by invoking the Gen Binary Codes function from
Algorithm 1 to produce sparse binary codes for the entire database (line 1).
24


## PDF page 25

Algorithm 5: Build Set Sketches(D, 𝐿𝑤𝑡𝑎, 𝑏)
Input: D = {V𝑖}𝑛
𝑖=1: vector set database, 𝐿𝑤𝑡𝑎: # of WTA, 𝑏: the length of
bloom filters
Output: S = {S(𝑖)}𝑛
𝑖=1: set sketches
// Generate Binary Codes:
Algorithm 1
1 DH = Gen Binary Codes(D, 𝐿𝑤𝑡𝑎);
// Construct Binary Bloom Filters
2 B = {B(𝑖)}𝑛
𝑖=1, where B(𝑖) ∈{0, 1}𝑏;
3 for 𝑖= 1 to 𝑛do
4
B(𝑖) = Ô
h∈H𝑖h, where H𝑖∈DH;
// Ô is 𝑂𝑅
// Build Set Sketches
5 S = {S(𝑖)}𝑛
𝑖=1, where S(𝑖) = B(𝑖);
6 return S;
It then constructs binary Bloom filters B for all vector sets (lines 2-4), where
each binary Bloom filter B(𝑖) is obtained via a bitwise OR operation (Ô)
over the binary codes of all vectors in the associated set H𝑖. In the final step
(line 5), the binary Bloom filters themselves are used directly as vector set
sketches S, which are then returned.
5.2. Search Execution in BioVSS++
This section presents the search execution process in BioVSS++. Leverag-
ing the inverted index and set sketches, BioFilter can efficiently eliminate
unrelated vector sets.
Our search execution approach is based on a dual-layer filtering mecha-
nism. At the first layer, we utilize the inverted index to rapidly remove a
substantial number of dissimilar vector sets. At the second layer, we apply
the vector set sketches for additional filtering. This addresses the drawbacks
of linear scanning in Algorithm 1, minimizing the computational overhead.
Algorithm 6 describes BioVSS++ top-𝑘query process, which utilizes a two-
stage filter BioFilter for efficient vector set search. The input comprises a
query set Q, the desired number of results 𝑘, the database D, a pre-computed
inverted index I, set sketches S, and parameters Θ. The output R holds the
top-𝑘most similar vector sets. The algorithm starts by constructing a count
Bloom filter C𝑄and a set sketch S𝑄for the query set (lines 1-2). BioFilter
subsequently applies two filtering stages: 1) inverted index filtering (lines
3-9): This stage utilizes the query’s count Bloom filter to locate potential
candidates. It chooses the top-𝐴positions with the highest counts in C𝑄and
25


## PDF page 26

Algorithm 6: BioVSS++ Topk Search(Q, 𝑘, D, I, S, Θ)
Input: Q: query vector set, 𝑘: # of results, D = {V𝑖}𝑛
𝑖=1: vector set
database, I: inverted index, S = {S(𝑖)}𝑛
𝑖=1: set sketches,
Θ = {𝐴: access number of lists, 𝑀: minimum count, 𝑐:
the size of candidate set,𝑇: # of candidates, 𝐿𝑤𝑡𝑎: # of WTA, 𝑏:
the length of bloom filters}
Output: R: top-𝑘vector sets
// Get Query Count Bloom Filter and Sketch:
Algorithm 3 and
5
1 C𝑄= Gen Count Bloom Filter(Q);
2 S𝑄= Build Set Sketch(Q, 𝐿𝑤𝑡𝑎, 𝑏);
// Filtering by Inverted Index
3 𝜋= ArgsortDescending(C𝑄) ;
4 P = {𝜋(𝑖) : 𝑖∈[1, 𝐴]};
5 F1 = ∅;
6 for 𝑝∈P do
7
for (𝑖, 𝑐(𝑖)
𝑝) ∈I[𝑝] do
8
if 𝑐(𝑖)
𝑝≥𝑀then
9
F1 = F1 ∪{𝑖};
// Filtering by Sketches
10 Initialize G ←empty max-heap with capacity 𝑐;
11 for 𝑖∈F1 do
12
𝑑= Hamming(S𝑄, S(𝑖));
13
if |G| < 𝑇then
14
G.push((𝑑, 𝑖));
15
else if 𝑑< G.top()[0] then
16
G.pop();
17
G.push((𝑑, 𝑖));
18 F2 = {𝑖|( , 𝑖) ∈G};
// Select top-𝑘results
19 D = ∅;
20 for each (𝑖, 𝑑𝐻) ∈F do
21
𝑑𝑖= 𝐻𝑎𝑢𝑠(Q, V𝑖);
22
D = D ∪{(V𝑖, 𝑑𝑖)};
23 R = {(V𝑖, 𝑑𝑖) ∈D | 𝑑𝑖≤𝑑(𝑘)
𝑖
}, where 𝑑(𝑘)
𝑖
is the 𝑘-th smallest 𝑑𝑖in D;
24 return R;
26


## PDF page 27

extracts vector sets from the inverted index I that possess counts exceeding a
threshold 𝑀at these positions. This generates an initial candidate set F1. 2)
sketch-based filtering (lines 10-18): This stage refines the candidates through
set sketches. It calculates the Hamming distance between the query sketch
S𝑄and each candidate’s sketch S(𝑖), preserving a max-heap of the 𝑇closest
candidates. This yields a further refined candidate set F2. Subsequently,
the algorithm evaluates the actual Hausdorff distance between Q and each
remaining candidate in F2 (lines 19-22), determining the top-𝑘results based
on these distances (line 23) and then returns it (line 24).
5.3. Theoretical Analysis of Dual-Layer Filtering Mechanism
BioFilter framework demonstrates potential compatibility with diverse
set-based distance metrics. This versatility stems from its filter structure
being independent of specific distance measurements. This dual-layer ap-
proach achieves efficiency through progressive refinement: the count Bloom
filter-based inverted index rapidly reduces the search space, while the binary
Bloom filter-based sketches enable similarity assessment of the remaining
candidates. To establish the theoretical foundation, we introduce the concept
of set connectivity and analyze its relationship with filter collision patterns.
Definition 11 (Set Connectivity). For two vector sets Q and V, their set
connectivity is defined as:
𝐶𝑜𝑛𝑛(Q, V) =
∑︁
q∈Q
∑︁
v∈V
𝑠𝑖𝑚(q, v),
where 𝑠𝑖𝑚(q, v) represents a pairwise similarity.
A fundamental insight of our framework is that the effectiveness of both
Bloom filters stems from their ability to capture set relationships through
hash collision positions. Specifically, when vector sets share similar elements,
their hash functions map to overlapping positions, manifesting as either accu-
mulated counts in the count Bloom filter or shared bit patterns in the binary
Bloom filter. This position-based collision mechanism forms the theoretical
foundation for both filtering layers. The following theorem establishes that
such collision patterns in both filter types correlate with set connectivity,
thereby validating the effectiveness of our dual-layer approach:
Theorem 5 (Collision-Similarity Relationship). For a query vector set
Q and two vector sets V1 and V2, where Q ∩ℎV denotes hash collisions
27


## PDF page 28

between elements from Q and V in either count Bloom filter or binary Bloom
filter. If their collision probability satisfies:
𝑃(Q ∩ℎV1 ≠∅) ≥𝑃(Q ∩ℎV2 ≠∅),
Then:
𝐶𝑜𝑛𝑛(Q, V1) ≳𝐶𝑜𝑛𝑛(Q, V2),
where ≳denotes that the left-hand side is approximately greater than the
right-hand side.
Proof. By the definition of collision probability:
𝑃(Q ∩ℎV1 ≠∅) = 1 −
Ö
q∈Q
Ö
v∈V1
(1 −𝑠𝑖𝑚(q, v)),
𝑃(Q ∩ℎV2 ≠∅) = 1 −
Ö
q∈Q
Ö
v∈V2
(1 −𝑠𝑖𝑚(q, v)).
The given condition implies:
1 −
Ö
q∈Q
Ö
v∈V1
(1 −𝑠𝑖𝑚(q, v)) ≥1 −
Ö
q∈Q
Ö
v∈V2
(1 −𝑠𝑖𝑚(q, v)).
Therefore:
Ö
q∈Q
Ö
v∈V1
(1 −𝑠𝑖𝑚(q, v)) ≤
Ö
q∈Q
Ö
v∈V2
(1 −𝑠𝑖𝑚(q, v)).
Taking the logarithm of both sides:
∑︁
q∈Q
∑︁
v∈V1
log(1 −𝑠𝑖𝑚(q, v)) ≤
∑︁
q∈Q
∑︁
v∈V2
log(1 −𝑠𝑖𝑚(q, v)).
Using the Taylor series expansion of log(1 −𝑥) at 𝑥= 0:
log(1 −𝑥) = −𝑥−𝑥2
2 −𝑥3
3 −. . . ,
𝑥∈[0, 1]
Approximating this for small 𝑥:
log(1 −𝑥) ≈−𝑥+ 𝑂(𝑥2).
28


## PDF page 29

Since 𝑠𝑖𝑚(q, v) ∈[0, 1] is a small value, we can approximate the following
relationship:
∑︁
q∈Q
∑︁
v∈V1
𝑠𝑖𝑚(q, v) ≳
∑︁
q∈Q
∑︁
v∈V2
𝑠𝑖𝑚(q, v)
This establishes:
𝐶𝑜𝑛𝑛(Q, V1) ≳𝐶𝑜𝑛𝑛(Q, V2)
□
This theoretical analysis demonstrates that higher collision probability in
our filters correlates with stronger set connectivity, providing a foundation
for the effectiveness of BioFilter. The relationship between hash collisions
and set connectivity validates that our dual-layer filtering mechanism effec-
tively preserves and identifies meaningful set relationships during the search
process, while the metric-independent nature of this correlation supports the
framework’s potential adaptability to various set distance measures.
5.4. Discussion on Metrics Extensibility
BioVSS++ algorithm demonstrates potential adaptability to a range of set-
based distance metrics beyond the Hausdorff distance, owing to the decoupled
design between its filtering mechanism and the specific metric applied. Rep-
resentative examples of compatible metrics include: (1) set distances defined
by maximal or minimal point-pair distances [46]; (2) mean aggregate distance
[47]; and (3) various Hausdorff distance extensions [48], among others.
This decoupling is particularly evident in the construction phase. During
the creation of inverted indexes or generation of hash codes, no dependence
on the specific distance metric is introduced. As a result, BioVSS++ holds
promise for extension to alternative distance functions. Additional discus-
sions are provided in Section 6.5.2 and 5.3.
6. Experiments
6.1. Settings
6.1.1. Datasets
For this study, we utilize text datasets derived from the Microsoft aca-
demic graph [49]. Two datasets were extracted from computer science and
29


## PDF page 30

Table 2: Summary of Datasets
Dataset # of Vectors # of Vector Sets Dim. # of Vectors/Set
CS
5,553,031
1,192,792
384
[2, 362]
Medicine
15,053,338
2,693,842
384
[2, 1923]
Picture
2,513,970
982,730
512
[2, 9]
medicine domains, specifically targeting authors with a minimum of two first-
author publications.
Paper texts within these datasets were transformed
into vector representations through the embedding model all-MiniLM-L6-v21
(widely adopted). Furthermore, we developed an image dataset employing
ResNet181 for feature extraction purposes. The following presents detailed
information regarding these datasets:
• Computer Science Literature (CS): It contains 1,192,792 vector sets in
the field of computer science, with 5,553,031 vectors. Each set comprises
2 to 362 vectors (Table 2).
• Medicine Literature (Medicine): It contains 2,693,842 vector sets in the
field of Medicine, with 15,053,338 vectors. Each set comprises 2 to 1923
vectors (Table 2).
• Product Pictures (Picture): It contains 982,730 vector sets sourced from
the AliProduct dataset [50], with 2,513,970 vectors. Each set represents
2 to 9 images of the same product, covering 50,000 different products
(Table 2).
6.1.2. Baselines
We evaluate our proposed approach against multiple indexing and quan-
tization methodologies. In particular, we utilize techniques from Facebook’s
Faiss library [31] as baseline methods for comparison. The evaluated ap-
proaches include: 1) IVFFLAT [24, 31], which utilizes an inverted file in-
dex with flat vectors for efficient processing of high-dimensional data; 2)
IndexIVFPQ [32, 33, 31], which integrates inverted file indexing with product
quantization to achieve vector compression and enhanced query performance;
3) IVFScalarQuantizer [31], which applies scalar quantization within the in-
verted file index framework to balance speed and accuracy; 4) BioVSS++, our
proposed approach that leverages a bio-inspired dual-layer cascade filter for
efficiency enhancement through effective pruning.
1https://www.sbert.net & https://huggingface.co
30


## PDF page 31

Given the lack of efficient direct vector set search approaches utilizing
Hausdorff distance in high-dimensional spaces, all techniques depend on cen-
troid vectors for index construction.
6.1.3. Evaluation Metric
To evaluate the performance of vector set search algorithms, we utilize
the recall rate across various top-𝑘values as our evaluation metric.
The recall rate at top-𝑘is formulated as: Recall@𝑘= |𝑅𝑘(Q)∩𝐺𝑘(Q)|
|𝐺𝑘(Q)|
, where
Q represents a query set, 𝑅𝑘(Q) denotes the algorithm’s top-𝑘retrieved re-
sults for query Q, and 𝐺𝑘(Q) indicates the ground-truth (precise calculations
according to Definition 4). We conduct 500 queries and present the averaged
recall rate.
6.1.4. Implementation
Our experiments were conducted on a computing platform with Intel
Xeon Platinum 8352V and 512 GB of memory. The core components of our
method2 were implemented in C++ and interfaced with Python.
6.1.5. Default Parameters
In our experiments, we focus on the following parameters: 1) the size of
Bloom filter {1024, 2048}; 2) the number of winner-takes-all {16, 32, 48, 64};
3) the list number of inverted index accessed {1, 2, 3}; 4) Minimum count
value of inverted index {1, 2}; 5) The size of candidate set {20𝑘, 30𝑘, 40𝑘, 50𝑘};
and 6) The number of results returned {3, 5, 10, 15, 20, 25, 30}. Underlined
values denote default parameters in our controlled experiments.
6.2. Storage and Construction Efficiency of Filter Structures
BioFilter consists of two sparse data structures: the count Bloom filter
and the binary Bloom filter. Both filters demonstrate sparse characteristics,
requiring optimized storage strategies.
The storage optimization utilizes two well-known sparse formats: Coordi-
nate (COO) [51] and Compressed Sparse Row (CSR) [51]. COO format handles
dynamic updates through (row, column, value) tuples. CSR format delivers
enhanced compression by preserving row pointers and column indices, which
proves especially suitable for static index structures.
Table 3 presents the results on CS. With a 1024-size Bloom filter, CSR
decreases the storage overhead from 9.1GB to 0.55GB for the count Bloom
2https://github.com/whu-totemdb/biovss
31


## PDF page 32

filter, reaching a 94% reduction ratio. Similar patterns appear across different
parameter settings, with CSR uniformly surpassing COO in storage efficiency.
Results for Medicine and Picture display comparable features and are detailed
in the Section 6.5.1. Table 4 depicts the time overhead of different stages.
While BioHash training represents the primary computational cost at 1504𝑠,
the construction of count and binary Bloom filters exhibits effectiveness,
requiring only 24𝑠and 22𝑠.
Table 3: Filter Storage Comparison on CS Dataset
Bloom
𝐿
Count Bloom Space (GB)
Binary Bloom Space (GB)
Dense
COO
CSR
Dense
COO
CSR
1024
16
9.1
1.09
0.55
1.14
0.36
0.19
32
2.02
1.01
0.67
0.34
48
2.84
1.43
0.95
0.48
64
3.59
1.8
1.2
0.6
2048
16
18.2
1.18
0.59
2.28
0.39
0.2
32
2.2
1.1
0.73
0.37
48
3.13
1.57
1.04
0.53
64
4
2
1.33
0.67
Table 4: Filter Processing Time on CS Dataset
Processing Stage
BioHash
BioHash
Count
Single
Training
Hashing
Bloom
Bloom
Time
1504s
14s
24s
22s
6.3. Performance Analysis and Parameter Experiments
In this section, we begin with preliminary experiments to evaluate the
effectiveness of BioVSS and BioVSS++, highlighting their advantages over
brute-force search. We then shift focus to BioVSS++, an improved variant
that integrates BioFilter to enhance efficiency. Lastly, we conduct compre-
hensive parameter tuning experiments on BioVSS++ to evaluate its perfor-
mance and determine optimal configurations.
6.3.1. Comparison with Brute-Force Search
We begin by evaluating the performance of BioVSS and BioVSS++ against
brute-force search, focusing on execution time and recall rate across the CS,
Medicine, and Picture datasets, each using a candidate set size of 20𝑘. As
presented in Tables 5, 6, and 7, both BioVSS and BioVSS++ achieve sub-
stantial speed improvements over brute-force search while preserving high
32


## PDF page 33

64
48
32
16
The Number of WTA
89
90
91
92
93
94
95
96
97
98
99
100
Recall Rate (%)
(a) CS Top-3 Search
20k
30k
40k
50k
64
48
32
16
The Number of WTA
89
90
91
92
93
94
95
96
97
98
99
100
Recall Rate (%)
(b) CS Top-5 Search
20k
30k
40k
50k
64
48
32
16
The Number of WTA
85
86
87
88
89
90
91
92
93
94
95
96
97
98
Recall Rate (%)
(c) Medicine Top-3 Search
20k
30k
40k
50k
64
48
32
16
The Number of WTA
85
86
87
88
89
90
91
92
93
94
95
96
97
98
Recall Rate (%)
(d) Medicine Top-5 Search
20k
30k
40k
50k
64
48
32
16
The Number of WTA
88
89
90
91
92
93
94
95
96
97
98
Recall Rate (%)
(e) Picture Top-3 Search
20k
30k
40k
50k
64
48
32
16
The Number of WTA
88
89
90
91
92
93
94
95
96
97
98
Recall Rate (%)
(f) Picture Top-5 Search
20k
30k
40k
50k
Figure 7: Recall Rate with Winner-Take-All Number (Bloom Size = 1024)
recall levels. On the CS dataset, BioVSS++ delivers a notable 46× speedup,
reducing the execution time from 9.16 seconds to 0.20 seconds, with top-3
and top-5 recall rates reaching 97.9% and 96.2%, respectively. Even greater
acceleration is observed on the Medicine dataset, where BioVSS++ achieves a
78× speedup, lowering execution time from 16.32 seconds to just 0.24 sec-
onds. On the Picture dataset, BioVSS++ reduces execution time from 8.75
seconds to 0.20 seconds—yielding a 44× improvement—while maintaining
97.8% and 96.1% recall for top-3 and top-5 results, respectively. These sig-
nificant gains primarily stem from the filtering mechanism incorporated in
BioVSS++, which effectively mitigates the global scanning overhead inherent
in BioVSS.
33


## PDF page 34

64
48
32
16
The Number of WTA
89
90
91
92
93
94
95
96
97
98
99
100
Recall Rate (%)
(a) CS Top-3 Search
20k
30k
40k
50k
64
48
32
16
The Number of WTA
89
90
91
92
93
94
95
96
97
98
99
100
Recall Rate (%)
(b) CS Top-5 Search
20k
30k
40k
50k
64
48
32
16
The Number of WTA
85
86
87
88
89
90
91
92
93
94
95
96
97
98
Recall Rate (%)
(c) Medicine Top-3 Search
20k
30k
40k
50k
64
48
32
16
The Number of WTA
85
86
87
88
89
90
91
92
93
94
95
96
97
98
Recall Rate (%)
(d) Medicine Top-5 Search
20k
30k
40k
50k
64
48
32
16
The Number of WTA
88
89
90
91
92
93
94
95
96
97
98
Recall Rate (%)
(e) Picture Top-3 Search
20k
30k
40k
50k
64
48
32
16
The Number of WTA
88
89
90
91
92
93
94
95
96
97
98
Recall Rate (%)
(f) Picture Top-5 Search
20k
30k
40k
50k
Figure 8: Recall Rate with Winner-Take-All Number (Bloom Size = 2048)
6.3.2. Parameter Study of BioVSS++
To optimize the performance of BioVSS++, we conducted a comprehen-
sive parameter study.
We examined several key parameters: the number
of winner-takes-all, the size of the Bloom filter, the list number of inverted
index accessed, and the minimum count value of the inverted index.
Number of Winners-Take-All.
The number of winner-takes-all (see
Definition 7) in BioVSS++ corresponds to the number of hash functions. As
shown in Figures 7 and 8, increasing this parameter from 16 to 48 led to sig-
nificant performance improvements across CS, Medicine, and Picture datasets
for Bloom filter sizes of 1024 and 2048. On CS dataset, with a 1024-size
Bloom filter and 20k candidates, recall improved by 5.7% when increasing
34


## PDF page 35

Figure 9: Impact of Bloom Filter Size on Recall
First
Layer
Second
Layer (50k)
Second
Layer (40k)
Second
Layer (30k)
Second
Layer (20k)
0.00
0.01
0.02
0.03
0.04
0.05
0.06
0.07
Time (seconds)
(a) The 1024-Size of Bloom Filter
CS
Medicine
Picture
First
Layer
Second
Layer (50k)
Second
Layer (40k)
Second
Layer (30k)
Second
Layer (20k)
0.00
0.01
0.02
0.03
0.04
0.05
0.06
0.07
Time (seconds)
(b) The 2048-Size of Bloom Filter
CS
Medicine
Picture
Figure 10: Filtering Times by the Size of Bloom Filter
the parameter from 16 to 48.
However, the performance gain plateaued
between 48 and 64, with only a 0.4% improvement in the same scenario.
Similarly, Medicine and Picture datasets exhibit the same trend when in-
creasing the parameter. This trend was consistent across different datasets
and Bloom filter sizes. This improvement can be attributed to the enhanced
distance-preserving property of the hash code as the number of winner-takes-
all increases. Hence, the default number of winner-takes-all is set to 64.
Size of the Bloom Filter.
The size of the Bloom filter directly influ-
ences the length of set sketches and the list number in the inverted index for
BioVSS++. As illustrated in Figure 9, our experiments explore filter sizes of
1024 and 2048, revealing that a Bloom filter with a size of 1024 achieves op-
timal recall rates across all candidate numbers. On CS dataset, the 1024-size
35


## PDF page 36

Table 5: Speedup vs. Linear Scan on CS Dataset
Method Total Time (s) Speedup Top-3 Recall Top-5 Recall
Brute
9.16
1x
100%
100%
BioVSS
0.73
12x
97.5%
97.2%
BioVSS++
0.20
46x
97.9%
96.2%
Table 6: Speedup vs. Linear Scan on Medicine Dataset
Method Total Time (s) Speedup Top-3 Recall Top-5 Recall
Brute
16.32
1x
100%
100%
BioVSS
2.13
8x
96.5%
95.8%
BioVSS++
0.24
78x
93.8%
92.3%
Table 7: Speedup vs. Linear Scan on Picture Dataset
Method Total Time (s) Speedup Top-3 Recall Top-5 Recall
Brute
8.75
1x
100%
100%
BioVSS
0.49
17x
100%
99.9%
BioVSS++
0.20
44x
97.8%
96.1%
filter configuration yields a 98.9% recall rate with a candidate set of 50𝑘, while
maintaining a robust 98% recall even with a reduced set of 20𝑘candidates.
Similarly, for Medicine and Picture datasets, the 1024-size filter achieves op-
timal recall rates. The effectiveness of this filter size can be attributed to its
capacity to capture discriminative features without over-emphasizing local
characteristics, thereby providing a good trade-off between specificity and
generalization in this process. As demonstrated in Figure 10, The various
sizes of the Bloom filters exhibit low latency below 70 milliseconds. Hence,
the default size of the Bloom filter is set to 1024.
List Number of Inverted Index Accessed. The list number of inverted
index accessed determines the search range in BioVSS, with larger values
leading to broader searches. As shown in Table 8, increasing this parameter
from 1 to 3 significantly improves recall rates while modestly increasing pro-
cessing time. For CS dataset with a 1024-size Bloom filter, the top-3 recall
improves from 92.9% to 98.9%, and the top-5 recall from 91.1% to 98.2%,
with processing time increasing from 0.008s to 0.013s. Similar improvements
are observed with a 2048-size Bloom filter. Medicine and Picture datasets
show the same trends, with slightly lower recall rates but similar time in-
creases. Notably, the recall rate gain diminishes when moving from 2 to 3
accesses. Hence, the default list number of inverted index accessed is 3.
36


## PDF page 37

Table 8: List Access Number for Top-3 (T-3) and Top-5 (T-5)
Dataset
1 Access
2 Access
3 Access
Recall (%) Time Recall (%) Time Recall (%) Time
T-3
T-5
(s)
T-3
T-5
(s)
T-3
T-5
(s)
CS (1024)
92.9
91.1
0.008 98.0
97.1
0.012 98.9
98.2
0.013
CS (2048)
91.9
89.4
0.008 97.6
96.4
0.011 98.4
97.3
0.011
Medicine (1024) 92.8
90.8
0.014 96.9
95.6
0.018 97.4
96.0
0.029
Medicine (2048) 90.3
86.8
0.014 94.3
92.2
0.019 95.5
93.6
0.028
Picture (1024)
85.1
76.3
0.017 94.8
91.2
0.024 97.9
96.6
0.030
Picture (2048)
81.5
73.6
0.013 93.1
88.0
0.018 97.0
94.0
0.023
Table 9: Minimum Count Value with Recall Rate and Time
Dataset
Minimum Count = 1
Minimum Count = 2
Top-3 Top-5 Total Time Top-3 Top-5 Total Time
CS (1024)
98.9% 98.2%
0.42s
96.5% 95.1%
0.45s
CS (2048)
98.4% 97.3%
0.45s
95.1% 94.0%
0.44s
Medicine (1024) 97.4% 96.0%
0.51s
93.3% 91.8%
0.50s
Medicine (2048) 95.5% 93.6%
0.48s
89.7% 87.0%
0.47s
Picture (1024)
97.9% 96.6%
0.42s
92.0% 87.3%
0.45s
Picture (2048)
97.0% 94.0%
0.43s
87.9% 81.6%
0.43s
Minimum Count Value of Inverted Index. The minimum count value
of the inverted index determines the threshold for accessing items in the
inverted index lists. As illustrated in Figure 9, this parameter impacts the
trade-off between recall. Setting the value to 0 requires traversing all items. A
value of 1 leverages the hash codes’ sparsity, eliminating most irrelevant items
while maintaining high recall. Increasing to 2 further reduces candidates but
at the cost of the recall rate. For CS dataset with a 1024-size Bloom filter,
the top-3 recall decreases from 98.9% at count 1 to 96.5% at count 2. Top-5
recall shows a similar trend, declining from 98.2% to 95.7%. Medicine and
Picture datasets exhibit similar trends, with comparable drops in top-3 and
top-5 recall rates. Thus, we set the default minimum count to 1.
Impact of Embedding Models on Algorithm Performance.
To as-
sess the robustness of BioVSS across various vector representations, we con-
ducted experiments using different embedding models.
These models in-
herently produce vectors of varying dimensions, allowing us to evaluate the
method’s performance across different vector lengths. The experiments were
designed to evaluate two critical aspects of embedding model impact. First,
the performance consistency was tested using different embedding models
37


## PDF page 38

within the same modality but with varying vector dimensions. Second, the
method’s versatility was examined across different modalities while main-
taining consistent vector dimensions. CS and Picture datasets represent text
and image modalities respectively, enabling these comparative analyses. To
validate the impact of different vector dimensions within the same modality,
two text embedding models were applied to CS dataset. All-MiniLM-L6-v21
and distiluse-base-multilingual-cased-v21 from Hugging Face generate 384-
dimensional and 512-dimensional vectors. Table 10 elucidates the efficacy
and robustness of BioVSS across diverse embedding dimensionalities. For
CS dataset with a Bloom filter cardinality of 1024, both the 384-dimensional
MiniLM and 512-dimensional DistilUse models exhibit exceptional recall per-
formance. MiniLM achieves Top-3 and Top-5 recall rates of 98.9% and 98.2%,
while DistilUse demonstrates 92.3% and 90.8%. Significantly, the computa-
tional latencies are nearly equivalent irrespective of vector dimensionality,
corroborating the dimension-agnostic nature of the search algorithm. The
algorithm maintains similarly high recall rates when increasing the Bloom
filter size to 2048. Notably, the search process exhibits dimension-invariant
computational complexity, ensuring efficient performance across varying di-
mensional scales.
To validate the impact of different modalities with the
same vector dimension, embedding models generating 512-dimensional vec-
tors were applied to CS and Picture datasets.
Specifically, with a Bloom
filter cardinality of 1024, both CS and Picture datasets demonstrate high re-
call rates ( 90% for Top-5), with comparable computational latencies (0.4s+).
This performance is maintained when scaling to a size of 2048, evidencing
the algorithm’s robustness across data modalities.
Table 10: Impact of Different Embedding Methods
Dataset
Embedding Embedding
Recall (%)
Time
Model
Dimension Top-3 Top-5
(s)
CS (1024)
MiniLM
384
98.9
98.2
0.42
CS (1024)
DistilUse
512
92.3
90.8
0.46
Picture (1024)
ResNet18
512
97.9
96.6
0.42
CS (2048)
MiniLM
384
98.4
97.3
0.43
CS (2048)
DistilUse
512
85.2
81.0
0.45
Picture (2048)
ResNet18
512
97.0
94.0
0.43
Impact of Top-k on Result Quality. To evaluate the impact of the top-𝑘
parameter on result quality, we conducted experiments using default param-
eters for both BioVSS and BioVSS++ on CS, Medicine, and Picture datasets.
38


## PDF page 39

Table 11 presents the recall rates for various top-𝑘values ranging from 3 to
30. On CS dataset, BioVSS demonstrates consistent performance, maintain-
ing a high recall rate above 98% across all top-𝑘values. BioVSS++ shows
slightly higher recall for top-3 (98.9%) but experiences a gradual decrease
as 𝑘increases, reaching 96.4% for top-30. Medicine dataset reveals a similar
trend, with BioVSS maintaining high recall rates (99.1% for top-3, decreasing
slightly to 96.9% for top-30), while BioVSS++ shows a decline (from 97.5%
for top-3 to 92.3% for top-30). Picture dataset exhibits a similar trend. The
slight performance degradation observed in BioVSS++ for larger 𝑘values sug-
gests a trade-off between efficiency and recall, which may be attributed to
its more aggressive filtering mechanism. Notably, both methods maintain
high recall rates (above 92%) even for larger 𝑘values, demonstrating their
effectiveness in retrieving relevant results across various retrieval scenarios.
The filtering mechanism of BioVSS++ has brought significant improvements
in query efficiency. Smaller top-𝑘values are typically more valuable and of-
ten yield the most relevant results in many applications. Consequently, we
will focus on BioVSS++ in our subsequent experiments.
Table 11: Recall Rate for Top-𝑘Across Different Datasets
Method
Top-3 Top-5 Top-10 Top-15 Top-20 Top-25 Top-30
CS Dataset
BioVSS
98.6% 98.7%
98.5%
98.5%
98.4%
98.2%
98.1%
BioVSS++ 98.9% 98.2%
97.5%
97.2%
96.9%
96.7%
96.4%
Medicine Dataset
BioVSS
99.1% 98.8%
97.8%
97.5%
97.2%
97.0%
96.9%
BioVSS++ 97.4% 96.0%
94.8%
93.9%
93.2%
92.7%
92.3%
Picture Dataset
BioVSS
100% 99.9%
99.8%
99.7%
99.6%
99.6%
99.5%
BioVSS++ 99.9% 99.9%
90.0%
80.0%
85.0%
80.0%
73.3%
Query Time Analysis.
Experiments evaluated query efficiency across
candidate sets ranging from 20k to 50k. As illustrated in Table 12, query
time scales approximately linearly with candidate count. With the configura-
tion of WTA=64 and Bloom filter size=1024, the method achieves consistent
and efficient query times (0.44s-0.51s for 50k candidates) across all datasets.
While reducing WTA hash count to 16 decreases query time by up to 15%,
such reduction compromises recall performance as shown in Figures 7 and 8.
Additionally, doubling the Bloom filter size to 2048 offers minimal efficiency
39


## PDF page 40

Table 12: Query Time (s) with Different Bloom Filter and WTA for Top-3&5
Dataset
B=1024, W=64
B=1024, W=48
B=1024, W=32
B=1024, W=16
50k
40k
30k
20k
50k
40k
30k
20k
50k
40k
30k
20k
50k
40k
30k
20k
CS
0.44
0.35
0.29
0.21
0.46
0.48
0.38
0.29
0.44
0.46
0.31
0.28
0.43
0.45
0.37
0.27
Medicine
0.51
0.40
0.34
0.24
0.49
0.41
0.32
0.25
0.47
0.39
0.28
0.22
0.45
0.36
0.28
0.19
Picture
0.44
0.36
0.28
0.20
0.45
0.36
0.29
0.20
0.45
0.39
0.30
0.21
0.45
0.38
0.29
0.21
Dataset
B=2048, W=64
B=2048, W=48
B=2048, W=32
B=2048, W=16
50k
40k
30k
20k
50k
40k
30k
20k
50k
40k
30k
20k
50k
40k
30k
20k
CS
0.45
0.34
0.28
0.20
0.44
0.33
0.27
0.20
0.43
0.33
0.27
0.20
0.39
0.32
0.26
0.19
Medicine
0.48
0.40
0.31
0.22
0.47
0.39
0.29
0.22
0.46
0.38
0.29
0.21
0.43
0.37
0.28
0.19
Picture
0.43
0.35
0.27
0.20
0.46
0.37
0.29
0.20
0.43
0.37
0.28
0.20
0.43
0.37
0.28
0.20
1B = Bloom Filter Size, W = WTA hash count
gains (≤0.03𝑠improvement), making the added memory overhead unjus-
tifiable. These observations support selecting 64 and 1024 as the optimal
configurations balancing efficiency and effectiveness.
6.4. Comparison Experiment
To evaluate the performance of the BioVSS++ method, we conducted com-
parative evaluations against baseline algorithms on the CS, Medicine, and
Picture datasets. The experiments focus on analyzing query time and recall
rate. The following section details the results and corresponding analysis.
Figures 11(a) and (b) illustrate the experimental outcomes on the CS
dataset. From the comparison of time-recall trends, it is clear that the pro-
posed BioVSS++ algorithm consistently surpasses the other three baselines.
In order to manage query time, we vary the candidate set size. Results in-
dicate that recall rates improve across all methods as query time increases.
Notably, BioVSS++ achieves a recall of 98.9% within only 0.2 seconds for the
top-3 query, and maintains a recall of 98.2% at 0.47 seconds. Even under
the top-5 setting, its recall remains above 90%. It is also observed that both
IVFScalarQuantizer and IVFFLAT exhibit sharp gains in recall as query time
grows, whereas the increase for IndexIVFPQ is more modest. This may be
attributed to inherent limitations in the product quantization encoding tech-
nique. Specifically, product quantization segments high-dimensional vectors
into multiple subvectors, each of which is quantized independently. Although
this enhances compression, it can also introduce information loss, restricting
further recall improvements.
Figures 11(c) and (d) report the query performance on the Medicine
dataset. Given the larger data volume, the overall query time increases ac-
cordingly. Among the baselines, IVFScalarQuantizer shows a steeper recall
40


## PDF page 41

growth curve as time progresses, suggesting greater sensitivity to candidate
set size in large-scale settings. In contrast, our BioVSS++ method continues
to deliver strong performance, even when candidate sets are relatively small.
Figures 11(e) and (f) present the results on the Picture dataset, where the
performance trends align closely with those observed in the CS dataset.
Figure 11: Recall Rate Comparison for Different Methods
6.5. Supplementary Experiments
This section presents additional experiments and analyses further to vali-
date the performance and versatility of BioVSS. We explore three key aspects:
storage analysis on Medicine and Picture datasets, exploration of alternative
distance metrics, and performance analysis via BioHash iteration.
6.5.1. Storage Analysis on Medicine and Picture Datasets
Tables 13 and 14 present the storage efficiency analysis on Medicine and
Picture datasets. These results corroborate the findings from CS, demon-
strating consistent storage reduction patterns across different data domains.
Table 13: Filter Storage Comparison on Medicine Dataset
Bloom
𝐿
Count Bloom Space (GB)
Binary Bloom Space (GB)
Dense
COO
CSR
Dense
COO
CSR
1024
16
7.5
0.57
0.29
0.94
0.19
0.1
32
1.13
0.57
0.38
0.19
48
1.67
0.84
0.56
0.28
64
2.18
1.1
0.73
0.37
2048
16
15
0.61
0.31
1.87
0.2
0.11
32
1.18
0.6
0.39
0.2
48
1.73
0.87
0.58
0.29
64
2.29
1.15
0.76
0.38
6.5.2. Exploration of Alternative Distance Metrics
while the main text demonstrates the effectiveness of BioVSS++ using
Hausdorff distance, the framework’s applicability extends to other set-based
41


## PDF page 42

Table 14: Filter Storage Comparison on Picture Dataset
Bloom
𝐿
Count Bloom Space (GB)
Binary Bloom Space (GB)
Dense
COO
CSR
Dense
COO
CSR
1024
16
20.55
2.79
1.41
2.57
0.93
0.48
32
5.14
2.58
1.71
0.87
48
7.26
3.64
2.42
1.22
64
9.22
4.62
3.07
1.55
2048
16
41.1
3.01
1.51
5.14
1
0.51
32
5.6
2.81
1.87
0.94
48
8.02
4.02
2.67
1.35
64
10.71
5.37
3.57
1.8
Table 15: Performance Comparison of MeanMin
Method
Top-3
Top-5
Time
DESSERT (tables=32, hashes per t=6)
45.9%
35.8%
0.21s
DESSERT (tables=32, hashes per t=12)
40.3%
28.8%
0.56s
DESSERT (tables=24, hashes per t=6)
42.3%
32.6%
0.18s
DESSERT (num t=24, hashes per t=12)
38.7%
27.6%
0.36s
BioVSS++ (default parameter)
59.0%
51.6%
0.46s
distance metrics.
To further validate this extensibility, additional experi-
ments were conducted using alternative distance measures.
Our experiments compared BioVSS++ against DESSERT3 under various
parameter configurations (Table 15). Using the MeanMin distance metric,
BioVSS++ achieved a Top-3 recall of 59.0% with a query time of 0.46 seconds.
These results demonstrate reasonable efficiency beyond the Hausdorff setting.
The performance disparity between Hausdorff and MeanMin distances arises
from their aggregation mechanisms. Hausdorff utilizes a three-level struc-
ture (𝑚𝑖𝑛- 𝑚𝑎𝑥- 𝑚𝑎𝑥), whereas MeanMin employs a two-level aggregation
(𝑚𝑖𝑛- 𝑚𝑒𝑎𝑛). Such three-level aggregation creates more distinctive distance
distributions. Consequently, similar sets exhibit higher collision probabilities,
while dissimilar sets are more effectively separated.
The extensibility to different metrics stems from BioVSS++’s decoupled
design. The filter structure operates independently of the specific distance
metric. This architectural choice enables metric flexibility while maintaining
the core filtering mechanisms.
3https://github.com/ThirdAIResearch/Dessert. Without IVF indexing.
42


## PDF page 43

6.5.3. Performance Analysis via BioHash Iteration
BioHash algorithm serves as a crucial component in our framework, with
its reliability directly impacting system performance. While BioHash has
been validated in previous research [22], a rigorous examination of its behav-
ior within our specific application domain is necessary.
The iteration count parameter plays a vital role in BioHash’s compu-
tational process, specifically in determining learning efficacy. At its core,
BioHash implements a normalized gradient descent optimization approach.
The magnitude of parameter updates functions as a key metric for quantify-
ing learning dynamics. This magnitude is defined as:
𝑀𝑡= max
𝑖,𝑗|Δ𝑊𝑡
𝑖𝑗|,
where 𝑀𝑡represents the update magnitude at iteration 𝑡, and Δ𝑊𝑡
𝑖𝑗denotes
the weight change for the synaptic connection between neurons 𝑖and 𝑗. This
metric effectively captures the most substantial parametric modifications oc-
curring within the network during each training iteration (𝑏𝑎𝑡𝑐ℎ𝑠𝑖𝑧𝑒= 10𝑘).
Figure 12 illustrates the parameter update magnitude dynamics across
three distinct datasets (CS, Medicine, and Picture) under varying Bloom filter
sizes (1024 and 2048 bits). The experimental results reveal several significant
patterns:
• Convergence Behavior: Across all configurations, the parameter up-
date magnitude exhibits a consistent decay pattern. The parameter up-
dates show high magnitudes during initial batches, followed by a rapid
decrease and eventual stabilization. This pattern indicates BioHash al-
gorithm’s robust convergence properties regardless of the domain-specific
data characteristics.
• Filter Size Consistency: The comparison between 1024 and 2048 Bloom
filter configurations demonstrates remarkable consistency in convergence
patterns. This observation suggests that BioHash maintains stable perfor-
mance characteristics independent of filter size, validating the robustness
of our parameter update mechanism across different capacity settings.
• Cross-Domain Consistency: The similar convergence patterns observed
across CS, Medicine, and Picture datasets validate the algorithm’s domain-
agnostic nature.
Despite the inherent differences in data distributions,
BioHash iteration mechanism maintains consistent performance charac-
teristics.
43


## PDF page 44

0
200
400
600
800
1000
Batch
0
200
400
600
800
1000
1200
1400
Parameter Update Magnitude
CS (Bloom Size = 1024)
0
200
400
600
800
1000
Batch
0
200
400
600
800
Parameter Update Magnitude
CS (Bloom Size = 2048)
0
500
1000
1500
2000
2500
Batch
0
200
400
600
800
1000
Parameter Update Magnitude
Medicine (Bloom Size = 1024)
0
500
1000
1500
2000
2500
Batch
0
200
400
600
800
1000
Parameter Update Magnitude
Medicine (Bloom Size = 2048)
0
50
100
150
200
250
Batch
0
10000
20000
30000
40000
Parameter Update Magnitude
Picture (Bloom Size = 1024)
0
50
100
150
200
250
Batch
0
5000
10000
15000
20000
Parameter Update Magnitude
Picture (Bloom Size = 2048)
Figure 12: Parameter Update Magnitude Across Batches in BioHash
Through extensive empirical analysis across diverse application scenar-
ios, we validated BioHash’s effectiveness as a core component of our system.
Our experiments demonstrated consistent and reliable convergence behav-
ior under various environmental configurations. The results confirmed that
BioHash achieves stable parameter updates well before completing the full
training process.
7. Conclusions
This work addresses the relatively underexplored challenge of vector set
search.
We propose BioVSS and BioVSS++ to enable efficient vector set
search based on the Hausdorff distance. Theoretical analysis demonstrates
44


## PDF page 45

the correctness of our algorithms. The dual-layer filtering mechanism effi-
ciently eliminates irrelevant candidates, thereby minimizing computational
costs.
Experimental results demonstrate that our approach attains more
than 50× acceleration over conventional linear scan techniques on million-
scale datasets, while maintaining recall rates up to 98.9%. Future research
directions include extending the framework to accommodate additional dis-
tance metrics
Acknowledgement
This work was supported by the National Key R&D Program of China
(2023YFB4503600), the National Natural Science Foundation of China (62202338,
62372337), and the Key R&D Program of Hubei Province (2023BAB081)
References
[1] P.-S. Huang, X. He, J. Gao, L. Deng, A. Acero, L. Heck, Learning deep
structured semantic models for web search using clickthrough data, in:
CIKM, 2013, pp. 2333–2338.
[2] P. Nigam, Y. Song, V. Mohan, V. Lakshman, W. Ding, A. Shingavi,
C. H. Teo, H. Gu, B. Yin, Semantic product search, in: SIGKDD, 2019,
pp. 2876–2885.
[3] J. Johnson, M. Douze, H. J´egou, Billion-scale similarity search with
gpus, IEEE Transactions on Big Data 7 (3) (2019) 535–547.
[4] P. Indyk, R. Motwani, Approximate nearest neighbors: towards remov-
ing the curse of dimensionality, in: STOC, 1998, pp. 604–613.
[5] X. Zhao, Y. Tian, K. Huang, B. Zheng, X. Zhou, Towards efficient
index construction and approximate nearest neighbor search in high-
dimensional spaces, Proceedings of the VLDB Endowment 16 (8) (2023)
1979–1991.
[6] Y. Fu, C. Chen, X. Chen, W. Wong, B. He, Optimizing the number
of clusters for billion-scale quantization-based nearest neighbor search,
IEEE Transactions on Knowledge and Data Engineering (01) (2024)
1–14.
45


## PDF page 46

[7] D. Yin, W. L. Tam, M. Ding, J. Tang, Mrt: Tracing the evolution
of scientific publications, IEEE Transactions on Knowledge and Data
Engineering 35 (1) (2021) 711–724.
[8] B. Sch¨afermeier, G. Stumme, T. Hanika, Mapping research trajectories,
CoRR abs/2204.11859 (2022).
[9] M. Wang, H. Wu, X. Ke, Y. Gao, X. Xu, L. Chen, An interactive multi-
modal query answering system with retrieval-augmented large language
models, CoRR abs/2407.04217 (2024).
[10] V. W. Liang, Y. Zhang, Y. Kwon, S. Yeung, J. Y. Zou, Mind the gap:
Understanding the modality gap in multi-modal contrastive representa-
tion learning, NeurIPS 35 (2022) 17612–17625.
[11] S. Mysore, M. Jasim, A. Mccallum, H. Zamani, Editable user profiles
for controllable text recommendations, in: SIGIR, 2023, p. 993–1003.
[12] J. Devlin, M. Chang, K. Lee, K. Toutanova, BERT: pre-training of deep
bidirectional transformers for language understanding, in:
NAACL-
HLT, 2019, pp. 4171–4186.
[13] M. Aum¨uller, M. Ceccarello, Solving k-closest pairs in high-dimensional
data, in: SISAP, Vol. 14289, 2023, pp. 200–214.
[14] J. Engels, B. Coleman, V. Lakshman, A. Shrivastava, DESSERT: an effi-
cient algorithm for vector set search with vector set queries, in: NeurIPS,
2023.
[15] F. Hausdorff, Grundz¨uge der mengenlehre, Vol. 7, von Veit, 1914.
[16] J. Henrikson, Completeness and total boundedness of the hausdorff met-
ric, MIT Undergraduate Journal of Mathematics 1 (69-80) (1999) 10.
[17] M. Leybovich, O. Shmueli, Efficient approximate search for sets of lin-
eage vectors, in: TaPP, ACM, 2022, pp. 5:1–5:8.
[18] S. Wang, Z. Bao, J. S. Culpepper, G. Cong, A survey on trajectory data
management, analytics, and learning, ACM Computing Surveys 54 (2)
(2022) 39:1–39:36.
46


## PDF page 47

[19] M. D. Adelfio, S. Nutanong, H. Samet, Similarity search on a large
collection of point sets, in: SIGSPATIAL, 2011, pp. 132–141.
[20] S. Nutanong, E. H. Jacox, H. Samet, An incremental hausdorff dis-
tance calculation algorithm, Proceedings of the VLDB Endowment 4 (8)
(2011) 506–517.
[21] S. Dasgupta, C. F. Stevens, S. Navlakha, A neural algorithm for a fun-
damental computing problem, Science 358 (6364) (2017) 793–796.
[22] C. Ryali, J. Hopfield, L. Grinberg, D. Krotov, Bio-inspired hashing for
unsupervised similarity search, in: ICML, Vol. 119, 2020, pp. 8295–8306.
[23] S. X. Luo, R. Axel, L. Abbott, Generating sparse and selective third-
order responses in the olfactory system of the fly, Proceedings of the
National Academy of Sciences 107 (23) (2010) 10713–10718.
[24] Q. Zhang, S. Xu, Q. Chen, G. Sui, J. Xie, Z. Cai, Y. Chen, Y. He,
Y. Yang, F. Yang, M. Yang, L. Zhou, VBASE: Unifying online vec-
tor similarity search and relational queries via relaxed monotonicity, in:
OSDI, 2023, pp. 377–395.
[25] W. Li, Y. Zhang, Y. Sun, W. Wang, M. Li, W. Zhang, X. Lin, Approxi-
mate nearest neighbor search on high dimensional data — experiments,
analyses, and improvement, IEEE Transactions on Knowledge and Data
Engineering 32 (8) (2020) 1475–1488.
[26] T. Wei, R. Alkhoury Maroun, Q. Guo, B. Webb, Devfly: Bio-inspired
development of binary connections for locality preserving sparse codes,
in: NeurIPS, Vol. 35, 2022, pp. 2320–2332.
[27] B. Zheng, Z. Xi, L. Weng, N. Hung, H. Liu, C. Jensen, Pm-lsh: A fast
and accurate lsh framework for high-dimensional approximate nn search,
Proceedings of the VLDB Endowment 13 (5) (2020) 643–655.
[28] Y. A. Malkov, D. A. Yashunin, Efficient and robust approximate nearest
neighbor search using hierarchical navigable small world graphs, IEEE
Transactions on Pattern Analysis and Machine Intelligence 42 (4) (2020)
824–836.
47


## PDF page 48

[29] W. Li, C. Feng, D. Lian, Y. Xie, H. Liu, Y. Ge, E. Chen, Learning
balanced tree indexes for large-scale vector retrieval, in: KDD, 2023,
pp. 1353–1362.
[30] G. Gupta, T. Medini, A. Shrivastava, A. J. Smola, BLISS: A billion scale
index using iterative re-partitioning, in: SIGKDD, 2022, pp. 486–495.
[31] M. Douze, A. Guzhva, C. Deng, J. Johnson, G. Szilvasy, P. Mazar´e,
M.
Lomeli,
L.
Hosseini,
H.
J´egou,
The
faiss
library,
CoRR
abs/2401.08281 (2024).
[32] T. Ge, K. He, Q. Ke, J. Sun, Optimized product quantization, IEEE
transactions on pattern analysis and machine intelligence 36 (4) (2014)
744–755.
[33] H. Jegou, M. Douze, C. Schmid, Product quantization for nearest neigh-
bor search, IEEE transactions on pattern analysis and machine intelli-
gence 33 (1) (2010) 117–128.
[34] S. Wang, Y. Sun, Z. Bao, On the efficiency of k-means clustering: Eval-
uation, optimization, and algorithm selection, Proceedings of the VLDB
Endowment 14 (2) (2020) 163–175.
[35] H. W. Kuhn, The hungarian method for the assignment problem, Naval
research logistics quarterly 2 (1-2) (1955) 83–97.
[36] M. J. Atallah, A linear time algorithm for the hausdorff distance between
convex polygons, Information Processing Letters 17 (4) (1983) 207–209.
[37] S. Wang, Z. Bao, J. S. Culpepper, Z. Xie, Q. Liu, X. Qin, Torch: A
search engine for trajectory data, in: SIGIR, ACM, 2018, pp. 535–544.
[38] G. Roh, J. Roh, S. Hwang, B. Yi, Supporting pattern-matching queries
over trajectories on road networks, IEEE Transactions on Knowledge
and Data Engineering 23 (11) (2011) 1753–1758.
[39] L. Buck, R. Axel, A novel multigene family may encode odorant recep-
tors: a molecular basis for odor recognition, Cell 65 (1) (1991) 175–187.
[40] L. D. Rhein, R. H. Cagan, Biochemical studies of olfaction: isolation,
characterization, and odorant binding activity of cilia from rainbow
48


## PDF page 49

trout olfactory rosettes., Proceedings of the National Academy of Sci-
ences 77 (8) (1980) 4412–4416.
[41] U. Pace, E. Hanski, Y. Salomon, D. Lancet, Odorant-sensitive adenylate
cyclase may mediate olfactory reception, Nature 316 (6025) (1985) 255–
258.
[42] L. B. Vosshall, A. M. Wong, R. Axel, An olfactory sensory map in the
fly brain, Cell 102 (2) (2000) 147–159.
[43] F. Bonomi, M. Mitzenmacher, R. Panigrahy, S. Singh, G. Varghese, An
improved construction for counting bloom filters, in: ESA, Vol. 4168,
2006, pp. 684–695.
[44] W. Mu la, N. Kurz, D. Lemire, Faster population counts using avx2
instructions, The Computer Journal 61 (1) (2018) 111–120.
[45] J. Qin, Y. Wang, C. Xiao, W. Wang, X. Lin, Y. Ishikawa, Gph: Simi-
larity search in hamming space, in: ICDE, 2018, pp. 29–40.
[46] G. T. Toussaint, An optimal algorithm for computing the minimum
vertex distance between two crossing convex polygons, Computing 32 (4)
(1984) 357–364.
[47] O. Fujita, Metrics based on average distance between sets, Japan Journal
of Industrial and Applied Mathematics 30 (2013) 1–19.
[48] A. Conci, C. S. Kubrusly, Distance between sets-a survey, Advances in
Mathematical Sciences and Applications 26 (1) (2018) 1–18.
[49] A. Sinha, Z. Shen, Y. Song, H. Ma, D. Eide, B.-J. Hsu, K. Wang,
An overview of microsoft academic service (mas) and applications, in:
WWW, 2015, pp. 243–246.
[50] L. Song, P. Pan, K. Zhao, H. Yang, Y. Chen, Y. Zhang, Y. Xu, R. Jin,
Large-scale training system for 100-million classification at alibaba, in:
SIGKDD, ACM, 2020, pp. 2909–2930.
[51] N. Bell, M. Garland, Implementing sparse matrix-vector multiplication
on throughput-oriented processors, in: SC, ACM, 2009.
49
