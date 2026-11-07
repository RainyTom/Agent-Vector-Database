# UniFilter 作者综述相关页

原始来源：https://raw.githubusercontent.com/Xiejiadong/Xiejiadong.github.io/main/files/paper/vector_survey.pdf

该综述不是 UniFilter 原文，不能据此填造原文算法或保证。

## PDF page 3

A Survey on Query Processing in Vector Databases
3
Query Processing in Vector Databases
Similarity Search
Proximity Graph
Index Design and Search Algorithms
NN-Descent [1], KGraph [32], EFANNA [39], SPTAG [23], DPG [93], HNSW [110], FANNG [59],
MRNG/NSG [41], SSG [40], Vamana/DiskANN [71], IEH [76], 𝜏-MG/𝜏-MNG [128], NSW [109],
𝛼-CG/𝛼-CNG [87], LMG/ALMG [170], FastHNSW [181], NGT [67], HCNNG [119], CSPG [177]
Reducing Indexing and Search Cost
DiskANN [71], FreshDiskANN [141], XN-Graph [191], BAMG [91], Starling [157], PageANN [80],
Gorgeous [183], PipeANN [55], VeloANN [198], CXL-ANNS [70], SONG [199], DGAI [105],
Second-tier Memory [27], GGNN [52], GANNS [187], CAGRA [123], Tagore [95], FastPG [181],
ParlayANN [111], Flash [156], RNN-Descent [122], LSH-APG [200], DST [74], P. Routing [106],
LAET [88], DARTH [20]
Index Maintenance
ADBV [165, 189], Milvus [153], SimJoin [169], NSW [109], HNSW [110], FreshDiskANN [141], IP-DiskANN [173],
Wolverine++ [100], PRO-HNSW [75]
Parameter Tuning
Grid Search [98], Random Search [13], VDTuner [182], PGTuner [34], FastPGT [204]
Quantization
Scalar Quantization
SQ4/SQ8 [33], LVQ [2], RaBitQ [47], RaBitQ+ [45], SAQ [89]
Product Quantization
PQ [72], FastScan [4], OPQ [48], LOPQ [78], ScaNN [56], JPQ [190], UNQ [117], LSQ [112], AQ [8], TQ [10]
Combinations with Indexes
IVFPQ [72], IMI [9], IVF-HNSW [11], DiskANN [71], SLB [102], SVS [2], SymphonyQG [51], IVF-RaBitQ [138]
Distance Computation
Approximate Computation
Product Quantization [56, 72], Quicker ADC [3, 4], Hashing-based Methods [154]
Pruning-Based Computation
Lower-bound Pruning [102], ADSampling [46], DADE [29], BSA [179], Tribase [174], TRIM [143]
Hard and Out-of-Distribution Queries
Query Hardness
LID [6], QE [6], LRC [6], Steiner-hardness [162], Escape Hardness [63]
Query Processing
RobustVamana [69], RoarGraph [22], NGFix [63]
Secure Search
Privacy-Preserving
AES/DES [142], HE [54, 201], AME [202], ASPE [166, 197, 206], DCPE [42], DCE [104], PACMANN [203]
Federated Secure
FedKNN [196], FedSQ [207], FedVSE [36], FedVS [37]
Filtered Similarity Search
Universal Index Approaches
Milvus [153], ADBV [165, 189], VBASE [195], ACORN [127], NaviX [136], NHQ [155], UniFilter [171], SIEVE [94]
Dedicated Index Approaches
Categorical Filter
Filtered-DiskANN [50], UNG [19], ELI [180], VecFlow [167]
Numerical Filter
Range
SeRF [208], iRangeGraph [175], DIGRA [73], WoW [164], RangePQ [192], UNIFY [97],
SuperPostfiltering [35], Dynamic Range-Filtering ANNS [130]
Interval
Hi-PNG [178]
Timestamp
TSANN [159]
Multi-Vector Similarity Search
Multi-Dense One-to-One
Milvus [153], VBASE [195], DEG [184], FCVI [60]
Multi-Dense One-to-Many
MUVERA [31], PLAID [134], ColBERTv2 [135], EMVB [120], IGP [15], GEM [148]
One-Modality Multi-Vector
RadiusSearch/RadiusSearch+ [168]
Dense-Sparse Combined
AdaptiveSearch [194], Allan-Poe [96], OneSparse [26]
Similarity Join
Approximate Similarity Join
Selection-Based
Hu et al. [62], Yu et al. [186], C2Net [90], Yuan et al. [188], MRS-join [132], HSJ [133], VBASE [195],
Aumüller and Ceccarello [7], XJoin [160]
Reuse-Aware
SimJoin [169], ES+MI+Adapt [83]
Exact Similarity Join
Brinkhoff et al. [18], Jacox and Samet [68], Ukey et al. [150–152], EGO [16], Super-EGO [79], FGF-Hilbert [131], DiskJoin [25]
Fig. 1. A taxonomy of approaches for query processing in vector databases
The remainder of this survey is organized as follows. Section 2 introduces preliminaries and formalizes the four
query types considered in this paper. Sections 3-6 review techniques for similarity search, filtered similarity search,
multi-vector similarity search, and similarity join, respectively. Section 7 discusses future directions and open challenges,
and Section 8 concludes the survey.
2
Preliminaries
In this paper, we focus on four query types that are widely studied in vector databases: similarity search, filtered
similarity search, multi-vector similarity search, and similarity join. In this section, we first formalize these queries and
present their definitions in the context of vector databases.
Manuscript submitted to ACM


