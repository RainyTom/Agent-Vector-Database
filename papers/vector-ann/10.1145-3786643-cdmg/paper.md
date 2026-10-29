URL: https://raw.githubusercontent.com/Lsyhprum/CDMG/9e6f3c5d14078efe1e1fbb9fa74227015645cba4/technical_report.pdf
Content-Type: application/octet-stream
Method: markit
Notes: Converted with markit

---

<!-- Page 1 -->

# Efficient and Robust Out-Of-Distribution Vector Similarity Search with Cross-Distribution Monotonic Graph

## Qiang Yue Mengzhao Wang Xiaoliang Xu

### Hangzhou Dianzi University Zhejiang University Hangzhou Dianzi University

### yq@hdu.edu.cn wmzssy@zju.edu.cn xxl@hdu.edu.cn

## Cheng Long

### Nanyang Technological University

c.long@ntu.edu.sg
ABSTRACT

|𝑞|𝑛|𝑛||
|---|---|---|---|
||||𝑛|
|||𝑛 𝑞|𝑛|
|entry (a) Navigability Issue|optima local|𝑛 (b) Clusterability Issue||

Vector similarity search is a key component in many artificial intelligence (AI) applications. While graph-based methods represent the state-of-the-art for vector similarity search, existing graph indexes often suffer from poor navigability and clusterability in Out-Of-Distribution (OOD) scenarios (e.g., database and queries are sourced from different modalities). Recent studies attempt to mitigate this issue by projecting and integrating query-derived auxiliary index structures, but these approaches remain largely heuristic and lack theoretical grounding. In this work, we propose Cross-Distribution Monotonic Graph (CDMG), a novel graph index designed to inherently support navigability and clusterability for OOD queries. CDMG introduces a fusion strategy that bridges the distribution gap between database and query vectors, ensuring a key property—cross-distribution monotonicity—that guarantees monotonic search paths for OOD queries on graph indexes. Theoretical analysis demonstrates that CDMG achieves optimal search time complexity in OOD scenarios compared to existing graph indexes. Furthermore, we introduce CDMG+, a practical variant of CDMG that improves construction efficiency and search robustness by optimizing distance computation, query sampling, and graph structure. Extensive empirical evaluations demonstrate that our techniques outperforms state-of-the-art methods, achieving up to a

