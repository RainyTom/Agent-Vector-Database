URL: https://www.vldb.org/pvldb/vol18/p1649-gottesbueren.pdf
Content-Type: application/pdf
Method: markit
Notes: Converted with markit

---

<!-- Page 1 -->

# Unleashing Graph Partitioning for Large-Scale Nearest Neighbor Search

## Lars Gottesbüren Laxman Dhulipala

### Karlsruhe Institute of University of Maryland,

### Technology Google Research

lars.gottesbueren@kit.edu laxman@umd.edu

ABSTRACT We consider the fundamental problem of decomposing a large-scale approximate nearest neighbor search (ANNS) problem into smaller sub-problems. The goal is to partition the input points into neighborhood-preserving *shards*, so that the nearest neighbors of any point are contained in only a few shards. When a query arrives, a *routing* algorithm is used to identify the shards which should be searched for its nearest neighbors. This approach forms the backbone of distributed ANNS, where the dataset is so large that it must be split across multiple machines. In this paper, we design simple and highly efficient routing methods based on clustering and locality-sensitive hashing. We prove strong theoretical guarantees for the LSH-based method, whereas the clustering-based method exhibits better empirical performance. A crucial characteristic of our routing algorithms is that they are inherently modular, and can be used with *any* partitioning method. This addresses a key drawback of prior approaches, where the routing algorithms are inextricably linked to their associated partitioning method. In particular, due to their modular structure, our routing methods enable the use of *balanced graph partitioning*, which is a high-quality partitioning method without a naturally associated routing algorithm. Prior routing methods compatible with graph partitioning are too slow to train on large-scale data. We provide the first routing methods that are simultaneously compatible with graph partitioning, fast to train, admit low latency, and achieve high recall. In a comprehensive evaluation of our partitioning and routing on billion-scale datasets, we show that our methods outperform existing scalable partitioning methods by significant margins, achieving up to 1*.*72× higher QPS at 90% 10-recall than the best competitor and 1*.*27× in the geometric mean. Through fast and modular routing we establish graph partitioning as the new method of choice for partitioning large-scale ANNS datasets.

PVLDB Reference Format: Lars Gottesbüren, Laxman Dhulipala, Rajesh Jayaram, and Jakub Łącki. Unleashing Graph Partitioning for Large-Scale Nearest Neighbor Search. PVLDB, 18(6): 1649 - 1662, 2025. doi:10.14778/3725688.3725696