## PDF page 18

18
Jiadong Xie, Yingfan Liu, and Jeffrey Xu Yu
Table 4. Summary of filtered similarity search approaches
Type
Method
Graph
Graph index type
Quantization
Platform
General
Milvus [153]
✓
Unrestricted
✓
CPU/GPU
ADBV [165, 189]
✓
Unrestricted
✓
CPU
VBASE [195]
✓
Unrestricted
✓
CPU
ACORN [127]
✓
HNSW
✗
CPU
NHQ [155]
✓
Unrestricted
✗
CPU
NaviX [136]
✓
HNSW
✓
CPU/Disk
JAG [172]
✓
Unrestricted
✗
CPU
UniFilter [171]
✓
Unrestricted
✗
CPU
SIEVE [94]
✓
Unrestricted
✗
CPU
Categorical
FilteredVamana/StitchedVamana [50]
✓
Vamana
✗
CPU/Disk
UNG [19]
✓
Unrestricted
✗
CPU
ELI [180]
✓
HNSW
✗
CPU
VecFlow [167]
✓
Unrestricted
✗
GPU
Numerical
SeRF [208]
✓
HNSW
✗
CPU
HSIG [97]
✓
HNSW
✗
CPU
SuperPostfiltering [35]
✓
Unrestricted
✗
CPU
iRangeGraph [175]
✓
HNSW
✗
CPU
DIGRA [73]
✓
HNSW
✗
CPU
WoW [164]
✓
Unrestricted
✗
CPU
RangePQ [192]
✗
N/A
✓
CPU
Dynamic Range-Filtering ANNS [130]
✓
Unrestricted
✗
CPU
Hi-PNG [178]
✓
Unrestricted
✗
CPU
TS-Graph [159]
✓
Unrestricted
✗
CPU
49]. For example, PACMANN [203] lets the client privately retrieve local subgraphs and compute distances locally,
avoiding expensive server-side cryptographic distance computation.
Federated Secure Similarity Search: Federated approaches assume that data are distributed across multiple parties
and cannot be centralized. (i) FedKNN [196] supports secure federated 𝑘-NN search by letting each provider compute
local results and using a secure central aggregator based on trusted execution environments and oblivious primitives. It
further proposes DANN for adaptive workload allocation, and its secure variant DANN∗provides (𝜖,𝛿)-differential
obliviousness. (ii) FedSQ [207] improves the efficiency-accuracy trade-off by selectively probing only a subset of
participants using a cost-aware framework, together with sampling and pruning for approximate global top-𝑘search.
(iii) FedVSE [36] supports both 𝑘-NN and hybrid queries by combining trusted execution environments with vector and
learned indexes. It uses a two-stage pipeline in which local candidates are first generated at each party and then securely
aggregated within enclaves. (iv) FedVS [37] further studies federated vector search with attribute filters. It adopts a
TEE-based two-phase framework for filter-aware local refinement and secure global top-𝑘aggregation, together with
cost-aware scheduling and adaptive allocation to reduce load imbalance and unnecessary computation.
4
Filtered Similarity Search
Filtered similarity search (also called filtered vector search) augments ANN search with constraints over structured
attributes. Each object 𝑜𝑖∈𝑂is represented as (𝑣𝑖,𝑎𝑖), where 𝑣𝑖∈R𝑑is the embedding vector and 𝑎𝑖denotes its
attribute(s). A filtered query is specified as 𝑞= (𝑣𝑞, 𝑝𝑞), where 𝑣𝑞∈R𝑑is the query vector and 𝑐𝑞associating with 𝑝𝑞is
an attribute constraint (e.g., a label set, a numeric range, or an interval/timestamp condition), which induces a predicate
𝑝𝑞(𝑎𝑖). Let 𝑂𝑐𝑞= {𝑜𝑖= (𝑣𝑖,𝑎𝑖) ∈𝑂| 𝑝𝑞(𝑎𝑖) is true} be the valid subset. Filtered 𝑘-ANN then returns a size-𝑘set from
{𝑣𝑖| 𝑜𝑖∈𝑂𝑐𝑞} that is approximately closest to 𝑣𝑞. In this section, we denote 𝑘-ANN search without predicates as 𝑐𝑞= ∅.
Taxonomy. Existing approaches for filtered similarity search can be broadly categorized into universal and dedicated
index approaches. Universal approaches aim to support arbitrary attributes and predicates using a single (or lightly
modified) vector index, whereas dedicated approaches build specialized index structures tailored to a specific attribute
type and predicate family. Table 4 summarizes these approaches from several perspectives, including whether they
Manuscript submitted to ACM