3.6× speedup at a 95% recall rate across all real-world OOD datasets. PVLDB Artifact Availability: The source code, data, and/or other artifacts have been made available at [https://github.com/Lsyhprum/CDMG](https://github.com/Lsyhprum/CDMG). 1 INTRODUCTION The growing prevalence of multimedia content, encompassing images, text, and audio, has intensified the need for effective multimedia data management solutions [20, 50, 74]. A widely adopted approach involves embedding such multimedia content into high-dimensional space as *vectors* using neural networks [30, 32, 59], followed by employing *vector similarity search* [13, 14, 70, 83] to retrieve relevant content. Specifically, given a collection of database vectors and a query vector, an index is constructed based on the database vectors. Vector similarity search then efficiently retrieves the *𝑘* nearest neighbors of the query from the database vectors using the prebuilt vector index. Recent studies and industrial systems have demonstrated that *graph-based vector indexes* achieve state-of-the-art performance in balancing search efficiency and accuracy [2, 7, 34, 56, 64, 71]. In these graph indexes, each vertex represents a
## Jiahui Wang

### Hangzhou Dianzi University

jiahuiwang@hdu.edu.cn

routing path of 𝑞!"routing path of 𝑞##"result of 𝑞!"result of 𝑞##" 𝑞<sub>('</sub> 𝑛!𝑛$

! 𝑞 𝑞<sub>('</sub> &&' % " &&'

<sub>#</sub>

Figure 1: The navigability and clusterability of graph indexes.

database vector, and each edge establishes a similarity relationship between corresponding vectors. The search process over a graph index employs a greedy routing strategy, guiding vertex traversal to rapidly approach the nearest neighbors of the query. Owing to their *navigability* for search processes [10, 31, 44] and *clusterability* for search results [11, 26, 66], graph-based indexes have become dominant in vector similarity search algorithms and are extensively integrated into mainstream vector databases [20, 22, 48, 67, 71]. When designing graph indexes, it is typically assumed that both query and database data originate from single-modal sources [8, 9, 33, 40, 52, 62]. In such cases, query vectors follow the same distribution as database vectors in high-dimensional space, referred to as In-Distribution (ID) queries (e.g., *𝑞𝑖𝑑* in Figure 1). This assumption is a critical prerequisite for the effectiveness of current graph-based methods in addressing the vector similarity search problem [18, 44, 49]. With the rise of multi-modal learning models, particularly multi-modal large language models (LLMs), multimodal retrieval has become a fundamental component of many emerging artificial intelligence (AI) applications, such as retrieval-augmented generation (RAG) [1, 36, 41, 79]. In this context, query and database data are derived from multi-modal sources. Due to the inherent *modality gap* [35, 38, 55], query vectors often exhibit a different distribution from database vectors, even when they share the same representation space [68, 86]. Such queries are termed Out-Of-Distribution (OOD) queries (e.g., *𝑞𝑜𝑜𝑑* in Figure 1). While current graph indexes perform well for ID queries, OOD queries face significant performance bottlenecks. For instance, on the Text-to-Image dataset [3], OOD queries using HNSW [44]—a state-of- the-art graph index—experience a 7× increase in search latency compared to ID queries to achieve the same search accuracy. Efficiently and effectively handling OOD queries on graph indexes remains a widely recognized challenge in the field [7, 26, 64]. We identify that the performance bottleneck of OOD queries stems from the disruption of two key properties—navigability and

<!-- Page 2 -->

### clusterability—that are essential for ensuring high search efficiency

### and accuracy in the graph indexes.

- Navigability Issue. Current graph indexes rely on the assumption that queries are located near database vectors, enabling the greedy routing to converge efficiently toward the nearest neighbors [17, 49]. However, OOD queries deviate significantly from database vectors, violating this assumption. For instance, on the WebVid dataset [4], OOD queries are positioned up to 9× farther from database vectors compared to ID queries. As illustrated in
Figure 1(a), while the search path for the ID query *𝑞𝑖𝑑* is short
 and direct, the greedy routing for the OOD query *𝑞𝑜𝑜𝑑* oscillates unpredictably, as the graph index directs the search toward local optima rather than true nearest neighbors.
- Clusterability Issue. Clusterability in graph indexes assumes that query results are closely grouped, allowing the greedy routing to accurately locate all nearest neighbors once one is identified [66]. However, the nearest neighbors of OOD queries tend to be widely dispersed and scattered across the graph index. For example, on WebVid, distances between neighbors of OOD queries are more than 2× larger than those of ID queries. As shown in
Figure 1(b), the five results (*𝑛*1to *𝑛*5) for the OOD query *𝑞𝑜𝑜𝑑*
 exhibit poor clusterability, with the neighbors of each result not overlapping with the retrieved results. In contrast, the nearest neighbors of the ID query *𝑞𝑖𝑑* demonstrate strong clusterability. To address the challenges posed by OOD queries on graph indexes, current solutions enhance existing graph indexes by incorporating the distribution information of OOD queries during index construction. Two state-of-the-art methods are particularly note- worthy: RobustVamana [26] and RoarGraph [7]. RobustVamana improves navigability and clusterability by adding edges among the nearest neighbors associated with OOD queries. By this optimization, RobustVamana reduces query latency by 15% compared to its original counterpart while maintaining the same search accuracy on the Text-to-Image dataset. Similar to RobustVamana, RoarGraph refines the edge selection for the nearest neighbors linked to OOD queries in the database vectors, thereby further enhancing navigability and clusterability. Consequently, RoarGraph achieves a 33% reduction in query latency compared to RobustVamana under the same search accuracy on the Text-to-Image dataset. Despite recent advancements, the search performance of OOD queries still lags significantly behind that of ID queries on graph-based indexes. For instance, even with the leading RoarGraph, the query latency for OOD queries remains 5.2× higher than for ID queries on the Text-to-Image dataset. We argue that these optimizations fail to fundamentally resolve the navigability and clusterability problems of OOD queries. Specifically, they refine the neighbor relationships for only a small subset of database vectors (i.e., vertices) using a sampled query set (typically 10% of the database [7]), leav- ing the *majority of vertices unoptimized* (referred to as unoptimized vertices). These unoptimized vertices employ the same candidate acquisition and neighbor selection strategies as the original graph indexes (see Figure 4), which are oriented toward ID queries. Consequently, OOD queries are forced to traverse numerous unnecessary vertices on the graph index, guided by these unoptimized vertices. Furthermore, if a nearest neighbor of an OOD query lies within these unoptimized vertices, the query cannot efficiently locate other
Qiang Yue, Mengzhao Wang, Xiaoliang Xu, Cheng Long, and Jiahui Wang

nearest neighbors through it. Our evaluation reveals that even with RoarGraph, 57% of vertices remain unoptimized on the Text-to-

Image dataset. Therefore, existing optimizations only partially

1 mitigate the navigability and clusterability issues in graph indexes. Additionally, current optimization methods are *heuristic in nature* *and lack theoretical guarantees for search efficiency and accuracy*, limiting their applicability in real-world systems. Our Solution and Contributions. This paper presents a novel graph index tailored for OOD queries. We first demonstrate that by reasonably bridging the database and query distributions, OOD greedy routing can be guided to follow the ID routing behavior. We then introduce a key property, cross-distribution monotonicity (§3.1), which ensures that the search path from a start vertex to the nearest neighbor of an OOD query remains monotonic on the graph indexes. Analogous to the established monotonicity property for ID queries [18, 49], cross-distribution monotonicity serves as a theoretical basis for achieving high search performance in OOD settings. Building upon this theoretical framework, we design the Cross-Distribution Monotonic Graph (CDMG) (§3.2), which integrates OOD query information into database vectors and connects neighbors under both distributions for each vertex via a new edge selection algorithm. Our theoretical analysis proves that CDMG achieves optimal OOD search complexity compared to existing methods. To enhance construction efficiency and search robustness, we present CDMG+, a practical variant of CDMG that optimizes distance calculations, query sampling, and graph structure (§3.3). Our key contributions are highlighted as follows:

- We identify the key fundamental theoretical limitation of graph-based methods when handling OOD queries. (§2.2)
- We formalize cross-distribution monotonicity, which ensures monotonic routing of OOD queries on graph indexes. (§3.1)
- We propose CDMG, an index characterized by cross-distribution monotonicity, and prove that greedy search under this property achieves an optimal complexity for OOD queries. (§3.2)
- We propose CDMG+, which enhances construction efficiency and search robustness through a series of simple yet effective optimization techniques. (§3.3)
- Extensive evaluations conducted on real-world OOD datasets clearly demonstrate the superiority of our proposed method. (§4) Additionally, §6 reviews related work, §5 discusses potential future research directions, and §6 concludes the paper.
### 2 PRELIMINARIES AND PROBLEM

2.1 Preliminaries Definition 1 (Vector Similarity Search [13, 14, 70, 83]). *Given a database* X *and a query vector 𝑞, let 𝛿*(·*,*·) *represent the* *similarity metric between two vectors. Vector similarity search aims to* *retrieve the 𝑘 results* R(*𝑞*)={*𝑛 ,...,𝑛*1 *𝑘*} *for the query vector 𝑞. Each* *result 𝑛𝑖*∈R(*𝑞*) *satisfies 𝛿*(*𝑛 ,𝑞𝑖*)≤ *𝛿* (*𝑥 ,𝑞𝑗*) *for all 𝑥𝑗*∈X\R(*𝑞*)*.* Achieving exact vector similarity search in a large database is generally impractical due to computational and storage constraints 1 The percentage of vertices retaining identical edges was calculated between two RoarGraph versions: one built with and the other without queries. Both follow the best parameters based on official guidelines [7].

<!-- Page 3 -->

Efficient and Robust Out-Of-Distribution Vector Similarity Search with Cross-Distribution Monotonic Graph

Table 1: Frequently used notations

### Notation Description

R *𝐷* *𝐷*-dimensional space. X*,𝑥𝑖* ∈X The database and the database vector. Q*,𝑞𝑖* ∈Q The query set and the query vector. *𝑛𝑖* The *𝑖*-th nearest neighbor of a vector. *𝛿*(·*,*·) The similarity metric between two vectors. The average distance from each database vector in Δ*𝑖* X to its *𝑖*-th nearest neighbor. A graph index*𝐺* where the set of vertices and *𝐺* (*𝑉,𝐸*) edges are*𝑉* and *𝐸*, respectively. *𝑁𝑜𝑢𝑡*(*𝑥𝑖*) The out-neighbors of database vector *𝑥𝑖* in *𝐺*.

[19, 27]. Therefore, existing methods focus on finding approximate results to balance efficiency and accuracy while minimizing overhead [23, 63, 84, 85]. We adopt the recall rate *Recall@k* to measure the quality of the*𝑘* results for*𝑞*. The *Recall@k* is defined as follows:

<u>|R(𝑞)∩G(𝑞)|</u> *𝑅𝑒𝑐𝑎𝑙𝑙*@*𝑘* =*,* (1) *𝑘* where G represents the *𝑘* nearest neighbors of the query vector *𝑞*. A rich line of vector similarity search methods has been devel- oped, with graph-based methods demonstrating particular promise. We introduce the graph-based vector similarity search as follows: Graph-Based Vector Similarity Search. Graph-based vector similarity search consists of two processes: construction and search. In the construction process, a graph index [37, 47, 69, 80] is constructed from the database X. Specifically, given a database X and a predefined edge selection criterion, the graph index is defined as *𝐺* = (*𝑉,𝐸*). The vertices *𝑉* correspond to the vectors in X, and an edge (*𝑥𝑖,𝑥𝑗*) ∈ *𝐸* is established between two close vectors *𝑥𝑖* and *𝑥𝑗* based on the predefined edge selection criterion. In the search process, graph-based vector similarity search utilizes a greedy routing algorithm on the graph index*𝐺* to identify the top-*𝑘* results for a query*𝑞* [2, 6, 39, 49, 57, 76]. Starting from a random or pre-selected entry vector*𝑒*, the greedy routing iteratively converges toward vectors that are closer to the query *𝑞*. At each iteration, the closest unvisited vector from the candidate set is selected as the next hop, and its out-neighbors are added to the candidate pool. The process terminates when all candidates have been visited, and the top-*𝑘* candidates are returned as the final results. Out-of-Distribution Query. Existing graph indexes are fundamentally designed under the assumption that the database X and the query vector *𝑞* originate from the same underlying distribution [5, 7, 26]. However, with the rise of complex AI systems [22, 24, 73], recent developments have increasingly focused on generating vectors learned from multiple modalities to tackle cross-modal tasks. In such scenarios, queries often significantly deviate from the distribution of the database. Given a database X, where the vectors are independently and identically distributed (i.i.d.) from a distribution *𝑃𝑖𝑑*, a query *𝑞* is considered out-of-distribution if it is drawn i.i.d. from a distribution *𝑃𝑜𝑜𝑑* that differs from *𝑃𝑖𝑑*, i.e., *𝑃𝑜𝑜𝑑* ≠ *𝑃𝑖𝑑*. Current vector similarity search methods lack rigorous quantifi- cation of OOD queries [26]. A common empirical approach involves

1e2 Text-to-Image 1e2 LAION 1e2 WebVid OOD Query OOD Query OOD Query 5 ID Query 6 ID Query 6 ID Query 4 4 4 Count 3 2 2 2 1 0 0 10 20 0 0 10 20 0 0 20 Mahalanobis Distance

Figure 2: Mahalanobis distances of OOD and ID queries.

using the Mahalanobis distance [42] histogram for evaluation. The Mahalanobis distance*𝑑* (*𝑞,𝑃*) quantifies the deviation of a query *𝑀 𝑖𝑑* *𝑞* from the database distribution *𝑃𝑖𝑑*. As depicted in Figure 2, we present the Mahalanobis distances of OOD queries and ID queries relative to the database for three representative real datasets: Text-to-Image [3], LAION [53], and WebVid [4]. The histograms demonstrate that the Mahalanobis distances for OOD queries exhibit significant deviations compared to those of ID queries.

2.2 Problem Analysis Based on the preceding definitions, this paper investigates the problem of designing a graph-based method specifically optimized for OOD queries. Recent graph-based methods are predominantly derived from the Monotonic Relative Neighborhood Graph (MRNG) [18], which ensures both navigability and clusterability. We first provide a detailed introduction to MRNG and its variants, followed by an analysis of their inherent limitations. Finally, we highlight the shortcomings of recent OOD-oriented graph-based methods. MRNG and Its Variants. As shown in Figure 4 (the black arrow), the construction of MRNG involves two important phases [71]: *candidate acquisition* and *neighbor selection*, which are designed to enable clusterability and navigability. <u>Clusterability of MRNG</u>. Clusterability refers to the likelihood that neighbors of a neighbor are also neighbors [11]. When the greedy routing identifies a result, the clusterability of graph indexes enables the rapid retrieval of all nearest neighbors [66, 75]. The clusterability of a graph index can be quantified as the ratio of mutual neighbors within the *𝑘* nearest neighbors of a query*𝑞*. Higher clusterability increases the probability that the nearest neighbors of *𝑞* are also neighbors of each other. To achieve high clusterability, MRNG and its variants [18, 49] acquire candidates using an approximate *𝑘*-Nearest Neighbor Graph (KNNG) (e.g., KGraph [11] and NSW [43]), which is constructed through either an iterative or incremental process [68]. The iterative process refines neighbors by iteratively updating connections, while the incremental process refines neighbors through a greedy search. In both cases, each vertex is linked to vertices that are closer to it in the final KNNG. <u>Navigability of MRNG</u>. The concept of navigability originates from early research on the small-world phenomenon [31, 45]. A graph index is considered navigable if the greedy routing can always find a monotonic path between any two vertices [81]. While a complete graph is navigable, it is inefficient [10]. The goal of MRNG and its variants is to construct a sparser graph that maintains monotonic routing [18, 77]. However, they assume that the query is a vertex within the graph index [18, 87], which is often impractical in real-world scenarios. Recent advancements, including NSSG [18],

<!-- Page 4 -->

Table 2: Comparison of *𝛿*(q*,*n1) and ∆i on OOD/ID datasets2

Dataset ↓ *𝛿*(q*,*n1) ∆1 ∆20 ∆40 ∆60 ∆80 ∆100

||𝛿 (q, n1)|∆1|∆20|∆40|∆60|∆80|∆100|Input: vector 𝑥𝑖, candidate set V, relaxation parameter 𝜏 > 0,|
|---|---|---|---|---|---|---|---|---|
|WebVid|0.719|0.109|0.176|0.190|0.199|0.206|0.211|threshold of out-degree 𝑜 > 0|
|LAION|0.695|0.247|0.338|0.366|0.375|0.385|0.392|Output: out-neighbors 𝑁𝑜𝑢𝑡 (𝑥𝑖 )|
|MNIST|0.261|0.253|0.419|0.471|0.505|0.532|0.554|Heap ←(V∪ 𝑁𝑜𝑢𝑡 (𝑥𝑖 ))\{𝑥𝑖 };|
|DEEP|0.575|0.574|0.831|0.907|0.958|0.997|1.028|𝑁𝑜𝑢𝑡 (𝑥𝑖 )←∅;|

𝑛%𝑛%

𝑒 𝑞!" 𝑒 ##" 𝑞##" 𝑛%𝑥$ 𝑞

𝑛&𝑒 𝜏 𝑛& 𝑛 & ≫ 𝜏 𝜏

𝜏

**(a) (b)(b) (c)(b) (c)**
Figure 3: (a) illustrates the greedy routing for an ID query

*𝑞𝑖𝑑* over MRNG. (b) and (c) compare the greedy routing for an OOD query *𝑞𝑜𝑜𝑑* under different *𝜏* settings.

Vamana [27], and *𝜏*-MNG [49], relax this limitation but still operate under the assumption that the query and database share an identical distribution [16, 17, 78, 83]. For instance, given an approximate KNNG, the state-of-the-art *𝜏*-MNG employs RobustPruning (described in Algorithm 1) to select the out-neighbors *𝑁𝑜𝑢𝑡*(*𝑥𝑖*) of *𝑥𝑖* based on a query relaxation parameter *𝜏* in two steps: (1) *𝑥𝑖* directly links to *𝑥𝑗* if *𝛿* (*𝑥𝑖,𝑥𝑗*) ≤ 3*𝜏* (line 9); (2) If *𝛿* (*𝑥𝑖,𝑥𝑗*) *>* 3*𝜏*, *𝑥𝑖* links to *𝑥𝑗* if and only if no existing neighbor *𝑥* ∗ ∈ *𝑁𝑜𝑢𝑡*(*𝑥𝑖*) occludes the edge from *𝑥𝑖* to *𝑥𝑗* (line 14). As illustrated in Figure 3(a), if *𝛿* (*𝑞𝑖𝑑,𝑛*1) ≤ *𝜏*, the greedy routing over *𝜏*-MNG moves at least *𝜏* closer to the query *𝑞𝑖𝑑* in each iteration [49], ultimately guaranteeing the discovery of the nearest neighbors (*𝑛*1and *𝑛*2). Limitations of MRNG. For an ID query, the query results tend to be mutual neighbors, and the query is close to its nearest neighbor. Thus, when constructing graph indexes for ID queries, MRNG can leverage KNNG to approximate the neighbor relationships among query results, and an appropriate query relaxation parameter *𝜏* can be readily determined to satisfy monotonicity. In contrast, for an OOD query: (1) *The query results are widely dispersed*, and the misalignment between KNNG and the actual query results fundamentally violates the principle of MRNG. (2) Additionally, *the query* *itself is often distant from its nearest neighbor*, further complicating the process. The underlying cause can be illustrated as follows. If the query relaxation parameter remains small, the condition *𝛿* (*𝑞𝑜𝑜𝑑,𝑛*1) *< 𝜏* is rarely satisfied. This impedes the greedy routing from converging along a specific direction, trapping it in a local minimum [49]. Let *𝐵*(*𝑞,𝑟*) denote a ball of radius *𝑟* centered at *𝑞*, and *𝑅*(*𝑞,𝑟*1*,𝑟*2) represent a ring within *𝐵*(*𝑞,𝑟*2) but outside *𝐵*(*𝑞,𝑟*1). As shown in Figure 3(a), given an ID query *𝑞𝑖𝑑* and an entry*𝑒* ∈ *𝑅*(*𝑞𝑖𝑑,*4*𝜏,*5*𝜏*), the greedy routing on the graph index identifies the next hop in *𝑅*(*𝑞𝑖𝑑,*3*𝜏,*4*𝜏*), progressing until it reaches the nearest neighbors (*𝑛*1and *𝑛*2). In contrast, for an OOD query *𝑞𝑜𝑜𝑑* (Figure 3(b)), all vectors in *𝑅*(*𝑞𝑜𝑜𝑑,*3*𝜏,*4*𝜏*) may be considered potential nearest neighbors due to the disparity between *𝑞𝑜𝑜𝑑* and the

|𝛿 (𝑞,𝑛|) denotes the distance between a query 𝑞 (either an ID query 𝑞|or an OOD|
|---|---|---|
|query 𝑞 for WebVid and LAION, whereas Euclidean distance is used for MNIST and DEEP.|) and its nearest neighbor 𝑛|) and Δ|

*𝑖𝑑* *𝑜𝑜𝑑* 1. Cosine similarity measures *𝛿* (*𝑞,𝑛*1 *𝑖*

### Algorithm 1 RobustPruning(𝑥𝑖, V, 𝜏, 𝑜)

1:

2: 3: 4: 5: while Heap ≠ ∅ and out-degree |*𝑁𝑜𝑢𝑡*(*𝑥𝑖*)| *< 𝑜* do 6: *𝑥𝑗*← closest vector to *𝑥𝑖*in Heap; 7: Heap ← Heap\{*𝑥𝑗*}; 8: // Step 1. Directly link if within relaxation radius 9: if *𝛿* (*𝑥 ,𝑥𝑖 𝑗*)≤ 3*𝜏* then 10: *𝑁𝑜𝑢𝑡*(*𝑥𝑖*)← *𝑁𝑜𝑢𝑡*(*𝑥𝑖*)∪{*𝑥𝑗*}; 11: continue; 12: end if 13: // Step 2. Ensure the new link is not occluded ∗ ∗ 14: if ∀*𝑥* ∈ *𝑁𝑜𝑢𝑡*(*𝑥* *𝑖* )s.t. *𝛿* (*𝑥𝑗,𝑥*)+ 3*𝜏 > 𝛿*(*𝑥𝑖,𝑥𝑗*) then 15: *𝑁𝑜𝑢𝑡*(*𝑥𝑖*)← *𝑁𝑜𝑢𝑡*(*𝑥𝑖*)∪{*𝑥𝑗*}; 16: end if 17: end while 18: return *𝑁𝑜𝑢𝑡*(*𝑥𝑖*)

Input Data Construction Process Graph Index 2 1 Candidate 3 Neighbor MRNG Database 𝒳 Acquisition Selection 1 2 2 3 RoarGraph 1 Initialization with Insert Robust-Query Set 𝒬 Query NNs Query NNs Vamana

Figure 4: Overview of construction pipeline for ID-oriented

graph-based methods (MRNG) and OOD-oriented graph-based methods (RobustVamana and RoarGraph).

database. As a result, the greedy routing may converge to a false result *𝑥*3, necessitating backtracking and traversal of numerous unnecessary vertices to locate the true nearest neighbors. When the query relaxation parameter is set to*𝛿* (*𝑞𝑜𝑜𝑑,𝑛*1), it sub- stantially exceeds the average distance between database vectors and their respective nearest neighbors, resulting in the condition *𝛿* (*𝑥𝑖,𝑥𝑗*)≤ 3*𝜏* is always met. As Table 2 shows, unlike ID datasets (MNIST and DEEP), where*𝛿* (*𝑞𝑖𝑑,𝑛*1) closely approximates Δ1, OOD datasets (WebVid and LAION) exhibit*𝛿* (*𝑞𝑜𝑜𝑑,𝑛*1) values far exceed- ing Δ100. Note that 100 exceeds the typical out-degree setting for *𝜏*-MNG, which has an expected out-degree of *𝑂*(ln*𝑛*) (e.g., for a billion-scale database, the expected out-degree of*𝜏*-MNG is only 21). In this case, RobustPruning selects out-neighbors solely based on *𝛿* (*𝑥𝑖,𝑥𝑗*), where*𝑥𝑗* with smaller*𝛿* (*𝑥𝑖,𝑥𝑗*) is preferred. Thus, when*𝜏* is determined by*𝛿* (*𝑞𝑜𝑜𝑑,𝑛*1), the*𝜏*-MNG degenerates into a KNNG, which lacks navigability and has proven inefficient in prior work [18, 68]. Figure 3(c) illustrates how an excessively large *𝜏* still leads to prolonged search paths and local optima for OOD queries. Limitations of OOD-oriented Methods. As illustrated in Figure 4, recent advancements have focused on optimizing graph indexes for OOD queries by incorporating query distribution information during their construction. For instance, RobustVamana extends an ID-oriented MRNG variant (i.e., Vamana) and utilizes the nearest

<!-- Page 5 -->

𝑃 !𝑃"𝑃&'(𝑃<sub>)</sub> 𝑃<sub>*+#,</sub>

𝑛"𝑛" 𝑛 𝑥 $ " 𝑒 𝑞##$𝑞$ 𝑞 "𝑞##$

<sup>∗</sup> 𝑥<sub>#</sub> 𝑞<sub>#</sub> 𝑛<sup>∗</sup> 𝑒 <sup>∗</sup> 𝑛 𝑛 𝑛! 𝑛 % 𝑛!

**(a) (b) (c)**
Figure 5: (a) illustrates the difference in routing behavior be-

tween ID and OOD queries. (b) and (c) depict the construction and search process for cross-distribution monotonic path.

neighbors of corresponding queries to fill vertices with available space in their neighbor lists. Similarly, RoarGraph directly initial- izes part of the graph index from a query-derived nearest neighbor subgraph (i.e., a projected graph [7]). However, these methods *continue to rely on ID-oriented candidate acquisition and neighbor selection*, which do not align with the inherent characteristics of OOD queries [26]. Specifically, during candidate generation, most candidates are selected based on the distances between database vectors, which fail to capture the underlying cluster structure of the query results. Additionally, the edge occlusion rule employed for neighbor selection becomes invalid when *𝛿* (*𝑞,𝑛*1) significantly exceeds the average distance between database vectors and their nearest neighbors (i.e., Δ1 in Table 2), leading to inefficient navigation. In summary, the limited performance of existing solutions stems from their reliance on the ID paradigm. This motivates us to (1) *generalize MRNG theory for OOD queries*, and (2) *optimize candidate* *acquisition and neighbor selection within the OOD context to enhance* *navigability and clusterability on graph indexes*.

### 3 THE PROPOSED CDMG

3.1 Cross-Distribution Monotonicity In this section, we investigate the following question: *Can OOD* *queries perform efficient monotonic search on graph indexes in a* *manner analogous to the routing behavior in ID queries?* To address this, we analyze the differences in routing behavior between ID and OOD queries on MRNG. In ID scenarios, the alignment between the distributions of the database and query vectors ensures a monotonic search path on MRNG: even when initiated from a distant entry point, greedy routing can efficiently locate the query’s nearest neighbor by following a monotonic path [18, 49]. In contrast, the search paths induced by OOD queries on MRNG lack monotonicity due to distributional discrepancies. Specifically, the routing process for an OOD query on MRNG unfolds in two distinct phases: (1) <u>In the first phase</u>, while the visited vertices remain distant from the distribution boundary, greedy routing follows a monotonic path and progressively converges toward the OOD query. (2) <u>In the second phase</u>, as the search approaches the distribution boundary, convergence toward the OOD query is disrupted due to the distribution gap, leading to a failure in maintaining a monotonic path. Example 1. *As shown in Figure 5(a), consider a database* X *(points* *in the blue region) and an OOD query set* Q *(points in the orange*
*region). Let* X∗ *denote the minimum convex hull that encloses both* X *and* Q*. The bridging set is defined as* B = X∗ \(X∪Q)*, which* *characterizes the region separating* X *and* Q*. Conceptually, an OOD* *𝑜𝑜𝑑* ∗ *query 𝑞 w.r.t.* X *can be interpreted as an ID query within the*

∗ *extended space*X*. When performing search on the MRNG constructed* *over* X∗*, 𝑞𝑜𝑜𝑑 follows a monotonic path 𝑃* = [*𝑃*1*,𝑃𝑏𝑟𝑖,𝑃𝑞*] *and finds* *its nearest neighbor 𝑛* ∗ *as the result, where 𝑃*1*, 𝑃, and 𝑃𝑞 consist of* *𝑏𝑟𝑖* *points from* X*,* B*, and* Q*, respectively. However, in practical settings,* *the bridging set* B *is not available, leading to erratic oscillations in* *the second phase of the OOD query search, denoted as 𝑃*2*.*

To bridge the distribution gap, a natural approach is to establish connections between database and query vectors during graph construction, thereby enabling monotonic search over the query set Q in the second-phase routing. However, this method is inefficient as it requires the graph to explicitly index Q, resulting in elongated routing paths and the inclusion of redundant query vectors in the retrieval results. To overcome this limitation, an intuitive strategy is to incorporate the topological structure induced by the MRNG built on Q into the MRNG constructed on X. Consequently, the two routing phases on the resulting integrated graph index can be characterized as follows: (1) <u>In the first phase</u>, greedy routing proceeds along a monotonic path guided by the database distribution toward the distribution boundary, and (2) <u>in the second phase</u>, routing continues along a monotonic path shaped by the query distribution until reaching the target query result.

Example 2. *As illustrated in Figure 5(b), each query vector in* Q *is linked to its nearest database vector (red dashed lines). Additional* *projected edges (orange lines) can be introduced between database* *vectors based on the neighborhood relationships of their corresponding* *query vectors. For example, since 𝑞*2*is connected to 𝑛* ∗ *and 𝑞*4*, 𝑛*2*is* *linked to 𝑛*1*and 𝑥*4*. During the search process (Figure 5(c)), 𝑃*1*and* *𝑃𝑝𝑟𝑜𝑗 combine to form a new monotonic routing path 𝑃* = [*𝑃*1*,𝑃𝑝𝑟𝑜𝑗*] *entirely within* X*. The projected path 𝑃 traces the path 𝑃, re-* *𝑝𝑟𝑜𝑗 𝑞* *turning 𝑛*1*and 𝑛*2*as query results because their corresponding query* *vectors (𝑞*2*and 𝑛 ) are closer to 𝑞* ∗ *. The OOD routing path (i.e.,* *𝑜𝑜𝑑* *[𝑃*1*, 𝑃*2*]) in Figure 5(a) requires seven hops to retrieve a single correct* *result (𝑛*1*), whereas the monotonic routing path 𝑃 (i.e., [𝑃*1*, 𝑃𝑝𝑟𝑜𝑗])* *retrieves two results (𝑛*1*and 𝑛*2*) in just four hops.*

Definition and Notation. Based on the preceding analysis, our key insight is to construct a graph index over database vectors that ensures *cross-distribution monotonicity* for OOD queries. We formally define the cross-distribution monotonic path as follows:

### Definition 2 (Cross-Distribution Monotonic Path). For

*a database* X *and a query set* Q*, each query 𝑞* ∈Q *is associated with* *its nearest database vector 𝑥* ∈X*. Let 𝐺 be a graph over* *𝑖* X*, and let the* *query relaxation parameter𝑖 𝜏* ≥ 0 *be a constant. A routing path 𝑃* =
[*𝑥 ,...,𝑥,...,𝑥 ,𝑛*] *on 𝐺 is a cross-distribution monotonic path for*
1 *𝑏 ℓ* 1 *an OOD query 𝑞 if it satisfies:* ∀*𝑖* = 1*,...,𝑏* − 1*, 𝛿*(*𝑞,𝑥*)− *𝜏 >* *𝑜𝑜𝑑 𝑜𝑜𝑑 𝑖* *𝛿* (*𝑞,𝑥*)*,* ∀*𝑗* = *𝑏,...,ℓ* − 1*, 𝛿*(*𝑞,𝑞*)−*𝜏 > 𝛿*(*𝑞,𝑞*)*, and* *𝑜𝑜𝑑 𝑖*+1 *𝑜𝑜𝑑 𝑗 𝑜𝑜𝑑 𝑗*+1 *𝛿* (*𝑞,𝑞*) *> 𝛿*(*𝑞,𝑛* ∗ )*, where 𝑛* ∗ *is the nearest query to 𝑞 and* *𝑜𝑜𝑑 ℓ 𝑜𝑜𝑑 𝑜𝑜𝑑* *𝑛* 1*is the nearest database vector of 𝑛* ∗ *.*

The cross-distribution monotonic path extends the concept of the *𝜏*-monotonic path [49] to the OOD scenario. When the query set Q and the database X follow the same distribution, a monotonic search path under the query distribution is equivalent to

<!-- Page 6 -->

𝑥<sub>'</sub> 𝑥$ 𝒳 𝑥<sub>'</sub> 𝑥$ 𝒳 𝑥<sub>'</sub> 𝑥$ 𝒳 𝑥<sub>#</sub> 𝑥&𝑥<sub>#</sub> **3 1** 𝑥&𝑥<sub>#</sub> 𝑥& 𝑥 "𝑥" **2** 𝑥" 𝑥 %𝜏𝑥%𝑥% 𝑞 $ 𝑞 & 𝑞 % 𝑞 $ 𝑞 & 𝑞 % 𝑞 $ 𝑞 & 𝑞 % 𝑞<sub>'</sub> 𝑞<sub>'</sub> **3** **4** 𝑞<sub>'</sub> q " 𝑥 q "𝑥 q" 𝑥 𝒬 q<sub>#</sub> q! ! 𝒬 q<sub>#</sub> **1** **2** q ! ! 𝒬 q<sub>#</sub> q! !

(a) Initialization (b) Neighbor Selection (b) Merging
Figure 6: Illustration of the CDMG construction process.

a monotonic search path under the database distribution. In this case, the cross-distribution monotonic path condition reduces to: ∀*𝑖* = 1*,...,ℓ* − 1,*𝛿* (*𝑞𝑜𝑜𝑑,𝑥𝑖*)−*𝜏 > 𝛿*(*𝑞𝑜𝑜𝑑,𝑥𝑖*+1), which corresponds to the *𝜏*-monotonic path in the ID scenario. Building upon this extended concept, we introduce cross-distribution monotonicity:

### Definition 3 (Cross-Distribution Monotonicity). Given

*a database* X *and query set* Q*, let 𝜏 >* 0 *be a constant. A graph 𝐺 over* X *has cross-distribution monotonicity if for any query 𝑞𝑜𝑜𝑑 satisfying* *𝛿* (*𝑞𝑜𝑜𝑑,𝑛* ∗ )≤ *𝜏, there exists a cross-distribution monotonic path in 𝐺* *from any entry point to the result 𝑛*1*. Here, 𝑛* ∗ *is the nearest query to* *𝑞𝑜𝑜𝑑 in* Q *and 𝑛*1*is the nearest database vector to 𝑛* ∗ *in* X*.*

3.2 Cross-Distribution Monotonic Graph We introduce the Cross-Distribution Monotonic Graph (CDMG), which incorporates OOD query information for each vertex to enhance clusterability while maintaining monotonicity through a unified edge occlusion rule. The formal definition follows: Definition 4 (Cross-Distribution Monotonic Graph). *Given a database* X *and query set* Q*, let 𝜏* ≥ 0 *be a constant.* *A CDMG is a directed graph 𝐺 where for any vertices 𝑣𝑖, 𝑣𝑗*∈ *𝐺* *with corresponding vectors 𝑥𝑖, 𝑥𝑗*∈X *and queries 𝑞𝑖, 𝑞𝑗*∈Q*: (1) If* *𝛿* (*𝑞𝑖,𝑞𝑗*) ≤ 3*𝜏,* (*𝑣𝑖,𝑣𝑗*) ∈ *𝐺; (2) If 𝛿*(*𝑞𝑖,𝑞𝑗*) *>* 3*𝜏 and* (*𝑣𝑖,𝑣𝑗*) ∉ *𝐺,* *there exists 𝑣* ∗
*with* (*𝑣𝑖,𝑣* ∗ ) ∈ *𝐺 such that its corresponding 𝑥* ∗ *and 𝑞* ∗ *satisfy: 𝑥* ∗ ∈ (*𝐵*(*𝑥𝑖,𝛿*(*𝑥𝑖,𝑥𝑗*))∩ *𝐵*(*𝑥𝑗,𝛿*(*𝑥𝑖,𝑥𝑗*)− 3*𝜏*)) *and* *𝑞* ∗ ∈(*𝐵*(*𝑞𝑖,𝛿*(*𝑞𝑖,𝑞𝑗*))∩ *𝐵*(*𝑞𝑗,𝛿*(*𝑞𝑖,𝑞𝑗*)− 3*𝜏*))*.*

### Lemma 1 establishes the monotonicity guarantee of CDMG:

### Lemma 1. CDMG guarantees cross-distribution monotonicity.

Proof. If the edge (*𝑣*1*,𝑛*1)∈ *𝐺*, the path *𝑃* = [*𝑣*1*,𝑛*1] is a cross-distribution monotonic path. We now consider the case where (*𝑣*1*,𝑛*1) ∉ *𝐺*. <u>Case 1:</u> If *𝛿* (*𝑞*1*,𝑛* ∗

)≤ 6*𝜏*, then *𝐺* must contain a cross-
distribution monotonic path *𝑃* = [*𝑣*1*,𝑣*2*,𝑛*1]. This follows because *𝑣* 1must have an out-neighbor *𝑣*2such that *𝑞*2∈ *𝐵*(*𝑞*1*,𝛿*(*𝑞*1*,𝑛* ∗ ))∩ *𝐵*(*𝑛* ∗ *,𝛿*(*𝑞*1*,𝑛* ∗

)− 3*𝜏*). Since *𝛿* (*𝑞*2*,𝑛*
∗

)≤ 3*𝜏*, it follows that (*𝑣*2*,𝑛*1)∈
### 𝐺. When𝛿 (𝑞𝑜𝑜𝑑,𝑛

∗ )≤ *𝜏*, we have*𝛿* (*𝑞𝑜𝑜𝑑,𝑞*2)≤ *𝛿* (*𝑞*1*,𝑛* ∗

)− 2*𝜏* and
### 𝛿 (𝑞𝑜𝑜𝑑,𝑞1) > 𝛿(𝑞1,𝑛

∗ )−*𝜏*, which leads to*𝛿* (*𝑞𝑜𝑜𝑑,𝑞*2) *< 𝛿*(*𝑞𝑜𝑜𝑑,𝑞*1)− *𝜏*. Therefore, the path *𝑃* = [*𝑣*1*,𝑣*2*,𝑛*1] is a cross-distribution monotonic path. <u>Case 2:</u> If *𝛿* (*𝑞*1*,𝑛* ∗

) *>* 6*𝜏*, then *𝐺* must contain a cross-
### distribution monotonic path𝑃 = [𝑣1,𝑣2,...,𝑣𝑏,𝑣𝑏+1,...,𝑣ℓ−1,𝑣ℓ,𝑛1].

In the first phase, since *𝐺* is a CDMG, it must satisfy *𝛿* (*𝑥*2*,𝑛* ∗ ) *<* *𝛿* (*𝑥*1*,𝑛* ∗

)−3*𝜏* and*𝛿* (*𝑥*2*,𝑞𝑜𝑜𝑑*) *< 𝛿*(*𝑥*1*,𝑞𝑜𝑜𝑑*)−*𝜏*, and at each step, the
path moves closer to*𝑞𝑜𝑜𝑑* by at least*𝜏* until reaching a vertex *𝑣𝑏*. In the second phase, similar to the case above, we have *𝛿* (*𝑞𝑏*+1*,𝑛* ∗ ) *<* *𝛿* (*𝑞𝑏,𝑛* ∗

)− 3*𝜏* and *𝛿* (*𝑞𝑜𝑜𝑑,𝑞𝑏*+1) *< 𝛿*(*𝑞𝑜𝑜𝑑,𝑞𝑏*)−*𝜏*. Eventually, the
search reaches a vertex *𝑣ℓ*−1such that *𝛿* (*𝑞ℓ*−1*,𝑛* ∗

) *<* 6*𝜏*, whose
### monotonicity has already been established. □

### Algorithm 2 BuildBipartiteGraph(X,Q)

1: Input: database X, query set Q 2: Output: Bipartite graph *𝐺𝑏𝑟𝑖* 3: // Step 1. Query-base assignment 4: *𝐺𝑏𝑟𝑖* ←∅*,* C←∅*,*A←X; 5: for ∀*𝑞𝑖*∈Q do 6: *𝑥𝑖* ← closest database vector to *𝑞𝑖* in X; 7: C←C∪{⟨*𝑥 ,𝑞* ⟩}; *𝑖 𝑖* 8: end for 9: while C ≠ ∅ do 10: ⟨*𝑥𝑖,𝑞𝑖*⟩← pair in C with the largest distance *𝛿* (*𝑥𝑖,𝑞𝑖*); 11: *𝐺𝑏𝑟𝑖* ← *𝐺𝑏𝑟𝑖* ∪(*𝑥𝑖,𝑞𝑖*)*,* C←C\{⟨*𝑥𝑖,𝑞𝑖*⟩}; 12: A←A\{*𝑥𝑖*}; *⊲* Mark *𝑥𝑖* as occupied 13: end while 14: // Step 2. Assign remaining database vectors 15: for ∀*𝑥𝑖* ∈A do 16: *𝑞𝑖* ← closest query to *𝑥𝑖* in Q; 17: *𝐺𝑏𝑟𝑖* ← *𝐺𝑏𝑟𝑖* ∪(*𝑥𝑖,𝑞𝑖*); 18: end for 19: return *𝐺𝑏𝑟𝑖*

Construction Process. As shown in Figure 6, CDMG is constructed through three components: (1) Initialization, (2) Neighbor Selection, and (3) Merging. We detail these components as follows.

<u>(1) Initialization.</u> Given a database X and a query set Q, we construct a 1-regular bipartite graph *𝐺𝑏𝑟𝑖*(*𝑉,𝑈,𝐸𝑏𝑟𝑖*) to bridge data from different distributions, where*𝑉* corresponds to database vectors and *𝑈* to query vectors. The edge set *𝐸𝑏𝑟𝑖* ⊂ *𝑉* × *𝑈* forms a perfect match, pairing each *𝑥𝑖* ∈ *𝑉* with a *𝑞𝑖* ∈ *𝑈*. Specifically, we employ BuildBipartiteGraph (Algorithm 2) to construct *𝐺*bri. This procedure initially assigns each query to its nearest neighbor in the database (indicated by green arrows), giving priority to queries located near the core of the query distribution. Subsequently, any remaining unassigned database vectors are matched to their nearest queries (indicated by red arrows), ensuring each vertex references two vectors from different distributions. This approach leverages queries that are farther from the database and generates more dispersed results to effectively guide the construction process.
<u>(2) Neighbor Selection.</u> For each vertex *𝑣𝑖* ∈ *𝐺*, CDMG selects out-neighbors from both the database and query set. Despite differences in data distribution, the same edge occlusion rule (Algorithm 1) applies in both cases. To select out-neighbors *𝑁𝑞*(*𝑣𝑖*) under the query distribution, the remaining query vectors Q\{*𝑞𝑖*} are ranked in a candidate list Heap by their distance to *𝑞𝑖*. Each candidate *𝑞𝑗* ∈ Heap ∗ is evaluated for occlusion by existing out-neighbors: if any *𝑞* ∈ *𝑁𝑞*(*𝑣𝑖*) occludes*𝑞𝑗*, the edge (*𝑞𝑖,𝑞𝑗*) is discarded; otherwise,*𝑞𝑗* is added to *𝑁𝑞*(*𝑣𝑖*). This process iterates until all candidates in Heap are processed. The out-neighbors *𝑁𝑥*(*𝑣𝑖*) are selected analogously by applying the same rule to *𝑥𝑖* with candidates from X\{*𝑥𝑖*}.
<u>(3) Merging.</u> Since all elements in *𝑁𝑞*(*𝑣𝑖*) are query vectors, we replace each query with its corresponding database vector to elim- inate the need for indexed queries. These database vectors are then merged with the out-neighbors *𝑁𝑥*(*𝑣𝑖*) to form the final out-neighbor set *𝑁𝑜𝑢𝑡*(*𝑣𝑖*). As illustrated in Figure 6, *𝑁𝑞*(*𝑣*1) contains *𝑞* 2 *,𝑞*4*,𝑞*5, and*𝑞*6, which correspond to *𝑥*2*,𝑥*4*,𝑥*5, and *𝑥*6. The resulting *𝑁𝑜𝑢𝑡*(*𝑣*1) combines neighbors selected from both distributions.

<!-- Page 7 -->

|Algorithm 3 BuildCDMG(X|, Q|,𝜏,𝑜|)|
|---|---|---|---|
|1: Input: database X, query set Q, relaxation parameter|||𝜏 > 0,|
|threshold of out-degree|𝑜 >|0||
|2: Output: CDMG 3: // Initialization|𝐺|||
|4: 𝐺𝑏𝑟𝑖 ← BuildBipartiteGraph(X,Q); 5: 𝐺 ←∅ 6: for ∀ 𝑥𝑖 ∈X do|||⊲ Algorithm 2|
|7: 𝑞𝑖 ← 𝐺𝑏𝑟𝑖 8: // Dual neighbor selection;|( 𝑥𝑖 );|||
|9: 𝑁𝑥 ( 𝑣𝑖 )← RobustPruning(|||𝑥𝑖 ,X\{ 𝑥𝑖 }, 𝜏, 𝑜 ); ⊲ Algorithm 1|
|10: 𝑁𝑞 ( 𝑣𝑖 )← RobustPruning( 11: // Merging||𝑞𝑖|,Q\{ 𝑞𝑖 }, 𝜏, 𝑜 );|
|12: 𝑁𝑜𝑢𝑡 ( 𝑣𝑖 13: 𝐺 ← 𝐺 14: end for 15: return 𝐺|)← 𝑁𝑥 ( 𝑣𝑖 )∪ 𝐺𝑏𝑟𝑖 ∪ 𝑁𝑜𝑢𝑡 ( 𝑣𝑖 );||( 𝑁𝑞 ( 𝑣𝑖 ));|

|Algorithm 4 GreedySearch(|𝐺,𝑞,𝑘,𝐿|)|
|---|---|---|
|1: Input: graph index 2: Output: top- 3: 𝑒 ← entry vector in 4: // Initialize candidates container|𝐺, query 𝑞 𝑘 results of 𝑞 𝐺;|, results size 𝑘, candidates size 𝐿|
|5: Heap ←{ 𝑒 6: while Heap has unvisited vector do 7: // Find next hop|};||
|8: 𝑥𝑖 ← closest unvisited vector to||𝑞 in Heap;|
|9: mark vector|𝑥𝑖 as visited;||
|10: 𝑁𝑜𝑢𝑡 ( 𝑥𝑖 11: // Expand candidates 12: Heap ← Heap ∪|)← out-neighbors of vector 𝑁𝑜𝑢𝑡 ( 𝑥𝑖 );|𝑥𝑖 in 𝐺;|
|13: if |Heap||> 𝐿 then||
|14: resize Heap to retain top- 15: end if 16: end while||𝐿 vectors for 𝑞;|
|17: return [closest|𝑘 vectors from Heap]||

Search Process. The search process of CDMG adopts the stan- dard GreedySearch approach (Algorithm 4), which is commonly utilized in graph-based methods. GreedySearch uses a parameter *𝑠* to regulate the capacity of the candidate container, referred to as Heap. The search initiates from an entry point *𝑒* (line 5). In each iteration, GreedySearch selects the vertex *𝑥𝑖* that is closest to the query *𝑞𝑜𝑜𝑑* from Heap, and computes the distances between *𝑞𝑜𝑜𝑑* and the out-neighbors of *𝑥𝑖* (lines 8–10). A new vertex is inserted into Heap if it is closer to *𝑞𝑜𝑜𝑑* than the current candidates or if Heap has not yet reached its capacity *𝐿* (lines 12–15). The search terminates when no closer vertex can be identified. Ultimately, the top-*𝑘* vectors in Heap are returned as the retrieval results. Complexity Analysis. We provide theoretical analysis of CDMG’s graph index size (Lemma 2), construction complexity (Lemma 3), and search complexity (Lemma 4), as detailed below. Lemma 2. *Given a database* X *and a query set* Q*, let 𝜏 >* 0 *be* *a constant, the CDMG 𝐺*(*𝑉,𝐸*) *built on* X *and* Q *has expected out-degree 𝑂*(2 · ln |*𝑉*|) *and expected index size 𝑂*(2 ·|*𝑉*| ln |*𝑉*|)*.*

Proof. Please see the detailed proof in Appendix A. □

Lemma 3. *Given a database* X *and a query set* Q*, let relaxation* *parameter 𝜏 >* 0 *be a constant, the construction complexity of CDMG* *𝐺* (*𝑉,𝐸*) *in Algorithm 3 is 𝑂*(|*𝑉*||*𝑄*|+|*𝑄*| ln |*𝑄*|+ 2 ·|*𝑉*|2 ln |*𝑉*|)*.*

Proof. The complexity of Algorithm 3 is derived as follows. Given a database X and a query set Q: (1) In the initialization phase, the algorithm first traverses each query to identify its nearest neighbor in the database, which requires *𝑂*(|*𝑉*||*𝑄*|) time. After collecting all candidate pairs, it selects the pair with the maximum distance in each iteration, an operation that can be performed in *𝑂*(|*𝑄*| ln |*𝑄*|) time. In the worst-case scenario, many database vectors remain unassigned and must undergo reverse matching, introducing an additional cost of *𝑂*(|*𝑉*||*𝑄*|). (2) In the neighbor selection phase, the RobustPruning procedure has a known time complexity of *𝑂*(|*𝑉*| ln |*𝑉*|) over X [49]. As this procedure is exe- cuted once for each database vector and its corresponding query, the total cost becomes *𝑂*(2 ·|*𝑉*|2 ln |*𝑉*|). (3) For the merging phase, since the out-degrees of both *𝑁𝑥*(*𝑣𝑖*) and *𝑁𝑞*(*𝑣𝑖*) are bounded by *𝑂*(ln |*𝑉*|), the merging operation incurs a cost of *𝑂*(|*𝑉*| ln |*𝑉*|).

Consequently, the overall construction complexity amounts to *𝑂*(|*𝑉*||*𝑄*|+|*𝑄*| ln |*𝑄*|+ 2 ·|*𝑉*|2 ln |*𝑉*|), where the constant in last term is retained to reflect the dual invocation of RobustPruning. □ Lemma 4. *Given a database* X *and a query set* Q *in the space* R *𝐷* *, where 𝐷 denotes the dimensionality, let 𝜏 be a non-negative* *constant. For an OOD query 𝑞 that follows the same distribution* *as* Q*, the expected routing path length* *𝑜𝑜𝑑* *ℓ over the CDMG 𝐺*(*𝑉,𝐸*) *is* <u>1 1</u> 2 *𝑂*(|*𝑉* | <sup>𝐷</sup> ln |*𝑉*|)*, and the search complexity is 𝑂*(|*𝑉* | <sup>𝐷</sup> (ln |*𝑉*|) )*.* Proof. The search complexity of graph-based methods can be expressed as*𝑂* (*𝑜ℓ*), where*𝑜* denotes the expected out-degree and *ℓ* represents the routing path length. As established in Lemma 2, the expected out-degree of CDMG is *𝑂*(ln |*𝑉*|). We further show that <u>1</u> the expected routing path length of CDMG is *𝑂*(|*𝑉* | <sup>𝐷</sup> ln |*𝑉*|). The cross-distribution monotonic path *𝑃* = [*𝑃*1*,𝑃𝑝𝑟𝑜𝑗*] for *𝑞𝑜𝑜𝑑* com- prises two phases: (1) To estimate the path length of ∗ *𝑃* ∗1, we consider an ID routing path *𝑃* on the minimal convex hull ∗ X that encloses both X and Q. The points in X are uniformly distributed, with |X∗ | = *𝜆*|V|, where *𝜆* is a constant. Since *𝑞𝑜𝑜𝑑* follows the same distribution as X, the length of ∗ *𝑃* ∗ is*𝑂*(|X ∗ 1/ | *𝐷* ln |X ∗ |). However, in practice, points within *𝐵*(*𝑞𝑜𝑜𝑑,𝜖*) are not indexed, where *𝜖* denotes the distance between *𝑞𝑜𝑜𝑑* and its nearest database vector. Therefore, the length of*𝑃*1is bounded by*𝑂* |X∗ |1/ *𝐷* ln |X∗ |−*𝜖*/*𝜏*.

(2) In the second phase, it is straightforward to prove that the expected length of *𝑃𝑝𝑟𝑜𝑗* is *𝑂* (*𝜖*/*𝜏*). Combining both phases, the total expected routing path length *ℓ* is *𝑂*(|*𝑉* |
1/*𝐷* ln |*𝑉*|), and the overall search complexity becomes *𝑂*(|*𝑉* | 1/*𝐷* (ln |*𝑉*|) ). 2 □

3.3 Construction of CDMG+ In this section, we propose CDMG+, an *approximate variant* of CDMG, designed to address several practical challenges: (1) CDMG requires a *dual* neighbor selection process, which introduces additional computational overhead and doubles the index size; (2) the construction process is highly *sensitive to the size of the query* *set*, making it vulnerable to variations in sample quantity; and (3) both the database and the query set are fully treated as candidates, resulting in low construction efficiency. To overcome these issues,

<!-- Page 8 -->

𝑥! 𝑥 " 𝜏!𝑥<sup>∗</sup> 𝑞 ! 𝑥! 𝑞! 𝑥! q<sup>∗</sup> 𝑥 " 𝑞" 𝑞 3𝜏𝑞" " 𝑥" 𝑞! 𝜖

(a) (b) (c)
Figure 7: Illustration of three different cases.

we first introduce a unified *cross-distribution distance* to stream- line the construction process. We then employ *query aggregation* to enhance the quality of query sampling. Finally, we design the *implementation* of the construction process to improve efficiency. Cross-Distribution Distance. As stated in §3.2, CDMG necessi- tates independent neighbor selection for database vectors and their associated queries, owing to *the lack of a unified mechanism for* *cross-distribution comparison*. To address this limitation, we propose a cross-distribution distance that not only simplifies computation but also maintains clusterability and cross-distribution monotonicity. Specifically, given a 1-regular bipartite graph *𝐺𝑏𝑟𝑖*, in which each database vector is paired with a corresponding query (i.e., *𝑞𝑖* with *𝑥𝑖*, and *𝑞𝑗* with *𝑥𝑗*), we formally define the cross-distribution distance *𝜉* (*𝑣𝑖,𝑣𝑗*) between vertices *𝑣𝑖* and *𝑣𝑗* as follows: *𝜉* (*𝑣𝑖,𝑣𝑗*) = *𝛿* (*𝑥𝑖* +*𝑞𝑖,𝑥𝑗* +*𝑞𝑗*)*.* (2) To assess its effectiveness, we decompose *𝜉* (*𝑣𝑖,𝑣𝑗*) under the assumption that*𝛿* represents the Euclidean distance, which facilitates insight into its underlying mechanisms. This leads to:

*𝜉* (*𝑣𝑖,𝑣𝑗*)=∥*𝑥𝑖* +*𝑞𝑖* −(*𝑥𝑗* +*𝑞𝑗*)∥2 = ∥(*𝑞𝑖* −*𝑥𝑗*)−(*𝑞𝑗* −*𝑥𝑖*)∥2

(3)
= *𝛿* (*𝑞𝑖,𝑥𝑗*)+*𝛿* (*𝑞𝑗,𝑥𝑖*)− 2∥*𝑞𝑖* −*𝑥𝑗* ∥∥*𝑞𝑗* −*𝑥𝑖* ∥ cos*𝜃,* where *𝜃* denotes the angle between the vectors *𝑞𝑖* −*𝑥𝑗* and *𝑞𝑗* −*𝑥𝑖*.

<u>(1) Asymmetric Distance</u>. For an OOD query *𝑞𝑜𝑜𝑑*, the search process determines whether a database vector *𝑥*
∗ qualifies as a result based on the distance*𝛿* (*𝑞𝑜𝑜𝑑,𝑥* ∗ ). Consequently, a natural approach is to use *𝛿* (*𝑞𝑖,𝑥𝑗*) to determine whether *𝑣𝑗* should be considered a neighbor of *𝑣𝑖*. This choice is consistent with the distance metric employed during the search process. As shown in Figure 7(a), both *𝑥𝑖* and*𝑥𝑗* lie on the boundary of the database distribution. Although *𝑥𝑖* and *𝑥𝑗* are spatially distant, their distances*𝛿* (*𝑞𝑖,𝑥𝑖*) and*𝛿* (*𝑞𝑖,𝑥𝑗*) to the query *𝑞𝑖* are equal (blue arrowed lines), permitting *𝑥𝑗* to be selected as an out-neighbor of *𝑥𝑖* (black arrowed lines). However, directly employing *𝛿* (*𝑞𝑖,𝑥𝑗*) as the distance measure overlooks the intrinsic relationships within the database and query distributions, namely, *𝛿* (*𝑞𝑖,𝑞𝑗*) and *𝛿* (*𝑥𝑖,𝑥𝑗*), which leads to two critical issues:

(1) it violates the proposed edge occlusion rule, and (2) it fails to ac- count for database vectors that deviate from the query distribution.
<u>(2) Symmetric Distance</u>. The symmetric distance form, *𝜉𝑠*(*𝑣𝑖,𝑣𝑗*) = *𝛿* (*𝑞𝑖,𝑥𝑗*)+*𝛿* (*𝑞𝑗,𝑥𝑖*), better reflects the mutual relationships between queries and database vectors. Its effectiveness is justified as follows: First, symmetric distance reformulates CDMG construction while enabling to preserve cross-distribution monotonicity. Specifically,
(1) if *𝜉𝑠*(*𝑣𝑖,𝑣𝑗*)≤ 2*𝜖*+ 3*𝜏*, a direct link from *𝑣𝑖* to *𝑣𝑗* is established; (2) if *𝜉𝑠*(*𝑣𝑖,𝑣𝑗*) *>* 2*𝜖*+ 3*𝜏*,*𝑣𝑖* links to*𝑣𝑗* only if *𝜉𝑠*(*𝑣𝑗,𝑣*
∗

)+ 3*𝜏 > 𝜉𝑠*(*𝑣𝑖,𝑣𝑗*).
The rationale behind this is as follows: (1) When *𝛿* (*𝑞𝑖,𝑞𝑗*)≤ 3*𝜏*, *𝑥𝑖* and *𝑥𝑗* are located near the boundary of the database distribution,

such that both *𝛿* (*𝑞𝑖,𝑥𝑗*) and *𝛿* (*𝑞𝑖,𝑥𝑖*) are approximately equal to a large constant *𝜖*. Therefore, using the symmetry distance yields:

*𝛿* (*𝑞𝑖,𝑥𝑗*)+*𝛿* (*𝑞𝑗,𝑥𝑖*)≤ *𝜖* + *𝜖* + *𝛿* (*𝑞𝑖,𝑞𝑗*)≤ 2*𝜖* + 3*𝜏*; (4) ∗ ∗ ∗

(2) For *𝛿* (*𝑞𝑖,𝑞𝑗*) ≥ 3*𝜏*, let *𝛿* (*𝑥 ,𝑥𝑖*) *< 𝜏* with *𝜏* ≪ *𝜏*. Given that *𝛿* (*𝑞𝑖,𝑞𝑗*)−*𝛿* (*𝑞*
∗ *,𝑞𝑗*)≤ 3*𝜏*, it follows that:

### 𝛿 (𝑞𝑖,𝑥𝑗)+𝛿 (𝑞𝑗,𝑥𝑖)−𝛿 (𝑞𝑗,𝑥

∗ )−*𝛿* (*𝑞* ∗ *,𝑥𝑗*)

### ≤(𝜖 + 𝛿 (𝑞 ,𝑞))+(𝛿 (𝑞 ,𝑥

∗ )+*𝜏* ∗ )−*𝛿* (*𝑞 ,𝑥* ∗ )−(*𝜖* + *𝛿* (*𝑞* ∗ *,𝑞*)) *𝑖 𝑗 𝑗 𝑗 𝑗* ≤ *𝜏* ∗ + *𝛿* (*𝑞𝑖,𝑞𝑗*)−*𝛿* (*𝑞* ∗ *,𝑞𝑗*)≤ *𝜏* ∗ + 3*𝜏.* (5)

Although symmetric distance preserves cross-distribution monotonicity in the second routing phase, it still has limitations. Specifically, (1) it is worth noting that symmetric distance is effective only when the corresponding queries are approximately co-directional. However, as illustrated in Figure 7(c), when the corresponding queries of *𝑥𝑖* and *𝑥𝑗* are far apart, *𝑣𝑗* may still be mistakenly considered a neighbor of *𝑣𝑖*. (2) In addition, it does not adequately handle database vectors located far from the distribution boundary.

<u>(3) Cross-Distribution Distance</u>. Counter-directional cases can be mitigated by incorporating a corrective term: −2∥*𝑞𝑖* − *𝑥𝑗* ∥∥*𝑞𝑗* − *𝑥𝑖* ∥ cos*𝜃*. Under this configuration, cos*𝜃* is negative, and the cor- rection penalizes counter-directional queries pairs while preserving the desired behavior for co-directional pairs. Furthermore, for the databsae vectors that are close to each other yet deviate from the boundary, their corresponding queries are highly similar or even identical. As a result, their cross-distribution distance can be approximate as *𝜉* (*𝑣𝑖,𝑣𝑗*)≈ *𝛿* (*𝑥𝑖,𝑥𝑗*). It is non-trivial to prove that it guarantees both clusterability and navigability. Query Aggregation. The performance of CDMG improves with the size of the query set. However, in practice, acquiring a suffi- ciently large and representative query set is challenging [28]. A limited query set results in many database vectors being assigned identical or highly similar query vectors during BuildBipartiteGraph, thereby diminishing the effectiveness of query guidance in the construction process. Conversely, a large query set can lead to significant overhead [26]. To alleviate bias stemming from the small size of the query set, we employ a query aggregation strategy that enhances the quality of the query vector associated with each database vector in the bipartite graph. This approach mitigates the influence of outlier or noisy queries by averaging over multiple nearest neighbors. Specifically, for each database vector *𝑥𝑖*, we identify its top-*𝑆* nearest queries, denoted as NN(*𝑥𝑖*) with |NN(*𝑥𝑖*)| = *𝑆*, and compute the mean of these neighbors as the updated query <u>1</u>Í∗ vector: *𝑞𝑖* = *𝑆 𝑞*
∗ ∈NN (*𝑥𝑖*)*𝑞*. This method is both computationally efficient and effective in enhancing the robustness of CDMG+. Implementation. We outline the construction algorithm BuildCDMG+ as follows. Given a database X and a query set Q, the algorithm first constructs a 1-regular bipartite graph *𝐺𝑏𝑟𝑖* using the query aggregation strategy, which requires *𝑂*(|*𝑉*||*𝑄*|) time. Each database vector is then fused with its corresponding query in *𝐺𝑏𝑟𝑖* to form the fused set F, where each vector *𝑓𝑖* ∈ F is defined as *𝑓𝑖* = *𝑥𝑖* + *𝐺𝑏𝑟𝑖*(*𝑥𝑖*) (lines 5-8 in Algorithm 5). This step takes *𝑂*(|*𝑉*|) time and allows for pre-computation of cross-distribution distances, thus reducing search overhead. Using the fused set F, BuildCDMG+ incrementally constructs the graph index. For each

<!-- Page 9 -->

### Algorithm 5 BuildCDMG+(X, Q,𝑆,𝐿,𝜏,𝑜)

1: Input: database X, query set Q, query aggregation parameter *𝑆*, candidates size *𝐿*, relaxation parameter *𝜏*, threshold of out-degree *𝑜* 2: Output: CDMG+*𝐺* 3: F ←∅, *𝐺* ←∅; 4: // Query Aggregation 5: for ∀*𝑥𝑖* ∈X do 6: NN(*𝑥𝑖*)← Í top-*𝑆* queries to *𝑥𝑖* in Q; 7: *𝑞𝑖* ← *𝑆* <u>1</u> *𝑞* ∗ ∈NN (*𝑥𝑖*)*𝑞* ∗; F ←F∪{*𝑥𝑖* +*𝑞𝑖*}; 8: end for 9: for ∀*𝑣𝑖* ∈ *𝐺* do 10: // Candidate Acquisition 11: V← GreedySearch(*𝐺,𝑓𝑖,𝐿,𝐿*); *⊲* Algorithm 4 12: // Neighbor Selection 13: *𝑁𝑜𝑢𝑡*(*𝑣𝑖*)← RobustPruning(*𝑓𝑖,*V*,𝜏,𝑜*); *⊲* Algorithm 1 14: *𝐺* ← *𝐺* ∪ *𝑁𝑜𝑢𝑡*(*𝑣𝑖*); 15: end for 16: return*𝐺*

vertex *𝑣𝑖*, it is treated as a query, and GreedySearch is used to retrieve the top-*𝐿* approximate nearest neighbors (line 11), avoiding exhaustive comparisons. The search starts from an entry point *𝑒* and moves toward *𝑣𝑖* by comparing distances *𝛿* (*𝑓𝑖,𝑓* ∗ ), where *𝑓* ∗ denotes the fused vector of a candidate in Heap. The candidate *𝐷* acquisition for each vertex requires *𝑂*(|*𝑉*|1/ (ln |*𝑉*|)2) time. After candidate acquisition, a single round of RobustPruning is applied to sparsify the out-degree to |*𝑁*out(*𝑣𝑖*)| = *𝑜* while preserving cross-distribution monotonicity (line 13). This neighbor selection takes *𝑂*(|*𝑉*| ln |*𝑉*|) time. Once all vertices are processed, BuildCDMG+ returns*𝐺* as the final graph index. The overall construction complexity is*𝑂*(|*𝑉*||*𝑄*|+|*𝑉*| ln |*𝑉*|(|*𝑉*|1/ *𝐷* ln |*𝑉*|+|*𝑉*|)). Since CDMG+ is designed to operate with a small query set, its construction complexity simplifies to *𝑂*(|*𝑉*|2 ln |*𝑉*|), which is comparable to that of mainstream MRNG variants such as *𝜏*-MNG and NSG [17, 49].

4 EXPERIMENTAL STUDY In this section, we evaluate our proposed CDMG+ with a comprehensive experimental study to answer the following questions:

- Q1: How do CDMG+ and other graph-based methods perform in terms of effectiveness and efficiency for OOD queries? (§4.2)
- Q2: How does CDMG+ impact the index size and construction time? (§4.3)
- Q3: To what extent does CDMG+ optimize the navigability and clusterability of the graph index? (§4.4)
- Q4: How do different strategies contribute to the search performance of CDMG+? (§4.5)
- Q5: How do parameters and query set size affect CDMG+? (§4.6)
- Q6: How is the scalability of CDMG?(§4.7)
4.1 Experimental Setup Datasets. We evaluate our proposed method on five modern large-scale OOD datasets [7, 60], as summarized in Table 3. These datasets vary in dimensions, metrics, modality, and OOD degree. Specifically, we use the Wasserstein distance*𝑊*2[29, 65] to measure the distribution difference between the OOD queries Q*𝑜𝑜𝑑*and ID queries
Table 3: Dataset Statistics

Dataset ↓ Dim Metric # Database # Query Type OOD Deg

Text-to-Image [3] 200 IP 1000,000 1,000 Image, Text 1.17 LAION [51] 512 Cos 1000,000 1,000 Image, Text 1.47 WIT [58] 512 IP 1000,000 1,000 Image, Text 1.72 WebVid [4] 512 Cos 1000,000 1,000 Video, Text 2.46 CC3M [54] 256 IP 1000,000 1,000 Image, Text 2.81

Q*𝑖𝑑*w.r.t. the database X. Their ratio*𝑊*2(X*,* Q*𝑜𝑜𝑑*)/*𝑊*2(X*,* Q*𝑖𝑑*) is also shown in Table 3, and we use it to roughly evaluate the OOD degree of dataset. The larger the value, the stronger the OOD degree. The details of each dataset are provided below: Text-to-Image is a cross-domain dataset derived from a visual search engine. The database vectors are image embeddings and the query vectors are textual embeddings. The distance metric used for the ground truth is the inner product (IP). LAION is a widely used text-image dataset. The distance metric using cosine similarity (Cos). WIT is a dataset derived from a text-to-image application. The inner product is used as a metric. WebVid is a caption-video dataset sourced from stock footage sites. The distance metric used is cosine similarity. CC3M is a dataset of image-text pairs designed for image captioning systems. Its ground truth is generated using the inner product. Baselines. We compare CDMG+ with several state-of-the-art graph-based methods known for their efficiency in vector similarity search.

- HNSW [44]: A well-known hierarchical graph-based method.
- *𝜏*-MNG [49]: An optimized version of MRNG that incorporates query relaxation to enhance performance.
- RobustVamana [26]: A specialized variant of Vamana tailored for handling OOD queries. Unless otherwise specified, the size of the queries is 10% of the database.
- RoarGraph [7]: RoarGraph is a recently proposed efficient graph index designed for OOD queries. The size of the queries is the same as in RobustVamana, representing 10% of the database. All algorithms are obtained from the official source codes, imple- mented in C++, and compiled using GCC 9.4.0. They were stripped of all relevant prefetching and SIMD optimizations. Our source code and datasets are available at: [https://github.com/Lsyhprum/CDMG](https://github.com/Lsyhprum/CDMG). Evaluation metrics. We evaluate the search accuracy using *Recall@k*, which is a widely used metric for vector similarity search [12, 17]. It is defined in Equation 1. We evaluate search efficiency with *QPS* and *NDC* [49, 68], where *QPS* (Queries Per Second) measures the number of handled queries per second, and *NDC* (Number of Distance Computations) quantifies computational cost. Besides, we introduce the number of routing hops (*HOP*) [7] as a supplemen- tary metric to assess the navigability of the graph index. To measure the clusterability of the graph index, we propose a novel metric called Query Nearest Neighbor Quality (*QNNQ*). Given a query *𝑞*, let G(*𝑞*)={*𝑔*1*,...,𝑔𝑘*+1} be its top-(*𝑘* + 1) nearest neighbors in the database, and let N(*𝑔*1)={*𝑔*1*,𝑛*1*...,𝑛*} be the top-(*𝑘* + 1) nearest
*𝑘* neighbors of *𝑔*1. Therefore, the *QNNQ@k* is formally defined as:

<u>G(𝑞)\{𝑔1}∩N(𝑔1)\{𝑔1}</u> *𝑄𝑁𝑁𝑄*@*𝑘* = *𝑘* *.*

A higher *QNNQ@k* value indicates better clusterability. For example, if all queries originate from the database, their nearest neighbors

<!-- Page 10 -->

||HNSW|-MNG|RobustVamana|RoarGraph|
|---|---|---|---|---|
|Text-to-Image,k=100 S P Q|LAION,k=100||CC3M,k=100|WebVid,k=100|

CDMG+ WIT,k=100 10<sup>3</sup> 10<sup>3</sup>

10<sup>2</sup> 10<sup>2</sup> 94 96 98 100 80 85 90 95 100 75 80 85 90 95 100

10<sup>4</sup> 10<sup>4</sup>

94 96 98 100 80 85 90 95 100 75 80 85 90 95 100 CC3M,k=10 WebVid,k=10 WIT,k=10

10<sup>3</sup> 10<sup>3</sup>

10<sup>2</sup> 10<sup>2</sup> 90 95 100 70 80 90 100 75 80 85 90 95 100

10<sup>4</sup> 10<sup>4</sup>

10<sup>3</sup> 10<sup>3</sup>

90 95 100 70 80 90 100 75 80 85 90 95 100 *Recall*(%) *Recall*(%) *Recall*(%)

Figure 8: Efficiency (*QPS* and *NDC*) and effectiveness (*Recall@k*) evaluation of all algorithms across five OOD datasets: For the

first and third rows, the top-right is better, while for the second and fourth rows, the bottom-right is better.

10<sup>3</sup> 10<sup>3</sup> 10<sup>3</sup>

10<sup>2</sup>

85 90 95 100 85 90 95 100 90 92

3 ) 010<sup>4</sup> 110<sup>4</sup> 10<sup>4</sup> ( × *C* *D* *N* 85 90 95 100 85 90 95 100 90 92 Text-to-Image,k=10 LAION,k=10

*S* 10<sup>3</sup> <sup>3</sup> *P* 10<sup>3</sup> 10 *Q* 10<sup>2</sup> 70 80 90 100 60 70 80 90 100 80 85

3 ) 10<sup>4</sup> 10<sup>4</sup> 10<sup>4</sup> 0 1 ( × *C* *N* 10 10<sup>3</sup> 10 *D <sup>3</sup> <sup>3</sup>*

70 80 90 100 60 70 80 90 100 80 85 *Recall*(%) *Recall*(%)

*NDC* reductions of 12.9%, 44.6%, 26%, and 67.7%. Notably, HNSW fails to reach *Recall@100* = 90% and *Recall@10*=95% on WIT dataset. Among the baseline methods, ID-oriented graph-based methods perform significantly worse than OOD-oriented methods, especially on datasets such as WebVid, WIT, and CC3M, which exhibit the highest OOD degrees (see Table 3). Although RobustVamana and RoarGraph are specifically designed for OOD queries, their improvements remain limited, particularly on Text-to-Image, LAION, and CC3M. This is mainly because they rely on heuristic optimization, often leading to unstable and suboptimal performance. In contrast, CDMG+ exhibits strong adaptability across all OOD datasets. It is also worth noting that while HNSW is originally designed for ID query scenarios, it achieves competitive results on certain datasets with low OOD degree, likely due to its hierarchical structure that facilitates better entry point selection.

4.3 Index Cost We evaluated the index size and construction time of existing algorithms, with the results shown in Figure 9. We observed that CDMG+ almost has the smallest index size compared to other methods, particularly on Text-to-Image, LAION, and WIT datasets. This allows CDMG+ to better adapt to resource-constrained en- vironments. In contrast, HNSW and *𝜏*-MNG incur larger index sizes, this is mainly because HNSW has an additional hierarchical structure for accessing entry points, while *𝜏*-MNG retains more
G will match N, resulting in *QNNQ@k* = 1. However, when the query differs from the database, *QNNQ@k <* 1. Implementation setup. All experiments are conducted on an Ubuntu 20.04 server equipped with Intel(R) Xeon(R) Gold 5218 CPU (2.30GHz) and 125GB of memory. The Eigen library [15] enhances matrix operations, while OpenMP enables parallel index construction execution using 20 threads.

4.2 Efficiency and Effectiveness Evaluation
Figure 8 presents the performance comparison of the proposed
 CDMG+ against state-of-the-art ID-oriented (HNSW and *𝜏*-MNG) and OOD-oriented (RobustVamana and RoarGraph) graph-based methods. We evaluate accuracy using *Recall@100* (rows 1 and 2) and *Recall@10* (rows 3 and 4) and measure search efficiency with *QPS* and *NDC*. In the figures illustrating *Recall@k* vs. *QPS* (rows 1 and 3), a curve positioned towards the top-right indicates better performance. Conversely, in those depicting *Recall@k* v.s. *NDC* (rows 2 and 4), a position toward the bottom-right is preferable. The results demonstrate that our proposed algorithm consistently outperforms existing methods across all datasets. At *Recall@100* = 90%, CDMG+ achieves *QPS* improvements of 1.3×, 1.76×,
1.53×, and 2.2× over baseline on Text-to-Image, LAION, CC3M, and WebVid, respectively. In terms of *NDC*, CDMG+ is 9.3%, 26.8%, 31.7%, and 47.6% *NDC* less than baseline. At Recall@10 = 95%, CDMG+ de- livers *QPS* gains of 1.07×, 2.1×, 1.9×, and 3.6×, with corresponding

<!-- Page 11 -->

Text-to-Image LAION WIT 10<sup>3</sup> 10<sup>3</sup> 10<sup>3</sup> RoarGraph RoarGraph RoarGraph ) CDMG+ CDMG+ CDMG+ 3 0 1 × ( *P* *O* *H* 10<sup>2</sup> 10<sup>2</sup> 10<sup>2</sup> 88 92 96 100 88 92 96 100 90 92 94 96 98 100 *Recall*@100 (%) *Recall*@100 (%) *Recall*@100 (%)

|Memory Usage (MB)||
|---|---|
|Text-to-Image|CC3M WebVid|

Figure 11: Hop statistics of RoarGraph and CDMG+.

Table 4: Clusterability Statistics (*QNNQ@100*)

400 363 HNSW RoarGraph 354 350-MNG CDMG+ 300 RobustVamana 251 249 248236 250 216 204199 204216202 204213214 200 150 100 96102 102102 9992 9090 9992 50 0 LAION WIT

Figure 9: Index Size.

4000HNSW

|Method|↓ Text-to-Image|LAION|CC3M|WEBVID|WIT|
|---|---|---|---|---|---|
|ID-ori|0.4568|0.4167|0.5394|0.3929|0.4190|
|OOD-ori|0.2634|0.2006|0.2744|0.1545|0.2106|
|Bipartite|0.2607|0.2142|0.3314|0.1692|0.2721|
|Ours|0.2919|0.2505|0.3175|0.1843|0.2460|

3628 3500-MNG OOD-DiskANN 30002742 RoarGraph 2537 2500CDMG+ 2138 2000 1833 1926 1693 15001476 992 1000 833 593747 691 579

|Indexing Time (s)||
|---|---|
|Text-to-Image|CC3M WebVid|

500 300359 290 294402 383 488 273 492 134 162 0 LAION WIT

Figure 10: Indexing Time.

edges due to the edge pruning process enabled by query relaxation. The index size of RobustVamana is 2.24×-2.57× larger than that of CDMG+. This is mainly because RobustVamana retains numerous edges from the database distribution and augments them with additional query-derived edges. For example, the average out-degree of RobustVamana increases from 22 to 57 after edge stitching. The results of indexing time are shown in Figure 10, CDMG+ also oc- cupies one of the leading positions, which reduces 56.9%-65.8% indexing time compared to the RobustVamana. Since each vertex in CDMG+ corresponds to two vectors from different distributions, its naive construction incurs a higher overhead than methods like HNSW and RoarGraph. However, with the introduction of the cross-distribution distance and implementation optimizations, CDMG+ achieves only slightly higher construction time while significantly outperforming existing methods in index quality.

4.4 Naviability and Clusterability In this section, we conduct a fine-grained comparison of two key properties of graph indexes: navigability and clusterability. These properties respectively reflect the difficulty for greedy search to converge to the query’s nearest neighbor and the difficulty of re- trieving additional relevant results once the nearest neighbor has been found. We conduct a comparison of the navigability and clusterability of CDMG+ against baseline methods, as detailed below: Navigability. We compare the number of hops *HOP* required by the optimized and original algorithms to achieve the target *Recall@100* on the Text-to-Image, LAION, and CC3M datasets. To ensure a fair comparison, we control the out-degree of the compared algorithms to be approximately the same. Specifically, we select RoarGraph as the baseline algorithm, and the average out-degree of CDMG+ and RoarGraph are 21 compared with 23, 25 compared with 25, and 23 compared with 24 on the three datasets, respectively. As shown in Figure 11, CDMG+ reaches the target *Recall* with fewer hops than RoarGraph. For example, on the LAION dataset, the *HOP* of RoarGraph is 1.25× that of CDMG+ under 95% Recall. This result demonstrates that CDMG+ effectively optimizes the graph structure, enabling faster convergence to query-relevant regions.
Text-to-Image LAION WIT CDMG 10<sup>3</sup> CDMG 10<sup>3</sup> CDMG 10<sup>3</sup> C CDDMMGG *A* <sub>S</sub> C CDDMMGG *A* <sub>S</sub> C CDDMMGG *A* <sub>S</sub> CDMG+ CDMG+ CDMG+ QPS 10<sup>2</sup> <sup>2</sup> 10<sup>2</sup> 10 85 90 95 100 85 90 95 85 90 95 *Recall*@100 (%) *Recall*@100 (%) *Recall*@100 (%)

Figure 12: Ablation comparisons.

Clusterability. For clustering quality, we construct KNNG, Bipartite Graph, and a CDMG-optimized KNNG, avoiding the edge-pruning influence and ensuring the neighbors are selected only based on distance. We use *QNNQ@100* to measure the clustering quality of query structures in these graphs. As illustrated in Table 4, in KNNG, the *QNNQ* scores of ID-ori and OOD-ori represent in-distribution and out-of-distribution queries, respectively, with OOD-ori being significantly lower since query nodes do not belong to the database. As a result, *QNNQ@100* < 1. The CDMG-optimized version improves *QNNQ@100* quality across all cases, with respective increases of 11%, 25%, 16%, 19%, and 17% compared to the unoptimized version.

4.5 Ablation Study We mainly compare the impact of key components in CDMG+, with the trade-offs between *Recall@100* and *QPS* presented in Figure
12. Specifically, we present the performance comparison between CDMG and CDMG+, as well as the differences among CDMG*𝐴*, CDMG*𝑆*, and CDMG+, which are constructed using asymmetric, symmetric, and cross-distribution distances, respectively. The naive CDMG simply merges the out-edges obtained from both distributions without an effective comparison process, resulting in a significantly higher out-degree. For example, on the Text-to-Image dataset, CDMG exhibits a 1.3× increase in out-degree compared to CDMG+. In contrast, CDMG+ adopts cross-distribution distance to jointly compute the edges from both distributions, effectively constraining the out-degree by the parameter *𝑜*, and thereby reducing the distance computations during the search process. We compare the impact of three distance computation strategies on search performance. Specifically, for a vertex *𝑣𝑖* and its candidate neighbor *𝑣𝑗*, we construct CDMG*𝐴*using asymmetric distance *𝛿* (*𝑞𝑖,𝑥𝑗*), CDMG*𝑆*using symmetric distance *𝛿* (*𝑞𝑖,𝑥𝑗*)+ *𝛿* (*𝑞𝑗,𝑥𝑖*),

<!-- Page 12 -->

Text-to-Image(RoarGraph) Text-to-Image(CDMG) CC3M(RoarGraph) CC3M(CDMG) WIT(RoarGraph) WIT(CDMG)

||0.1%|1%|10%|100%|
|---|---|---|---|---|
|)|||||
|0 1|||||
|(×|||||
|C D|||||
|N|||||
||Recall@100 (%)|Recall@100 (%)|Recall@100 (%)|Recall@100 (%)|

10<sup>4</sup> 10<sup>4</sup> 10<sup>4</sup> 3 10<sup>4</sup>

88 92 96 100 88 92 96 100 88 92 96 100 92.5 95.0 97.5 100.0

Figure 13: Performance comparison across different query set sizes for index construction.

Text-to-Image CC3M WIT 10<sup>4</sup> S=1 10<sup>4</sup> S=1 3 ) 10<sup>4</sup> S=5 S=5 S=5 S=1 0 S=10 S=10 S=10 1S=15 S=15 S=15 ( × S=30 S=30 S=30 *C* *D* *N*

88 92 96 100 88 92 96 100 88 92 96 100 *Recall*@100 (%) *Recall*@100 (%) *Recall*@100 (%)

Figure 14: Impacts from different parameter *𝑆*.

Text-to-Image 10M Text-to-Image 10M 900 10000RoarGraph 800 700 3)8000CDMG+ 600 10 QPS500*C* ( 6000 × 400 *D* *N* 300 RoarGraph4000 200CDMG+ 2000 80 85 90 95 80 85 90 95 *Recall*@100 (%) *Recall*@100 (%)

Figure 15: 10M-scale efficiency and effectiveness evaluation.

and CDMG+ using cross-distribution distance *𝛿* (*𝑥𝑖* +*𝑞𝑖,𝑥𝑗* +*𝑞𝑗*). As shown in Figure 12, asymmetric distance performs the worst on the Text-to-Image and LAION datasets, as it captures only partial clusterability and fails to maintain monotonicity. In comparison, symmetric distance improves performance by better preserving monotonicity. However, on the WIT dataset, it underperforms the asymmetric variant—likely due to its vulnerability in cases where vectors have similar database vectors but queries pointing in different directions. Cross-distribution distance introduces a corrective term and ensures both clusterability and navigability, consistently achieving the best search performance across all datasets.

4.6 Parameter Sensitivity In this section, we examine the impact of different query set sizes |Q| and the parameter of query aggregation *𝑆* on performance. Query set size. We evaluate search performance under varying query set sizes—0.1%, 1%, 10%, and 100% of the database—on three datasets: Text-to-Image, CC3M, and WIT. As shown in Figure 13, we assess the trade-off between *Recall@100* and *NDC*. CDMG+ consistently enhances search performance across all query set sizes and outperforms the baseline, RoarGraph. Using 1% of the dataset strikes a favorable balance between efficiency and accuracy. Parameter*𝑆*. We analyze the performance impact of the parameter *𝑆* in query aggregation. As shown in Figure 14, we compare different settings: *𝑆* = 1, 5, 10, 15, and 30. Search performance improves as *𝑆* increases, as averaging vectors helps mitigate the impact of smaller training sets and yields better query representations.*𝑆* = 15
is a suitable choice across all datasets. However, as *𝑆* increases, the improvement in search performance gradually diminishes. Compared to *𝑆*=15, larger *𝑆* values provide only a slight gain at low recall levels, while the construction cost increases significantly.

4.7 Scalability We further evaluate the performance of CDMG+ on a larger dataset, Text-to-Image10M. As shown in Figure 15, the left and right plots il- lustrate the trade-offs between *Recall@100* and *QPS*, and *Recall@100* and *NDC*, respectively. CDMG+ surpasses state-of-the-art methods and demonstrates excellent scalability on this larger dataset. 5 DISCUSSION Further Analysis of ID Datasets. Although ID queries have been extensively studied, some difficult ones [25, 72] still fall into local optima during search. This stems from the assumption of uniform data distributions in existing methods, which overlook real-world patterns like skewness and long tails. We view such hard queries as an OOD problem relative to the database, and future work will explore optimizations for non-uniform ID scenarios. Streaming Algorithms. In real-world applications, efficiently sup- porting dynamic storage in graph indexing remains an open challenge. Specifically, data distributions in the database often evolve over time, leading to OOD effects between pre-deployed databases and new queries [82]. Additionally, when similar data is removed in batches, effectively inserting new nodes while maintaining strong clusterability and navigability is another direction for future work. Hybrid Optimization. A promising direction for further work is to jointly optimize entry point selection [46], dimensionality reduction [60, 61], quantization [26], and hardware-specific acceleration [21] under OOD scenarios, in conjunction with CDMG+. 6 CONCLUSION This work presents CDMG+, a theoretically grounded (with CDMG as its theoretical counterpart) and computationally efficient graph index designed for OOD queries. By enforcing cross-distribution monotonicity, CDMG guarantees reliable and efficient search paths even under significant distributional shifts between database and query vectors. Theoretical analysis establishes the optimality of CDMG in search time complexity for OOD scenarios, while the practical refinements in CDMG+ ensure scalable and robust performance. Extensive experiments demonstrate the superiority of our approach, achieving substantial gains in speed and accuracy over existing methods across diverse benchmarks.

<!-- Page 13 -->

REFERENCES [1] Josh Achiam, Steven Adler, Sandhini Agarwal, Lama Ahmad, Ilge Akkaya, Floren- cia Leoni Aleman, Diogo Almeida, Janko Altenschmidt, Sam Altman, Shyamal Anadkat, et al. 2023. Gpt-4 technical report. *arXiv preprint arXiv:2303.08774* (2023). [2] Martin Aumüller and Matteo Ceccarello. 2023. Recent Approaches and Trends in Approximate Nearest Neighbor Search, with Remarks on Benchmarking. *IEEE* *Data Eng. Bull.*46, 3 (2023), 89–105. [3] Artem Babenko and Dmitry Baranchuk. 2021. Text-to-Image Dataset for Billion-Scale Similarity Search. Retrieved August 23, 2023, from [https://research.yandex](https://research.yandex). com/datasets/text-to-image-dataset-for-billion-scale-similarity-search. [4] Max Bain, Arsha Nagrani, Gül Varol, and Andrew Zisserman. 2021. Frozen in time: A joint video and image encoder for end-to-end retrieval. In *Proceedings of* *the IEEE/CVF international conference on computer vision*. 1728–1738. [5] Dmitry Baranchuk, Matthijs Douze, Yash Upadhyay, and I Zeki Yalniz. 2023. Dedrift: Robust similarity search under content drift. In *Proceedings of the* *IEEE/CVF International Conference on Computer Vision*. 11026–11035. [6] Dmitry Baranchuk, Dmitry Persiyanov, Anton Sinitsin, and Artem Babenko. 2019. Learning to route in similarity graphs. In *International Conference on Machine* *Learning*. PMLR, 475–484. [7] Meng Chen, Kai Zhang, Zhenying He, Yinan Jing, and X Sean Wang. 2024. Roargraph: A projected bipartite graph for efficient cross-modal approximate nearest neighbor search. *arXiv preprint arXiv:2408.08933* (2024). [8] Thomas Cover and Peter Hart. 1967. Nearest neighbor pattern classification. *IEEE transactions on information theory* 13, 1 (1967), 21–27. [9] Yijie Dang, Nan Jiang, Hao Hu, Zhuoxiao Ji, and Wenyin Zhang. 2018. Image classification based on quantum K-Nearest-Neighbor algorithm. *Quantum* *Information Processing* 17 (2018), 1–18. [10] Haya Diwan, Jinrui Gou, Cameron Musco, Christopher Musco, and Torsten Suel. 2024. Navigable Graphs for High-Dimensional Nearest Neighbor Search: Constructions and Limits. *arXiv preprint arXiv:2405.18680* (2024). [11] Wei Dong, Charikar Moses, and Kai Li. 2011. Efficient k-nearest neighbor graph construction for generic similarity measures. In *Proceedings of the 20th international conference on World wide web*. 577–586. [12] Matthijs Douze, Alexandre Sablayrolles, and Hervé Jégou. 2018. Link and code: Fast indexing with graphs and compact regression codes. In *Proceedings of the* *IEEE conference on computer vision and pattern recognition*. 3646–3654. [13] Karima Echihabi. 2020. High-dimensional vector similarity search: from time series to deep network embeddings. In *Proceedings of the 2020 ACM SIGMOD* *International Conference on Management of Data*. 2829–2832. [14] Karima Echihabi, Kostas Zoumpatianos, and Themis Palpanas. 2021. New trends in high-d vector similarity search: al-driven, progressive, and distributed. *Proceedings of the VLDB Endowment* 14, 12 (2021), 3198–3201. [15] Eigen Library. 2009. Eigen is a C++ template library for linear algebra: matrices, vectors, numerical solvers, and related algorithms. [https://eigen.tuxfamily.org/](https://eigen.tuxfamily.org/) index.php?title=Main_Page. [Online; accessed 01-June-2024]. [16] Cole Foster, Berk Sevilmis, and Benjamin Kimia. 2025. Generalized relative neighborhood graph (GRNG) for similarity search. *Pattern Recognition Letters* 188 (2025), 103–110. [17] Cong Fu, Changxu Wang, and Deng Cai. 2021. High dimensional similarity search with satellite system graph: Efficiency, scalability, and unindexed query compatibility. *IEEE Transactions on Pattern Analysis and Machine Intelligence* 44, 8 (2021), 4139–4150. [18] Cong Fu, Chao Xiang, Changxu Wang, and Deng Cai. 2017. Fast approximate nearest neighbor search with the navigating spreading-out graph. *arXiv preprint* *arXiv:1707.00143* (2017). [19] Jianyang Gao and Cheng Long. 2023. High-dimensional approximate nearest neighbor search: with reliable and efficient distance comparison operations. *Proceedings of the ACM on Management of Data* 1, 2 (2023), 1–27. [20] Rentong Guo, Xiaofan Luan, Long Xiang, Xiao Yan, Xiaomeng Yi, Jigao Luo, Qianya Cheng, Weizhi Xu, Jiarui Luo, Frank Liu, et al. 2022. Manu: a cloud native vector database management system. *arXiv preprint arXiv:2206.13843* (2022). [21] Ruiqi Guo, Philip Sun, Erik Lindgren, Quan Geng, David Simcha, Felix Chern, and Sanjiv Kumar. 2020. Accelerating large-scale inference with anisotropic vector quantization. In *International Conference on Machine Learning*. PMLR, 3887–3896. [22] Yikun Han, Chunjiang Liu, and Pengfei Wang. 2023. A comprehensive survey on vector database: Storage and retrieval technique, challenge. *arXiv preprint* *arXiv:2310.11703* (2023). [23] Ben Harwood and Tom Drummond. 2016. Fanng: Fast approximate nearest neighbour graphs. In *Proceedings of the IEEE Conference on Computer Vision and* *Pattern Recognition*. 5713–5722. [24] Jui-Ting Huang, Ashish Sharma, Shuying Sun, Li Xia, David Zhang, Philip Pronin, Janani Padmanabhan, Giuseppe Ottaviano, and Linjun Yang. 2020. Embedding-based retrieval in facebook search. In *Proceedings of the 26th ACM SIGKDD* *International Conference on Knowledge Discovery & Data Mining*. 2553–2561.

[25] Piotr Indyk and Haike Xu. 2023. Worst-case performance of popular approximate nearest neighbor search implementations: Guarantees and limitations. *Advances* *in Neural Information Processing Systems* 36 (2023), 66239–66256. [26] Shikhar Jaiswal, Ravishankar Krishnaswamy, Ankit Garg, Harsha Vardhan Simhadri, and Sheshansh Agrawal. 2022. Ood-diskann: Efficient and scalable graph anns for out-of-distribution queries. *arXiv preprint arXiv:2211.12850* (2022). [27] Suhas Jayaram Subramanya, Fnu Devvrit, Harsha Vardhan Simhadri, Ravishankar Krishnawamy, and Rohan Kadekodi. 2019. Diskann: Fast accurate billion-point nearest neighbor search on a single node. *Advances in Neural Information* *Processing Systems* 32 (2019). [28] Taewon Jeong and Heeyoung Kim. 2020. Ood-maml: Meta-learning for few-shot out-of-distribution detection and classification. *Advances in Neural Information* *Processing Systems* 33 (2020), 3907–3916. [29] Leonid V Kantorovich. 1960. Mathematical methods of organizing and planning production. *Management science* 6, 4 (1960), 366–422. [30] Douwe Kiela and Léon Bottou. 2014. Learning image embeddings using convo- lutional neural networks for improved multi-modal semantics. In *Proceedings of* *the 2014 Conference on empirical methods in natural language processing (EMNLP)*. 36–45. [31] Jon M Kleinberg. 2000. Navigation in a small world. *Nature* 406, 6798 (2000), 845–845. [32] Qiuqiang Kong, Yin Cao, Turab Iqbal, Yuxuan Wang, Wenwu Wang, and Mark D Plumbley. 2020. Panns: Large-scale pretrained audio neural networks for audio pattern recognition. *IEEE/ACM Transactions on Audio, Speech, and Language* *Processing* 28 (2020), 2880–2894. [33] Mr Ramesh Krishnamaneni and AN Murthy. 2021. Advancing Drug Dealing Detection Using Neural Embedding and Nearest Neighbour Searching Techniques. *International Journal on Recent and Innovation Trends in Computing and* *Communication* 9, 7 (2021), 19–23. [34] Wen Li, Ying Zhang, Yifang Sun, Wei Wang, Mingjie Li, Wenjie Zhang, and Xuemin Lin. 2019. Approximate nearest neighbor search on high dimensional data—experiments, analyses, and improvement. *IEEE Transactions on Knowledge* *and Data Engineering* 32, 8 (2019), 1475–1488. [35] Victor Weixin Liang, Yuhui Zhang, Yongchan Kwon, Serena Yeung, and James Y Zou. 2022. Mind the gap: Understanding the modality gap in multi-modal contrastive representation learning. *Advances in Neural Information Processing* *Systems* 35 (2022), 17612–17625. [36] Weizhe Lin, Jinghong Chen, Jingbiao Mei, Alexandru Coca, and Bill Byrne. 2023. Fine-grained late-interaction multi-modal retrieval for retrieval augmented visual question answering. *Advances in Neural Information Processing Systems* 36 (2023), 22820–22840. [37] Jie Liu, Xiao Yan, Xinyan Dai, Zhirong Li, James Cheng, and Ming-Chang Yang.

2020. Understanding and improving proximity graph based maximum inner product search. In *Proceedings of the AAAI Conference on Artificial Intelligence*, Vol. 34. 139–146.
[38] Yuchen Liu, Junnan Zhu, Jiajun Zhang, and Chengqing Zong. 2020. Bridging the modality gap for speech-to-text translation. *arXiv preprint arXiv:2010.14920* (2020). [39] Kejing Lu, Mineichi Kudo, Chuan Xiao, and Yoshiharu Ishikawa. 2021. HVS: hierarchical graph structure based on voronoi diagrams for solving approximate nearest neighbor search. *Proceedings of the VLDB Endowment* 15, 2 (2021), 246–

258.
[40] Junshui Ma, Robert P Sheridan, Andy Liaw, George E Dahl, and Vladimir Svetnik.

2015. Deep neural nets as a method for quantitative structure–activity relationships. *Journal of chemical information and modeling* 55, 2 (2015), 263–274.
[41] Zi-Ao Ma, Tian Lan, Rong-Cheng Tu, Yong Hu, Heyan Huang, and Xian-Ling Mao.

2024. Multi-modal Retrieval Augmented Multi-modal Generation: A Benchmark, Evaluate Metrics and Strong Baselines. *arXiv preprint arXiv:2411.16365* (2024).
[42] Prasanta Chandra Mahalanobis. 2018. On the generalized distance in statistics. *Sankhyā: The Indian Journal of Statistics, Series A (2008-)* 80 (2018), S1–S7. [43] Yury Malkov, Alexander Ponomarenko, Andrey Logvinov, and Vladimir Krylov.

2014. Approximate nearest neighbor algorithm based on navigable small world graphs. *Information Systems* 45 (2014), 61–68.
[44] Yu A Malkov and Dmitry A Yashunin. 2018. Efficient and robust approximate nearest neighbor search using hierarchical navigable small world graphs. *IEEE* *transactions on pattern analysis and machine intelligence* 42, 4 (2018), 824–836. [45] Stanley Milgram. 1967. The small world problem. *Psychology today* 2, 1 (1967), 60–67. [46] Yutaro Oguri and Yusuke Matsui. 2024. Theoretical and Empirical Analysis of Adaptive Entry Point Selection for Graph-based Approximate Nearest Neighbor Search. *arXiv preprint arXiv:2402.04713* (2024). [47] Hiroyuki Ootomo, Akira Naruse, Corey Nolet, Ray Wang, Tamas Feher, and Yong Wang. 2024. Cagra: Highly parallel graph construction and approximate nearest neighbor search for gpus. In *2024 IEEE 40th International Conference on Data* *Engineering (ICDE)*. IEEE, 4236–4247. [48] James Jie Pan, Jianguo Wang, and Guoliang Li. 2024. Survey of vector database management systems. *The VLDB Journal* 33, 5 (2024), 1591–1615.

<!-- Page 14 -->

[49] Yun Peng, Byron Choi, Tsz Nam Chan, Jianye Yang, and Jianliang Xu. 2023. Efficient approximate nearest neighbor search in multi-dimensional databases. *Proceedings of the ACM on Management of Data* 1, 1 (2023), 1–27. [50] Samira Pouyanfar, Yimin Yang, Shu-Ching Chen, Mei-Ling Shyu, and SS Iyengar.

2018. Multimedia big data analytics: A survey. *ACM computing surveys (CSUR)* 51, 1 (2018), 1–34.
[51] Alec Radford, Jong Wook Kim, Chris Hallacy, Aditya Ramesh, Gabriel Goh, Sandhini Agarwal, Girish Sastry, Amanda Askell, Pamela Mishkin, Jack Clark, et al.

2021. Learning transferable visual models from natural language supervision. In *International conference on machine learning*. PmLR, 8748–8763.
[52] Andrey V Savchenko. 2017. Maximum-likelihood approximate nearest neighbor method in real-time image recognition. *Pattern Recognition* 61 (2017), 459–469. [53] Christoph Schuhmann, Richard Vencu, Romain Beaumont, Robert Kaczmarczyk, Clayton Mullis, Aarush Katta, Theo Coombes, Jenia Jitsev, and Aran Komatsuzaki.

2021. Laion-400m: Open dataset of clip-filtered 400 million image-text pairs. *arXiv preprint arXiv:2111.02114* (2021).
[54] Piyush Sharma, Nan Ding, Sebastian Goodman, and Radu Soricut. 2018. Conceptual Captions: A Cleaned, Hypernymed, Image Alt-text Dataset For Automatic

Image Captioning. In *Proceedings of ACL*.

[55] Peiyang Shi, Michael C Welle, Mårten Björkman, and Danica Kragic. 2023. Towards understanding the modality gap in CLIP. In *ICLR 2023 Workshop on Multimodal Representation Learning: Perks and Pitfalls*. [56] Harsha Vardhan Simhadri, George Williams, Martin Aumüller, Matthijs Douze, Artem Babenko, Dmitry Baranchuk, Qi Chen, Lucas Hosseini, Ravishankar Krish- naswamny, Gopal Srinivasa, et al. 2022. Results of the NeurIPS’21 challenge on billion-scale approximate nearest neighbor search. In *NeurIPS 2021 Competitions* *and Demonstrations Track*. PMLR, 177–189. [57] Aditi Singh, Suhas Jayaram Subramanya, Ravishankar Krishnaswamy, and Harsha Vardhan Simhadri. 2021. Freshdiskann: A fast and accurate graph-based ann index for streaming similarity search. *arXiv preprint arXiv:2105.09613* (2021). [58] Krishna Srinivasan, Karthik Raman, Jiecao Chen, Michael Bendersky, and Marc Najork. 2021. Wit: Wikipedia-based image text dataset for multimodal multilingual machine learning. In *Proceedings of the 44th international ACM SIGIR* *conference on research and development in information retrieval*. 2443–2449. [59] Jian Tang, Meng Qu, and Qiaozhu Mei. 2015. Pte: Predictive text embedding through large-scale heterogeneous text networks. In *Proceedings of the 21th* *ACM SIGKDD international conference on knowledge discovery and data mining*. 1165–1174. [60] Mariano Tepper, Ishwar Singh Bhati, Cecilia Aguerrebere, Mark Hildebrand, and Ted Willke. 2023. LeanVec: Search your vectors faster by making them fit. *arXiv* *preprint arXiv:2312.16335* (2023). [61] Mariano Tepper, Ishwar Singh Bhati, Cecilia Aguerrebere, and Ted Willke. 2024. GleanVec: Accelerating vector search with minimalist nonlinear dimensionality reduction. *arXiv preprint arXiv:2410.22347* (2024). [62] Sergios Theodoridis and Konstantinos Koutroumbas. 2006. *Pattern recognition*. Elsevier. [63] Bing Tian, Haikun Liu, Yuhang Tang, Shihai Xiao, Zhuohui Duan, Xiaofei Liao, Xuecang Zhang, Junhua Zhu, and Yu Zhang. 2024. FusionANNS: An Efficient CPU/GPU Cooperative Processing Architecture for Billion-scale Approximate Nearest Neighbor Search. *arXiv preprint arXiv:2409.16576* (2024). [64] Yao Tian, Ziyang Yue, Ruiyuan Zhang, Xi Zhao, Bolong Zheng, and Xiaofang Zhou. 2023. Approximate Nearest Neighbor Search in High Dimensional Vector Databases: Current Research and Future Directions. *IEEE Data Eng. Bull.*46, 3 (2023), 39–54. [65] Leonid Nisonovich Vaserstein. 1969. Markov processes over denumerable prod- ucts of spaces, describing large systems of automata. *Problemy Peredachi Infor-* *matsii* 5, 3 (1969), 64–72. [66] Hongya Wang, Zhizheng Wang, Wei Wang, Yingyuan Xiao, Zeng Zhao, and Kaixiang Yang. 2020. A note on graph-based nearest neighbor search. *arXiv* *preprint arXiv:2012.11083* (2020). [67] Jianguo Wang, Xiaomeng Yi, Rentong Guo, Hai Jin, Peng Xu, Shengjun Li, Xi- angyu Wang, Xiangzhou Guo, Chengming Li, Xiaohai Xu, et al. 2021. Milvus: A purpose-built vector data management system. In *Proceedings of the 2021* *International Conference on Management of Data*. 2614–2627. [68] Kaiye Wang, Qiyue Yin, Wei Wang, Shu Wu, and Liang Wang. 2016. A comprehensive survey on cross-modal retrieval. *arXiv preprint arXiv:1607.06215* (2016). [69] Mengzhao Wang, Lingwei Lv, Xiaoliang Xu, Yuxiang Wang, Qiang Yue, and Jiongkang Ni. 2024. An efficient and robust framework for approximate nearest neighbor search with attribute constraint. *Advances in Neural Information* *Processing Systems* 36 (2024). [70] Mengzhao Wang, Weizhi Xu, Xiaomeng Yi, Songlin Wu, Zhangyang Peng, Xi- angyu Ke, Yunjun Gao, Xiaoliang Xu, Rentong Guo, and Charles Xie. 2024. Starling: An I/O-Efficient Disk-Resident Graph Index Framework for High-Dimensional Vector Similarity Search on Data Segment. *Proceedings of the* *ACM on Management of Data* 2, 1 (2024), 1–27. [71] Mengzhao Wang, Xiaoliang Xu, Qiang Yue, and Yuxiang Wang. 2021. A comprehensive survey and experimental comparison of graph-based approximate

nearest neighbor search. *arXiv preprint arXiv:2101.12631* (2021). [72] Zeyu Wang, Qitong Wang, Xiaoxing Cheng, Peng Wang, Themis Palpanas, and Wei Wang. 2024. Steiner-Hardness: A Query Hardness Measure for Graph-Based ANN Indexes. *arXiv preprint arXiv:2408.13899* (2024). [73] Chuangxian Wei, Bin Wu, Sheng Wang, Renjie Lou, Chaoqun Zhan, Feifei Li, and Yuanzhe Cai. 2020. AnalyticDB-V: a hybrid analytical engine towards query fusion for structured and unstructured data. *Proceedings of the VLDB Endowment* 13, 12 (2020), 3152–3165. [74] Yi Wu, Edward Y Chang, Kevin Chen-Chuan Chang, and John R Smith. 2004. Optimal multimodal fusion for multimedia data analysis. In *Proceedings of the* *12th annual ACM international conference on Multimedia*. 572–579. [75] Xiaoliang Xu, Mengzhao Wang, Yuxiang Wang, and Dingcheng Ma. 2021. Two- stage routing with optimized guided search and greedy algorithm on proximity graph. *Knowledge-Based Systems* 229 (2021), 107305. [76] Yuexuan Xu, Jianyang Gao, Yutong Gou, Cheng Long, and Christian S Jensen.

2024. iRangeGraph: Improvising Range-dedicated Graphs for Range-filtering Nearest Neighbor Search. *Proceedings of the ACM on Management of Data* 2, 6 (2024), 1–26.
[77] Ming Yang, Yuzheng Cai, and Weiguo Zheng. 2024. CSPG: Crossing Sparse Proximity Graphs for Approximate Nearest Neighbor Search. *Advances in Neural* *Information Processing Systems* 37 (2024), 103076–103100. [78] Shuo Yang, Jiadong Xie, Yingfan Liu, Jeffrey Xu Yu, Xiyue Gao, Qianru Wang, Yanguo Peng, and Jiangtao Cui. 2024. Revisiting the index construction of proximity graph-based approximate nearest neighbor search. *arXiv preprint* *arXiv:2410.01231* (2024). [79] Shi Yu, Chaoyue Tang, Bokai Xu, Junbo Cui, Junhao Ran, Yukun Yan, Zheng- hao Liu, Shuo Wang, Xu Han, Zhiyuan Liu, et al. 2024. Visrag: Vision-based retrieval-augmented generation on multi-modality documents. *arXiv preprint* *arXiv:2410.10594* (2024). [80] Yuanhang Yu, Dong Wen, Ying Zhang, Lu Qin, Wenjie Zhang, and Xuemin Lin.

2022. GPU-accelerated proximity graph approximate nearest Neighbor search and construction. In *2022 IEEE 38th International Conference on Data Engineering* *(ICDE)*. IEEE, 552–564.
[81] Qiang Yue, Xiaoliang Xu, Yuxiang Wang, Yikun Tao, and Xuliyuan Luo. 2024. Routing-Guided Learned Product Quantization for Graph-Based Approximate Nearest Neighbor Search. In *2024 IEEE 40th International Conference on Data* *Engineering (ICDE)*. IEEE, 4870–4883. [82] Huayi Zhang, Lei Cao, Yizhou Yan, Samuel Madden, and Elke A Rundensteiner.

2020. Continuously adaptive similarity search. In *Proceedings of the 2020 ACM* *SIGMOD International Conference on Management of Data*. 2601–2616.
[83] Qianxi Zhang, Shuotao Xu, Qi Chen, Guoxin Sui, Jiadong Xie, Zhizhen Cai, Yaoqi Chen, Yinxuan He, Yuqing Yang, Fan Yang, et al. 2023. {VBASE}: Unifying Online Vector Similarity Search and Relational Queries via Relaxed Monotonicity. In *17th USENIX Symposium on Operating Systems Design and Implementation* *(OSDI 23)*. 377–395. [84] Ting Zhang, Chao Du, and Jingdong Wang. 2014. Composite quantization for approximate nearest neighbor search. In *International Conference on Machine* *Learning*. PMLR, 838–846. [85] Xi Zhao, Yao Tian, Kai Huang, Bolong Zheng, and Xiaofang Zhou. 2023. Towards efficient index construction and approximate nearest neighbor search in high-dimensional spaces. *Proceedings of the VLDB Endowment* 16, 8 (2023), 1979–1991. [86] Liangli Zhen, Peng Hu, Xu Wang, and Dezhong Peng. 2019. Deep supervised cross-modal retrieval. In *Proceedings of the IEEE/CVF conference on computer* *vision and pattern recognition*. 10394–10403. [87] Dantong Zhu and Minjia Zhang. 2021. Understanding and Generalizing Monotonic Proximity Graphs for Approximate Nearest Neighbor Search. *arXiv preprint* *arXiv:2107.13052* (2021).

<!-- Page 15 -->

APPENDIX

A. PROOF FOR LEMMA 2 Since BuildCDMG (Algorithm 3) obtains edges by invoking RobustPruning (Algorithm 1) twice and then merging the results, we begin by proving that the expected out-degree from a single invocation of RobustPruning is *𝑂*(ln |*𝑉*|), where |*𝑉*| is the number of vertices. <u>Case 1</u>: We first consider the scenario where *𝛿* (*𝑥𝑖,𝑥𝑗*) ≤ 3*𝜏* and show that the expected out-degree is *𝑂*(ln |*𝑉*|). Because RobustPruning connects each point to others within the ball *𝐵*(*𝑥𝑖,*3*𝜏*), the expected out-degree equals the expected number of points in this ball. Assuming points are uniformly distributed in the space and the point density in this space is *𝑔* = *𝑂*(ln |*𝑉*|), the expected number of points within the ball *𝐵*(*𝑥𝑖,*3*𝜏*) is *𝑂*(ln |*𝑉*|), as *𝜏* is a constant. <u>Case 2</u>: For *𝛿* (*𝑥𝑖,𝑥𝑗*) *>* 3*𝜏*, vertex *𝑣𝑖* links to *𝑣𝑗* if and only if ∗ ∗ *𝛿* (*𝑥𝑗,𝑥*)+ 3*𝜏 > 𝛿*(*𝑥𝑖,𝑥𝑗*). The probability that *𝑥* falls within the region *𝑉𝑜𝑙*(*𝑙𝑢𝑛𝑒*(*𝑥𝑖,𝑥𝑗*)\ *𝐵*(*𝑥𝑗,𝛿*(*𝑥𝑖,𝑥𝑗*)− 3*𝜏*)) is given by
### <u>𝑉𝑜𝑙(𝑙𝑢𝑛𝑒(𝑥𝑖,𝑥𝑗)\ 𝐵(𝑥𝑗,𝛿(𝑥𝑖,𝑥𝑗)− 3𝜏))</u>

### 𝑃 (𝑥𝑖,𝑥𝑗) =

*𝑉𝑜𝑙*(*𝑙𝑢𝑛𝑒*(*𝑥𝑖,𝑥𝑗*))

(6)
3*𝜏𝐷* *<* 1 −(1 −)*.* *𝛿* (*𝑥𝑖,𝑥𝑗*)

### Using Bernoulli’s inequality, the expected out-degree becomes:

∑︁ <u>3𝜏</u> *𝑃* = 1 −(1 −) *𝐷*

||𝛿 (𝑥 ,𝑥 )|||ln 𝑅 − ln(𝑅 + 𝜏 )|||
|---|---|---|---|---|---|---|
|𝑥 ∈{𝑉 \𝑥}|𝑖 𝑗||E[ln 𝛿 (𝑞,𝑥|)]−ln (𝑅+𝜏 ) ln 𝑅 −ln (𝑅+𝜏 )|||
|𝑥 ∈{𝑉 \𝑥|} 𝑖 𝑗|′ 𝑓 (𝑅 ) =|𝑅+𝜏 1(ln (𝑅 + 𝜏 )− ln 𝑅 ) (ln 𝑅 − ln(𝑅 + 𝜏 ))|(ln (𝑅 + 𝜏 )− E[ln 𝛿 (𝑞,𝑥 2+|𝑅 1−𝑅+𝜏 1 2|)).|
|𝑅 3𝜏||||𝑉 |||𝑉 | 0|(16)||
|𝑅|||𝑔|𝑔|||

*𝑗 𝑖* ∑︁ <u>1</u>

(7)
### < 𝐷 · 3𝜏.

### 𝛿 (𝑥 ,𝑥)

*𝑗 𝑖* For continuous distributions, let *𝑅* denote the maximum distance between any two points in the space. Then: ∫ <u>1</u> *𝑃* ≤ *𝐷* · 3*𝜏 𝑑𝑥* ≤ *𝐷* · 3*𝜏*(ln*𝑅* − ln 3*𝜏*)*,* (8) *𝑥* which gives the expected out-degree is *𝑂*(ln*𝑅*). Let the volume of the entire space be bounded by *𝑉𝑜𝑙*(*𝐵*(*𝑅*)) ≤ *𝜓* ·*𝑉𝑜𝑙𝑉* ≤ *𝜓* · <u>|𝑉</u> *𝑔* <u>|</u>. <u>𝜋</u> <u>𝐷/2 𝐷𝜋𝐷/2</u> Since *𝑉𝑜𝑙*(*𝐵*(*𝑅*)) = Γ (1+*𝐷*/2), let*𝑏* = Γ (1 +*𝐷*/2). Then: <u>𝜓 |𝑉 | 1</u> *𝑅* ≤() <sup>𝐷</sup>*.* (9) *𝑔𝑏* Hence, the expected out-degree from a single invocation of RobustPruning is *𝑂*(ln*𝑅*) = *𝑂*(ln |*𝑉*|1/ *𝐷* ) = *𝑂*(ln |*𝑉*|). As BuildCDMG invokes RobustPruning twice and merges the results, the overall expected out-degree is*𝑂*(2 · ln |*𝑉*|), which simplifies to*𝑂*(ln |*𝑉*|). □

B. PROOF FOR LEMMA 4 Given a database X⊂ R
*𝐷* and a query *𝑞* ∈ R *𝐷*, both sharing the same underlying distribution, the query *𝑞* and its nearest neighbor *𝑥𝑛* satisfy that *𝛿* (*𝑞,𝑥𝑛*) *< 𝜏*. For a query *𝑞* and radius *𝑟 >* 0, let *𝐵*(*𝑞,𝑟*) denote a ball of radius *𝑟* centered at *𝑞*, and *𝑉𝑜𝑙*(*𝐵*(*𝑞,𝑟*)) represent the volume of *𝐵*(*𝑞,𝑟*). Consider a graph *𝐺* constructed as a *𝜏*-MNG on the database

X. There exists a monotonic path *𝑃* = [*𝑥*0*,...,𝑥ℓ*−1*,𝑥𝑛*], where *ℓ* is the length of the monotonic path. We can construct a sequence balls centered at *𝑞*: *𝐵*(*𝑞,𝛿*(*𝑞,𝑥*0))*,...,𝐵*(*𝑞,𝛿*(*𝑞,𝑥ℓ*−1).
<u>𝑉𝑜𝑙(𝐵 (𝑞,𝛿(𝑞,𝑥𝑖+1))</u> Define *𝜂𝑞,𝑖* = *𝑉𝑜𝑙*(*𝐵* (*𝑞,𝛿*(*𝑞,𝑥𝑖*)) for *𝑖* = 0*,...,ℓ* − 2. It follows:

<u>𝛿 (𝑞,𝑥𝑖+1)</u>*𝐷* *𝜂𝑞,𝑖* = ( *𝛿* (*𝑞,𝑥*) )*,𝑖* = 0*,...,ℓ* − 2*.* (10) *𝑖* Due to the graph *𝐺* satisfy that *𝛿* (*𝑞,𝑥𝑖*)− *𝛿* (*𝑞,𝑥𝑖*+1) *> 𝜏,𝑖* = 0*,...,ℓ* − 2. Let *𝑅𝑞* = *𝑚𝑎𝑥𝑥*∈X*𝛿* (*𝑞,𝑥*). It follows that: <u>𝛿 (𝑞,𝑥)−𝜏 𝑅 − 𝜏</u> <u>𝑖</u> *𝐷* <u>𝑞</u> *𝐷* *𝜂𝑞,𝑖* ≤() ≤() (11) *𝛿* (*𝑞,𝑥𝑖*) *𝑅𝑞* and

### 𝑉𝑜𝑙(𝐵(𝑞,𝛿(𝑞,𝑥ℓ−1))) = 𝑉𝑜𝑙(𝐵(𝑞,𝛿(𝑞,𝑥0)))𝜂𝑞,0...𝜂𝑞,ℓ−2

### <u>𝑅𝑞 − 𝜏</u>

*𝐷 ℓ* ≤ *𝑉𝑜𝑙*(*𝐵*(*𝑞,𝑅𝑞*)) *𝑅*

*.* (12)
*𝑞* Recall that *𝑅* denotes the largest distance between any two base vectors in the database X. Since *𝑅𝑞*≤ *𝑅* + *𝜏*, we obtain:

### <u>𝑉𝑜𝑙(𝐵(𝑞,𝛿(𝑞,𝑥ℓ)) 𝑅</u>

*𝐷 ℓ* <u>−1</u> ≤*.* (13) *𝑉𝑜𝑙*(*𝐵*(*𝑞,𝑅𝑞*)) *𝑅* + *𝜏* *𝐷* Let *𝜂*ˆ = <u>𝑅</u>. Since *𝜂*ˆ *<* 1, we have: *𝑅*+*𝜏*

### 𝑉𝑜𝑙(𝐵(𝑞,𝛿(𝑞,𝑥ℓ−1))) 𝛿 (𝑞,𝑥ℓ−1)

*ℓ* ≤ *𝑙𝑜𝑔𝜂*ˆ= *𝐷* ·*𝑙𝑜𝑔𝜂*ˆ*.* (14) *𝑉𝑜𝑙*(*𝐵*(*𝑞,𝑅𝑞*)) *𝑅* + *𝜏*

The expectation of *ℓ* over all possible *𝑞* is

E[*ℓ*]≤ <u>E[ln(𝛿 (𝑞,𝑥ℓ−1))]− ln(𝑅 + 𝜏)</u> (15)

Define *𝑓* (*𝑅*) = <u>ℓ−1</u>. Then, E[*ℓ*] ≤ *𝑓* (*𝑅*). The derivative of *𝑓* (*𝑅*) w.r.t. *𝑅* is

<u>ℓ−1)])(</u> (ln*𝑅* − ln (*𝑅* + *𝜏*))

Since *𝑅* + *𝜏 > 𝛿* (*𝑞,𝑥ℓ*−1)*,𝑓* ′

(*𝑅*) *>* 0, implying that *𝑓* is an
increasing function. Due to the base vectors are uniformly distributed with density *𝑔*. We have *𝑔* · *𝑉*X= |*𝑉*|. Since *𝑔* increases with |*𝑉*|, it must satisfy *𝑔* ≥ *𝑔* for some constant *𝑔*0. Therefore, *𝑉𝑜𝑙*(*𝐵*(*𝑅*))≤ ≤ 0. The volume of ball *𝑉𝑜𝑙*(*𝐵*(*𝑅*)) is given by:

<u>𝜋</u> *𝐷*/2 <u>𝑅</u> *𝐷* *𝑉𝑜𝑙*(*𝐵*(*𝑅*)) =*,* (17) Γ (1 + *𝐷*/2) *𝐷*/2 where Γ is the Gamma function. Let *𝑏* = *𝜋*. Then, *𝑅* ≤ Γ (1+*𝐷*/2) ( *𝑔* <u>|𝑉</u> *𝑏* <u>|</u>

)1/ *𝐷*. Define *𝑟*(|*𝑉*|) = (
*𝑔* <u>|𝑉</u> *𝑏* <u>|</u>

)1/ *𝐷*, since *𝑓* is an increasing func-
0 0 tion, we have the following

### <u>E[ln𝛿 (𝑞,𝑥ℓ−1)]− ln(𝑟(|𝑉|)+𝜏))</u>

E[*ℓ*]≤ *𝑓* (*𝑟*(|*𝑉*|)) =*.* (18) ln*𝑟*(|*𝑉*|)− ln(*𝑟*(|*𝑉*|)+*𝜏*)

√ Since *𝛿* (*𝑞,𝑥ℓ*−1) ≥ *𝛿* (*𝑞,𝑥𝑛*). We analyze two cases: (i) If √ *𝜏* ≥ *𝐷* 2 *𝐷*<u>1</u>*𝐷* 2 *𝐷* 2 ( *𝑔*

)1/, E[ln*𝛿* (*𝑞,𝑥𝑛*)] ≥ ln( 21 ( ln2
*𝑛* ) <sup>𝑚</sup>). (ii) If *𝜏* ≤ 2 ( *𝑔*

)1/,
### E[ln𝛿 (𝑞,𝑥𝑛)]≥ ln(𝜏/2). Therefore,

<u>(𝑟(|𝑉|)+𝜏) ln(𝑟(|𝑉|)+𝜏)</u> E[*ℓ*]≤ *𝑂* ()*.* (19) *𝜏* Since *𝜏* is a constant and independent of |*𝑉*|, it follows that <u>1</u> E[*ℓ*] = *𝑂* (*𝑟*(|*𝑉*|) ln*𝑟*(|*𝑉*|)) = *𝑂*(|*𝑉* | <sup>𝐷</sup> ln |*𝑉*|) (20) Due to the expected out-degree of MRNG is*𝑂*(ln |*𝑉*|), therefore the search time complexity is *𝑂*(|*𝑉* | <sup>𝐷</sup> (ln |*𝑉*|)2). □

<!-- Page 16 -->

C. DATASET We provide a detailed introduction to the five modern large-scale OOD datasets as follows:
- Text-to-Image: The Text-to-Image3 is a cross-domain dataset derived from a visual search engine. The database vectors are image embeddings generated by the Se-ResNext-101 model, while the query vectors are extracted form user-specified textual queries using a variant of the DSSM model.
- LAION: The LAION4 is a widely used text-image dataset. Both images and text are embedded using CLIP-ViT-B/32.
- WIT: The WIT dataset5 is a dataset derived from a text-to-image application. The image-text pairs are extracted from Wikipedia pages. Database vectors are encoded using CLIP-ViT-B/32, while queries are generated with CLIP-ViT-B32-multilingual-v1.
- WebVid: The WebVid6 is a caption-video dataset sourced from stock footage sites. The embeddings are encoded using CLIP-ViT- B/32.
- CC3M: The CC3M7 is a dataset of image-text pairs designed for image captioning systems. Database vectors are encoded using ViT-B/16, while attached text are transformed using BERT. In our experiments, all vectors across the datasets are normalized, and the ground truth is computed based on cosine similarity.
D. BASELINES & PARAMETER SETTINGS We compare CDMG with several state-of-the-art graph-based methods, with their detailed descriptions and experimental parameter settings provided as follows.
- HNSW: A well-known hierarchical graph-based search method. We set the out-degree parameter to *𝑀* = 32 after testing values from 8 to 48, and the *𝑒𝑓𝐶𝑜𝑛𝑠𝑡𝑟𝑢𝑐𝑡𝑖𝑜𝑛* is set to 500.
- *𝜏*-MNG: An optimized version of NSG with additional connections for improved recall. It shares parameters *𝑅* = 64*,𝐶* = 500*,𝐿* = 500 with NSG, and we fine-tune *𝜏* from 0.01 to 0.3, setting *𝜏* = 0*.*01 for optimal performance.
- RobustVamana: A graph index specifically designed for OOD queries. We set *𝑅* = 64, *𝐿* = 500, and *𝛼* = 1*.*0 after tuning *𝛼* from
1.0 to 1.2. Unless otherwise specified, the size of the queries is 10% of the database.
- RoarGraph: RoarGraph is an efficient graph index for OOD queries. We configure *𝑁𝑞* = 100*,𝑀* = 35 and *𝐿* = 500 following the official instructions. The size of the queries is the same as in RobustVamana, representing 10% of the database.
- CDMG (proposed): We configure *𝑆* = 15 during query aggregation, with *𝑀* = 35 and *𝐿* = 500 for graph index construction. The size of the queries is 10% of the database.
E. RELATED WORK Graph-based Vector Similarity Search. Numerous algorithms have been proposed to address the vector similarity search issue, 3 [https://research.yandex.com/datasets/text-to-image-dataset-for-billion-scale-similarity-search](https://research.yandex.com/datasets/text-to-image-dataset-for-billion-scale-similarity-search) 4 [https://laion.ai/blog/laion-400-open-dataset/](https://laion.ai/blog/laion-400-open-dataset/) 5 [https://github.com/IntelLabs/VectorSearchDatasets](https://github.com/IntelLabs/VectorSearchDatasets) 6 [https://github.com/m-bain/webvid](https://github.com/m-bain/webvid) 7 [https://github.com/google-research-datasets/conceptual-captions](https://github.com/google-research-datasets/conceptual-captions)
broadly divided into four types: graph-based, quantization-based, hash-based, and tree-based methods. Among these, graph-based methods have gained a strong position in the trade-off between search latency and accuracy. This advantage is due to two key properties of graph indexes: (i) they exhibit superior navigability, enabling rapid graph traversal to locate a small set of database vectors close to the query, and (ii) they effectively capture and ex- press clusterability for search results. Existing graph indexes are derived from base graphs with strong theoretical guarantees, e.g., DG, RNG. However, they were not originally designed for vector similarity search problems and typically suffer from high construction and search complexities. Current graph indexes enhance these base graphs in various ways to better suit retrieval tasks. For instance, HNSW improves DG and RNG with a hierarchical structure and a heuristic edge selection process, maintaining a hierarchical structure based on neighbor distances, which achieves nearly loga- rithmic search time complexity. NSG introduces MRNG, ensuring that a monotonous path exists between any two vertices in the resulting graph. Vamana and *𝜏*-MNG further enhance the flexibility of edge selection by adding a query relaxation parameter. OOD-oriented Graph-based Methods. Mainstream graph-based methods are designed for ID queries, resulting in limited search performance on OOD vector similarity search. To mitigate this limitation, two state-of-the-art methods have been proposed: RobustVamana [26] and RoarGraph [7]. RobustVamana integrates a subset of the query set during construction and establishes connections between database vectors that co-occur as out-neighbors of an indexed query. RoarGraph constructs the graph index under the guidance of query distribution. It follows three sequential processes. First, a query-base graph is constructed as the founda- tion, where queries are treated as bridges to connect their scattered ground truths. Then, the query-base graph is projected onto the base vectors, preserving only the relationships of the base vectors identified by the queries. Finally, neighbors of each base vector are supplemented to enhance connectivity. However, both methods de- pend on ID-oriented candidate acquisition and neighbor selection, which do not align with OOD queries. Recent studies have also investigated the OOD vector similarity search problem through approaches such as product quantization (e.g., APQ), dimensionality reduction (e.g., LeanVec and GleanVec), and entry point selection. These algorithms are orthogonal to the graph index.