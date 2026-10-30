URL: https://arxiv.org/html/2411.09205
Content-Type: text/html
Method: native

---

---
meta-viewport: width=device-width, initial-scale=1, shrink-to-fit=no
title: "FlexFlood: Efficiently Updatable Learned Multi-dimensional Index"
---

![](/static/base/1.0.1/images/icons/smileybones-small.svg) arXiv is now an independent nonprofit! [Learn more](https://info.arxiv.org/about) ×

[License: CC BY 4.0](https://info.arxiv.org/help/license/index.html#licenses-available)

arXiv:2411.09205v2 [cs.DS] 15 Nov 2024

# FlexFlood: Efficiently Updatable Learned Multi-dimensional Index

Fuma Hidaka
Affiliation: The University of Tokyo
Email: [hidaka@hal.t.u-tokyo.ac.jp](mailto:)    Yusuke Matsui
Affiliation: The University of Tokyo
Email: [matsui@hal.t.u-tokyo.ac.jp](mailto:)

###### Abstract

A learned multi-dimensional index is a data structure that efficiently answers multi-dimensional orthogonal queries by understanding the data distribution using machine learning models. One of the existing problems is that the search performance significantly decreases when the distribution of data stored in the data structure becomes skewed due to update operations. To overcome this problem, we propose FlexFlood, a flexible variant of Flood. FlexFlood partially reconstructs the internal structure when the data distribution becomes skewed. Moreover, FlexFlood is the first learned multi-dimensional index that guarantees the time complexity of the update operation. Through experiments using both artificial and real-world data, we demonstrate that the search performance when the data distribution becomes skewed is up to 10 times faster than existing methods. We also found that partial reconstruction takes only about twice as much time as naive data updating.

## 1 Introduction

Filtering, scanning, and updating of data are fundamental operations for databases, and various data structures have been studied to perform these operations efficiently. In the real world, we often need to handle multi-dimensional data, and Kd-tree and its variants [[5](#bib.bib5), [20](#bib.bib20), [31](#bib.bib31)] are typical data structures for handling them. These data structures are widely used in real-world applications [[3](#bib.bib3), [23](#bib.bib23), [22](#bib.bib22)].

Recently, there has been active research to improve data structures by learning the distribution of data and queries with machine learning models. Such data structures are called learned index [[24](#bib.bib24)]. One of the significant challenges in learned multi-dimensional indexes [[32](#bib.bib32), [12](#bib.bib12), [25](#bib.bib25), [19](#bib.bib19), [8](#bib.bib8)] is that many do not support data update operations. Even if they do, none describe the time complexity for updating.

Therefore, we proposed a flexible variant of Flood (FlexFlood) that supports efficient data updating by adaptively modifying the internal structure of the existing learned multi-dimensional index, Flood [[32](#bib.bib32)]. We proved that the amortized time complexity of updating is O⁡(D​log⁡N)O(D\log N) under two assumptions that the data increases at an approximately constant pace and that the training results of the ML model satisfy certain conditions. Here, DD is the dimensionality of the data, and NN is the total number of data. Furthermore, experiments using multiple artificial and real-world datasets confirm that the “certain conditions” can be sufficient. The original Flood’s search speed slows down as we update data, but FlexFlood remains fast. As a result, FlexFlood is up to 10 times faster than Flood after many updates. The source code for FlexFlood is available at <https://github.com/mti-lab/FlexFlood>.

## 2 Related Work

Many classical data structures have been proposed for handling multi-dimensional data. For example, tree-based structures include Kd-tree [[5](#bib.bib5), [16](#bib.bib16)], R-tree [[20](#bib.bib20), [4](#bib.bib4)], and Oct-tree [[28](#bib.bib28)]. Grid File [[33](#bib.bib33)] is also proposed as a space-partitioning structure. Additionally, there is the Z-order curve algorithm [[31](#bib.bib31)], which reduces multi-dimensional data into one dimension using a special sorting technique.

Learned index [[24](#bib.bib24), [13](#bib.bib13), [2](#bib.bib2)] is a data structure that incorporates machine learning models into classical data structures such as B-tree [[1](#bib.bib1)], Hash Map [[10](#bib.bib10)], and Bloom Filter [[6](#bib.bib6)]. Learned indexes improve performance by taking advantage of the distribution of data and queries. Recently, learned indexes have been actively researched. For example, several learned Bloom Filters have been proposed [[29](#bib.bib29), [26](#bib.bib26), [30](#bib.bib30), [34](#bib.bib34), [35](#bib.bib35)]. They achieved a better memory/accuracy trade-off than the original Bloom Filter.

Learned indexes are vulnerable to data update operations in general. This is because if the data distribution is distorted by updating operations, the accuracy of the machine learning model will decrease, and search performance will decrease, too. To address this problem, learned indexes that support efficient updating operations have been proposed [[11](#bib.bib11), [15](#bib.bib15), [14](#bib.bib14), [37](#bib.bib37)].

The above learned indexes can handle only one-dimensional data. On the other hand, learned indexes for multi-dimensional data have also been proposed. For example, Flood [[32](#bib.bib32)], Tsunami [[12](#bib.bib12)], Lisa [[25](#bib.bib25)], RLR-tree [[19](#bib.bib19)], Waffle [[8](#bib.bib8)], and so on [[2](#bib.bib2)]. Flood and Tsunami do not support data updating operations. Lisa, RLR-tree and Waffle support them, but there are no discussion of time complexity.

## 3 Preliminary

First, we define the problem setting. NN DD-dimensional vectors 𝐯1,…,𝐯N{\mathbf{v}_{1},\dots,\mathbf{v}_{N}} are given, where 𝐯n∈ℝD\mathbf{v}_{n}\in\mathbb{R}^{D}. We denote the dd-th dimension of 𝐯n\mathbf{v}_{n} as vn​[d]∈ℝv_{n}[d]\in\mathbb{R}. Initially, we construct a index based on the NN vectors. Then, we process QQ queries sequentially. Queries are provided in the following formats:

- •

  Search Query: 𝐥∈ℝD\mathbf{l}\in\mathbb{R}^{D} and 𝐫∈ℝD\mathbf{r}\in\mathbb{R}^{D} are provided. 𝐥\mathbf{l} and 𝐫\mathbf{r} represent the endpoints of the diagonal of the search range hyper-rectangle. We enumerate all vectors 𝐯n\mathbf{v}_{n} contained within the data structure, such that for all d∈{1,2,…,D}d\in\\{1,2,\dots,D\\}, l⁡[d]≤vn​[d]≤r⁡[d]l[d]\leq v_{n}[d]\leq r[d] holds.

- •

  Insert Query: A DD-dimensional vector 𝐯∈ℝD\mathbf{v}\in\mathbb{R}^{D} is provided. We add it if this vector is not in the data structure. We do nothing if the same vector already exists within the data structure.

- •

  Erase Query: A DD-dimensional vector 𝐯∈ℝD\mathbf{v}\in\mathbb{R}^{D} is provided. We remove it if this vector exists within the data structure. We do nothing if the vector is not in the data structure.

Next, we explain the Flood algorithm [[32](#bib.bib32)], which is the basis of the proposed method. Flood divides the DD-dimensional space into approximately equal parts by (D−1)(D-1)-dimensional grid cells. Given DD-dimensional input vectors, Flood assigns them to the corresponding cell. Within each cell, Flood keeps vectors sorted using the value of the DD-th dimension that is not used for the grid division. The parameters of Flood are the sort dimension and the number of cell partitions. Flood optimizes these parameters using a gradient descent method with a random forest regression model [[7](#bib.bib7), [36](#bib.bib36)].

Flood uses sorted arrays to hold data in cells, but updating data on a sorted array is very expensive. We can make Flood updatable by replacing sorted arrays with B-trees. (Appendix [A](#A1 "Appendix A Sorted Array vs B-tree ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index") discussed this overhead.) However, if the data distribution changes, the grid partitioning at the initialization becomes meaningless and the updatable Flood’s search speed slows down greatly. Therefore, we propose an efficient data updating algorithm to solve this problem, using the updatable Flood as a baseline.

## 4 Proposed Method: FlexFlood

Based on the updatable Flood, we propose FlexFlood, a data updating algorithm that ensures fast search even if data distribution changes. Figure [1](#S4.F1 "Figure 1 ‣ 4 Proposed Method: FlexFlood ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index") is an overview of FlexFlood. Remember Flood constructs cells so that for each axis dd, the total number of data in the cell with the same dd-dimensional value is Nxd\frac{N}{x_{d}} (an equal number of data for each cell). Here, xdx_{d} is the number of cell partitions on axis dd. When the number of vectors in each cell deviates far from Nxd\frac{N}{x_{d}} due to data updating, the search of the updatable Flood slows down greatly. Therefore, FlexFlood re-partitions around the cells as follows. (Appendix [B](#A2 "Appendix B Threshold for Re-partitioning ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index") discussed thresholds for re-partitioning in more detail.)

*Figure 1: Overview of our method (D=2D=2): Even if N=27N=27 data are equally divided into x1=3x_{1}=3 cells at the initialization, the distribution can become skewed due to data updating, and the updatable Flood slows down. We aim to ensure the search performance by partially re-partitioning the cells.*

- •

  If the cell contains more than 2​Nxd\frac{2N}{x_{d}} vectors, we split the cell into two.

- •

  If the cell contains less than N3​xd\frac{N}{3x_{d}} vectors and the neighboring cell contains less than 7​N6​xd\frac{7N}{6x_{d}} vectors, we merge the two cells.

- •

  If the cell contains less than N3​xd\frac{N}{3x_{d}} vectors and the neighboring cell contains more than 7​N6​xd\frac{7N}{6x_{d}} vectors, we equalize the number of vectors in the two cells.

Re-partitioning in this way keeps the number of vectors in the cell constant and keeps the search speed fast. However, since the above re-partition requires a large amount of movement of data within the cells, it is not obvious whether the update operation can be performed efficiently.

We therefore analyzed the time complexity of this algorithm. Based on the insight that re-partitions with very high computational costs occur only with sufficiently low frequency, we found that the computational complexity of the updating operation is not large when considered in terms of amortization. Under the two assumptions that (1) the data increases at a pace that can be regarded as constant and (2) ∏d=1Dxd​∑d=1Dxd≤D​N​log⁡N\prod_{d=1}^{D}x_{d}\sum_{d=1}^{D}x_{d}\leq DN\log N, we proved that the updating operation of FlexFlood is amortized at O⁡(D​log⁡N)O(D\log N). (See Appendix [C](#A3 "Appendix C Amortized Time Complexity Analysis ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index") for detailed analysis.)

The update operation of the updatable Flood is O⁡(log⁡N)O(\log N), which is DD times faster than ours. Therefore, we can interpret FlexFlood as an algorithm that ensures high search performance even when the data distribution is skewed, instead of sacrificing up to DD times the complexity of the update operations.

## 5 Experiment

We evaluate FlexFlood. The runtime environment is Intel Core i7-11800H, 8 cores, 2.3 GHz, 32 GB memory. The four data structures used for comparison and their implementations are shown below.

- •

  Self-Balancing Kd-tree (SB-Kdtree) [[5](#bib.bib5), [16](#bib.bib16)]: We implemented it in C++.

- •

  R-tree [[20](#bib.bib20)]: We used C++ boost::geometry::index::rtree [[17](#bib.bib17)].

- •

  Updatable Flood: We implemented Flood in C++, then we replaced Flood’s sorted arrays with B-trees published by Google [[18](#bib.bib18)].

- •

  FlexFlood: We added cell re-partitioning algorithm to the updatable Flood.

As a dataset, we used (1) normal distribution, (2) Stock Price dataset [[21](#bib.bib21)], and (3) Open Street Map dataset [[9](#bib.bib9)]. See Appendix [D](#A4 "Appendix D Dataset Details ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index") for details on data and query generation methods.

## 6 Result

Figures [2](#S6.F2 "Figure 2 ‣ 6 Result ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index") illustrate the cumulative query processing time for each data structure for each dataset. The number of cell partitions and whether the condition ∏d=1Dxd​∑d=1Dxd≤D​N​log⁡N\prod_{d=1}^{D}x_{d}\sum_{d=1}^{D}x_{d}\leq DN\log N was satisfied or not in each dataset are shown in Table [1](#S6.T1 "Table 1 ‣ 6 Result ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index").

First, looking at Table [1](#S6.T1 "Table 1 ‣ 6 Result ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index"), we can see that the assumption (2) was always satisfied within the range of the experiments conducted here. Therefore, we can assume that the amortized time complexity of the data updating operation of FlexFlood is O⁡(D​log⁡N)O(D\log N) for practical purposes.

We then turn focus on Figures [2(a)](#S6.F2.sf1 "In Figure 2 ‣ 6 Result ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index"), [2(b)](#S6.F2.sf2 "In Figure 2 ‣ 6 Result ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index"), [2(c)](#S6.F2.sf3 "In Figure 2 ‣ 6 Result ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index"), the results of the update queries. Comparing FlexFlood with the classical data structures such as SB-Kdtree and R-tree, FlexFlood processes update queries about 1.1 to 2.9 times faster. FlexFlood is better than the SB-Kdtree and R-tree for all datasets. Comparing FlexFlood with the updatable Flood, FlexFlood requires at most 2.0 times longer runtime. Remember that in theory, the cell re-partitioning algorithm takes about DD times longer runtime than the updatable Flood’s data updating operation in the worst case. In light of this, we believe that FlexFlood is not only theoretically guaranteed to be computationally feasible but also fast enough for practical use (e.g., FlexFlood could be D=5D=5 times slower than the updatable Flood for 5D Open Street Map dataset, but Figure [2(c)](#S6.F2.sf3 "In Figure 2 ‣ 6 Result ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index") shows that FlexFlood is only about 2.0 times slower in practice).

Finally, we refer to Figures [2(d)](#S6.F2.sf4 "In Figure 2 ‣ 6 Result ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index"), [2(e)](#S6.F2.sf5 "In Figure 2 ‣ 6 Result ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index"), [2(f)](#S6.F2.sf6 "In Figure 2 ‣ 6 Result ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index"), the results of the search queries. Comparing FlexFlood with the updatable Flood, FlexFlood processes search queries about 3.3 to 10 times faster, outperforming the updatable Flood on all datasets. Comparing FlexFlood with SB-Kdtree and R-tree, FlexFlood processes queries on the normal distribution dataset and the Stock Price dataset about 1.2 to 12 times faster. On the Open Street Map dataset, however, FlexFlood is slower than the R-tree although FlexFlood is faster than SB-Kdtree. Regarding this result, the benchmark paper [[27](#bib.bib27)] pointed out that the learned indexes perform poorly compared to classical data structures on the Open Street Map dataset because it lacks local structure, making them difficult to learn. Figure [2(f)](#S6.F2.sf6 "In Figure 2 ‣ 6 Result ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index") also shows that the slope of the curve near the origin for the updatable Flood is steeper than that of SB-Kdtree and R-tree. In light of this fact, we interpret this result as consistent with the results of previous studies.

| Dataset                | {xd}d=1D\\{x_{d}\\}_{d=1}^{D}      | ∏d=1Dxd​∑d=1Dxd\prod_{d=1}^{D}x_{d}\sum_{d=1}^{D}x_{d} | D​N​log⁡NDN\log N      | Assumption (2) |
| ---------------------- | ---------------------------------- | ------------------------------------------------------ | ---------------------- | -------------- |
| 3D Normal Distribution | {21,17,1}\\{21,17,1\\}             | 1.4⋅1041.4\cdot 10^{4}                                 | 5.0⋅1065.0\cdot 10^{6} | ✓              |
| 4D Stock Price         | {19,19,1,39}\\{19,19,1,39\\}       | 1.1⋅1061.1\cdot 10^{6}                                 | 1.4⋅1071.4\cdot 10^{7} | ✓              |
| 5D Open Street Map     | {17,17,13,23,1}\\{17,17,13,23,1\\} | 6.1⋅1066.1\cdot 10^{6}                                 | 1.0⋅1081.0\cdot 10^{8} | ✓              |

*Table 1: Number of cell partitions and whether the conditional expression was satisfied or not*

*(a) Normal Distribution Update*

*(b) Stock Price Update*

*(c) Open Street Map Update*

*(d) Normal Distribution Search*

*(e) Stock Price Search*

*(f) Open Street Map Search*

*Figure 2: Experimental results: The upper panel shows the update queries, and the lower panel shows the results for the search queries. (Lower is better.)*

## 7 Conclusion

By adaptively reconstructing the internal structure of Flood, we proposed FlexFlood, which supports efficient data updating. Experimental results show that FlexFlood does not reduce the search speed and has advantages over classical data structures. Furthermore, we proved that the amortized time complexity of data updating is O⁡(D​log⁡N)O(D\log N) under two experimentally valid assumptions.

On the other hand, FlexFlood loses the optimality guarantees regarding the sort dimension and the number of cell divisions after the data update. Therefore, it may be possible to ensure even faster search by periodically relearning the distribution. (Appendix [E](#A5 "Appendix E Re-initialization ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index") discusses this in more detail.)

## References

- [1] Database architects blog: The case for b-tree index structures.

    <http://databasearchitects.blogspot.com/2017/12/the-case-for-b-tree-index-structures.html>.

- [2] Abdullah Al-Mamun, Hao Wu, Qiyang He, Jianguo Wang, and Walid G. Aref.

    A survey of learned indexes for the multi-dimensional space, 2024.

- [3] Amazon AWS.

    Amazon redshift engineering’s advanced table design playbook: Compound and interleaved sort keys.

    2016.

    <https://aws.amazon.com/jp/blogs/big-data/amazon-redshift-engineerings-advanced-table-design-playbook-compound-and-interleaved-sort-keys/>.

- [4] Norbert Beckmann, Hans-Peter Kriegel, Ralf Schneider, and Bernhard Seeger.

    The r*-tree: An efficient and robust access method for points and rectangles.

    In *Proceedings of the 1990 ACM SIGMOD International Conference on Management of Data*, pages 322–331, 1990.

- [5] Jon Louis Bentley.

    Multidimensional binary search trees used for associative searching.

    *Communications of the ACM*, 18(9):509–517, 1975.

- [6] Burton H Bloom.

    Space/time trade-offs in hash coding with allowable errors.

    *Communications of the ACM*, 13(7):422–426, 1970.

- [7] Leo Breiman.

    Random forests.

    *Machine learning*, 45:5–32, 2001.

- [8] Dalsu Choi, Hyunsik Yoon, Hyubjin Lee, and Yon Dohn Chung.

    Waffle: in-memory grid index for moving objects with reinforcement learning-based configuration tuning system.

    *Proceedings of the VLDB Endowment*, 15(11):2375–2388, 2022.

- [9] Contributers.

    Open street map.

    <https://download.geofabrik.de/>.

    Viewed 10 January 2024, Licence: ODbL 1.0.

- [10] Martin Dietzfelbinger, Anna Karlin, Kurt Mehlhorn, Friedhelm Meyer Auf Der Heide, Hans Rohnert, and Robert E Tarjan.

    Dynamic perfect hashing: Upper and lower bounds.

    *SIAM Journal on Computing*, 23(4):738–761, 1994.

- [11] Jialin Ding, Umar Farooq Minhas, Jia Yu, Chi Wang, Jaeyoung Do, Yinan Li, Hantian Zhang, Badrish Chandramouli, Johannes Gehrke, Donald Kossmann, et al.

    Alex: an updatable adaptive learned index.

    In *Proceedings of the 2020 ACM SIGMOD International Conference on Management of Data*, pages 969–984, 2020a.

- [12] Jialin Ding, Vikram Nathan, Mohammad Alizadeh, and Tim Kraska.

    Tsunami: A learned multi-dimensional index for correlated data and skewed workloads.

    *Proceedings of the VLDB Endowment*, 14(2):74–86, 2020b.

- [13] Paolo Ferragina and Giorgio Vinciguerra.

    Learned data structures.

    In *Recent Trends in Learning From Data: Tutorials from the INNS Big Data and Deep Learning Conference (INNSBDDL2019)*, pages 5–41. Springer, 2020a.

- [14] Paolo Ferragina and Giorgio Vinciguerra.

    The pgm-index: a fully-dynamic compressed learned index with provable worst-case bounds.

    *Proceedings of the VLDB Endowment*, 13(8):1162–1175, 2020b.

- [15] Alex Galakatos, Michael Markovitch, Carsten Binnig, Rodrigo Fonseca, and Tim Kraska.

    Fiting-tree: A data-aware index structure.

    In *Proceedings of the 2019 ACM SIGMOD International Conference on Management of Data*, pages 1189–1206, 2019.

- [16] Igal Galperin and Ronald L Rivest.

    Scapegoat trees.

    In *Proceedings of the fourth annual ACM-SIAM Symposium on Discrete algorithms*, pages 165–174, 1993.

- [17] Barend Gehrels, Bruno Lalande, Mateusz Loskot, Adam Wulkiewicz, and Oracle and/or its affiliates.

    boost::geometry::index::rtree.

    <https://beta.boost.org/doc/libs/1_82_0/libs/geometry/doc/html/geometry/reference/spatial_indexes/boost__geometry__index__rtree.html>.

    Viewed 18 September 2024, License: Boost Software License 1.0.

- [18] Google.

    cpp-btree.

    <https://code.google.com/archive/p/cpp-btree/wikis/UsageInstructions.wiki>.

    Viewed 30 January 2024, License: Apache License 2.0.

- [19] Tu Gu, Kaiyu Feng, Gao Cong, Cheng Long, Zheng Wang, and Sheng Wang.

    The rlr-tree: A reinforcement learning based r-tree for spatial data.

    *Proceedings of the ACM on Management of Data*, 1(1):1–26, 2023.

- [20] Antonin Guttman.

    R-trees: A dynamic index structure for spatial searching.

    In *Proceedings of the 1984 ACM SIGMOD International Conference on Management of Data*, pages 47–57, 1984.

- [21] Evan Hallmark.

    Daily historical stock prices (1970 - 2018).

    <https://www.kaggle.com/datasets/ehallmar/daily-historical-stock-prices-1970-2018>.

    Viewed 10 January 2024, License: Unknown.

- [22] IBM.

    The spatial index.

    <https://www.ibm.com/docs/en/informix-servers/12.10?topic=data-spatial-index>.

- [23] Adrian Ionescu.

    Processing petabytes of data in seconds with databricks delta.

    <https://www.databricks.com/blog/2018/07/31/processing-petabytes-of-data-in-seconds-with-databricks-delta.html>.

- [24] Tim Kraska, Alex Beutel, Ed H Chi, Jeffrey Dean, and Neoklis Polyzotis.

    The case for learned index structures.

    In *Proceedings of the 2018 ACM SIGMOD International Conference on Management of Data*, pages 489–504, 2018.

- [25] Pengfei Li, Hua Lu, Qian Zheng, Long Yang, and Gang Pan.

    Lisa: A learned index structure for spatial data.

    In *Proceedings of the 2020 ACM SIGMOD international Conference on Management of Data*, pages 2119–2133, 2020.

- [26] Qiyu Liu, Libin Zheng, Yanyan Shen, and Lei Chen.

    Stable learned bloom filters for data streams.

    *Proceedings of the VLDB Endowment*, 13(12):2355–2367, 2020.

- [27] Ryan Marcus, Andreas Kipf, Alexander van Renen, Mihail Stoian, Sanchit Misra, Alfons Kemper, Thomas Neumann, and Tim Kraska.

    Benchmarking learned indexes.

    *Proceedings of the VLDB Endowment*, 14(1):1–13, 2020.

- [28] Donald Meagher.

    Octree encoding: A new technique for the representation, manipulation and display of arbitrary 3-d objects by computer.

    1980.

- [29] Michael Mitzenmacher.

    A model for learned bloom filters and optimizing by sandwiching.

    *Proceedings of the NeurIPS*, pages 462–471, 2018.

- [30] Michael Mitzenmacher.

    Partitioned learned bloom filters.

    *Proceedings of the ICLR*, 2021.

- [31] Guy M Morton.

    A computer oriented geodetic data base and a new technique in file sequencing.

    1966.

- [32] Vikram Nathan, Jialin Ding, Mohammad Alizadeh, and Tim Kraska.

    Learning multi-dimensional indexes.

    In *Proceedings of the 2020 ACM SIGMOD International Conference on Management of Data*, pages 985–1000, 2020.

- [33] Jürg Nievergelt, Hans Hinterberger, and Kenneth C Sevcik.

    The grid file: An adaptable, symmetric multikey file structure.

    *ACM Transactions on Database Systems (TODS)*, 9(1):38–71, 1984.

- [34] Atsuki Sato and Yusuke Matsui.

    Fast partitioned learned bloom filters.

    *Proceedings of the NeurIPS*, 2023.

- [35] Atsuki Sato and Yusuke Matsui.

    Fast construction of partitioned learned bloom filter with theoretical guarantees.

    *arXiv preprint arXiv:2410.13278*, 2024.

- [36] scikit-learn developers.

    Randomforestregressor.

    <https://scikit-learn.org/dev/modules/generated/sklearn.ensemble.RandomForestRegressor.html>.

    Viewed 28 October 2024, License: BSD License.

- [37] Jiacheng Wu, Yong Zhang, Shimin Chen, Jin Wang, Yu Chen, and Chunxiao Xing.

    Updatable learned index with precise positions.

    *Proceedings of the VLDB Endowment*, 14(8):1276–1288, 2021.

## Appendix A Sorted Array vs B-tree

The updatable Flood replaces the Flood’s sorted arrays with B-trees, which slows down the search. Therefore, we conducted comparative experiments on workloads where no update queries existed. The data structures used for the comparison are (1) Self-Balancing Kd-tree (SB-Kdtree), (2) R-tree, (3) Flood, and (4) updatable Flood. Note that FlexFlood is exactly the same as the updatable Flood if there are no update queries. The dataset used for the experiments are (1) normal distribution, (2) Stock Price dataset, and (3) Open Street Map dataset. We initialized each data structure, and performed 10410^{4} search queries to measure the processing time per search query.

The experimental results are in Figure [3](#A1.F3 "Figure 3 ‣ Appendix A Sorted Array vs B-tree ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index"). The search speed of the updatable Flood is about 1.5 to 2.7 times slower than that of Flood. Therefore, we should use regular Flood for workloads where it is known in advance that there will be no update queries at all. However, for many datasets, updatable Flood achieves faster search than classical data structures. Therefore, we believe that updatable Flood is worth using for workloads where update queries are likely to be present.

*(a) Normal Distribution*

*(b) Stock Price*

*(c) Open Street Map*

*Figure 3: Processing time per search query.*

## Appendix B Threshold for Re-partitioning

The thresholds for the re-partitioning algorithm introduced in Section [4](#S4 "4 Proposed Method: FlexFlood ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index") are hyperparameters. In this section, we discuss the intuitive reasons for determining the thresholds as in Section [4](#S4 "4 Proposed Method: FlexFlood ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index") and how the performance changes when the thresholds are varied.

### B.1 Threshold Selection Criteria

We explain why the threshold for “split” in Section [4](#S4 "4 Proposed Method: FlexFlood ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index") is set to 2​Nxd\frac{2N}{x_{d}} and the threshold for “merge” and “equalize” to N3​xd\frac{N}{3x_{d}}. The purpose of cell re-partitioning is to maintain the number of data in cells at a baseline value Nxd\frac{N}{x_{d}} when the number of data in a particular cell increases or decreases too much. Since “split” halves the number of data in a cell, it is efficient to perform a “split” when the number of data in a cell is 2​Nxd\frac{2N}{x_{d}}. For the same reason, it seems intuitive that “merge” and “equalize” should be performed when the number of data in a cell reaches N2​xd\frac{N}{2x_{d}}. However, the number of data in the neighboring cell is greater than N2​xd\frac{N}{2x_{d}} and less than 2​Nxd\frac{2N}{x_{d}}. Therefore, the number of data in the cell after “merge” and “equalize” is considered to be more than Nxd\frac{N}{x_{d}}. For these reasons, we selected N3​xd\frac{N}{3x_{d}} as the threshold for “merge” and “equalize”, which is slightly smaller than N2​xd\frac{N}{2x_{d}}. This is expected to bring the number of data in the cell after “merge” and “equalize” closer to Nxd\frac{N}{x_{d}}.

Also, we explain why the threshold for switching between “merge” and “equalize” is set to 7​N6​xd\frac{7N}{6x_{d}}. This is because 7​N6​xd=N3​xd+2​Nxd2\frac{7N}{6x_{d}}=\frac{\frac{N}{3x_{d}}+\frac{2N}{x_{d}}}{2}. When “merge” or “equalize” is performed, the number of data in the neighboring cell is between N3​xd\frac{N}{3x_{d}} and 2​Nxd\frac{2N}{x_{d}}. Therefore, we switch between “merge” and “equalize” at the intermediate value.

### B.2 Performance Variation with Thresholds

In order to confirm the appropriateness of the thresholds in Section [4](#S4 "4 Proposed Method: FlexFlood ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index"), we describe the results of the experiments in which FlexFlood’s performance changed when the thresholds were changed. We conducted the experiments by varying the coefficients of Nxd\frac{N}{x_{d}} on the threshold of “split” in the range {1.5,1.6,1.7,1.8,1.9,2.0,2.1,2.2,2.3,2.4,2.5}\\{1.5,1.6,1.7,1.8,1.9,2.0,2.1,2.2,2.3,2.4,2.5\\} and on the threshold of “merge” and “equalize” in the range {0.1,0.15,0.2,0.25,0.3,0.35,0.4,0.45,0.5,0.55,0.6}\\{0.1,0.15,0.2,0.25,0.3,0.35,0.4,0.45,0.5,0.55,0.6\\}. The threshold for switching between “merge” and “equalize” was set to a value halfway between the two thresholds for re-partitioning. The experimental settings are the same as in Section [5](#S5 "5 Experiment ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index").

![Refer to caption](2411.09205v2/normal_read_partitioning_test.png)

*(a) Normal Distribution Search*
![Refer to caption](2411.09205v2/normal_update_partitioning_test.png)

*(b) Normal Distribution Update*
![Refer to caption](2411.09205v2/stock_read_partitioning_test.png)

*(c) Stock Price Search*
![Refer to caption](2411.09205v2/stock_update_partitioning_test.png)

*(d) Stock Price Update*
![Refer to caption](2411.09205v2/osm_read_partitioning_test.png)

*(e) Open Street Map Search*
![Refer to caption](2411.09205v2/osm_update_partitioning_test.png)

*(f) Open Street Map Update*

*Figure 4: Heatmaps show percentage changes compared to experimental results in Section [6](#S6 "6 Result ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index"). Blue squares mean better performance.*

Figures [4](#A2.F4 "Figure 4 ‣ B.2 Performance Variation with Thresholds ‣ Appendix B Threshold for Re-partitioning ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index") shows the results of the experiments in which search time and update time were measured separately. Red squares in the heatmap indicate a long search/update time, while blue squares indicate a short time. That is, blue squares mean better performance. In more detail, we calculated the percentage change compared to the search/update time when the threshold was set as section [4](#S4 "4 Proposed Method: FlexFlood ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index"). Figures [4](#A2.F4 "Figure 4 ‣ B.2 Performance Variation with Thresholds ‣ Appendix B Threshold for Re-partitioning ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index") shows that there are few thresholds that are blue squares in the heatmaps for both search and update. Therefore, we believe that the thresholds of Section [4](#S4 "4 Proposed Method: FlexFlood ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index") are one of the best practical values of hyperparameters.

## Appendix C Amortized Time Complexity Analysis

We prove that the amortized time complexity of FlexFlood’s update operation is O⁡(D​log⁡N)O(D\log N) under two assumptions. Let XX denote the total number of cells, that is, ∏d=1Dxd=X\prod_{d=1}^{D}x_{d}=X.

### C.1 Worst Time Complexity of Vector Insertion or Erasion

First, we show that the worst time complexity of vector insertion or erasion is O⁡(log⁡N)O(\log N). Insertion or erasion of a vector 𝐯∈ℝD\mathbf{v}\in\mathbb{R}^{D} consists of two steps: (1) identifying the cell that should contain 𝐯\mathbf{v} and (2) inserting or erasing 𝐯\mathbf{v} for a B-tree within the cell. For (1), for each dimension d∈{1,2,…,D}d\in\\{1,2,\dots,D\\}, we can perform a binary search on the set of cell boundary coordinates to determine where v⁡[d]v[d] should be placed. This takes O(∑d=1Dlogxd)=O(log∏d=1Dxd)=O(logX)O(\sum_{d=1}^{D}\log x_{d})=O(\log\prod_{d=1}^{D}x_{d})=O(\log X). We can do (2) with worst O⁡(log⁡N)O(\log N) because we only insert or erase 𝐯\mathbf{v} into the B-tree that holds the data in the identified cell. The total computational cost of inserting or erasing vectors is O⁡(log⁡X+log⁡N)O(\log X+\log N). Here, X<NX<N is considered to be valid unless the learning of the distribution is very unsuccessful. This is because X≥NX\geq N means that the number of cells is larger than the total number of data, which is obviously wasteful. Table [1](#S6.T1 "Table 1 ‣ 6 Result ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index") also shows that this is valid. Therefore, the worst time complexity of vector insertion or erasion is O⁡(log⁡N)O(\log N).

*Figure 5: Overview of “split” (D=3,x1=8,x2=3,X=24,N=48D=3,x_{1}=8,x_{2}=3,X=24,N=48): The cells outlined in red are subject to “split” because they contain 13 data in total. This value is larger than the “split” condition: 2⋅Nx1=2⋅488=122\cdot\frac{N}{x_{1}}=2\cdot\frac{48}{8}=12. We insert Xx1=248=3\frac{X}{x_{1}}=\frac{24}{8}=3 new cells (B-trees), and distribute the 12 data points evenly between the old and new cells, with 6 points in each.*

### C.2 Worst Time Complexity of Cell Re-partition

Next, we discuss the worst time complexity of cell re-partition. Since cell re-partition occurs independently for each axis d∈{1,2,…,D}d\in\\{1,2,\dots,D\\}, we consider each axis dd independently and sum up later. We assume the following two conditions.

- •

  The number of data NN increases at an approximately constant rate.

- •

  ∏d=1Dxd​∑d=1Dxd≤D​N​log⁡N\prod_{d=1}^{D}x_{d}\sum_{d=1}^{D}x_{d}\leq DN\log N

We define the first assumption as increasing the number of data by Δ∈[0,1]\Delta\in[0,1] per updating query. From a micro perspective, of course, each update operation either increases or decreases the data count by one. However, from a macro perspective, we assume that after performing kk update operations, the number of data NN can be approximated as N=N0+k​ΔN=N_{0}+k\Delta using the initial number of data N0N_{0}.

We discuss the worst-case time complexity of the “split” operation. An overview of the “split” is shown in Figure [5](#A3.F5 "Figure 5 ‣ C.1 Worst Time Complexity of Vector Insertion or Erasion ‣ Appendix C Amortized Time Complexity Analysis ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index"). “Split” first inserts empty cells (B-trees) in the appropriate location, and then distributes the data equally between the old and new cells. We can implement the insertion of empty cells by simultaneously inserting new Xxd\frac{X}{x_{d}} cells while sliding (at most) XX cells that already exist. Thus, the insertion of empty cells costs O⁡(Xxd+X)=O⁡(X)O(\frac{X}{x_{d}}+X)=O(X). For the data distribution, the erase from old B-trees and insert into new B-trees are performed 2​Nxd\frac{2N}{x_{d}} times in total (Remember the definition of the “split”. Since Δ≥0\Delta\geq 0 holds, the “split” occurs when the number of data of the target cells is exactly 2​Nxd\frac{2N}{x_{d}}, and the number of B-tree operations cannot be more than 2​Nxd\frac{2N}{x_{d}}). Therefore, the time complexity of operating B-trees for data distribution is O⁡(Nxd​log⁡N)O(\frac{N}{x_{d}}\log N). Thus, the worst time complexity of the “split” is O⁡(X+Nxd​log⁡N)O(X+\frac{N}{x_{d}}\log N). Considering the “merge” and the “equalize” in the same way, the worst time complexities are O⁡(X+Nxd​log⁡N),O⁡(Nxd​log⁡N)O(X+\frac{N}{x_{d}}\log N),O(\frac{N}{x_{d}}\log N), respectively.

### C.3 Amortized Time Complexity of Cell Re-partition

However, under the assumption that N=N0+k​ΔN=N_{0}+k\Delta, we can show that a re-partition occurs at most once every O⁡(Nxd)O(\frac{N}{x_{d}}). When N=N0+k​ΔN=N_{0}+k\Delta, we insert (1+Δ)​k2\frac{(1+\Delta)k}{2} times and erase (1−Δ)​k2\frac{(1-\Delta)k}{2} times. “Splits” due to intensive insertions occur only (1+Δ)​k2⋅xd−2​ΔN\frac{(1+\Delta)k}{2}\cdot\frac{x_{d}-2\Delta}{N} times at most. This is because the “split” occurs when the number of data in the cell reaches exactly 2​Nxd\frac{2N}{x_{d}} (because Δ≥0\Delta\geq 0 holds), and once the “split” is executed, the number of data in the cell is reset to Nxd\frac{N}{x_{d}}. So, we need Nxd−2​Δ\frac{N}{x_{d}-2\Delta} consecutive intensive insertions to execute the “split” again (Check Nxd+Nxd−2​Δ=2⋅N+Nxd−2​Δ​Δxd\frac{N}{x_{d}}+\frac{N}{x_{d}-2\Delta}=2\cdot\frac{N+\frac{N}{x_{d}-2\Delta}\Delta}{x_{d}}). In the same way, we can say that the “merge” and the “equalize” by intensive erasion occur only (1−Δ)​k2⋅3​xd+Δ2​N\frac{(1-\Delta)k}{2}\cdot\frac{3x_{d}+\Delta}{2N} times at most. Moreover, even for cells that are not erased at all, the number of data in the cell may reach the threshold of “merge” or “equalize” because NN increases. About this case, we can prove that the number of computations is maximized when the “merge” or “equalize” occurs immediately when the number of data reaches the threshold. Therefore, the “merge” and the “equalize” due to increasing data can occur only xd⋅k2​NΔx_{d}\cdot\frac{k}{\frac{2N}{\Delta}} times at most. From the above, the upper bound on the number of cell re-partitions that occur after kk update operations is k⁡((5+Δ)​xd−3​Δ−5​Δ2)4​N\frac{k((5+\Delta)x_{d}-3\Delta-5\Delta^{2})}{4N}. That is, the cell re-partitioning occurs at most once every 4​N(5+Δ)​xd−3​Δ−5​Δ2=O⁡(Nxd)\frac{4N}{(5+\Delta)x_{d}-3\Delta-5\Delta^{2}}=O(\frac{N}{x_{d}}) times.

Remember the worst-case complexity of the cell re-partitioning is O⁡(X+Nxd​log⁡N)O(X+\frac{N}{x_{d}}\log N). This happens once every O⁡(Nxd)O(\frac{N}{x_{d}}) times, so the amortized time complexity of the cell re-partitioning is O⁡(XN​xd+log⁡N)O(\frac{X}{N}x_{d}+\log N). Since we have considered each axis dd independently, the amortized time complexity of the overall cell re-partitioning is O⁡(XN​∑d=1Dxd+D​log⁡N)O(\frac{X}{N}\sum_{d=1}^{D}x_{d}+D\log N) by summing them up. Here, from the second assumption ∏d=1Dxd​∑d=1Dxd≤D​N​log⁡N\prod_{d=1}^{D}x_{d}\sum_{d=1}^{D}x_{d}\leq DN\log N, XN​∑d=1Dxd<D​log⁡N\frac{X}{N}\sum_{d=1}^{D}x_{d}<D\log N holds. Therefore, it is proved that the amortized time complexity of the cell re-partitioning is O⁡(D​log⁡N)O(D\log N).

### C.4 Limitation

We proved that the amortized time complexity of the FlexFlood’s update operation is O⁡(D​log⁡N)O(D\log N) when N≃N0+k​ΔN\simeq N_{0}+k\Delta and ∏d=1Dxd​∑d=1Dxd≤D​N​log⁡N\prod_{d=1}^{D}x_{d}\sum_{d=1}^{D}x_{d}\leq DN\log N are assumed to hold. It remains to be discussed how realistic these assumptions are. In the real world, data basically tends to increase. However, for example, there may be cases where periods of rapid data growth alternate with periods of slower growth on an annual cycle. It is very important to observe how FlexFlood performs in such cases. If the performance drops significantly, we should develop a hybrid approach that balances these periods. Furthermore, although ∏d=1Dxd​∑d=1Dxd≤D​N​log⁡N\prod_{d=1}^{D}x_{d}\sum_{d=1}^{D}x_{d}\leq DN\log N always held in our experiment, it may not hold for some datasets. We should observe how much the update speed decreases due to the breakdown of this assumption. Furthermore, it would be interesting to identify common characteristics among datasets where this assumption breaks down.

## Appendix D Dataset Details

We describe the details of the three dataset used in the experiments. In addition, we also introduce the method for generating queries.

### D.1 Normal Distribution Dataset

Dataset 1 is a Normal Distribution Dataset. We tested D=3D=3 as the number of dimensions of the normal distribution. As the initial data, we independently generated 10510^{5} data. Each data was generated independently for each axis according to a normal distribution with μ=3⋅108\mu=3\cdot 10^{8}, σ=108\sigma=10^{8}. Note that μ\mu is the mean and σ\sigma is the standard deviation of the normal distribution.

The number of queries we generated is 2⋅1062\cdot 10^{6}, with an update and search queries alternating every 10410^{4} queries. The ii-th query is an update query if ⌊i104⌋≡0(mod2)\lfloor\frac{i}{10^{4}}\rfloor\equiv 0\pmod{2}, and a search query otherwise. (Note that ⌊x⌋\lfloor x\rfloor denotes the largest integer less than or equal to xx.)

When we generated search queries, we first generated a hyper-rectangle, a search region. The length of one side of the hyper-rectangle was independently set to a random value less than 3⋅1083\cdot 10^{8}. We placed that hyper-rectangle uniformly at random inside the hyper-rectangle whose diagonals are (0,0,0)(0,0,0) and (109,109,109)(10^{9},10^{9},10^{9}), and we used this as a search query.

The update query has a 50%50\\% chance of being selected as an insert query and a 50%50\\% chance of being selected as an erase query. We generated the insertion queries according to a normal distribution with μ=3⋅108+4⋅108⋅i2⋅106\mu=3\cdot 10^{8}+4\cdot 10^{8}\cdot\frac{i}{2\cdot 10^{6}}, σ=108\sigma=10^{8} independently for each axis when the query was the ii-th from the first. This simulates the gradual change of the data distribution. We generate deletion queries by randomly selecting one of the data currently in the data structure.

### D.2 Stock Price Dataset

Dataset 2 is the Stock Price Dataset [[21](#bib.bib21)] used by [[12](#bib.bib12)]. we tested D=4D=4 as the number of dimensions. Of the approximately 2⋅1072\cdot 10^{7} of data included in the dataset, we randomly chose 2⋅1062\cdot 10^{6} data and used them. Each data has four attributes: lowest price, highest price, volume, and date.

Out of 2⋅1062\cdot 10^{6} data points, we took 2⋅1052\cdot 10^{5} with the oldest dates as the initial data. We generated 7.2⋅1067.2\cdot 10^{6} queries, with the search and update query order matching Dataset 1. Hyper-rectangles were generated randomly for search queries, searching approximately 0.1%\\% of the data per query. Insert queries were generated by selecting the oldest data not yet in the structure, and erase queries by selecting the oldest data in the structure. This simulated keeping the last 2⋅1052\cdot 10^{5} data points and evaluated each data structure’s robustness to real-world data distribution shifts over time.

### D.3 Open Street Map Dataset

Dataset 3 is the Open Street Map Dataset [[9](#bib.bib9)] used by [[32](#bib.bib32)]. We tested D=5D=5 as the number of dimensions. Of the approximately 10810^{8} of data, we randomly extracted 2⋅1062\cdot 10^{6} data and used them. Each data set has five attributes: ID, version, date, latitude, and longitude.

First, we clustered the dataset into two groups of 10610^{6} each using K-means, and used one cluster as the initial data. We generated 2⋅1062\cdot 10^{6} queries, with the search and update query order matching Dataset 1. We generated hyper-rectangles randomly for search queries so that approximately 0.3%\\% of the data were searched per query. Insert queries were generated by randomly selecting data from the cluster not selected as the initial data, while erase queries were generated by randomly selecting data from the initial cluster.

## Appendix E Re-initialization

There is a delta-buffer method according to the survey paper on learned multi-dimensional indexes [[2](#bib.bib2)]. It stores data updating in a small array and periodically merges them with the data structure. We compare the performance of FlexFlood with that of a delta-buffered version of Flood.

In designing the delta-buffer Flood, it is computationally too expensive to incorporate re-learning of parameters (sort dimension and the number of cell partitions). In our implementation, learning the distribution took 32 seconds for the normal distribution dataset, 46 seconds for the Stock Price dataset, and 138 seconds for the Open Street Map dataset. Since there are about 10610^{6} update queries in our workload, we need to re-learn more than 10210^{2} times even if we re-learn every 10410^{4} times. The total re-learning time would then be about 10410^{4} seconds ≃\simeq 3 hours, which is too long as shown in Figures [2](#S6.F2 "Figure 2 ‣ 6 Result ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index"). Therefore, we adopt the method of re-initializing the data structure without changing the parameters.

In this experiment, we used Flood10000, which re-initializes the entire structure after every 10410^{4} data updates, and used Flood20000 with re-initialization after every 2⋅1042\cdot 10^{4} updates, in addition to the four data structures described in Section [5](#S5 "5 Experiment ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index"). As noted in Appendix [D](#A4 "Appendix D Dataset Details ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index"), search queries and update queries come alternately, 10410^{4} each. Thus, Flood10000 is re-initialized for each set of update queries, and Flood20000 stores the first set of update queries in the buffer and re-initializes after the second set.

*(a) Normal Distribution Update*

*(b) Stock Price Update*

*(c) Open Street Map Update*

*(d) Normal Distribution Search*

*(e) Stock Price Search*

*(f) Open Street Map Search*

*Figure 6: Experimental results: The upper panel shows the update queries, and the lower panel shows the results for the search queries. (Lower is better.)*

Figures [6](#A5.F6 "Figure 6 ‣ Appendix E Re-initialization ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index") illustrate the cumulative query processing time for each data structure for each dataset. Looking at the update query results (Figures [6(a)](#A5.F6.sf1 "In Figure 6 ‣ Appendix E Re-initialization ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index"), [6(b)](#A5.F6.sf2 "In Figure 6 ‣ Appendix E Re-initialization ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index"), [6(c)](#A5.F6.sf3 "In Figure 6 ‣ Appendix E Re-initialization ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index")), Flood10000 and Flood20000 are slower than all the compared data structures. Especially, both are more than five times slower than FlexFlood. From this result, the delta buffer Flood is undesirable when there are many update queries.

We then turn our attention to the search query results (Figures [6(d)](#A5.F6.sf4 "In Figure 6 ‣ Appendix E Re-initialization ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index"), [6(e)](#A5.F6.sf5 "In Figure 6 ‣ Appendix E Re-initialization ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index"), [6(f)](#A5.F6.sf6 "In Figure 6 ‣ Appendix E Re-initialization ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index")). Flood10000 has the fastest search speed of all data structures. This is because Flood10000 re-initializes every update query set, so Flood10000 can achieve virtually the same performance as Flood, and the original Flood is extremely fast, as discussed in Appendix [A](#A1 "Appendix A Sorted Array vs B-tree ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index"). On the other hand, Flood20000 is slower than FlexFlood for normal distribution dataset and Stock Prices dataset, but achieves a very fast search speed for Open Street Map dataset. This is likely due to the large amount of data being searched in the Open Street Map dataset (See Appendix [D](#A4 "Appendix D Dataset Details ‣ FlexFlood: Efficiently Updatable Learned Multi-dimensional Index")). The time spent searching the internal structure of the Flood has become the rate-limiting step, making the time spent reading from the buffer relatively negligible.

Therefore, delta-buffer Flood is worth considering in situations where an extremely large amount of data is being searched, or when spending a relatively long time on updates is not an issue. On the other hand, we believe that FlexFlood excels in scenarios where high update speed is required or when the amount of data being searched is relatively small.

[](javascript:toggleReadingMode(); "Disable reading mode, show header and footer")