## PDF page 20

20
Jiadong Xie, Yingfan Liu, and Jeffrey Xu Yu
in search, reducing wasted expansions on infeasible neighbors. (b) JAG [172] improves robustness across different
selectivities and filter types by building a graph that jointly captures vector proximity and attribute proximity. It
introduces attribute and filter distances, turning a binary constraint into a continuous navigational signal. By optimizing
the graph with respect to both vector similarity and attribute proximity, JAG reduces navigational dead-ends under
filtering and aims to perform consistently across diverse predicates, such as label, range, subset, and Boolean filters.
(3) Index Introduction: UniFilter [171] differs from prior universal approaches by introducing an auxiliary predicate-
aware AND/OR graph and integrating it with the PG built for 𝑐𝑞= ∅into a unified index. It constructs an AND graph
for categorical attributes and an OR graph for numerical attributes, where each predicate node is associated with the
objects satisfying the corresponding predicate. Given a query predicate 𝑝𝑞, UniFilter finds a minimal coverage set
of predicate nodes covering O𝑝𝑞, and links them to vector nodes through carefully selected cross-edges to form an
enhanced PG. This enables traversal to move between predicate space and vector space, so that when invalid vector
nodes are reached, the search can jump to regions containing valid objects, reducing wasted exploration. Since the
original PG is preserved, the same index efficiently supports both unfiltered and filtered 𝑘-ANN queries.
(4) Workload-Aware Indices: SIEVE [94] builds a collection of indexes, each tailored to different predicate forms or
selectivity ranges, motivated by the observation that a single graph may fail to preserve navigability for all filtered queries.
To construct this collection under a memory budget, it proposes a three-dimensional analytical model capturing the
relationships among index size, search time, and recall. It uses this model to guide index selection and parameterization
jointly. At query time, the same model is used to dynamically choose both the index and its search parameters that
provide the fastest search for the target recall.
4.2
Dedicated Index Approaches
Dedicated approaches build specialized indices for a given predicate family and can substantially improve search
efficiency, but they often do so at the cost of higher index complexity and larger space or maintenance overhead.
Categorical Attributes: Dedicated index approaches for categorical attributes assume that 𝑎𝑖⊆𝐴𝐶, 𝑐𝑞⊆𝐴𝐶, and an
object 𝑜𝑖= (𝑣𝑖,𝑎𝑖) satisfies the predicate if 𝑐𝑞⊆𝑎𝑖. (1) Label-Aware PG Construction. Filtered-DiskANN [50] proposes
two Vamana-based indices for categorical filtering. FilteredVamana incrementally inserts vectors and biases neighbor
discovery and edge selection toward vectors with compatible labels, so that predicate-satisfying nodes remain well
connected in the resulting graph. StitchedVamana first builds a separate Vamana graph for each label filter and then
merges these graphs into a single index by stitching their edges together, followed by pruning to bound the out-degree.
At query time, both methods perform beam search while restricting traversal to nodes satisfying the query predicate. (2)
Multiple PGs with a Navigating Graph. UNG [19] builds a PG 𝐺𝑓for each distinct label set 𝑓appearing in the dataset,
where each object is maintained in the PG corresponding to its own label set. In addition, it builds a navigating graph
𝐺𝑁over these label sets, where an edge connects a label set to its minimum superset. Given a query predicate 𝑐𝑞, UNG
first identifies the minimal supersets of 𝑐𝑞in 𝐺𝑁, and then searches the corresponding PGs. In this way, it bridges label
containment and vector proximity through an explicit graph structure over categorical predicates. (3) Elastic Index
Selection. ELI [180] selectively indexes only a subset of label sets to support all queries. Its key observation is that an
index built for a label set 𝐿can also serve any query label set 𝐿𝑞with 𝐿⊆𝐿𝑞, and the extra query cost is captured
by an elastic factor: the ratio between the numbers of vectors matching 𝐿and 𝐿𝑞. Based on this, ELI formulates index
selection as a constrained optimization problem under space and query-efficiency requirements, and proposes greedy
algorithms to decide which label sets should be indexed. Since the selected indexes are built on top of an underlying ANN
structure, ELI is index-agnostic and offers a flexible space-performance trade-off. (4) GPU-Oriented Hybrid Indexing.
Manuscript submitted to ACM