PVLDB Artifact Availability: The source code is available at [https://github.com/larsgottesbueren/gp-ann](https://github.com/larsgottesbueren/gp-ann).

This work is licensed under the Creative Commons BY-NC-ND 4.0 International License. Visit [https://creativecommons.org/licenses/by-nc-nd/4.0/](https://creativecommons.org/licenses/by-nc-nd/4.0/) to view a copy of this license. For any use beyond those covered by this license, obtain permission by emailing info@vldb.org. Copyright is held by the owner/author(s). Publication rights licensed to the VLDB Endowment. Proceedings of the VLDB Endowment, Vol. 18, No. 6 ISSN 2150-8097. doi:10.14778/3725688.3725696

## Rajesh Jayaram Jakub Łącki

### Google Research Google Research

rkjayaram@google.com jlacki@google.com

1 INTRODUCTION Nearest neighbor search is a fundamental algorithmic primitive that is employed extensively across several fields including computer vision, information retrieval, and machine learning [42]. Given a set of points *𝑃* in a metric space (X*,𝑑*) (where *𝑑*: X×X↦→ R≥0 is a distance function between elements of X), the goal is to design a data structure which can quickly retrieve from *𝑃* the *𝑘* closest points from any query point *𝑞* ∈X. The typical quality measure of interest is *𝑘-recall*, which is defined as the fraction of true*𝑘* nearest neighbors found. Solving the problem with perfect recall in high-dimensional space effectively requires a linear scan over *𝑃* in practice and in theory [2]. Therefore, most work has focused on finding *approximate* nearest neighbors (ANNS). The most successful methods are based on quantization [24, 31] and pruning the search space using an index data structure to avoid exhaustive search. Widely employed index data structures are k-d trees, k-means trees [8, 36, 38], graph-based indices such as DiskANN/Vamana [44] or HNSW [35] and flat inverted indices [13, 15, 28, 31, 34]. For a comprehensive review we refer to one of the several surveys on ANNS [1, 6, 33]. While graph indices offer the best recall versus throughput trade-off, they are restricted to run on a single machine due to fine-grained exploration dependencies and thus communication requirements. For a distributed system, the currently best approach is to decom- pose the problem into a collection of smaller ANNS problems that fit in memory [13]. The decomposition starts by performing *partitioning* – splitting the set *𝑃* into some number *𝑠* of subsets ∪ *𝑠𝑖* =1 *𝑆𝑖* = *𝑃* (the *shards*). Then, to query for a point*𝑞* ∈X, a lightweight *routing* algorithm is used to identify a small subset of the shards to search for the nearest neighbors of *𝑞*. The search within a shard is then performed using an in-memory solution such as a graph index if the shards are large (e.g., in distributed ANNS), or exhaustive search if the shards are small. Optimizing the partition is crucial to achieving good query performance, because it allows querying only a small subset of the shards. The partitioning and routing methods are closely connected, and, in some cases, inextricably intertwined. As a result, many existing routing methods require a particular partitioning method to be used. For example, consider partitioning via k-means clustering. Each shard (a *𝑘*-means cluster) is naturally represented by the cluster center. The folklore *center-based routing* algorithm identifies the shards whose centers are closest to the query point to be searched,

i.e., solves a much smaller ANNS problem first. In a parameter regime where exhaustive search in a shard is feasible (e.g., |*𝑆𝑖* |≤ 105) this approach is known as IVF (inverted file), another popular in-memory ANNS data structure [4, 24, 31]. For billion-scale datasets this entails *𝑠* ≥ 104 shards. In this paper,

<!-- Page 2 -->

on the other hand, we are interested in investigating these methods for *𝑠* in the order of *𝑠* ∈[10*,*100], with substantially more points per shard |*𝑆𝑖* |≈ 107 − 108, as needed in the distributed setting. We are interested in determining the best partitioning method for large-scale ANNS. In the following, we argue that despite being a very natural idea, the potential of *balanced graph partitioning* has remained untapped at billion-scale until now, due to several limitations. The idea is to first compute a *𝑘*-nearest neighbor graph *𝐺* = (*𝑉,𝐸*) of *𝑃* and then compute a balanced partition of *𝐺* with approximately minimal edge cut, which is a well-known NP-hard optimization problem with highly developed heuristic off-the-shelf solvers [7]. The *𝑘*-nearest neighbor graph (*𝑘*-NN) is a graph *𝐺* whose vertices are points in *𝑃*, in which each point has edges to its *𝑘* nearest neighbors in *𝑃*. The shards are computed by partitioning the vertices into roughly equal size sets, such that the number of edges which connect different sets is approximately minimized. More formally, compute a partition 𝚷 : *𝑉*→[*𝑠*] of *𝐺* into *𝑠* ∈ N shards of size |𝚷 −1

(*𝑖*)| ≤ <u>(1+𝜀</u> *𝑠* <u>)|𝑃 |</u>
### ∀𝑖 ∈ [𝑠], while minimizing

|{(*𝑢,𝑣*)∈ *𝐸* | 𝚷(*𝑢*) ≠ 𝚷(*𝑣*)}|. This attempts to put the maximum possible number of nearest neighbors of each vertex *𝑣* in the same shard as *𝑣*. Recently, Dong et.al. [15] achieved good results with this method on million-scale data as an IVF data structure, beating the widely used k-means partitioning. To tackle the routing problem, they train a neural model after partitioning. Unfortunately, training their routing model is extremely slow even at million-scale, ruling out use at billion-scale. While partitioning with *𝑘*-NN graphs promises significantly improved recall, it comes with two main limitations, which have hindered its adoption in favor of k-means partitioning.

(1) The shards do not admit natural geometric properties, such as convexity of shards computed by k-means clustering. As a result, there is no obvious method to route query points to shards. In fact, as mentioned, the best known routing method requires training computationally expensive neural models [15, 17].
(2) A *𝑘*-NN graph is required to partition the pointset. This is computationally expensive and seemingly introduces a chicken and egg problem, as computing a *𝑘*-NN graph requires solving the ANNS problem. In this paper, we show how to address the above limitations and *unleash* the benefits of balanced graph partitioning for billion-scale ANNS problems. Contribution 1: Fast, Inexact Graph Building Suffices for High-Quality Partitions. We empirically demonstrate that a very rough approximate *𝑘*-NN graph built by recursively performing *dense ball carving* suffices to obtain high quality partitions and query performance, and that such a coarse approximation can be computed quickly. Contribution 2: Fast, Accurate, and Modular Combinatorial Routing. We devise two combinatorial routing methods which are fast, high quality, and can be used with *any* partitioning method, in particular with graph partitioning, as they are trained on a finalized partition. Both adapt center-based routing to large shards, which is needed for distributed ANNS. Our first and empirically stronger routing method is called kRt. It is based on sub-clustering the points within each shard using hierarchical*𝑘*-means. We use either
the tree representation of the clustering or HNSW to retrieve center points, routing the query to the shards associated with the closest retrieved points. Our second method called hRt is based on locality sensitive hashing (LSH). It works by locating the query point in the sorted ordering of LSH compound hashes, and inspecting nearby points to determine which shards to query. Contribution 3: Theoretical Guarantees for Routing. We provide a theoretical analysis of hRt, and establish rigorous guarantees on its performance. Specifically, we prove that each query point will be routed to a shard containing one of its approximate nearest neighbors with high probability. To the best of our knowledge, these are the first theoretical guarantees for the routing step. While kRt performs better in practice, it does not offer theoretical guarantees, which is the benefit of hRt. This gap between theory and practice is common in approximate nearest neighbor search, where LSH-based methods provide better theoretical un- derstanding of the problem, whereas clustering or graph-based methods perform better empirically due to being data-dependent. Contribution 4: Empirical Evaluation. In our extensive evaluation, we demonstrate that shards obtained via balanced graph partitioning attain up to 1.72x higher throughput than the best competing partitioning method, and 1.27x in the geometric mean at billion-scale. While the partitioning time is 3× slower, the absolute time is reasonable: 2 hours for the highest-dimensional instance, giving a highly favorable trade-off. We analyze the shards and find that the concentration of ground-truth neighbors in the top-ranked shard per query is significantly higher (up to +25%). Our routing methods are multiple orders of magnitude faster to train than the existing neural network based approaches [15, 17] while obtaining similar or better recall at similar or lower routing time. Specifically, our kRt method can be trained in half an hour on billion-point datasets, compared to multiple hours required by the neural network approaches on 1000x smaller million-point datasets where kRt training takes under a second.

2 PARTITIONING We present two improvements to*𝑘*-NN graph partitioning. To speed up the *𝑘*-NN graph construction we present a highly scalable approximate algorithm in Section 2.1. Moreover, we propose an algorithm to compute overlapping shards in Section 2.2, to prevent recall losses in boundary regions. To compute the initial disjoint partition, we use the KaMinPar graph partitioner [23], for which we give a brief description in Section 5.

2.1 Approximate *𝑘*-NN Graph-Building To speed up *𝑘*-NN graph building, we use a simple approximate algorithm based on recursive splitting with *dense ball clusters*, as outlined in Algorithm 1. As long as the number of points is not sufficiently small for all-pairs comparisons, we split them using the following heuristic. We sample a small number of pivots *𝐿* from the pointset, and assign each point to the cluster *𝑃𝑐* represented by its closest pivot *𝑐*. Ties are broken arbitrarily. The clusters are treated recursively, either splitting them again or generating edges for all pairs in the cluster, filtered down to the top *𝑘* neighbors for each point. We take the union of the edges generated in different clusters.

<!-- Page 3 -->

### Algorithm 1: Recursive Dense Ball Graph Construction

### Input: Pointset 𝑃, all-pairs threshold 𝛼, pivot sampling

### fraction 𝛽, maximum number of pivots𝛾

### if |𝑃|≤ 𝛼

### Compute𝑑 (𝑃 × 𝑃) in parallel

Emit top *𝑘* neighbors for each *𝑝* ∈ *𝑃* else Sample *𝐿* ⊂ *𝑃* uniformly at random with |*𝐿*| = min(*𝛽* ·|*𝑃* |*,𝛾*) Compute*𝑑* (*𝑃* × *𝐿*) in parallel for *𝑐* ∈ *𝐿* do in parallel *𝑃𝑐* ←{*𝑝* ∈ *𝑃* | *𝑑* (*𝑝,𝑐*) = min*𝑙* ∈*𝐿𝑑* (*𝑝,𝑙*)} Recurse on *𝑃𝑐*

0*.*84
0*.*82
degree 8 100 5 query recall80 3

0*.*8050
20 approx 10 exact

0*.*2 0*.*4 0*.*6 0*.*8 1*.*0
graph recall

Figure 1: The *𝑥*-axis shows the recall of the approximate *𝑘*-

NN graph used for partitioning. The*𝑦*-axis shows the average recall of queries for 10 nearest neighbors when only a single shard is inspected. The plot uses a 1M slice of the DEEP dataset.

The intuition is that top-*𝑘* neighbors will be clustered together with good probability. We use a constant threshold *𝛼* below which we deem a cluster sufficiently small for all-pairs comparison. The number of pivots *𝑙* = |*𝐿*| is determined with a parameter *𝛽* ∈ (0*,*1) and a large constant*𝛾* (around 1000), as *𝑙* = min(*𝛽* ·|*𝑃* |*,𝛾*). Improved graph quality is attained via*𝑟* independent repetitions, and assigning points to the *𝑓* closest pivots instead of just one. The latter can only be done a few times without increasing running time exponentially, as this replicates each point in the current cluster to *𝑓* subclusters for recursion. We use *𝑓 >* 1 on the first recursion level and *𝑓* = 1 at lower levels. We again take the union of generated edges from different repetitions and filter down to the top *𝑘* for each point. Using an approximate *𝑘*-NN graph for partitioning the dataset is acceptable since only coarse local structure must be captured to ensure partition quality, i.e., the edges need only connect near neighbors together, not necessarily the exact top-*𝑘*. Figure 1 demonstrates that even low-quality graphs (*𝑘*-NN graph recall ≈ 0*.*3) lead to high query recall: more than 81% of top-10 neighbors are concentrated in one shard per query. Using an exact *𝑘*-NN graph (100% recall) for partitioning only increases the query ground

truth neighbor concentration to 84%. Here we use a slice of the DEEP dataset [3, 43] with *𝑛* = 10 6 points partitioned into *𝑠* = 16 shards of size |*𝑆𝑖* | ≤ 65625 each (*𝜀* = 5%). The x-axis shows the recall of the *𝑘*-NN graph; the *𝑦*-axis shows the average fraction of top−10 ground-truth neighbors of a query in the shard with most ground-truth neighbors (the concentration). To obtain approximate graphs with different quality scores, we vary the number of repetitions *𝑟* ∈{2*,*3*,*5*,*8*,*10}, top-level fanout *𝑓* ∈{2*,*3*,*5*,*8*,*10}, cluster size threshold *𝛼* ∈ {500*,*1000*,*2000*,*5000*,*10000} and the degree ∈{3*,*5*,*8*,*10*,*20*,*50*,*80*,*100}. The pivot sample fraction *𝛽* is a constant set to *𝛽* = 0*.*005. We also observe that higher degrees may in some cases lead to slightly worse query recall. We suspect that since we use a low-quality method, adding more approximate neighbors pollutes the graph structure. Note that this effect does not appear with an exact *𝑘*-NN graph computed via all-pairs comparison (marked as diamond in the plot). Notably, the query performance between approximate and exact graphs differs only by a small margin. Overall, these observations justify the use of highly sparse approximate graphs with only a few edges per point for the partitioning step, without sacrificing much of the query performance.

2.2 Partitioning into Overlapping Shards When partitioning into disjoint shards, we can incur losses on points on the boundary between shards, whose *𝑘*-NNs straddle multiple shards. To address this issue, we propose a greedy algorithm inspired by local search for graph partitioning [18] which eliminates cut edges by replicating nodes in a post-processing step. The set of cut (*𝑘*-NN) edges is precisely the recall loss when the points themselves are queries [15], thus minimizing cut edges is a natural strategy in our setting. We introduce a new parameter *𝑜* ≥ 1 to restrict the amount of replication. For a fair comparison with disjoint shards in memory-constrained settings, we keep the maximum shard size *𝐿*max(*𝑠*) fixed and instead increase the number of shards to *𝑠*
′ = *𝑜* · *𝑠*. We first compute a disjoint partition into *𝑠* ′ shards of size <u>(1+𝜀</u> ′ <u>)|𝑃 |</u> and *𝑠* <u>(1+𝜀)|𝑃 |</u> then apply an overlap algorithm with *𝑠* as the final size. In each step, our *overlap algorithm* takes a node*𝑢* and places it in the shard *𝑆𝑖*that contains the plurality of its neighbors and not <u>cut (𝑢,𝑆𝑖)</u> *𝑢*. This increases the average recall by *𝑘* |*𝑃* |, where cut(*𝑢,𝑆𝑖*) = |{(*𝑢,𝑣*)∈ *𝐸* | *𝑣* ∈ *𝑆𝑖*}| for all*𝑆𝑖* with*𝑢* ∉ *𝑆𝑖*. We repeat this procedure until there is no more placement into a below size-constraint shard which removes at least one edge from the cut. We greedily select the node whose placement eliminates the most cut edges for the next step. To parallelize this seemingly sequential procedure, we observe that nodes whose placement removes the same number of cut edges can be placed at the same time, i.e., grouped into bulk-synchronous rounds. Moreover, the number of possible cut values is small; at most *𝑘*. Thus, only few rounds are necessary since each node removes at least one cut edge and no new cut edges are added.

3 ROUTING The goal of routing is to identify a *small number of* shards which contain a large fraction of the *𝑘* nearest neighbors of a given query *𝑞* ∈X. We adapt center-based routing for our purpose. When using

<!-- Page 4 -->

### Algorithm 2: kRt: Training

*q* Input: number of shards *𝑠*, shards *𝑆𝑖*, number of centroids *𝑙*, index size*𝑚*, cluster size *𝜆* *c₂* *c₁* for *𝑖* = 1 *to 𝑠* do in parallel create root node *𝑣𝑖* <u>|𝑆𝑖|(</u> <u>𝑚</u> <u>−𝑠)</u> Build(*𝑆𝑖,𝑙,* *,𝜆,𝑣𝑖*) |*𝑃* | func Build(*𝑃, 𝑙, 𝑚, 𝜆, 𝑣*): if *𝑚* ≤ 1 return centroids(*𝑣*)← K-Means(*𝑃,𝑙*) for *𝑐* ∈ centroids(*𝑣*) do in parallel *𝑃𝑐* ← *𝑘*-means cluster of *𝑐* create new tree-node *𝑣𝑐* add tree-edge from parent *𝑣* to *𝑣𝑐* if |*𝑃𝑐*| *> 𝜆* <u>(𝑚−𝑙)|𝑃𝑐|</u> Build(*𝑃𝑐,𝑙,* *,𝜆,𝑣𝑐*) |*𝑃* |

### Algorithm 3: kRt: Routing

### Input: Search budget𝑏, query point 𝑞

PQ ←{(*𝑣 ,𝑖*0*,𝑖*)| *𝑖* ∈[*𝑠*]} *// min-heap with (tree node, key,* *shard ID)* prioritized by key min-dist[*𝑖*]←∞ ∀ *𝑖* ∈[*𝑠*] while PQ *not empty and 𝑏*−− *>* 0 do (*𝑣,𝑑𝑣,𝑠𝑣*)← PQ.pop() for *𝑐* ∈ centroids(*𝑣*) do min-dist[*𝑠𝑣*]← min(min-dist[*𝑠𝑣*]*,𝑑*(*𝑞,𝑐*)) if *𝑐 has a sub-tree* add (*𝑣𝑐,𝑑*(*𝑞,𝑐*)*,𝑠𝑣*) to PQ return sort [*𝑠*] by min-dist

Figure 2: Illustration of an example where routing using a

single center per shard fails. The nearest neighbors of*𝑞* are in the cluster of*𝑐*2, but*𝑑* (*𝑞,𝑐*1) *< 𝑑*(*𝑞,𝑐*2). If the hierarchical subclusters are represented with their own centers, the routing works correctly.

IVF for |*𝑃*| = 109 points, a typical configuration uses*𝑠* ≈ 106 shards, with |*𝑆𝑖* | ≈ 103 points each [4]. Using a single center per shard works well in this setup. In contrast, in our setup we have few shards *𝑠* ∈[10*,*100] of large size |*𝑆𝑖*| *>* 10 7 and use a graph index per shard to retrieve neighbors. Large shards cannot be as easily represented with a single center. Figure 2 illustrates the problem where routing fails because sub-cluster structures are not represented well by the center. Moreover, the shards from graph partitioning are not connected convex regions as in *𝑘*-means clustering, which further aggravates this issue. For example, routing with a single center resulted in only 28% 10-recall in the top-ranked shard on the MS Turing dataset, as opposed to 81% with our best routing index. Instead of a single center, we represent each shard *𝑆𝑖* ⊂ *𝑃* by multiple points *𝑅𝑖* with the goal of accurately capturing sub-cluster structure. At query time, given a query point *𝑞* ∈X we retrieve from *𝑅* = ∪*𝑖*∈[*𝑠*]*𝑅𝑖* a set *𝑄* of (approximately) closest points from *𝑞*. Then, we route the query to the shards which the points of*𝑄* belong to. More precisely, we compute a probe order of the shards, ranking each shard *𝑖* based on the distance between *𝑞* and the closest point in *𝑄* ∩ *𝑅𝑖*. The size of *𝑅* is limited to a parameter *𝑚* so that the routing index fits in memory. Our routing algorithms differ in the way the coarse representatives *𝑅𝑖* are constructed at training time and by the ANNS retrieval data structure used to determine *𝑄*. The first algorithm uses hierarchical *𝑘*-means to coarsen *𝑃* to *𝑅* and uses the resulting *𝑘*-means tree for retrieval. This method is called *kRt* for*𝑘*-means RoutTing. In the experiments we also build an HNSW index on all points of *𝑅* for even faster retrieval. The second algorithm called *hRt* (HashRouTing) uses uniform random sampling to coarsen and a variant of LSH Forest [5] to find *𝑄*. In Section 3.3 we provide theoretical bounds for the quality of hRt, and later demonstrate that both methods perform well in practice, with kRt having a sizeable advantage empirically.

3.1 K-Means Tree Routing Index: kRt Algorithm 2 shows the training stage for kRt. We create one root node *𝑣𝑖* per shard *𝑆𝑖* and hierarchically construct a separate *𝑘*- means tree for each shard. The set *𝑅𝑖* consists of centroids across all recursion levels of hierachical *𝑘*-means. Per level we use *𝑙* = 64 centroids for the sub-tree roots. Furthermore, we are given a maximum index size *𝑚*, which we split proportionately among
sub-trees according to their cluster size. We subtract *𝑙* from the budget on each level, to account for the centroids. The recursive coarsening stops once the budget*𝑚* is exceeded or the number of points is below a cluster size threshold *𝜆* (we use 350). In contrast to usual *𝑘*-means search trees, we do not store or search the points in leaf-nodes, as our goal is to coarsen the dataset. Algorithm 3 shows the routing algorithm which is similar to beam-search [41]. At a non-leaf node we score the centroids against the query*𝑞* and insert the non-leaf children with the associated cen- troid distance into a priority queue for further exploration. Initially the priority queue contains the tree roots. The search terminates when either the priority queue is empty or a search budget *𝑏* (a parameter) is exceeded.

3.2 Frequency-Based Routing In addition to routing by minimum distance, we propose a second signal that is good at routing: the frequency of how often a shard appears in*𝑄*. Let*𝑤* be a second beam-width parameter and let*𝑄*
′ ⊂ *𝑄* be the*𝑤* closest points to the query from *𝑄*. Then the priority of shard *𝑆𝑖* is |{*𝑣* ∈ *𝑄* ′ | *𝑣* is in subtree rooted at *𝑣𝑖*}|, and shards are sorted by decreasing priority. For kRt with HNSW acceleration, we use*𝑄* ′ = *𝑄*, i.e., use the same beam-width*𝑤* = ef_search as HNSW.

<!-- Page 5 -->

### Algorithm 4: hRt: Training

Input: Shards *𝑆𝑖*, index size*𝑚* ≤ *𝑛*, repetition parameter *𝑟*, sketch size *𝑡* for *𝑖* = 1 *to 𝑠* do in parallel <u>𝑚·|𝑆𝑖|</u> Sample ⌊ ⌋ points *𝑅𝑖* from *𝑆𝑖* uniformly without |*𝑃* | replacement *𝑠𝑖* *𝑅* = ∪ *𝑅𝑖* =1 for *𝑗* = 1 *to 𝑟* do in parallel Draw independent random hash functions *ℎ𝑗,*1*,...,ℎ𝑗,𝑡* from a LSH family for X For each *𝑥* ∈ *𝑅* define a sorting key *ℎ𝑗*(*𝑥*)=(*ℎ𝑗,*1(*𝑥*)*,...,ℎ𝑗,𝑡*(*𝑥*)) Sort *𝑅* lexicographically with keys *ℎ𝑗* to obtain *𝐼 𝑗*

### Algorithm 5: hRt: Routing

Input: Query point *𝑞*, window size*𝑊* ≥ 1, partition 𝚷′ : *𝑅*→[*𝑠*] min-dist[*𝑖*]←∞ ∀ *𝑖* ∈[*𝑠*] for *𝑗* = 1 *to 𝑟* do Binary search in *𝐼 𝑗* for position *𝜏* of the point lexicographically closest to *ℎ𝑗*(*𝑞*) for *𝑐* ∈ *𝐼 𝑗*[*𝜏* − *𝑊* : *𝜏* + *𝑊*] do min-dist[𝚷′ (*𝑐*)]← min(min-dist[𝚷′ (*𝑐*)*,𝑑*(*𝑞,𝑐*)) return sort [*𝑠*] by min-dist

However, for plain kRt, the set *𝑄* contains too many irrelevant points, as it is yet not filtered by distance during the search. Empirically, we found that frequency-based routing is good at finding the first-ranked shard to probe, but performs worse than distance-based routing at ranking later probed shards. Therefore, we also consider a hybrid approach, where the first shard to probe is determined using frequency-based routing and the remaining shards are ranked by distance.

3.3 Sorting-LSH Routing Index: hRt We now describe a routing scheme based on *Locality Sensitive Hashing* (LSH). At a high level, an LSH family H is a family of hash functions of the form *ℎ* : X→{0*,*1}, such that similar points are more likely to collide; namely, the probability Pr*ℎ*∼H[*ℎ*(*𝑥*) = *ℎ*(*𝑦*)] should be large when *𝑥,𝑦* ∈X are similar, and small when *𝑥,𝑦* are farther apart. We formalize this in the proof of Theorem 1, and describe the routing index assuming we have an LSH family. Algorithm 4 describes the construction of a SortingLSH index. We first subsample *𝑚* points *𝑅* from *𝑃* uniformly at random. A single SortingLSH index is created as follows: we hash each point *𝑥* ∈ *𝑅* multiple times via independent hash functions from H, and concatenate the hashes into a string (*ℎ*1(*𝑥*)*,ℎ*2(*𝑥*)*,...,ℎ𝑡*(*𝑥*)). Importantly, we use the same hash functions *ℎ*1*,...,ℎ𝑡* for each point in *𝑅*. We *sort* the points in *𝑅 lexicographically based on these* *strings of hashes*. The index is the points stored in sorted order along with their hashes. Intuitively, similar points are more likely to collide often, thus share a longer prefix in their compound hash string, and thus are more likely to be closer together in the sorted
order. We repeat the process *𝑟* times to improve retrieval quality (up to 24 in our experiments). Next, Algorithm 5 describes the routing procedure for SortingLSH indices. Given a query point *𝑞*, for each of the *𝑟* indices, we hash *𝑞* with the LSH functions used for that repetition, compute the compound hash string*ℎ*(*𝑞*)=(*ℎ*1(*𝑞*)*,...,ℎ𝑡*(*𝑞*)) for*𝑞*, and then find the position *𝜏* ∈[*𝑚*] that is lexicographically closest to *ℎ*(*𝑞*). We retrieve the window [*𝜏* −*𝑊,𝜏* + *𝑊*] in the sorted order to consider these points’ distances to the query in the ranking. One key advantage of employing LSH is that we can prove for- mal guarantees for the retrieved points. Since our approach is more involved than searching through a single set of hash buckets, we provide a new analysis which demonstrates that it provably recovers a set of relevant points*𝑄* that results in routing to a shard which contains an approximate nearest neighbor. We remark that, while the routing only guarantees we are routed to a shard containing an *approximate* nearest neighbor, under mild assumptions on the partition, most of the approximate nearest neighbors of a given point will be in the same shard as the closest point. Under such a natural assumption, our SortingLSH index also recovers the true nearest neighbors.

Theorem 1. *Fix any approximation factor 𝑐 >* 1*, and let 𝑃* ⊂ (R *𝑑* *,* ∥·∥*𝜌*) *be a subset of the 𝑑-dimensional space equipped with the* *ℓ𝜌 norm, for any 𝜌* ∈ [1*,*2]*. Let 𝑛* = |*𝑃* | *and 𝑚 denote the routing* *index size. Set stretch factor 𝛼* = *𝑂* (*𝑐*)*, repetitions 𝑟* = *𝑂* (*𝑛* 1/*𝑐* ) *and* *window size 𝑊* = *𝑂*(1)*. Denote by 𝜋𝑘*(*𝑞*) *the distance from 𝑞 to the* *𝑘-th nearest neighbor of 𝑞 in 𝑃. Then the following holds for any* *query 𝑞* ∈ R *𝑑* *:*

- *If 𝑚* = *𝑛, then with probability* 1 − 1/poly(*𝑛*) *𝑞 gets routed* *to a shard* 𝚷 (*𝑝*) *containing at least one point 𝑝* ∈ *𝑃 that* *satisfies* ∥*𝑝* − *𝑞*∥*𝜌*≤ *𝛼𝜋*1(*𝑞*)*.*
- *If 𝑚 < 𝑛, then for any 𝛿* ∈(0*,*1)*, with probability* 1 − *𝛿 the* *query 𝑞 gets routed to a shard* 𝚷(*𝑝*) *containing at least one* *point 𝑝* ∈ *𝑃 that satisfies* ∥*𝑝* − *𝑞*∥*𝜌*≤ *𝛼* · *𝜋*⌈log*𝛿*−1
*𝑚* <u>𝑛</u>⌉(*𝑞*)*.*

Proof. The proof consists of the following steps: 1) embedding into Hamming space, 2) constructing a hash family and SortingLSH index for Hamming space, 3) analyze the prefix collision probabili- ties and lastly 4) apply these to routing decisions. To simplify the construction of the hash family H, we first embed the pointset *𝑃* into a subset of the *𝑑* ′ -dimensional Hamming cube {0*,*1} *𝑑* ′ with a constant distortion in distances. Let <u>max𝑥,𝑦∈𝑃∥𝑥 −𝑦 ∥𝜌</u> Φ = min*𝑥,𝑦*∈*𝑃*∥*𝑥* −*𝑦* ∥*𝜌* denote the aspect ratio of *𝑃*. By Lemma

A.2 and A.3 of [10
′], for any *𝜌* ∈[1*,*2] there exists an embedding *𝑓𝜌* : *𝑃* →{0*,*1} *𝑑*, with *𝑑* ′ = *𝑂* (*𝑑*Φ log*𝑛*), such that there exists a constant *𝐶* so that with probability 1 − 1/poly(*𝑛*) for all *𝑥,𝑦* ∈ *𝑃*, we have ∥*𝑓𝜌*(*𝑥*)− *𝑓𝜌*(*𝑦*)∥0 ≤ ∥*𝑥* − *𝑦* ∥*𝜌*≤ *𝐶* ·∥*𝑓𝜌*(*𝑥*)− *𝑓𝜌*(*𝑦*)∥0, where ∥*𝑎* − *𝑏*∥0 = |{*𝑖* ∈ [*𝑑* ′]|*𝑎𝑖* ′ ≠ *𝑏𝑖*}| is the Hamming distance between any two *𝑎,𝑏* ∈{0*,*1} *𝑑*. The constant *𝐶* will go into the approximation factor of the retrieval, but note that*𝐶* can be set to (1 +*𝜀*) by increasing the dimension by a *𝑂*(1/*𝜀* 2 ) factor. Thus, in *𝑑* ′ what follows, we may assume that *𝑃* ⊂{0*,*1} as well as the query *𝑞* are a subset of the *𝑑* ′ -dimensional hypercube equipped with the Hamming distance.

<!-- Page 6 -->

We now construct the hash family H which we will use for the SortingLSH Index. A draw *ℎ* ∼ H is generated as follows:

(1) First, sample *𝑖*1*,...,𝑖ℓ* ∼[*𝑑*
′] uniformly at random, where *ℓ* = ′ *𝑑*′ *𝑂* (*𝑑* log*𝑛*), (2) for any*𝑥* ∈{0*,*1} define*ℎ*(*𝑥*)=(*𝑥𝑖₁,𝑥𝑖₂,...,𝑥𝑖<sub>ℓ</sub>*)∈ {0*,*1} *ℓ*. As notation, for any *ℓ* ′ ≤ *ℓ* and hash function *ℎ* defined in *ℓ* ′ the above way, we write *ℎℓ*′ (*𝑥*)=(*𝑥𝑖₁,𝑥𝑖₂,...,𝑥𝑖<sub>ℓ</sub><sub>′</sub>*) ∈ {0*,*1} to ′ denote the *ℓ*-prefix of *ℎ*. We are now ready to prove the main claims of the Theorem. First, suppose we are in the case that *𝑚* = *𝑛*, and thus the pointset is *not* subsampled before the construction of the SortingLSH index. ∗ Let *𝑝* = arg min*𝑝* ∈*𝑅*∥*𝑝* − *𝑞*∥0 be the nearest neighbor to *𝑞*, with ∗ ties broken arbitrarily. We first claim that *𝑝* and*𝑞* share a *𝑡*-length prefix in at least one of the *𝑖* ∈[*𝑟*] repetitions of SortingLSH. Set ∗ <u>4𝑑′ln𝑛</u> ∗ *𝑡* = *𝑐* ∥*𝑝*∗−*𝑞* ∥0. Then the probability that *𝑝,𝑞* share a prefix of length at least *𝑡* ∗ – namely, the event *ℎ𝑡*∗ (*𝑝* ∗ ) = *ℎ𝑡*∗ (*𝑞*), is at least

(︃ ∗ )︃ *𝑡* ∗ (︃ ∗

)︃<u>(4/𝑐)</u> ∗ <u>𝑑</u> <u>′ log𝑛</u>
### (︃)︃ (8/𝑐) log𝑛

### ∥𝑝 − 𝑞∥0∥𝑝 − 𝑞∥0∥𝑝 −𝑞∥01

### 1 −

′ = 1 − ′ ≥ *𝑑 𝑑* 2

= *𝑛𝑂* (1/*𝑐*)

*𝑥* where we used the inequality (1 − *𝑥*/2)1/ ≥ 1/2 for any 0 *<* ∗ *𝑥 <* 1. Next, for any*𝑥* such that ∥*𝑥* −*𝑞*∥0 ≥ 10/*𝑐* ∥*𝑝* −*𝑞*∥0, we have:

(︃)︃*𝑡* ∗(︃)︃<u>(1/𝑐)</u>
∗ <u>𝑑</u> <u>′ log𝑛</u> <u>∥𝑥 − 𝑞∥0∥𝑥 − 𝑞∥0</u>∥*𝑝* −*𝑞*∥04 1 − ′ = 1 − ′ ≤ 1/*𝑛* *𝑑 𝑑*

where we used the inequality that (1 −*𝑥*) *𝑛*/*𝑥* ≤(1/2) *𝑛* for any *𝑥* ∈(0*,*1] and*𝑛* ≥ 1. Union bounding over all*𝑟 < 𝑛* trials, it follows that with probability at least 1−1/*𝑛* 3, we never have*ℎ𝑡*∗ (*𝑥*) = *ℎ𝑡*∗ (*𝑞*) for any *𝑥* such that ∥*𝑥* − *𝑞*∥0 ≥ 10/*𝑐* ∥*𝑝* ∗ − *𝑞*∥0, and moreover we do have *ℎ𝑡*∗ (*𝑝* ∗ ) = *ℎ𝑡*∗ (*𝑞*) for at least one repetition of the sampling. Let *ℎ* 1 *,ℎ* 2 *,...,ℎ𝑟* be the *𝑟* independent hash functions drawn for the SortingLSH routing index. By the above, there exists a repetition *𝑖* ∈[*𝑟*] such that *ℎ𝑖𝑡<sub>∗</sub>* (*𝑝* ∗ ) = *ℎ𝑖𝑡<sub>∗</sub>* (*𝑞*). First, suppose that *𝑝* ∗ was added to the window. Then since *𝑝* ∗ is the closest point to *𝑞*, by construction of the algorithm the point*𝑞* will be deterministically routed to 𝚷 [*𝑝* ′] for a point *𝑝* ′ such that ∥*𝑞* − *𝑝* ′ ∥ *𝜌*= ∥*𝑞* − *𝑝* ∗ ∥ *𝜌*, which completes the proof in this case. Otherwise, on repetition*𝑖*, if *𝑝* ∗ was not added to the window, then since *ℎ𝑖𝑡<sub>∗</sub>* (*𝑝* ∗ ) = *ℎ𝑖𝑡<sub>∗</sub>* (*𝑞*), there must be another point *𝑝* ′ with *ℎ𝑖𝑡<sub>∗</sub>* (*𝑝* ′ ) = *ℎ𝑖𝑡<sub>∗</sub>* (*𝑞*) on that repetition. By the above, such a point must satisfy ∥*𝑞* − *𝑝* ′ ∥ *𝜌*≤ *𝑂* (*𝑐*)∥*𝑞* − *𝑝* ∗ ∥ *𝜌*as needed. Finally, the case of*𝑚 < 𝑛* follows by noting that the*𝑂*(log(1/*𝛿*)· *𝑛*/*𝑚*)-th nearest neighbor to *𝑞* will survive after sub-sampling *𝑚* points from *𝑛* with probability at least 1 −*𝛿*/2, in which case the above analysis applies. □

4 EMPIRICAL EVALUATION We evaluate the performance of partitioning-based approximate nearest neighbor search algorithms using different partitioning and routing algorithms in terms of the recall of retrieving top-10 nearest neighbors versus number of shards probed *𝜂* and throughput (queries-per-second). In order to assess the quality of partitions alone, we additionally look at recall with a *hypothetical* routing

oracle that knows the entire dataset and picks an optimal sequence of shards to probe, i.e., one that maximizes recall.

4.1 Experimental Setup *Datasets.* We test on the kNN datasets from [https://big-ann-benchmarks.com/neurips21.html](https://big-ann-benchmarks.com/neurips21.html), which have one billion points each; namely *DEEP* (dim = 96, *ℓ*2), *Text-to-Image* (dim = 200, inner product, abbreviated as TTI) and *Turing* (dim = 100, *ℓ*2) which have real-valued coordinates, as well as SIFT1B (dim = 128, *ℓ*2, also known as BIGANN) and SpaceV (dim = 100, *ℓ*2) which have byte-valued coordinates. These are some of the *most challenging* *billion-scale ANNS datasets* that are currently being evaluated by the community. For example Text-to-Image exhibits out-of-distribution query characteristics, as demonstrated in [30]. We allow *𝜀* = 5% imbalance of shard sizes. In addition to disjoint partitions, we also consider overlapping partitions with 20% replication (*𝑜* = 1*.*2). We use *𝑠* = 40 shards in the non-overlapping case and *𝑠* = 48 shards in the overlapping case (so that the maximum shard size is the same in both cases). As the neural routing methods do not scale to billions of points, we also benchmark on two smaller datasets, namely SIFT1M (dim = 128, *ℓ*2) and GLOVE (dim = 100, angular distance), which have roughly a million points, partitioned into*𝑠* = 16 shards with*𝜀* = 5%. *Machine Setup.* The query experiments are run on a 64-core (1 socket) EPYC 7702P clocked at 2GHz with 1TB RAM and 256 MB L3 cache. The partitioning experiments are run on a 128-core (2 sockets, 16 NUMA nodes) EPYC 7713 clocked at 1.5GHz with 2TB RAM and 256 MB L3 cache. The small-scale experiments comparing GP with the neural methods are run on a 128-core EPYC 7742 clocked at
2.25GHz with 1TB RAM and 256 MB L3 cache.
4.2 Methods We compare graph partitioning (GP) with three prior scalable partitioning methods: *𝑘*-means (KM) clustering, balanced *𝑘*-means (BKM) clustering [11], and Pyramid [13]. To distinguish disjoint and overlapping partitioning we prefix the corresponding method name with O (OGP, OKM, OBKM). OKM and OBKM create overlap by assigning points to second-closest clusters (etc.) as proposed by [8]. We implemented our algorithms and baselines in a common framework in C++ for a fair comparison. Our source code is available at [https://github.com/larsgottesbueren/gp-ann](https://github.com/larsgottesbueren/gp-ann). We note that the original source code for Pyramid [13] is not available, and the original source code for BKM [11] is not parallel, which is prohibitive for us. Thus we implemented a parallel version of BKM. To compute graph partitions, we use KaMinPar [23] which is the currently fastest algorithm. For an overview of the field of graph partitioning, we refer to a recent survey [7] and for a brief description of KaMinPar, we refer to Section 5. The *𝑘* for the *𝑘*-NN graph used in graph partitioning is set to 10. *Neural Routing.* Later on, we also compare with two methods that use neural routing: BLISS [25] and USP [17]. We exclude Neural-LSH [15] since it is strictly outperformed by USP in all aspects, but remark that its training took over two days on GLOVE. USP and BLISS jointly learn the partition and routing index using neural

<!-- Page 7 -->

Table 1: Imbalance and 10-recall for Pyramid at *𝜂* = 1.

Table 2: Parameter overview and default values for |*𝑃*| = 109.

|dataset|algorithm|max shard size|routing oracle recall 𝜂 = 1||
|---|---|---|---|---|
|DEEP|Pyramid imbalanced|34.4M||80.3%|
|DEEP|Pyramid reassign|26.25M||77.2%|
|DEEP|Pyramid bin-packing|26.2M||68.8%|
|Turing|Pyramid imbalanced|31.2M||75.9%|
|Turing|Pyramid reassign|26.25M||75.2%|
|Turing|Pyramid bin-packing|26.21M||70.6%|
|Text-to-Image|Pyramid imbalanced|47.2M||80.5%|
|Text-to-Image|Pyramid reassign|26.25M||76.9%|
|Text-to-Image|Pyramid bin-packing|26.01M||70.6%|
|SpaceV|Pyramid imbalanced|30.8M||87.4%|
|SpaceV|Pyramid reassign|26.25M||87.2%|
|SpaceV|Pyramid bin-packing|26.25M||82.1%|
|SIFT1B|Pyramid imbalanced|41.4M||70.8%|
|SIFT1B|Pyramid reassign|26.25M||68.1%|
|SIFT1B|Pyramid bin-packing|26.23M||59.6%|

|Parameter|Value|Description|
|---|---|---|
|𝛼|5000|threshold for all-pairs|
|𝛽|0.005|# pivots = min(𝛾,𝛽𝑛)|
|𝛾|1500 | 950|max pivots. recursion | top level|
|𝑟|3|graph-building repetitions|
|𝑓|3|assign to 𝑓 pivots on top-level|
|𝑚|20K - 10M|routing index size|
|𝑙|64|kRt centroids|
|𝜆|350|kRt recursion cutoff|
|𝑊|50-4000|hRt window size|
|𝑟|1 - 24|hRt repetitions|
|𝑏|5000-50000|max comparisons for hRt and kRt|

networks. Given a query, the neural net infers a probability distribution of the shards, which is interpreted as a probe order. BLISS uses a cross-entropy loss formulation to maximize the number of points which have at least one near-neighbor in their shard, and uses the power of *𝑘* choices (top ranked shards) to achieve a balanced assignment. USP uses a linear combination of edge cut and normalized squared shard size as the loss function. Both methods rely on building a *𝑘*-NN graph for their loss function. We use the publicly available Python implementations in PyTorch (USP) and TensorFlow (BLISS).

### 𝑘-means Balancing. While 𝑘-means clusters are fairly balanced,

they often do not adhere to the strict shard size limit. For KM, we tested two rebalancing approaches: 1) migrate points from overloaded clusters to their second closest center (etc.) and 2) splitting overloaded clusters recursively with*𝑘*-means using smaller*𝑘*. Rem- igrating is better for QPS which is why we opted for this approach.

*Pyramid.* In the following we describe the partitioning and routing used in Pyramid [12, 13]. First the dataset *𝑃* is randomly subsampled to a smaller pointset *𝑃* ′. *𝑃* ′ is then further aggregated via flat *𝑘*-means to *𝑃*ˆ, with |*𝑃*ˆ | = 10000. On *𝑃*ˆ a (HNSW) graph is built, which is partitioned into balanced shards using graph partitioning. This HNSW graph is used as the routing index – all shards with points visited in a query are probed. Hence, the search-beamwidth affects the number of probes. The partition of *𝑃*ˆ is extended to *𝑃* by assigning each point to the shard of its nearest neighbor in *𝑃*ˆ. Because of the last assignment step, the shards are highly imbalanced. In Table 1 we report that Pyramid exhibits between

23*.*2% − 88*.*8% imbalance. If we enforce a 5% imbalance for Pyramid by reassigning points to the closest cluster below the size constraint, recall drops by 0*.*2% − 3*.*6%. We use this balanced version in the experiments. Another idea is to overpartition into more shards and perform bin-packing into balanced shards. We overpartition by 10x. As shown this method performs worse than reassignment. Note that we achieved better partitions and query throughput by partitioning a*𝑘*-NN graph on *𝑃*ˆ rather than partitioning the HNSW graph. For routing we still use the HNSW graph built on *𝑃*ˆ.
4.3 Configuration In Table 2 we recap the different parameters of the graph-building and routing algorithms and provide sensible default or grid search values that were tuned for index building speed and good query
9 recall at |*𝑃*| = 10 points. The values for graph-building and routing index construction also work well for other pointset sizes. The routing query parameters (comparison budget, window size or beamwidth) should be tuned for each dataset. The routing index size*𝑚* should be tuned offline with consideration for memory consumption and routing recall.

### Partitioning Configuration. For 𝑘-NN graph building with dense

ball clusters we use *𝑟* = 3, *𝑓* = 3, *𝛼* = 5000, *𝛽* = 0*.*005. Larger values of *𝛼,𝑟,𝑓* lead to higher quality graphs at the expense of higher running time, with linear dependence on *𝑟* and *𝑓* and quadratic dependence on *𝛼*. As we showed earlier, we can obtain high query recall even with a low-recall graph. Therefore, these values are chosen to save on index building time. The number of pivots*𝑙* = |*𝐿*| is set to *𝑙* = min(1500*,𝛽*|*𝑃*|) on the lower recursion levels; and reduced to*𝑙* = min(950*,𝛽*|*𝑃*|) on the top level to save running time. This value should balance between splitting the dataset into small clusters (large value) and retaining the sampled pivots in cache. We use the *default* preset of KaMinPar. For SIFT1M and GLOVE we use *𝑟* = 5 and *𝑓* = 5 as well as the *strong* preset of KaMinPar since the budget for index construction time is not as constrained as for billion-scale.

### Routing Configuration for Billion-Scale. Unless mentioned other-

wise, all methods use kRt for routing, since it is also the general- ization of *𝑘*-means’ native routing method for large shards, which is the regime we are interested in. For the throughput evaluation we accelerate the routing query with HNSW. For *𝑘*-means based partitioning methods, we also use the native routing method with one center per shard. We use a cluster size threshold of *𝜆* = 350, number of centroids *𝑙* = 64 and tree search budget*𝑏* = 50*𝐾*. In preliminary experiments, we tested different parameter settings and found the results were not sensitive to *𝜆* and *𝑙* which is why these results are not reported here, whereas*𝑏* does influence routing quality. Note that we do not explore different budgets *𝑏* here, as kRt with tree-search is only

<!-- Page 8 -->

|8 DEEP|Turing|TTI|
|---|---|---|
|7|||
|6|||
|5|||
|4|||
|3|||
|2|||
|1|||
|0.8 0.9|1.0 0.8|1.0 0.8 1.0|
|recall|recall|recall|

GP Oracle GP kRt GP hRt

shards probed

Figure 3: Recall vs shards probed *𝜂* comparing routing with

hRt vs kRt using GP as the partition and exhaustive search in the shards.

used in Section 4.7. To explore different routing quality configurations, we therefore vary the size *𝑚* of the set of coarse representatives *𝑅*, testing *𝑚* ∈ {20*𝐾,*100*𝐾,*200*𝐾,*500*𝐾,*1*𝑀,*2*𝑀,*5*𝑀,*10*𝑀*}. Larger values of*𝑚* lead to more accurate routing, at the expense of a linear increase in memory and sublinear increase in routing time. We observe that larger*𝑚* also lead to higher QPS for all values we tested, though with diminishing returns. The HNSW configuration for routing uses a degree of 32 and beamwidth ef_construction = 200 for insertion. To explore different routing quality trade-offs we vary the beamwidth during search ef_search ∈{20*,*40*,*80*,*120*,*200*,*250*,*300*,*400*,*500}. The memory consumption for the routing index is *𝑚* ·(*𝑑𝑖𝑚* + deg +1)· 4 bytes. For example, using *𝑚* = 500*𝐾,𝑑𝑖𝑚* = 100 and degree 32 leads to 266MB, or*𝑚* = 5*𝑀* leads to 2.66GB. Based on preliminary testing, we use distance-based routing for the datasets Turing, DEEP, and SpaceV, and we use the distance-frequency-hybrid for Text-to-Image and SIFT1B. Changing this parameter does not require rebuilding any data structures, hence it can be easily tuned at query time. For routing with hRt we test the number of repetitions *𝑟* ∈ {1*,*4*,*8*,*16*,*24} and window size*𝑊* ∈{50*,*100*,*200*,*400*,*1000*,*2000*,*4000}. We use the same values for*𝑚* as with kRt. More repetitions and larger windows lead to more accurate routing through more accurate retrieval at the cost of higher running time. The window size can be tuned directly at query time. Similarly, the number of repetitions can be increased incrementally when needed, without having to rebuild earlier SortingLSH indices. The results in Figure 3 use the best performing configuration with search budget*𝑏* = *𝑟* · 2*𝑊* ≤ 50*𝐾*. We use SimHash as the locality sensitive hash function with 64 pro- jections, i.e., the compound hash of each vector contains 64 hash bits. This enables efficient encoding into 64 bit integers and allows us to use standard sorting algorithms to construct the SortingLSH index, instead of more costly string sorting algorithms. Since we use SortingLSH [5], we need not tune the number of bits to an appropriate hash bucket size.

### In-Shard Search Configuration for Billion-Scale. For in-shard search,

we also use degree 32 and ef_construction = 200. For different in-shard search recall trade-offs we again vary the beam-width parameter ef_search ∈{50*,*80*,*100*,*150*,*200*,*250*,*300*,*400*,*500}.

*Configuration for Million-Scale.* On SIFT and GloVe we use a smaller router and HNSW graph with kRt. We set the router size *𝑚* = 50*𝐾*, the search budget*𝑏* = 5*𝐾*, number of centroids*𝑙* = 32 and cluster size threshold*𝜆* = 200. The HNSW graphs use degree 16 and ef_construction = 200. For routing we use ef_search = 60, whereas for in-shard search we use ef_search = 120. These parameters are reduced compared to billion-scale, to cut down on running time for routing. For GloVe we use distance-based routing. For SIFT1M we use the distance-frequency-hybrid routing.

4.4 Routing: kRt vs hRt In Figure 3 we compare hRt versus kRt by recall versus number of shards probed *𝜂*, using GP as the partitioning method and exhaustive search in the shards. The plot also shows the performance of the aforementioned routing oracle, to provide context. While hRt performs decently, empirically kRt consistently finds better
routes. This is caused by the uniform random sampling which is necessary for the theoretical analysis of hRt. In the remainder of the evaluation we therefore focus on kRt exclusively. We note that the relevance of hRt lies in its theoretical guarantees.

4.5 Large-Scale Throughput Evaluation Next, we perform an evaluation of recall vs throughput (queries per second) of approximate nearest neighbor queries. Due to re- source constraints we simulate distributed execution on a single machine, processing hosts one after another. While this setup does not take into account networking latency, we note that its impact should be negligible: communication latency in modern networks is less than 1 microsecond [39] whereas HNSW search latency is around 1 millisecond or higher. Moreover, our algorithms (and all sharding-based algorithms in general) perform the least amount of communication possible – only the query vector as well as neighbor IDs and distances are transmitted. We assume a distributed architecture where each machine hosts one shard and the routing index. For routing, queries are distributed evenly. The machine that receives a query forwards it to the hosts that are supposed to be probed. We use HNSW to search in the shards and to accelerate routing on the kRt points. In total we use 60 hosts. This is larger than the number of shards, as we replicate popular shards to counteract query load imbalance. We process queries on 32 cores per host in parallel. Throughput is calculated based on the maximum runtime of any host. The work for query routing is distributed evenly across hosts, whereas in-shard searches are only accounted for the queries that probe the shard on the host. Replicas of the same shard evenly distribute work amongst themselves. To obtain different throughput-recall trade-offs, we try different configurations of kRt (index size*𝑚*) and HNSW (ef_search), as well as number of shards probed *𝜂* or the probe filter methods proposed by [13] and [8], which determine a different *𝜂* for each query. For Pyramid, we also included its native routing method. Additionally, for *𝑘*-means based partitioning methods (KM, OKM, BKM, OBKM), we included their native routing method with one center per shard. We only plot the Pareto-optimal configurations, which is standard practice [16]. We note that in order to obtain the most competitive configurations, it is necessary to tune all of the above parameters.

<!-- Page 9 -->

DEEP Turing Text-to-Image 6 10

|106||||||105||
|---|---|---|---|---|---|---|---|
||||105|||||
|105|||104|||104||
||0.75 0.80|0.85 0.90 recall|1.00|0.75 0.80|0.85 0.90 recall|1.00 0.75|0.80 0.85 recall|

Throughput [QPS]

0*.*95
SpaceV 6 10 106

Throughput [QPS] 105 104

||0.75 0.80|0.85 0.90|0.95 1.00|0.75 0.80|0.85 0.90|
|---|---|---|---|---|---|
|||recall|||recall|

Figure 4: Throughput vs recall evaluation on big-ann-benchmarks for *𝑘* = 10 nearest neighbors, comparing both overlapping

and disjoint partitions. Using kRt with HNSW acceleration for routing and HNSW search in the shards. For Pyramid and -means based partitionings we additionally use their native routing methods. *𝑘*

Table 4: Partitioning times in minutes. GB = graph-building.

Table 3: Throughput in 10

queries per second at 0*.*9 recall For graph partitioning we report the time for KaMinPar as for*𝑘* = 10 nearest neighbors. Best results highlighted in bold. GP and the time for GB + GP in brackets

|𝑘 = 10|||
|---|---|---|
|QPS [·103] GP OGP BKM OBKM Pyramid||GB GP (GB+GP) OGP BKM OBKM Pyramid|
|DEEP 1157.7 1299.3 1030.2 1056.8 810.9 Turing 353.3 450.4 177.5 99.2 261.5 TTI 81.6 76.4 70.6 59.6 59.4 SpaceV 872.3 893.7 608.2 292.9 689.1 SIFT1B 793.8 895.8 376.4 459.9 608.7||DEEP 63 24 (87) +10 24 +16 32 Turing 69 17 (86) +11 36 +18 35 107 17 (124) +10 65 +34 60 TTI SpaceV 75 15 (90) +8 56 +16 34 SIFT1B 89 20 (109) +8 823 +19 41|

The Pareto optimal configurations combine high-quality routing with low-quality shard search and vice versa across the Pareto front.

Figure 4 shows the recall vs throughput plot for *𝑘* = 10 nearest

neighbors. In the full version of the paper [21], we also report results for *𝑘* = 1 and *𝑘* = 100 nearest neighbors. The overall trends are the same as for *𝑘* = 10. The algorithm ranking remains the same. Throughput is higher for *𝑘* = 1 and lower for *𝑘* = 100. Additionally,

Table 3 shows the throughput for a fixed recall value of 0*.*9 for*𝑘* = 10.

GP outperforms all baselines, and does so by a large margin on Turing and TTI. Adding overlap with OGP improves results further across the whole Pareto front on DEEP, Turing, SpaceV and SIFT1B.

0*.*95 0*.*90 0*.*95
SIFT1B

GP OGP BKM OBKM Pyramid KM OKM

0*.*95 1*.*00
On Text-to-Image, OGP loses to GP on recall below 0*.*93, but OGP can achieve higher maximum recall. Note that the maximum recall is constrained by the approximate in-shard HNSW search. Overall GP or OGP improves QPS over the next best competitor at 90% recall for *𝑘* = 10 by 1.23x, 1.72x, 1.16x, 1.3x, and 1.03x respectively on DEEP, Turing, TTI, SpaceV and SIFT1B, as well as 1.27x in the geometric mean. Moreover OGP improves over GP by up to 1.27x and by 1.09x in the geometric mean.

<!-- Page 10 -->

8 DEEP Turing Text-to-Image 7 6 5 4 3 shards probed 2 1

0*.*7 0*.*8 0*.*9 1*.*0 0*.*6 0*.*7
0*.*8 0*.*9 1*.*0 0*.*7 0*.*8 0*.*9 1*.*0
recall recall recall 8 SpaceV SIFT1B 7 GP Oracle 6 GP kRt KM Oracle 5 KM kRt 4 BKM Oracle BKM kRt 3 shards probed Pyramid Oracle 2 Pyramid kRt 1

0*.*7 0*.*8 0*.*9 1*.*0 0*.*6 0*.*7
0*.*8 0*.*9 1*.*0
recall recall

Figure 5: Evaluating disjoint partitioning methods with kRt and routing oracle (dashed), with exhaustive search in the shards

### for 𝑘 = 10 nearest neighbors.

8 DEEP Turing TTI 7 6 5 4 3 shards probed2 1

0*.*8 1*.*0 0*.*6 0*.*8 0*.*8 1*.*0 recall recall recall
8 SpaceV SIFT1B 7 6GP 5 OGP BKM 4OBKM 3 KM shards probed2OKM 1

0*.*8 1*.*0 0*.*6 0*.*8 1*.*0 recall recall
Figure 6: Evaluating overlapping vs disjoint partitions with

kRt routing and exhaustive shard search for *𝑘* = 10 nearest neighbors.

4.6 Training Time In Table 4 we report partitioning times. The improved query performance of GP does come at the cost of a moderately increased partitioning time compared to the baselines. This is due to the graph-building step (GB). While previous works [15, 17, 25] identi- fied graph partitioning as the bottleneck, we observed that it can be
8 DEEP Turing TTI 7 6 5 4 3 shards probed2 1

0*.*8 0*.*9 1*.*0 0*.*8 0*.*9 1*.*0 0*.*8 1*.*0 recall recall recall
8 SpaceV SIFT1B 7 6Oracle 5 No Coarsening Exact NN 4Approx NN 3 shards probed2 1

0*.*9 1*.*0 0*.*8 1*.*0 recall recall
Figure 7: Ablation study analyzing the sources of routing

losses for *𝑘* = 10 nearest neighbors. Different curves corre- spond to different variants of a routing algorithm used. We note that all but *Approx NN* are hypothetical and/or infeasi- ble in practice. Also note that we used distance-based routing on TTI to enable comparison with *Exact NN*, whereas in other experiments we use the frequency-distance hybrid on TTI.

made very fast by using the KaMinPar [23] partitioner. The overall partitioning times are quite moderate for datasets of this scale, and

<!-- Page 11 -->

Table 5: Routing time in microseconds per query for different

batch sizes *𝑏* on SIFT and GLOVE. Queries in a batch are processed in parallel.

8 7 SIFT GLOVE

6 USP 5GP BKM

||SIFT||GLOVE||
|---|---|---|---|---|
||𝑏 = 1 𝑏 = 32|𝑏 = ∞|𝑏 = 1 𝑏 = 32|𝑏 = ∞|
|kRt|95|13 2.4|92|11 2.1|
|kRt + HNSW|110|37 7.8|106|33 7.6|
|BLISS|660|150 110|730|110 110|
|USP|320|20 1.2|512|23 1.2|

4KM 3BLISS shards probed 2 1

0*.*6 0*.*8 1*.*0 0*.*6 0*.*8 1*.*0 recall recall
Figure 8: Comparison against neural network baselines.

our implementations of the baselines are competitive. The Pyramid paper reports a partitioning time of 87 minutes on a 500M sample of DEEP, demonstrating that our reimplementation is faster. Reference [9] reports 4-5 days training time for their hierarchical balanced *𝑘*-means tree SPANN on SpaceV and SIFT. The long training time of BKM on SIFT1B is caused by the need to iteratively escalate to a large penalty factor to achieve a balanced solution. While our graph-building implementation is already optimized, it can be further accelerated using GPUs [32] or more hardware- friendly implementation of bulk distance computations [24]. Coarsening with kRt takes roughly 1000s on Turing, 800s on DEEP, and 2000s on TTI. HNSW training in a shard of roughly 25M points takes 600s-1500s for Turing and DEEP, and 800s-1900s for TTI. Note that these numbers apply to all baseline partitioners, as they use the same routing algorithms and in-shard search. Training hRt takes under 20 seconds.

4.7 Analyzing Partitioning and Routing Quality To further assess the quality of the partitions and routing, we study recall independent of HNSW searches in the shards. Specifically, we look at the number of probed shards versus the recall with exhaustive search in the shards, in order to assess the quality of the routing algorithm. All recall values reported in Figures 5 to
Figure 7 and Section 4.7 assume exhaustive search. Furthermore,
 to assess the quality of the partition alone, we look at recall with a *hypothetical* routing oracle that knows the entire dataset and picks an optimal sequence of shards to probe. *Disjoint Partitions.* Figure 5 shows the results for disjoint partitions with the oracle marked as dashed lines. GP outperforms Pyramid, KM and BKM on all three datasets. In particular on Turing the margin of improvement is very significant: +25% recall over BKM and +13% over Pyramid at *𝜂* = 1 probes with kRt and +11%, respectively +20% with the oracle. *Overlapping Partitions.* Next, we investigate the effects of using 20% overlap in Figure 6. Since we use more shards instead of increasing their size (*𝑠*
′ = 48 instead of *𝑠* = 40), it is not guaranteed that overlap will lead to strictly better results. Fortunately, we see consistent and substantial improvement of all overlapping methods over their disjoint counterparts. For example, on Text-to-Image, OGP achieves 77.8% 10-recall in the first shard (85.5% routing oracle) compared to GP with just 74.4% (81.2% oracle). The improvements are most pronounced when the disjoint partitioning method is still

performing poorly, as there is more headroom. For example on DEEP, the 64% recall for KM gets boosted to 74.4% with OKM. Overall these results demonstrate that overlapping shards significantly boost recall. We note that while this was known for KM [8, 45], we are the first to show this for GP.

*Losses.* While our routing algorithms perform well, there is still a significant gap to the optimal routing oracle. We leverage fast inexact components in two places. We coarsen the pointset, and we use an approximate search index to speed up the routing ANN query. In the following, we thus investigate how much the inexact components contribute to the losses by replacing them with an exact version. We present the results in Figure 7. We test distance-based routing based on the entire pointset to determine the effect of coarsening and study how distance-based routing loses compared to the oracle (No Coarsening). Additionally, we test computing distances to all points in *𝑅* (Exact NN), to detect how much approximate search loses in routing by missing relevant points as employed in the base version of the algorithm (Approx NN). Note that for all five datasets including Text-to-Image, we use pure distance-based routing, not the frequency-distance-hybrid. At *𝜂* = 1 probed shard, routing by ranking distances already loses significantly – see Oracle against No Coarsening. It is able to catch up at *𝜂* = 3, suggesting that the distinction among the top 3 should be improved, but overall ranking distances is the correct approach. At *𝜂 >* 1 coarsening incurs the highest losses – see No Coarsening vs Exact NN. Fortunately, Approx NN incurs only a small loss against Exact NN; at *𝜂* ≥ 3 on Turing and *𝜂* ≥ 5 on TTI.

4.8 Small-Scale Evaluation for Learned Routing In this section, we compare with two additional baselines BLISS [25] and USP [17], which are examples of neural routing methods. Training BLISS and USP is too slow to run on the big-ann datasets, so we test on SIFT1M and GLOVE, which have roughly a million points, partitioned into *𝑠* = 16 shards. Queries are run sequentially, except for the batch-parallel results in Table 5 which use 64 cores. Index training uses 128 cores. The results for GP and (B)KM use kRt routing without HNSW acceleration.
Figure 8 shows the recall versus number of probed shards. First,
 we observe that GP and USP perform similarly and are the frontrun- ners on both datasetes, whereas BLISS is completely outperformed. This comparison uses exhaustive search in the shards, which takes
1.98ms per shard probe on SIFT. Alternatively, using HNSW takes
0.23ms per shard probe with near-equivalent recall (77% vs 76.7% in the first shard). BLISS exhibits 13-15ms for exhaustive shard

<!-- Page 12 -->

search, which is presumably an implementation issue of interfacing from Python to C++. For USP we cannot get a clean measurement, because the shard-search is mixed with the recall calculation. In

Table 5 we report the routing times. USP and BLISS benefit from

routing in batches, as PyTorch leverages parallelism and batched linear algebra operations. Our approach similarly benefits from parallelism, but could be further improved with batched distance computations. In contrast to the results on large-scale data, we observe that routing with HNSW is slower than with kRt. In terms of retrieval performance, USP is a good data structure. However, it is extremely slow to train, taking 6 hours on 128 cores for GLOVE. The main bottleneck is training the routing model on the base points over several epochs. On the other hand BLISS is fast to train at 70s (neural network) + 566s (*𝑘*-NN graph construction), but has poor retrieval performance. We excluded Neural LSH [15] from the retrieval comparison as it is already outperformed by USP, but remark that its neural net training took over two days. Our method has the best retrieval speed at relevant batch sizes *and* is also the fastest to train at just 4.76s, of which 2.95s are graph-building, 0.88s partitioning, and 0.93s kRt.

5 RELATED WORK *Approximate 𝑘-Nearest Neighbor Graph Construction.* There are several works on approximate *𝑘*-NN graph construction, which are relevant. *NN-Descent* by Dong et al [14] is an iterative improvement algorithm, which refines an initial *𝑘*-NN graph. This can be a random initial graph or computed with some other method. The intuition underlying their iterative improvement approach is to introduce neighbors of neighbors to one another, as the neighbors of a node’s current neighbors are likely good neighbor candidates themselves. One round of NN-Descent is typically implemented by computing the transpose of the current graph and merging the resulting lists. EFANNA [19] feeds the pointset as queries to *𝑘*-d trees to build an initial *𝑘*-NN graph to be refined with NN-Descent. Tang et al [46] utilize random projection trees instead of *𝑘*-d trees to build the initial graph. Another approach is based on locality sensitive hashing and using all-pairs comparisons to emit edges in the resulting hash buckets [47]. Our graph construction method is an instatiation of this approach with a data-dependent LSH function (the cluster assignment). It is also related to HCNNG [37], a graph-structured ANNS index, which uses two random pivots for splitting the data and computes a degree-constrained MST as the recursive base case to build a navigable search graph.

### Graph-Structured Indices for Approximate Nearest Neighbor Search.

Due to the curse of dimensionality, and the resulting sub-par performance of data structures like*𝑘*-d trees on high-dimensional data, recent advances in ANNS have focused on graph-structured indices. DiskANN [44] and HNSW (Hierarchical Navigable Small-World Graph) [35] have become widely established due to their excellent recall-time trade-off. The general idea is to build a navigable proximity graph on the dataset points, such that a beam search converges to nearest neighbors of a query. In each step, the explored vertex closest to the query is expanded by scanning its neighbors in the search graph and computing their distance to the query. Explored

vertices are stored in a size-constrained priority queue (the beam) to restrict the search direction towards near neighbors.

*Distributed Nearest Neighbor Search.* Despite the need for distributed ANNS in scaling beyond a billion points, there is limited published work on the subject [8, 13, 36]. We believe this is partially due to the difficulty of query load imbalance, as also briefly noted in SPANN [8]. Their approach is to load-balance queries during the partitioning step by incorporating query access frequencies. However, if the online query distribution differs or the training set is not sufficiently representative, this can still incur load imbalance. We argue that in a large-scale setup, where replica machines are needed for fault-tolerance and increased throughput, it is worth- while to selectively replicate heavily loaded machines, which can also be made robust to distribution shift by tracking query access frequencies during the online stage. FLANN [36] is a distributed search solution, which uses random sharding, routes a query to all machines, and uses a *𝑘*-means tree as the in-memory index for each shard. Note that the QPS increase from adding replicas is much more limited for random sharding, because a query is always routed to all shards, as opposed to locality-optimized partitions (such as GP or BKM) where only a small subset of the shards is probed.

*Graph Partitioning.* The KaMinPar [23] graph partitioner follows

the multilevel approach [27], where we first repeatedly contract vertex clusters to obtain successively smaller graphs, which constitute the levels. On the smallest graph, we compute an initial partition using a portfolio of randomized greedy heuristics [22]. In the un- coarsening stage we then revert the contractions level by level, project the partition to the next graph and iteratively improve it using refinement algorithms. KaMinPar uses size-constrained label propagation [40] for coarsening and refinement, a simple iterative and highly parallel greedy vertex moving procedure.

6 CONCLUSION We presented fast, modular and high-quality routing methods which unleash balanced graph partitioning for large-scale nearest neighbor search and establish it as the partitioning method of choice. Our routing methods build upong center-based routing, and achieve compatibility with graph partitioning by training representatives independently for each shard. Additionally, employing multiple and diverse representative vectors to encapsulate the hierarchical substructure within shards substantially improves routing decisions and thus recall. Moreover, our overlapping partitioning method, based on further eliminating *𝑘*-NN graph cut edges, substantially improves the recall-throughput trade-off compared to the disjoint partition. Our benchmarks on billion-scale, high-dimensional data show that our methods achieve up to 1.72x higher throughput at 90% recall than the best baseline, and 1.27x in the geometric mean. As a future work it would be interesting to explore accuracy and efficiency improvements for routing, for example exploring quantization to compress the routing index. Another direction is to study the benefits of our approach for partitioning ANNS problems across different types of computing units, e.g. GPUs. Additionally, we are interested in advanced partitioning cost functions that further optimize locality in approximate nearest neighbor search.

<!-- Page 13 -->

REFERENCES [1] Alexandr Andoni, Piotr Indyk, and Ilya Razenshteyn. 2018. Approximate nearest neighbor search in high dimensions. In *Proceedings of the International Congress* *of Mathematicians: Rio de Janeiro 2018*. World Scientific, 3287–3318. [2] Alexandr Andoni, Thijs Laarhoven, Ilya Razenshteyn, and Erik Waingarten. 2017. Optimal hashing-based time-space trade-offs for approximate near neighbors. In *Proceedings of the Twenty-Eighth Annual ACM-SIAM Symposium on Discrete* *Algorithms*. SIAM, 47–66. [3] Artem Babenko and Victor Lempitsky. 2016. Efficient indexing of billion-scale datasets of deep descriptors. In *Proceedings of the IEEE Conference on Computer* *Vision and Pattern Recognition*. 2055–2063. [4] Dmitry Baranchuk, Artem Babenko, and Yury Malkov. 2018. Revisiting the Inverted Indices for Billion-Scale Approximate Nearest Neighbors. In *Computer* *Vision-ECCV 2018 (Lecture Notes in Computer Science)*, Vol. 11216. Springer, 209–224. [https://doi.org/10.1007/978-3-030-01258-8_13](https://doi.org/10.1007/978-3-030-01258-8_13) [5] Mayank Bawa, Tyson Condie, and Prasanna Ganesan. 2005. LSH Forest: Self- tuning Indexes for Similarity Search. In *Proceedings of the 14th international* *conference on World Wide Web, WWW 2005*, Allan Ellis and Tatsuya Hagino (Eds.). ACM, 651–660. [https://doi.org/10.1145/1060745.1060840](https://doi.org/10.1145/1060745.1060840) [6] Sebastian Bruch. 2024. Foundations of Vector Retrieval. *arXiv preprint* *arXiv:2401.09350* (2024). [7] Ümit V. Çatalyürek, Karen D. Devine, Marcelo Fonseca Faraj, Lars Gottesbüren, Tobias Heuer, Henning Meyerhenke, Peter Sanders, Sebastian Schlag, Christian Schulz, Daniel Seemaier, and Dorothea Wagner. 2023. More Recent Advances in (Hyper)Graph Partitioning. *Comput. Surveys* 55, 12 (2023), 253:1–253:38. [https://doi.org/10.1145/3571808](https://doi.org/10.1145/3571808) [8] Qi Chen, Bing Zhao, Haidong Wang, Mingqin Li, Chuanjie Liu, Zengzhong Li, Mao Yang, and Jingdong Wang. 2021. SPANN: Highly-efficient Billion-scale Approximate Nearest Neighborhood Search. In *Advances in Neural Information Processing Systems 34: NeurIPS 2021*, Marc’Aurelio Ranzato, Alina Beygelzimer, Yann N. Dauphin, Percy Liang, and Jennifer Wortman Vaughan (Eds.). 5199–5212. [https://proceedings.neurips.cc/paper/2021/hash/](https://proceedings.neurips.cc/paper/2021/hash/) 299dc35e747eb77177d9cea10a802da2-\Abstract.html [9] Qi Chen, Bing Zhao, Haidong Wang, Mingqin Li, Chuanjie Liu, Zengzhong Li, Mao Yang, and Jingdong Wang. 2021. SPANN Paper Discussion. https: //openreview.net/forum?id=-1rrzmJCp4&noteId=zhMe9y8w25b. Accessed: 2023- 04-14. [10] Xi Chen, Rajesh Jayaram, Amit Levi, and Erik Waingarten. 2022. New streaming algorithms for high dimensional EMD and MST. In *Proceedings of the 54th Annual* *ACM SIGACT Symposium on Theory of Computing*. 222–233. [11] Rieke de Maeyer, Sami Sieranoja, and Pasi Fränti. 2023. Balanced k-means Revisited. *Applied Computing and Intelligence* 3, 2 (2023), 145–179. [12] Shiyuan Deng, Xiao Yan, Kelvin Kai Wing Ng, Chenyu Jiang, and James Cheng.

2019. Pyramid: A General Framework for Distributed Similarity Search. *CoRR* abs/1906.10602 (2019). arXiv:1906.10602 [http://arxiv.org/abs/1906.10602](http://arxiv.org/abs/1906.10602)
[13] Shiyuan Deng, Xiao Yan, Kelvin Kai Wing Ng, Chenyu Jiang, and James Cheng.

2019. Pyramid: A General Framework for Distributed Similarity Search on Large-scale Datasets. In *2019 IEEE International Conference on Big Data (IEEE BigData)*, Chaitanya K. Baru, Jun Huan, Latifur Khan, Xiaohua Hu, Ronay Ak, Yuanyuan Tian, Roger S. Barga, Carlo Zaniolo, Kisung Lee, and Yanfang (Fanny) Ye (Eds.). IEEE. [https://doi.org/10.1109/BigData47090.2019.9006219](https://doi.org/10.1109/BigData47090.2019.9006219)
[14] Wei Dong, Moses Charikar, and Kai Li. 2011. Efficient k-nearest neighbor graph construction for generic similarity measures. In *Proceedings of the 20th International Conference on World Wide Web, WWW 2011, Hyderabad, India, March 28 -* *April 1, 2011*. ACM, 577–586. [https://doi.org/10.1145/1963405.1963487](https://doi.org/10.1145/1963405.1963487) [15] Yihe Dong, Piotr Indyk, Ilya P. Razenshteyn, and Tal Wagner. 2020. Learning Space Partitions for Nearest Neighbor Search. In *8th International Conference on* *Learning Representations, ICLR 2020*. OpenReview.net. [https://openreview.net/](https://openreview.net/) forum?id=rkenmREFDr [16] Matthijs Douze, Alexandr Guzhva, Chengqi Deng, Jeff Johnson, Gergely Szilvasy, Pierre-Emmanuel Mazaré, Maria Lomeli, Lucas Hosseini, and Hervé Jégou. 2024. The Faiss library. *arXiv preprint arXiv:2401.08281* (2024). [17] Abrar Fahim, Mohammed Eunus Ali, and Muhammad Aamir Cheema. 2022. Un- supervised Space Partitioning for Nearest Neighbor Search. *CoRR* abs/2206.08091 (2022). [https://doi.org/10.48550/arXiv.2206.08091](https://doi.org/10.48550/arXiv.2206.08091) arXiv:2206.08091 [18] Charles M. Fiduccia and Robert M. Mattheyses. 1982. A linear-time heuristic for improving network partitions. In *Proceedings of the 19th Design Automation* *Conference, DAC 1982*. ACM/IEEE, 175–181. [https://doi.org/10.1145/800263](https://doi.org/10.1145/800263). 809204 [19] Cong Fu and Deng Cai. 2016. EFANNA : An Extremely Fast Approximate Nearest Neighbor Search Algorithm Based on kNN Graph. *CoRR* abs/1609.07228 (2016). arXiv:1609.07228 [http://arxiv.org/abs/1609.07228](http://arxiv.org/abs/1609.07228) [20] Cong Fu, Chao Xiang, Changxu Wang, and Deng Cai. 2019. Fast Approximate Nearest Neighbor Search With The Navigating Spreading-out Graph. *Proceedings* *of the VLDB Endowment* 12, 5 (2019), 461–474. [https://doi.org/10.14778/3303753](https://doi.org/10.14778/3303753). 3303754

[21] Lars Gottesbüren, Laxman Dhulipala, Rajesh Jayaram, and Jakub Lacki. 2024. Unleashing Graph Partitioning for Large-Scale Nearest Neighbor Search. *CoRR* abs/2403.01797 (2024). arXiv:2403.01797 [https://doi.org/10.48550/arXiv.2403](https://doi.org/10.48550/arXiv.2403). 01797 [22] Lars Gottesbüren, Tobias Heuer, Nikolai Maas, Peter Sanders, and Sebastian Schlag. 2024. Scalable High-Quality Hypergraph Partitioning. *ACM Trans.* *Algorithms* 20, 1 (2024), 9:1–9:54. [https://doi.org/10.1145/3626527](https://doi.org/10.1145/3626527) [23] Lars Gottesbüren, Tobias Heuer, Peter Sanders, Christian Schulz, and Daniel Seemaier. 2021. Deep Multilevel Graph Partitioning. In *29th Annual European* *Symposium on Algorithms, ESA 2021*. [https://doi.org/10.4230/LIPIcs.ESA.2021.48](https://doi.org/10.4230/LIPIcs.ESA.2021.48) [24] Ruiqi Guo, Philip Sun, Erik Lindgren, Quan Geng, David Simcha, Felix Chern, and Sanjiv Kumar. 2020. Accelerating Large-Scale Inference with Anisotropic Vector Quantization. In *Proceedings of the 37th International Conference on Machine* *Learning, ICML 2020 (Proceedings of Machine Learning Research)*, Vol. 119. PMLR, 3887–3896. [http://proceedings.mlr.press/v119/guo20h.html](http://proceedings.mlr.press/v119/guo20h.html) [25] Gaurav Gupta, Tharun Medini, Anshumali Shrivastava, and Alexander J. Smola.

2022. BLISS: A Billion scale Index using Iterative Re-partitioning. In *KDD ’22:* *The 28th ACM SIGKDD Conference on Knowledge Discovery and Data Mining*, Aidong Zhang and Huzefa Rangwala (Eds.). ACM, 486–495. [https://doi.org/10](https://doi.org/10). 1145/3534678.3539414
[26] Ben Harwood and Tom Drummond. 2016. FANNG: Fast approximate nearest neighbour graphs. In *Proceedings of the IEEE Conference on Computer Vision and* *Pattern Recognition*. 5713–5722. [27] Bruce Hendrickson and Robert Leland. 1993. *A Multilevel Algorithm for Partitioning Graphs*. Technical Report SAND93-1301. Sandia National Laboratories. [28] Piotr Indyk and Rajeev Motwani. 1998. Approximate nearest neighbors: towards removing the curse of dimensionality. In *Proceedings of the thirtieth annual ACM* *symposium on Theory of computing*. 604–613. [29] Masajiro Iwasaki and Daisuke Miyazaki. 2018. Optimization of Indexing Based on k-Nearest Neighbor Graph for Proximity Search in High-dimensional Data. *CoRR* abs/1810.07355 (2018). arXiv:1810.07355 [http://arxiv.org/abs/1810.07355](http://arxiv.org/abs/1810.07355) [30] Shikhar Jaiswal, Ravishankar Krishnaswamy, Ankit Garg, Harsha Vardhan Simhadri, and Sheshansh Agrawal. 2022. OOD-DiskANN: Efficient and Scalable Graph ANNS for Out-of-Distribution Queries. *CoRR* abs/2211.12850 (2022). [https://doi.org/10.48550/arXiv.2211.12850](https://doi.org/10.48550/arXiv.2211.12850) arXiv:2211.12850 [31] Hervé Jégou, Matthijs Douze, and Cordelia Schmid. 2011. Product Quantization for Nearest Neighbor Search. *IEEE Transactions on Pattern Analysis and Machine* *Intelligence* 33, 1 (2011), 117–128. [https://doi.org/10.1109/TPAMI.2010.57](https://doi.org/10.1109/TPAMI.2010.57) [32] Jeff Johnson, Matthijs Douze, and Hervé Jégou. 2021. Billion-Scale Similarity Search with GPUs. *IEEE Transactions on Big Data* 7, 3 (2021), 535–547. https: //doi.org/10.1109/TBDATA.2019.2921572 [33] Wen Li, Ying Zhang, Yifang Sun, Wei Wang, Mingjie Li, Wenjie Zhang, and Xuemin Lin. 2019. Approximate nearest neighbor search on high dimensional data—experiments, analyses, and improvement. *IEEE Transactions on Knowledge* *and Data Engineering* 32, 8 (2019), 1475–1488. [34] Qin Lv, William Josephson, Zhe Wang, Moses Charikar, and Kai Li. 2007. Multi-Probe LSH: Efficient Indexing for High-Dimensional Similarity Search. In *Proceedings of the 33rd International Conference on Very Large Data Bases (VLDB) 2007*. ACM, 950–961. [http://www.vldb.org/conf/2007/papers/research/p950-lv.pdf](http://www.vldb.org/conf/2007/papers/research/p950-lv.pdf) [35] Yury A. Malkov and Dmitry A. Yashunin. 2020. Efficient and Robust Approximate Nearest Neighbor Search Using Hierarchical Navigable Small World Graphs. *IEEE* *Transactions on Pattern Analysis and Machine Intelligence* 42, 4 (2020), 824–836. [https://doi.org/10.1109/TPAMI.2018.2889473](https://doi.org/10.1109/TPAMI.2018.2889473) [36] Marius Muja and David G. Lowe. 2014. Scalable Nearest Neighbor Algorithms for High Dimensional Data. *IEEE Transactions on Pattern Analysis and Machine Intelligence* 36, 11 (2014), 2227–2240. [https://doi.org/10.1109/TPAMI.2014.2321376](https://doi.org/10.1109/TPAMI.2014.2321376) [37] Javier Alvaro Vargas Muñoz, Marcos André Gonçalves, Zanoni Dias, and Ricardo da Silva Torres. 2019. Hierarchical Clustering-Based Graphs for Large Scale Approximate Nearest Neighbor Search. *Pattern Recognition* 96 (2019). https: //doi.org/10.1016/j.patcog.2019.106970 [38] David Nistér and Henrik Stewénius. 2006. Scalable Recognition with a Vocabulary Tree. In *2006 IEEE Computer Society Conference on Computer Vision and Pattern* *Recognition (CVPR 2006)*. IEEE Computer Society, 2161–2168. [https://doi.org/10](https://doi.org/10). 1109/CVPR.2006.264 [39] Oded Paz. 2014. InfiniBand Essentials Every HPC Expert Must Know. (2014). [https://www.hpcadvisorycouncil.com/events/2014/swiss-workshop/](https://www.hpcadvisorycouncil.com/events/2014/swiss-workshop/) presos/Day_1/1_Mellanox.pdf HPC Advisory Council Switzerland Conference. [40] Usha Nandini Raghavan, Réka Albert, and Soundar Kumara. 2007. Near Linear Time Algorithm to Detect Community Structures in Large-Scale Networks. *Physical Review E* 76, 3 (2007), 036106. [41] Stuart J. Russell and Peter Norvig. 2009. *Artificial Intelligence: a modern approach* (3 ed.). Pearson. [42] Gregory Shakhnarovich, Trevor Darrell, and Piotr Indyk. 2008. Nearest-neighbor methods in learning and vision. *IEEE Trans. Neural Networks* 19, 2 (2008), 377. [43] Harsha Vardhan Simhadri, George Williams, Martin Aumüller, Matthijs Douze, Artem Babenko, Dmitry Baranchuk, Qi Chen, Lucas Hosseini, Ravishankar Krishnaswamy, Gopal Srinivasa, Suhas Jayaram Subramanya, and Jingdong Wang.

<!-- Page 14 -->

2021. Results of the NeurIPS’21 Challenge on Billion-Scale Approximate Nearest Neighbor Search. In *NeurIPS 2021 Competitions and Demonstrations Track* *(Proceedings of Machine Learning Research)*, Douwe Kiela, Marco Ciccone, and Barbara Caputo (Eds.), Vol. 176. PMLR, 177–189. [https://proceedings.mlr.press/](https://proceedings.mlr.press/) v176/simhadri22a.html
[44] Suhas Jayaram Subramanya, Fnu Devvrit, Harsha Vardhan Simhadri, Ravishankar Krishnaswamy, and Rohan Kadekodi. 2019. DiskANN: Fast Accurate Billion-point Nearest Neighbor Search on a Single Node. In *Advances* *in Neural Information Processing Systems 32: Annual Conference on Neural* *Information Processing Systems 2019*, Hanna M. Wallach, Hugo Larochelle, Alina Beygelzimer, Florence d’Alché-Buc, Emily B. Fox, and Roman Gar- nett (Eds.). 13748–13758. [https://proceedings.neurips.cc/paper/2019/hash/](https://proceedings.neurips.cc/paper/2019/hash/) 09853c7fb1d3f8ee67a61b6bf4a7f8e6-\Abstract.html

[45] Philip Sun, David Simcha, Dave Dopson, Ruiqi Guo, and Sanjiv Kumar. 2023. SOAR: Improved Indexing for Approximate Nearest Neighbor Search. In *Thirty-* *seventh Conference on Neural Information Processing Systems*. [46] Jian Tang, Jingzhou Liu, Ming Zhang, and Qiaozhu Mei. 2016. Visualizing Large-scale and High-dimensional Data. In *Proceedings of the 25th International* *Conference on World Wide Web, WWW 2016, Montreal, Canada, April 11 - 15, 2016*. ACM, 287–297. [https://doi.org/10.1145/2872427.2883041](https://doi.org/10.1145/2872427.2883041) [47] Yan-Ming Zhang, Kaizhu Huang, Guanggang Geng, and Cheng-Lin Liu. 2013. Fast kNN Graph Construction with Locality Sensitive Hashing. In *Machine Learning* *and Knowledge Discovery in Databases-European Conference, ECML PKDD 2013,* *Prague, Czech Republic, September 23-27, 2013, Proceedings, Part II (Lecture Notes* *in Computer Science)*, Vol. 8189. Springer, 660–674. [https://doi.org/10.1007/978-](https://doi.org/10.1007/978-) 3-642-40991-2_42