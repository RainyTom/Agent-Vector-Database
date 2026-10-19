
## PDF page 1
1
ADDITIONAL MATERIAL
1.1
Query performance (Relative distance error)
In evaluating approximate nearest neighbors (ANN) algorithms, recall versus queries per second (QPS) is a common metric to assess the
trade-off between accuracy and efficiency. However, recall alone does not fully capture the quality of retrieved neighbors, as it only measures
set overlap without considering the relative distances to the query. To address this limitation, we incorporate relative distance error (RDE) as
an additional metric. We define RDE as follows:
RDE = 1
𝑁
𝑁
∑︁
𝑞=1
 ANN_avg(𝑞)
GT_avg(𝑞)
−1

,
where ANN_avg(𝑞) and GT_avg(𝑞) are the average distances of intersecting ANN and ground truth neighbors for query 𝑞, and 𝑁is the
number of queries. RDE measures how closely ANN distances approximate the true distances.
We evaluate the performance of our method, RWalks, by tracking both recall and distance metrics. These results are compared against
Acorn, the most competitive baseline.
0.2
0.4
0.6
0.8
1
0
0.01
0.02
0.03
0.04
Methods
acorn-1
acorn-g
rwalks
Recall vs rde.rde
Recall
rde.rde
(a) RDE - 5%
0.2
0.4
0.6
0.8
1
54k
54.5k
55k
55.5k
Methods
acorn-1
acorn-g
rwalks
Recall vs rde.avg_dist_ann
Recall
rde.avg_dist_ann
(b) AVG distance - 5%
0.4
0.6
0.8
1
0
0.02
0.04
0.06
Methods
acorn-1
acorn-g
rwalks
Recall vs rde.rde
Recall
rde.rde
(c) RDE - 10%
0.4
0.6
0.8
1
47k
47.5k
48k
48.5k
49k
Methods
acorn-1
acorn-g
rwalks
Recall vs rde.avg_dist_ann
Recall
rde.avg_dist_ann
(d) AVG distance - 10%
Figure 1: Relative distance error with varied specificity (Sift1M)
0.85
0.9
0.95
0
0.005
0.01
Methods
acorn-1
acorn-g
rwalks
Recall vs rde.rde
Recall
rde.rde
(a) RDE - 10%
0.85
0.9
0.95
42.5k
42.6k
42.7k
42.8k
42.9k
Methods
acorn-1
acorn-g
rwalks
Recall vs rde.avg_dist_ann
Recall
rde.avg_dist_ann
(b) AVG distance - 10%
Figure 2: Relative distance error with varied specificity (Yfcc10M)
1

## PDF page 2
0 0.2 0.4 0.6 0.8 1
100
2k
20k
Recall@10
QPS
(a) 1%
0
0.2 0.4 0.6 0.8
1
100
2k
20k
Recall@10
(b) 5%
0
0.2 0.4 0.6 0.8
1
100
2k
20k
Recall@10
(c) 10%
0 0.2 0.4 0.6 0.8 1
100
2k
20k
Recall@10
(d) 30%
Figure 3: Query performance with varied specificity (Sift10M)
0 0.2 0.4 0.6 0.8 1
500
5k
50k
Recall@10
QPS
(a) 1%
0
0.2 0.4 0.6 0.8
1
100
2k
20k
Recall@10
(b) 5%
0.4
0.6
0.8
1
500
5k
50k
Recall@10
(c) 10%
0.4
0.6
0.8
1
500
5k
50k
Recall@10
(d) 30%
Figure 4: Query performance with varied specificity (Sift1M)
0 0.2 0.4 0.6 0.8 1
500
5k
50k
Recall@10
QPS
(a) 1%
0.4
0.6
0.8
1
500
5k
50k
Recall@10
(b) 5%
0.4
0.6
0.8
1
500
5k
50k
Recall@10
(c) 10%
0.4
0.6
0.8
1
500
5k
50k
Recall@10
(d) 30%
Figure 5: Query performance with varied specificity (Deep1M)

## PDF page 3
IVF-Inline
HQANN
HNSW-Inline
STF
RWalks
Filtered-DiskANN
1M
10M 50M 100M
1k
10k
50k
Dataset Size
QPS for Recall90
(a) Low specificity (5%)
1M
10M 50M 100M
100
1k
10k
50k
Dataset Size
QPS for Recall90
(b) High specificity (30%)
Figure 6: Query answering scalability with dataset size (Deep, Recall@10 = 0.9).
5 hops
3 hops
2 hops
0.8
0.85
0.9 0.95
10k
20k
30k
Recall@10
QPS
(a) Low specificity (5%)
0.96 0.97 0.98 0.99
1
10k
30k
50k
Recall@10
QPS
(b) High specificity (30%)
Figure 7: Effect of Random walks depth on QPS-Recall trade-off.
1.2
Query performance on Sift10M
1.3
Query performance on Sift1M
1.4
Query performance on Deep1M
1.5
Impact of Dataset Size on Query Efficiency at High Recall (Deep dataset)
1.6
Impact of Random Walks Depth on the Accuracy/Efficiency Trade-offs
1.7
Impact of Random Walks number on the Accuracy/Efficiency Trade-offs
1.8
Impact of Search hyper-parameters on the Accuracy/Efficiency Trade-offs

## PDF page 4
0.6
0.8
1
10k
20k
30k
40k
50k
m values
m = 1
m = 3
m = 5
m = 10
QPS vs Recall for Different m
Recall
QPS (Queries per Second)
Figure 8: Effect of Random walks number (m) on QPS-Recall trade-off.
0.5
0.6
0.7 0.8 0.9
20k
30k
40k
50k
60k
exp_name
Pr = 0.03
Pr = 0.0
Pr = 0.01
Recall@10
QPS
(a) Pruning factor (𝑃𝑟)
0.75
0.8
0.85
0.9
15k
20k
25k
exp_name
h = 0.1
h = 0.05
h = 0.01
Recall@10
QPS
(b) Hybrid factor (ℎ)
Figure 9: Effect of search hyper-parameters on QPS-recall trade-off.
0.5
0.6 0.7 0.80.9 1
10k
30k
Recall@10
QPS
Figure 10: Increasing walks depth (D) up to 10 decrease performances.
