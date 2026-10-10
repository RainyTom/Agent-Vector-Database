# Similarity Join Size Estimation using Locality Sensitive Hashing

Hongrae Lee University of British Columbia xguy@cs.ubc.ca

Raymond T. Ng University of British Columbia rng@cs.ubc.ca

Kyuseok Shim Seoul National University shim@ee.snu.ac.kr

## ABSTRACT

Similarity joins are important operations with a broad range of applications. In this paper, we study the problem of vector similarity join size estimation (VSJ). It is a generalization of the previously studied set similarity join size estimation (SSJ) problem and can handle more interesting cases such as TF-IDF vectors. One of the key challenges in similarity join size estimation is that the join size can change dramatically depending on the input similarity threshold.

We propose a sampling based algorithm that uses Locality-Sensitive-Hashing (LSH). The proposed algorithm LSH-SS uses an LSH index to enable efective sampling even at high thresholds. We compare the proposed technique with random sampling and the state-of-the-art technique for SSJ (adapted to VSJ) and demonstrate LSH-SS ofers more accurate estimates throughout the similarity threshold range and small variance using real-world data sets.

## 1. INTRODUCTION

Given a similarity measure and a minimum similarity threshold, a similarity join is to find all pairs of objects whose similarity is not smaller than the similarity threshold. The object in a similarity join is often a vector. For instance, a document can be represented by a vector of words in the document, or an image can be represented by a vector from its color histogram. In this paper, we focus on the vector representation of objects and study the following problem.

Definition 1 (The VSJ Problem). Given a collection of real-valued vectors $V ~ = ~ \{ v _ { 1 } , . . . , v _ { n } \}$ and a threshold τ on a similarity measure sim, estimate the number of pairs $J = | \{ ( u , v ) : u , v \in V , s i m ( u , v ) \geq \tau , u \neq v \} |$

Similarity joins have a broad range of applications including near duplicate document detection and elimination, query refinement for web search, coalition detection [3], and data cleaning processes [17, 2]. Accordingly, similarity joins have recently received much attention, e.g. [17, 2, 6, 3, 11]. Chaudhuri et al. identified a similarity join operation as a primitive operator in database systems [6].

To successfully incorporate similarity join operations in database systems, it is imperative that we have reliable size estimation technique for them. The query optimizer needs accurate size estimations to produce an optimized query plan. Thus, in this paper, we focus on the size estimation of vector similarity joins.

In the literature, the similarity join size estimation problem has been defined using sets as follows:

Definition 2 (The SSJ Problem). Given a collection of real-valued sets $S = \{ s _ { 1 } , . . . , s _ { n } \}$ and a threshold τ on a similarity measure sim, estimate the number of pairs $J =$ $\left| \left\{ ( r , s ) : r , s \in S , s i m ( r , s ) \geq \tau , r \neq s \right\} \right|$ |.

Note that our formulation of similarity joins with vectors is more general and can handle more practical applications. For instance, while in the SSJ problem a document is simply a set of words in the document, in the VSJ problem a document can be modeled with a vector of words with TF-IDF weights. It can also deal with multiset semantics with occurrences. In fact, most of the studies on similarity joins first formulate the problem with sets and then extend it with TF-IDF weights, which is indeed a vector similarity join.

The SSJ problem has been previously studied by Lee et al. [14]. A straightforward extension of SSJ techniques for the VSJ problem is to embed a vector into a set space. We convert a vector into a set by treating a dimension as an element and repeating the element as many times as the dimension value, using standard rounding techniques if values are not integral [2]. In practice, however, this embedding can have adverse efects on performance, accuracy or required resources. Intuitively, a set is a special case of a binary vector and is not more dificult to handle than a vector. For instance, Bayardo et al. [3] define the vector similarity join problem and add special optimizations that are possible when vectors are binary vectors (sets).

In our VSJ problem, we consider cosine similarity as the similarity measure sim since it has been successfully used across several domains [3]. Let u[i] denote the i-th dimension value of vector u. Cosine similarity is defined as $\begin{array} { r } { c o s ( u , v ) = } \end{array}$ u·v/ kuk kvk, where $\begin{array} { r } { u \cdot v = \sum _ { i } u [ i ] \cdot v [ i ] } \end{array}$ and $\begin{array} { r } { \| u \| = \sqrt { \sum _ { i } u [ i ] ^ { 2 } } . } \end{array}$ We focus on self-joins and discuss extensions to general joins in Appendix B.2.

One of the key challenges in similarity join size estimation is that the join size can change dramatically depending on the input similarity threshold. While the join size can be close to $n ^ { 2 }$ at low thresholds where n is the database size, it can be very small at high thresholds. For instance, in the DBLP data set, the join selectivity is only about 0.00001 % at $\tau = 0 . 9$ . While many sampling algorithms have been proposed for the (equi-)join size estimation, their guarantees fail in such a high selectivity range, e.g. [15, 10, 9]. Intuitively, it is not practical to apply simple random sampling when the selectivity is very high. This is problematic since similarity thresholds between 0.5 and 0.9 are typically used [3]. Note that the join size in that range may be large enough to afect query optimization due to the large cross product size. Moreover, as observed in [13], join size errors propagate. That is, even if the original errors are small, their transitive efect can be devastating.

In this paper, we propose sampling based techniques that exploit the Locality Sensitive Hashing (LSH) scheme, which has been successfully applied in similarity searches across many domains. LSH builds hash tables such that similar objects are more likely to be in the same bucket. Our key idea is that although sampling a pair satisfying a high threshold is very dificult, it is relatively easy to sample the pair using the LSH scheme because it groups similar objects together. We show that the proposed algorithm LSH-SS gives good estimates throughout the similarith threshold range with a sample size of $\Omega ( n )$ pairs of vectors (i.e. $\Omega ( { \sqrt { n } } )$ tuples from each join relation in an equi-join) with probabilistic guarantees. The proposed solution only needs minimal addition to the existing LSH index and thus is readily applicable to many similarity search applications. As a summary, we make the following contributions:

• We present two baseline methods in Section 3. We consider random sampling and adapt Lattice Counting (LC) [14] which is proposed for the SSJ problem.

• We extend the LSH index to support similarity join size estimation in Section 4. We also propose LSH-S which relies on an LSH function analysis.

• We describe a stratified sampling algorithm LSH-SS that exploits the LSH index in Section 5. We apply diferent sampling procedures for the two partitions induced by an LSH index: pairs of vectors that are in the same bucket and those that are not.

• We compare the proposed solutions with random sampling and LC using real-world data sets in Section 6. The experimental results show that LSH-SS is the most accurate with small variance.

## 2. RELATED WORK

Many algorithms have been proposed on the similarity join processing, e.g. [17, 2, 6, 3]. Their focus is not on size estimation. In fact, the existence of many join processing algorithms motivates the size estimation study for query optimization. Most of them studied set similarity joins with complex weights such as TF-IDF or vector similarity joins.

Lee et al. proposed LC for the SSJ problem [14]. We present this technique in more details in the following section and compare with the proposed solution in the experiments. Hadjieleftheriou et al. studied the problem of selectivity estimation for set similarity selection (not join) queries [11].

There have been many studies using random sampling for the (equi-)join size estimation; some examples include adaptive sampling [15], cross/index/tuple sampling [10], bifocal sampling [9], and tug-of-war [1]. Some of them can be adapted to similarity joins, but their guarantees do not hold due to diferences in sampling cost models or they require impractical sample sizes. Below, we outline two closely related sampling algorithms.

Adaptive sampling is proposed by Lipton et al [15]. Its main idea is to terminate sampling process when the query size (the accumulated answer size from the sample so far) reaches a threshold not when the number of samples reaches a threshold. While it does not produce reliable estimates in skewed data, we observe that its adaptive nature can still be useful for the VSJ problem. It is used as a subroutine in our solution in Section 5.

Bifocal sampling is proposed by Ganguly et al. to cope with the skewed data problem [9]. It tackles the problem by considering high-frequency values and low-frequency values with separate procedures. However, as will be shown shortly, it cannot guarantee good estimates at high thresholds when applied to the VSJ problem.

## 3. BASELINE METHODS

## 3.1 Random Sampling

The first baseline method is uniform random sampling. We select m pairs of vectors uniformly at random (with replacement) and count the number of pairs satisfying the similarity threshold τ . We return the count scaled up by $M / m$ where M denotes the total number of pairs of vectors in the database V. We also consider an alternative method of first sampling $\sqrt { m }$ records and computing similarities of all the pairs in the sample (cross sampling [10]).

Equi-join size estimation techniques mostly do not ofer clear benefits over the simple random sampling in the VSJ problem. We note two challenges in the VSJ problem compared to the equi-join size estimation. In the equi-join size of |R ./ S|, we can focus on frequency distribution on the join column of each relation R and S. For instance, if we know a value v appears $n _ { r } ( v )$ times in R and $n _ { s } ( v )$ times in S, the contribution of v in the join size is simply $n _ { r } ( v ) \cdot n _ { s } ( v )$ , i.e. multiplication of two frequencies. We do not need to compare all the $n _ { r } ( v ) \cdot n _ { s } ( v )$ pairs. In similarity joins, however, we need to actually compare the pairs to measure similarity. This dificulty invalidates the use of popular auxiliary structures such as indexes [10, 7] or histograms [7].

Furthermore, similarity join size at high thresholds can be much smaller than the join size assumed in equi-joins. For instance, in the DBLP data set $( n = 8 0 0 K )$ , the join size of Ω(n log n) assumed in bifocal sampling is more than 15M pairs and corresponds to the cosine similarity value of only about 0.4. In the most cases, users will be interested in much smaller join sizes and thus higher thresholds.

## 3.2 Adaptation of Lattice Counting

Lattice Counting (LC) is proposed by Lee et al. [14] to estimate SSJ size with Jaccard similarity. Jaccard similarity between two sets A, B is defined as sim $_ { J } ( A , B ) =$ $| A \cap B | / | A \cup B |$ . LC relies on succinct representation of sets using Min-Hashing [4]. A useful property of Min-Hashing is that if h is a Min-Hash function then $P ( h ( A ) = h ( B ) ) =$ $s i m _ { J } ( A , B )$ . For each set A, a signature of the set $s i g ( A )$ is constructed by concatenating k Min-Hash functions. Jaccard similarity between two sets A, B can be estimated by the number of positions of $s i g ( A )$ and $s i g ( B )$ which overlap. LC performs an analysis on the signatures of all sets. For our purposes, LC can be treated as a black box.

We observe that the analysis of LC is valid as long as the number of matching positions in the signatures of two objects is proportional to their similarity. Note that this requirement is exactly the property of the LSH scheme. Thus LC can be applied for the VSJ problem with an appropriate LSH scheme. In fact, Min-Hashing is the LSH scheme for Jaccard similarity. For the VSJ problem, we first build the signature database by applying an LSH scheme to the vector database and then apply LC.

## 4. LSH INDEX FOR THE VSJ PROBLEM

We first describe how we extend the LSH index and present a naive method with a uniformity assumption, and then present LSH-S which improves it with random sampling.

## 4.1 Preliminary: LSH Indexing

Let H be a family of hash functions such that $h \in \mathcal H$ $\mathbb { R } ^ { d } \to \mathcal { U }$ . Consider a function h that is chosen uniformly at random from H and a similarity function sim $: \mathbb { R } ^ { d } \times \mathbb { R } ^ { d } $ [0, 1]. The family H is called locality sensitive if it satisfies the following property [5].

Definition 3. [Locality Sensitive Hashing] For any vectors $u , v \in \mathbb { R } ^ { d }$ ，

$$
P (h (u) = h (v)) = s i m (u, v).
$$

That is, the more similar a pair of vectors is, the higher the collision probability is. The LSH scheme works as follows $\left[ 1 2 , 5 \right] :$ For an integer k, we define a function family $\mathcal { G } =$ $\{ g : { \dot { \mathcal { R } } } ^ { d }  { \mathcal { U } } ^ { k } \}$ such that $g ( v ) = ( h _ { 1 } ( v ) , . . . , h _ { k } ( v ) )$ , where $h _ { i } \in \mathcal { H } .$ , i.e. g is the concatenation of k LSH functions. For an integer \`, we choose \` functions $G = \{ g _ { 1 } , . . . , g _ { \ell } \}$ from G independently and uniformly at random. Each $g _ { i } , 1 \leq i \leq \ell$ efectively constructs a hash table denoted by $D _ { g _ { i } } . \mathrm { ~ A ~ }$ bucket in $D _ { g _ { i } }$ stores all $v \in V$ that have the same $g _ { i }$ values. For space, only existing buckets are stored using standard hash ing. G defines a collection of \` tables $I _ { G } = \{ D _ { g _ { 1 } } , . . . , D _ { g _ { \ell } } \}$ and we call it an LSH index. Table 3 in appendix is a sum mary of notations.

LSH families have been developed for several (dis)similarity measures including Hamming distance, $\ell _ { p }$ distance, Jaccard similarity, and cosine similarity. We rely on the LSH scheme proposed by Charikar [5] that supports cosine similarity. The proposed algorithms can easily support other similarity measures by using an appropriate LSH family.

## 4.1.1 Extending The LSHScheme with Bucket Counts

We describe algorithms based on a single LSH table: $g =$ $( h _ { 1 } , . . . , h _ { k } )$ with k hash functions and an LSH table $D _ { g } .$ . Extensions with multiple LSH tables are in Appendix B.2. Suppose that $D _ { g }$ has $n _ { g }$ buckets; all vectors in the database are hashed into one of the $n _ { g }$ buckets. We denote a bucket by $B _ { j } , 1 \le j \le n _ { g } .$ Given a vector $v , B ( v )$ denotes the bucket to which v belongs. In the LSH table, each bucket $B _ { j }$ stores the set of vectors that are hashed into $B _ { j }$ . We extend the conventional LSH table by adding a bucket count $b _ { j }$ for each bucket $B _ { j }$ that is the number of vectors in the database that are hashed into $B _ { j }$ . The overhead of adding a bucket count to each bucket is not significant compared to other information such as vectors. Depending on implementation, the count may be readily available.

![](images/317f720f48610afb1fcdbdfbad262edfb80101f5535596b64678faaab4215f01.jpg)  
Figure 1: Probability Density Functions of (non-) collision in the LSH scheme

## 4.2 Estimation with Uniformity Assumption

Given a collection of vectors V, and a similarity threshold $\tau ,$ let M denote the number of total pairs in $V , \mathrm { i . e . } M = \textstyle { \binom { n } { 2 } }$ Consider a random pair $( u , v ) , u , v , \in V , u \neq v$ . We denote the event $s i m ( u , v ) \geq \tau$ by $T$ , and the event $s i m ( u , v ) <$ τ by F. We call $( u , v )$ a true pair (resp. false pair) if sim $( u , v ) \geq \tau$ (resp. $s i m ( u , v ) < \tau )$ . Depending on whether u and v are in the same bucket, we denote the event $B ( u ) =$ $B ( v )$ by H and the event $B ( u ) \neq B ( v )$ by L. With these notations, we can define various probabilities. For instance, $P ( T )$ is the probability of sampling a true pair. $P ( H | T )$ is the probability that a true pair is in the same bucket and $P ( T | H )$ is the probability that a pair of vectors from a bucket is a true pair. We use $N _ { \mathcal { E } }$ to denote the cardinality of the set of pairs that satisfy the condition of event E. For instance, $N _ { T }$ is the number of true pairs (the VSJ size J) and $N _ { H }$ is the number of pairs in the same bucket. N<sub>H</sub> can be computed by $\begin{array} { r } { N _ { H } = \sum _ { j = 1 } ^ { \mathbf { \hat { n } } _ { g } } \binom { b _ { j } } { 2 } } \end{array}$

The key observation is that any pair of vectors from a bucket is either a true pair or a false pair. Using Bayes’ rule [16], we can express this observation as follows: $N _ { H } =$ ${ N _ { T } } \cdot \dot { P ( H | T ) } + { N _ { F } } \cdot P ( H | F )$ . That is, the total number of pairs of vectors in the same bucket is the sum of the number of true pairs that are in the same bucket $( N _ { T } \cdot P ( H | T ) )$ and the number of false pairs that happened to be in the same bucket $( N _ { F } \cdot P ( H | F ) )$ . Since $N _ { F } = M - N _ { T } ,$ rearranging the terms gives $\dot { N _ { T } } = \dot { ( N _ { H } - M \cdot P ( H | F ) ) / ( P ( H | T ) - P ( H | F ) ) }$ Using ‘ ˆ ’ for an estimated quantity, we have an estimator for the join size $J ( = N _ { T } )$ as follows:

$$
\hat {N} _ {T} = \hat {J} _ {U} = \frac {N _ {H} - M \cdot \hat {P} (H | F)}{\hat {P} (H | T) - \hat {P} (H | F)}.\tag{1}
$$

Note that M and $N _ { H }$ are constants given V and $D _ { g }$ . The conditional probabilities in Equation (1) need to be estimated to compute $\hat { N } _ { T }$ . We next present our first estimator that relies on an LSH function analysis and a uniformity assumption to estimate the conditional probabilities.

Consider a random pair $( u , v )$ such that $s i m ( u , v )$ is selected from [0, 1] uniformly at random. Recall that when sim $( u , v ) \ = \ s , \ P ( h ( u ) \ = \ h ( v ) ) \ = \ s$ from Definition 3. $P ( g ( u ) = g ( v ) ) = s ^ { k }$ since g concatenates k hash values. Figure $\mathrm { 1 ( a ) }$ shows the collision probability density function $( \mathrm { P D F } ) ~ f ( s ) = s ^ { k }$ where $s = s i m ( u , v )$ . The vertical dotted line represents the similarity threshold $\tau .$ The darker area on the right side of the τ line is the good collision probability, i.e. the probability that $\boldsymbol { B } ( \boldsymbol { u } ) = \boldsymbol { B } ( \boldsymbol { v } )$ (u and v are in the same bucket) and $s i m ( u , v ) \geq \tau$ . Thus the area represents $P ( H \cap T )$ . Likewise, the area on the left side of the τ line is $P ( H \cap F )$ , which is the probability that sim $( u , v ) < \tau ,$ but $\boldsymbol { B } ( \boldsymbol { u } ) = \boldsymbol { B } ( \boldsymbol { v } )$ . Notice that the area below $f ( s )$ does not equal to 1 since it does not cover the entire event space; u and v may not be in the same bucket. Figure 1(b) shows the other case where u and v are in diferent buckets. Its PDF is $1 - f ( s )$ as shown as the curve. $P ( L \cap T )$ and $P ( L \cap F )$ are defined similarly as shown in the figure.

Given g (and thus $f )$ and $\tau , P ( H \cap F ) , P ( H \cap T ) , P ( L \cap F )$ and $P ( L \cap T )$ can be estimated by computing the corresponding areas in Figure 1. Based on these areas, we can estimate the desired conditional probabilities using the following:

$$
P (H | T) = \frac {P (H \cap T)}{P (H \cap T) + P (L \cap T)}\tag{2}
$$

$$
P (H | F) = \frac {P (H \cap F)}{P (H \cap T) + P (L \cap F)}.\tag{3}
$$

Plugging $P ( H | T )$ and $P ( H | F )$ computed as above into Equation (1), we have the following estimator for the VSJ size:

$$
\hat {J} _ {U} = \frac {(k + 1) N _ {H} - \tau^ {k} \cdot M}{\sum_ {i = 0} ^ {k - 1} \tau^ {i}}.\tag{4}
$$

We give its derivation in Appendix A.1.

We note that ${ \hat { J } } _ { U }$ implicitly assumes that the similarity of pairs is uniformly distributed in [0, 1]. However, this distribution is generally highly skewed [14]; most of pairs have low similarity values and only a small number of pairs have high similarity values. We next present LSH-S that removes the uniformity assumption with sampling.

## 4.3 LSH-S: Removing Uniformity Assumption

We consider two methods to remove the uniformity assumption. First, we estimate the conditional probabilities by random sampling without resorting to the LSH function analysis. For instance, we can estimate $P ( H | T )$ by counting the number of pairs in the same bucket among the true pairs in the sample. Second, we weight the conditional probabilities using samples. For example, if all the pairs in the sample have a similarity value of 0.3, we can only consider similarity $s = 0 . 3$ in Figure 1 without considering the whole area. We call the second method LSH-S and present only LSH-S since it outperformed the first one in our experiments. In LSH-S, for each similarity s that appears in the sample, the $f ( s )$ value is weighted by the occurrence of s in the sample. For a sample $S ,$ we use the following weight: $w ( s ) = | \{ ( u , v ) \in S : s i m ( u , v ) = s \} | / | S |$ For instance, if similarities in $S$ are $\{ 0 . 1 , 0 . 1 , 0 . 1 , 0 . 2 , 0 . 2 \}$ 2 $w ( 0 . 1 ) = 3 / 5 , w ( 0 . 2 ) = 2 / 5$ and $w ( s ) = 0$ for s /∈ {0.1, 0.2}. Let $S _ { T }$ denote the subset of true pairs in S and S<sub>F</sub> denote $S - S _ { T }$ . With this weighting scheme, since $f ( s ) = s ^ { k }$

$$
\hat {P} (H | T) = \sum_ {(u, v) \in S _ {T}} (s i m (u, v)) ^ {k} / | S _ {T} |\tag{5}
$$

$$
\hat {P} (H | F) = \sum_ {(u, v) \in S _ {F}} (s i m (u, v)) ^ {k} / | S _ {F} |.\tag{6}
$$

LSH-S uses Equations (5) and (6) in Equation (1).

## 5. STRATIFIED SAMPLING USING LSH

A dificulty in LSH-S is that the conditional probabilities, e.g. $P ( H | T )$ , need to be estimated and it is hard to acquire reliable estimates of them. In this section, we present an algorithm that overcomes this dificulty by using the LSH index in a slightly diferent way.

An interesting view of an LSH index is that it partitions all pairs of vectors in V into two strata: pairs that are in the same bucket and pairs that are not. The pairs in the same bucket are likely to be more similar from the property of LSH (Def. 3). Recall that the dificulty of sampling at high thresholds is that the join size is very small and sampling a true pair is hard. Our key intuition is that even at high thresholds it is relatively easy to sample a true pair from the set of pairs that are in the same bucket.

We demonstrate our intuition with a real-world example. Table 1 shows actual probabilities varying τ in the DBLP data set. We observe that other than at low thresholds, say 0.1 ∼ 0.3, P(T) is close to 0, which implies that naive random sampling is not going to work well with any reasonable sample size. However, note that $P ( T | H )$ is consistently higher than 0.04 even at high thresholds. That is, it is not dificult to sample true pairs among the pairs in the same bucket. $P ( H | T )$ is large at high thresholds but very small at low thresholds. This means that at high thresholds, a suficient fraction of true pairs are in the same bucket. But at low thresholds, most of true pairs are not in the same bucket, which implies that the estimate from the pairs in the same bucket is not enough. However, at low thresholds, $P ( T | L )$ becomes larger and thus sampling from the pairs that are not in the same bucket becomes feasible.

<table><tr><td>τ</td><td>P(T)</td><td>P(T|H)</td><td>P(H|T)</td><td>P(T|L)</td></tr><tr><td>0.1</td><td>.082</td><td>0.31</td><td>0.00001</td><td>0.082</td></tr><tr><td>0.3</td><td>.00024</td><td>0.054</td><td>0.00041</td><td>0.00024</td></tr><tr><td>0.5</td><td>.0000034</td><td>0.049</td><td>0.0028</td><td>0.000032</td></tr><tr><td>0.7</td><td>3.9E-7</td><td>0.045</td><td>0.21</td><td>2.8E-7</td></tr><tr><td>0.9</td><td>9.1E-8</td><td>0.040</td><td>0.86</td><td>1.3E-8</td></tr></table>

Table 1: An example probabilities in DBLP

For sampling in general, it has been observed that “if intelligently used, stratification nearly always results in a smaller variance for the estimated mean or total than is given by a comparable simple random sampling” [8, p99]. We propose below one specific scheme for stratified sampling using LSH, and show its benefit empirically at Section 6.

## 5.1 LSH-SS: Stratified Sampling

We define two strata of pairs of vectors as follows depending on whether two vectors of a pair are in the same bucket.

• Stratum H $( S _ { H } ) \colon \{ ( u , v ) : u , v \in V , B ( u ) = B ( v ) \}$

• Stratum $L \left( S _ { L } \right) \colon \left\{ ( u , v ) : u , v \in V , B ( u ) \neq B ( v ) \right\}$

Note that $S _ { H }$ and $S _ { L }$ are disjoint and thus we can independently estimate the join size from each stratum and add the two estimates. $S _ { H }$ and $S _ { L }$ are fixed given $D _ { g }$ . Let $J _ { H } = | \{ ( u , v ) \in S _ { H } : s i m ( u , v ) \geq \tau \} | , \ J _ { L } = | \{ ( u , v ) \in S _ { L } \ ;$ sim $\left( u , v \right) \geq \tau \} |$ , and let $\hat { J } _ { H }$ and $\hat { J } _ { L }$ be their estimates. We estimate the join size as follows:

$$
\hat {J} _ {S S} = \hat {J} _ {H} + \hat {J} _ {L}.\tag{7}
$$

A straightforward implementation would be to perform uniform random sampling in $S _ { H }$ and $S _ { L }$ , and aggregate the two estimates. However, this simple approach may not work well. The problem is that it can be harder to guarantee a small error of $\hat { J } _ { L }$ with the same sample size due to very small $P ( T | L )$ at high thresholds [15]. See the next example.

Example 1. Assume that $N _ { L } = 1 , 0 0 0 , 0 0 0 , \ J _ { L } = 1$ at $\tau = 0 . 9$ , and the sample size is 10; only one pair out of

1, 000, 000 pairs satisfies $\tau = 0 . 9$ and we sample 10 pairs. In most cases, the true pair will not be sampled and $\hat { J } _ { L } = 0$ But if the only true pair is sampled, $\hat { J } _ { L } = 1 0 0 , 0 0 0$ . The estimate fluctuates between 0 and 100, 000 and is not reliable.

Our solution for this problem is to use diferent sampling procedures in the two strata. Recall that similarities of the pairs in $S _ { H }$ are higher and $P ( T | H )$ is not too small, even at high thresholds. Thus, for $S _ { H }$ , we use uniform random sampling. Relatively small sample size will sufice for reliable estimation in $S _ { H }$ . In $S _ { L }$ , however, $P ( T | L )$ varies significantly depending on the threshold. In general, while at low thresholds $P ( { \cal T } | L )$ is relatively high and the estimate is reliable, $P ( { \cal T } | L )$ becomes very small at high thresholds and the resulting estimate is more likely to have a large error and is much less reliable. For the same sample size, the variance itself decreases as the join size decreases (e.g. Lemma 4.1 of [1]). However, we may need many more samples at high thresholds, where the join size can be very small, to have the same error guarantee [15, 9].

Thus, we use $\hat { J } _ { L }$ only when it is expected to be reliable and discard $\hat { J } _ { L }$ when it is not. Discarding $\hat { J } _ { L }$ at high thresholds generally does not hurt accuracy much since the contribution of $J _ { L }$ in J is not large. In Table 1, when the similarity thresholds is high, $P ( H | T )$ is large, which means that a large fraction of true pairs are in $S _ { H }$ , not in $S _ { L }$ . We use adaptive sampling [15] in $S _ { L }$ since it enables us to detect when the estimate is not reliable. A novelty is that we return a safe lower bound when we cannot have a good error guarantee within the allowable sample size.

Algorithm 1 describes the proposed stratified sampling algorithm LSH-SS. It applies a diferent sampling subroutine to each stratum. For $S _ { H }$ , it runs the random sampling subroutine SampleH. For $S _ { L }$ , it runs the adaptive sampling subroutine SampleL. The final estimate is the sum of estimates from the two Strata as in Equation (7) (line 3).

## 5.1.1 Sampling in Stratum H

SampleH of Algorithm 1 describes the random sampling in $S _ { H }$ . First, a bucket $B _ { j }$ is randomly sampled weighted by the number of pairs in the bucket, $\bar { w e i g h t ( B _ { j } ) } = \breve { \binom { b _ { j } } { 2 } }$ (line 3). We then select a random pair $( u , v )$ from $B _ { j } \ ( \mathrm { l i n e \ 4 } )$ The resulting pair $( u , v )$ is a uniform random sample from $S _ { H } .$ . SampleH has one tunable parameter m which is the sample size. We count the number of pairs satisfying the similarity threshold τ in m<sub>H</sub> sample pairs and return the count scaled up by $N _ { H } / m _ { H }$ (line 9).

## 5.1.2 Sampling in Stratum L

SampleL of Algorithm 1 employs adaptive sampling [15] in $S _ { L }$ . It has two tunable parameters δ and $m _ { L }$ . δ is the answer size threshold, the number of true samples to give a reliable estimate, and $m _ { L }$ is the maximum number of samples. We sample a pair from $S _ { L }$ (line 3) and see if it satisfies the given threshold (line 4). In case LSH computation for bucket checking at line 3 is expensive, we can delay the checking till line $5 ,$ which in efect results in slightly more samples. The while-loop at line 2 can terminate in two ways: (1) by acquiring a suficiently large number of true samples, $n _ { L } \ge \delta$ or (2) by reaching the sample size threshold, $i \geq m _ { L }$ , where i is the number of samples taken. In the former case, we return the count scaled up by $N _ { L } / i$ (line 12). Theorem 2.1 and 2.2 of adaptive sampling [15] provide error bounds in such cases. In the latter case, adaptive sampling returns a loose upper bound. We cannot guarantee that the estimate is reliable and simply return the number of true pairs found in the sample, $n _ { L } ,$ as $\hat { J } _ { L }$ without scaling it up. Since $J _ { L }$ is at least as large as $n _ { L }$ , we call $\ddot { J } _ { L } = n _ { L }$ a safe lower bound.

<div class="mineru-algorithm" style="white-space: pre-wrap; font-family:monospace;">
Algorithm 1 LSH-SS

Procedure LSH-SS

Input: similarity threshold $\tau$, sample size for Stratum H $m_H$, answer size threshold $\delta$, max sample size for Stratum L $m_L$

1: $\hat{J}_H = SampleH(\tau, m_H)$

2: $\hat{J}_L = SampleL(\tau, \delta, m_L)$

3: return $\hat{J}_{SS} = \hat{J}_H + \hat{J}_L$

Procedure SampleH

Input: $\tau, m_H$ (sample size)

1: $n_H = 0$

2: for $i$ from 1 to $m_H$ do

3: sample a bucket $B_j$ with weight ($B_j$) = $\binom{b_j}{2}$

4: sample two vectors $u, v \in B_j, u \neq v$

5: if sim(u, v) $\geq \tau$ then

6: $n_H = n_H + 1$

7: end if

8: end for

9: return $\hat{J}_H = n_H \cdot N_H / m_H$

Procedure SampleL

Input: $\tau, \delta$ (answer size th.), $m_L$ (sample size th.)

1: $i = 0, n_L = 0$

2: while $n_L &lt; \delta$ and $i &lt; m_L$ do

3: sample a uniform random pair $(u, v), B(u) \neq B(v)$

4: if sim(u, v) $\geq \tau$ then

5: $n_L = n_L + 1$

6: end if

7: $i = i + 1$

8: end while

9: if $i \geq m_L$ then

10: return $\hat{J}_L = n_L$ // or $\hat{J}_L = n_L \cdot c_s(N_L / m_L)$ when a dampening factor $0 &lt; c_s \leq 1$ is used.

11: end if

12: return $\hat{J}_L = n_L \cdot (N_L / i)$
</div>

This conservative approach does not degrade accuracy much since the majority of true pairs are in $S _ { H }$ at high thresholds. It is possible, however, that there can be “grey area” of thresholds. While the random sampling does not guarantee its accuracy, there are enough true pairs in $S _ { L }$ to make its impact on underestimation significant. Hence, we consider the use of a dampened scale-up constant $0 < c _ { s } \le 1$ which is multiplied to the full scaling-up factor at line 10. We analyze the impact of this dampened scale-up factor in the following section.

For the tunable parameters, we used $m _ { H } = n$ and $m _ { L } = n$ and $\delta = \log { n ^ { 1 } }$ where n is the database size, $n = | V |$ . Note that the size is expressed in the number of pairs. This corresponds to sampling $\sqrt { n }$ vectors from two collections of vectors $( n = { \sqrt { n } } \times { \sqrt { n } } )$ in equi-joins, which is even smaller than the sample size $\sqrt { n }$ log n in [9]. These parameter values give provably good estimates at both high and low thresholds. We give the details in the following section.

## 5.2 Analysis

Let $\alpha = P ( T | H )$ and $\beta = P ( T | L )$ for the sake of presentation. In our analysis, a similarity threshold τ is considered high when $\alpha \geq \log n / n$ and $\beta < 1 / n$ , and is considered low when $\alpha \geq \log n / n$ and $\beta \geq \log n / n$ . Our model is analogous to the classic approach of distinguishing high frequency values from low frequency values to meet the challenge of skewed data, $\mathrm { { e . g . } } [ 9 , 7 ]$ . Our distinction efectively models diferent conditional probabilities that are observed in diferent threshold values as in Table 1.

We first analyze the high threshold case and then the low threshold case. This distinction is only for the analysis purposes. The behavior of Algorithm 1 is adaptive and users do not need to distinguish two cases by parameters.

## 5.2.1 Guarantees at high thresholds

Recall that $\alpha = P ( T | H )$ is the probability that a pair of vectors in a bucket is indeed a true pair and $\beta = P ( T | L )$ is the probability that a pair of vectors that are not in the same bucket is a true pair. We assume α ≥ log $n / n$ and $\beta < 1 / n$ at high thresholds. The condition on α intuitively states that even when the join size is small, the fraction of true pairs in $S _ { H }$ is not too small from the property of LSH. Our assumption on α is not a stringent condition, i.e. log $n / n$ is usually a very small value and will be easily satisfied by any reasonably working LSH index. The condition on $\beta$ states that it is hard to sample true pairs in $S _ { L }$ at high thresholds.

As a sanity check, consider the example in Table 1. In the data set, $n = 3 4$ , 000 and $\beta \sim 0 .$ .00003 $( \sim 1 / n )$ at $\tau =$ 0.5. High thresholds correspond $\mathrm { t o ~ } [ 0 . 5 , 1 . 0 ]$ . It that range $\alpha = P ( T | H )$ is consistently higher than 0.04 which is well over the assumed value of α which is 0.00046 (∼ log $n / n )$ for the data set. $\beta = P ( T | L )$ is also below or very close to the calculated 0.00003 in the range. Table 2 in Appendix C shows more examples of α and $\beta$ values in other data sets, which again satisfy our assumptions.

The following theorem states that LSH-SS gives a good estimate at high thresholds. See Appendix A.2 for a proof.

Theorem 1. Let $0 ~ < ~ \epsilon ~ < ~ 1$ be an arbitrary constant. Assume $\alpha \geq$ log n/n and $\beta < 1 / n$ Then for suficiently large n with $c = 1 / ( \log e \cdot \epsilon ^ { 2 } )$ , m $\iota _ { H } = c n$ and $m _ { L } = c n$ ，

$$
P r (| \hat {J} _ {S S} - J | \leq (1 + \epsilon) J) \geq 1 - \frac {2}{n}.
$$

The term 1 in the error bound is from the conservative nature of the SampleL procedure of not scaling up the result when the accuracy is not guaranteed. However, since the majority of true pairs are in $S _ { H }$ at high thresholds, its impact is rather insignificant. For overestimations, the bound is very conservative and our experiments in Section 6 show much better results than the theoretical guarantees in Theorem 1. For underestimations, the error bound in Theorem 1 is not meaningful because underestimation is capped by −100%. The following theorem, which is based on the dampened scale-up factor, has the advantage of providing a meaningful bound for underestimations as well.

In the approach using a dampened scale-up constant $0 <$ $c _ { s } \leq 1$ , we multiply the full scale-up factor by $c _ { s } .$ . The following theorem quantifies the efect of using the dampened scale-up factor. See Appendix A.3 for a proof.

Theorem 2. For a constant $0 < \epsilon < 1$ , using a dampened scale-up factor of $0 \textless c _ { s } \textless 1$ at line 10 of SampleL in Algorithm 1 guarantees

$$
P r (| \hat {J} _ {L} - J _ {L} | \geq \epsilon^ {\prime} J _ {L}) \leq \epsilon^ {- 2} \cdot \frac {1 - \beta}{m _ {L} \beta}.
$$

where $\epsilon ^ { \prime } = 1 - ( 1 - \epsilon ) c _ { s }$ . When $\hat { J } > J _ { : }$ , i.e. overestimation, we have a tighter bound $\epsilon ^ { \prime } = c _ { s } ( 1 + \epsilon ) - 1$

Compared with Theorem 1, the guarantee of Theorem 2 is weaker in terms of probability, but it provides a tighther bound; $1 - c _ { s } < \epsilon ^ { \prime } < 1 .$ The dampened scale-up factor $c _ { s }$ represents the trade-ofs between the accuracy and the probabilistic guarantee. Using a smaller $c _ { s }$ enables a stronger guarantee in probability reducing variance by a factor of $c _ { s }$ (see Appendix A.3), but the relative error bound increases. We discuss the choice of $c _ { s }$ in Section 6.

## 5.2.2 Guarantees at low thresholds

At low thresholds, we assume that $\alpha \geq \log n / n$ and $\beta \geq$ log $n / n$ The rationale is that as the actual join size increases, more true pairs are in $S _ { L }$ and sampling true pairs in $S _ { L }$ becomes not so dificult any more. Again these conditions are usually met when the threshold is low as in the example in Table 1. In fact, the contribution from $S _ { L } , \ J _ { L }$ dominates the join size at low thresholds. The following theorem states that LSH-SS gives a reliable estimate even when the threshold is low. See Appendix A.4 for a proof.

Theorem 3. Let $0 ~ < ~ \epsilon ~ < ~ 1$ be an arbitrary constant. Assume $\alpha \geq \log n / n$ and $\beta \ \geq \ \log n / n$ . Then with $c =$ $4 / ( \log e \cdot \epsilon ^ { 2 } ) , c ^ { \prime } = m a x ( c , 1 / ( 1 - \epsilon ) ) , m _ { H } = c n ~ a n d m _ { L } = c ^ { \prime } n$

$$
P r (| \hat {J} _ {S S} - J | \leq \epsilon J) \geq 1 - \frac {3}{n}.
$$

We demonstrate that our guarantees indeed hold and thus LSH-SS provides reliable estimates at both high and low thresholds with real-world data sets in the following section.

## 6. EXPERIMENTAL EVALUATION

## 6.1 Set Up

Data sets: We have conducted experiments using three data sets. The DBLP data set consists of about 800K vectors. The NYT data set is NYTimes news articles and consists of about 150K vectors. The PUBMED data set is PubMed abstracts and consists of 400K vectors. We give detailed data set descriptions in Appendix C.1. We only report results of the DBLP and NYT data set; results on the PUBMED data set is in Appendix C.4.

Algorithms compared: We implemented the following algorithms for the VSJ problem.

$L C ( \xi )$ is the Lattice Counting algorithm in [14] with a minimum support threshold $\xi .$

$L S H { - } S$ is the LSH index based algorithm in Section 4.

• LSH-SS is the LSH index based stratified sampling algorithm in Section 5. We used $m _ { 1 } = n , \delta = \log n .$ $m _ { 2 } = n$ and $k = 2 0$ unless specified otherwise. LSH-$S S ( D )$ is $L S H  – S S$ with a dampened scale-up factor. We used $c _ { s } = n _ { L } / \delta$ . See Appendix C.3 for a discussion on alternative choices.

• We consider two random sampling methods: RS(pop) and RS(cross). In $R S ( p o p )$ , we sample pairs from the cross product. RS(cross) is cross sampling [10], where we sample $\lceil \sqrt { m _ { R } } \ \rceil$ records and compare all the pairs in the sample. $m _ { R } = d \cdot n$ where d is a constant to compare algorithms with roughly the same runtime.

Evaluation metric: We use average relative error to show the accuracy. A relative error is defined as est size / true size. We show errors of overestimations and underestimations separately to clearly depict the characteristics of each algorithm. To measure reliability, we report the standard deviation (STD) of estimates. We report figures over 100 experiments. For eficiency, we measure the runtime, which is time taken for estimation. For all algorithms, we loaded necessary data structures (data or index) in memory.

![](images/c7ca5009b871fe6dd2a98ad21a56bfea237d5fe32fb966c80b014076ff674a9f.jpg)  
Similarity Threshold τ  
(a) Relative error (Overest.)

![](images/2f6f1f7ddcf876f2d5e5d5ed85c9c931437cc6b83b698779e357612faf102576.jpg)  
(b) Relative error (Underest.)

![](images/caa78788ce78e9416f8ac42dbcb319ddc23eea87c4cc3d1e8519a48abdeb8cd4.jpg)  
(c) Variance  
Figure 2: Accuracy/Variance on DBLP

We implemented all the algorithms in Java. We ran all our experiments on a server running 64 GNU/Linux 2.6.27 over 2.93 GHz Intel Xeon CPUs with 64GB of main memory.

## 6.2 Accuracy, Variance and Efficiency

DBLP: We first report the results on the accuracy, variance and runtime using the DBLP data set. Figure 2(a) (resp. Figure 2(b)) shows relative errors of overestimations (resp. underestimations) over the similarity threshold range. Figure 2(c) shows the variance of the estimates.

LSH-SS delivers accurate estimations over the whole threshold range. In Figure 2(a), we see that LSH-SS hardly overestimates, which is expected from the discussions in Section 5.2. LSH-SS(D) occasionally overestimates, but its error is smaller than 30%. Figure 2(b) shows the underestimation tendency of LSH-SS, but it is much less severe than those of RS(pop) and RS(cross). We observe that LSH-SS(D) shows smaller underestimations.

The following table shows the actual join size J and its selectivity at various similarity threshold τ.

Note the dramatic diferences in the join size depending on τ. At τ = 0.1 there are more than 100 billion true pairs, and its selectivity is about 30 %. But at $\tau = 0 . 9$ there are only 42, 000 true pairs, and the selectivity is only 0.00001%. Yet LSH-SS is able to estimate the join size quite accurately and reliably exploiting the LSH index. Moreover, in Figure 2 (c), LSH-SS shows very small variance at high thresholds.

![](images/b169e69a207d6dc8e27728eebc45accffdc33aead713d588a705a59ee1392631.jpg)  
Similarity Threshold τ  
(a) Relative error (Overest.)

![](images/704909a5c10049d27d7333fc308d6b93c124f917dd51a64cfaaa98d20aa0783a.jpg)  
(b) Relative error (Underest.)

![](images/c2ebf26d075cce874676b9e56b981f187b8e28f581ff4230740ff9c86ed4f2a3.jpg)  
(c) Variance

Figure 3: Accuracy/Variance on NYT

<table><tr><td> $\tau$ </td><td>0.1</td><td>0.3</td><td>0.5</td><td>0.7</td><td>0.9</td></tr><tr><td> $J$ </td><td>105B</td><td>267M</td><td>11M</td><td>103K</td><td>42K</td></tr><tr><td>selectivity</td><td>33%</td><td>0.085%</td><td>0.0036%</td><td>0.000064%</td><td>0.000013%</td></tr></table>

RS(pop) and RS(cross) are as accurate as LSH-SS at low thresholds. However as the threshold increases, their errors rapidly increase; their estimations fluctuate between huge overestimation and extreme underestimation (i.e. −100%) in Figure 2(a) and (b). As a consequence, their variance is quite large in Figure 2(c).

LSH-S and LC show consistently outperformed by others, and we omit their results. LSH-S has large errors at high thresholds, e.g. $\tau \geq 0 . 6 $ This is because the estimations of conditional probabilities are not reliable due to insuficient number of true pairs sampled. LC underestimates over the whole threshold range. We hypothesize that it is because of the characteristic of the binary LSH function for cosine similarity. Intuitively the binary LSH functions need more hash functions (larger k) to distinguish objects, which has negative impact on the runtime. It appears that LC is not adequate for binary LSH functions.

For runtime, LSH-SS and LSH-SS(D) took less than 750 msec and RS(pop) and RS(cross) took about 780 sec on the average. The runtime of LSH-S was 1028 msec, and that of LC was about 3 sec.

LSH-SS LSH-S  
![](images/04dd0f37c037e915cf954ce24eb337ba8dd2c571839371a21d3727a95a88a9e3.jpg)  
(a) Accuracy, τ = 0.5

![](images/3eb645880805efc3e8358676ae64a734968d0f8a09c3cc14e89eaa1d7d46e11b.jpg)  
(b) Accuracy, $\tau = 0 . 8$  
Figure 4: Impact of k on DBLP

NYT: Figure 3 shows relative errors and variance on the NYT data set. LSH-SS gives good estimates at high thresholds. It shows underestimation at $\tau \leq 0 . 5$ . In general, however, this is not the most interesting similarity range and this is probably not a serious flaw. The problem can be addressed either by using a bigger dampened scale-up factor $c _ { s }$ or increasing the sample size. We see that LSH-SS(D) has smaller underestimation problems. Again, we observe that estimations of RS(pop) and RS(cross) fluctuate at high thresholds. Their variance is larger than the variance of LSH-SS or LSH-SS(D) throughout the threshold range.

For runtime, LSH-SS took 1091 msec and RS took 920 msec on the average.

## 6.3 Impact of Parameters

We assume a pre-built LSH index with parameters optimized for its similarity search. The relevant parameter for our estimation purposes is k which specifies the number of hash functions used for an LSH table. We analyze the impact of k on accuracy and variance.

Figure 4 shows accuracy at $\tau = 0 . 5 , 0 . 8$ . The conclusion is the same at other thresholds. We observe that LSH-SS is not much afected by k. This is because an LSH table provides suficient distinguishing power with relatively small k. LSH-SS will work with any reasonable choice of k. LSH-S is highly sensitive to k for the reason specified in Section 6.2. The same observation is made in variance as well.

<table><tr><td></td><td>k=10</td><td>k=20</td><td>k=30</td><td>k=40</td><td>k=50</td></tr><tr><td>size (MB)</td><td>3.2</td><td>7.5</td><td>12.6</td><td>14.1</td><td>16.5</td></tr></table>

The above table shows space occupied by an LSH table on the DBLP data set ignoring implementation-dependent overheads. When $k = 2 0$ , there are about 480K non-empty buckets which add 7.5M of space for the g function values, bucket counts, and vector ids. The DBLP binary is 50M.

If we are given the freedom of choosing k, we observe that slightly smaller k values, say between 5 and 15, generally give better accuracy. See Appendix B.1 for more discussions.

## 7. CONCLUSION

We propose size estimation techniques for vector similarity joins. The proposed methods rely on the ubiquitous LSH indexing and enable reliable estimates even at high similarity thresholds. We show that LSH-SS gives good estimates throughout the threshold range with probabilistic guarantees. The proposed techniques only need minimal addition to the existing LSH index and can be easily applied.

## 8. REFERENCES

[1] N. Alon, P. B. Gibbons, Y. Matias, and M. Szegedy. Tracking join and self-join sizes in limited storage. In Proc. SIGMOD, pages 10–20, 1999.

[2] A. Arasu, V. Ganti, and R. Kaushik. Eficient Exact Set-Similarity Joins. In Proc. VLDB, pages 918–929, 2006.

[3] R. J. Bayardo, Y. Ma, and R. Srikant. Scaling up all pairs similarity search. In Proc. WWW, pages 131–140, 2007.

[4] A. Z. Broder. On the Resemblance and Containment of Documents. In Proc. SEQUENCES, pages 21–29, 1997.

[5] M. S. Charikar. Similarity Estimation Techniques from Rounding Algorithms. In Proc. STOC, pages 380–388, 2002.

[6] S. Chaudhuri, V. Ganti, and R. Kaushik. A Primitive Operator for Similarity Joins in Data Cleaning. In Proc. ICDE, pages 5–16, 2006.

[7] S. Chaudhuri, R. Motwani, and V. Narasayya. On Random Sampling over Joins. In Proc. SIGMOD, 1999.

[8] W. G. Cochran. Sampling Techniques. John Wiley & Sons, 1977.

[9] S. Ganguly, P. B. Gibbons, Y. Matias, and A. Silberschatz. Bifocal sampling for skew-resistant join size estimation. In Proc. SIGMOD, pages 271–281, 1996.

[10] P. J. Haas, J. F. Naughton, S. Seshadri, and A. N. Swami. Fixed-Precision Estimation of Join Selectivity. In Proc. PODS, pages 190–201, 1993.

[11] M. Hadjieleftheriou, X. Yu, N. Koudas, and D. Srivastava. Hashed Samples: Selectivity Estimators For Set Similarity Selection Queries. In Proc. VLDB, pages 201–212, 2008.

[12] P. Indyk and R. Motwani. Approximate Nearest Neighbors: Towards Removing the Curse of Dimensionality. In Proc. STOC, pages 604–613, 1998.

[13] Y. E. Ioannidis and S. Christodoulakis. On the Propagation of Errors in the Size of Join Results. In Proc. SIGMOD, pages 268–277, 1991.

[14] H. Lee, R. T. Ng, and K. Shim. Power-Law Based Estimation of Set Similarity Join Size. In Proceedings of the VLDB Endowment, pages 658–669, 2009.

[15] R. Lipton, J. F. Naughton, and D. A. Schneider. Practical selectivity estimation through adaptive sampling. In Proc. SIGMOD, pages 1–11, 1990.

[16] R. Motwani and P. Raghavan. Randomized Algorithms. Cambridge University Press, 1995.

[17] S. Sarawagi and A. Kirpal. Eficient set joins on similarity predicates. In Proc. SIGMOD, pages 743–754, 2004.

## APPENDIX

## A. PROOFS

## A.1 Equation (4)

Since $f ( s ) = s ^ { k }$ , the four areas defined in Figure 1 can be calculated as follows:

$$
\begin{array}{r c l} P [ H \cap F ] & = & \int_ {0} ^ {\tau} f (s) d s = \frac {\tau^ {k + 1}}{k + 1} \\ P [ H \cap T ] & = & \int_ {\tau} ^ {1} f (s) d s = \frac {1 - \tau^ {k + 1}}{k + 1} \\ P [ L \cap F ] & = & \int_ {0} ^ {\tau} 1 - f (s) d s = \tau - \frac {\tau^ {k + 1}}{k + 1} \\ P [ L \cap T ] & = & \int_ {\tau} ^ {1} 1 - f (s) d s = 1 - \tau - \frac {1 - \tau^ {k + 1}}{k + 1}. \end{array}
$$

Using the above probabilities in Equation (2) and (3) gives,

$$
{ P ( H | T ) } { = } { \frac { 1 } { 1 - \tau } \int _ { \tau } ^ { 1 } f ( s ) d s = \frac { \sum _ { i = 0 } ^ { k } \tau ^ { i } } { k + 1 } }\tag{8}
$$

$$
{ P ( H | F ) } { = } { \frac { 1 } { \tau } \int _ { 0 } ^ { \tau } f ( s ) d s = \frac { \tau ^ { k } } { k + 1 } . }\tag{9}
$$

We have the following estimator ${ \hat { J } } _ { U }$ by using above $P ( H | T )$ and $P ( H | F )$ in Equation (1):

$$
\begin{array}{r c l} \hat {J} _ {U} & = & \frac {N _ {H} - M \cdot \hat {P} (H | F)}{\hat {P} (H | T) - \hat {P} (H | F)} \\ & = & \frac {N _ {H} - M \cdot \frac {\tau^ {k}}{k + 1}}{\frac {\sum_ {i = 0} ^ {k} \tau^ {i}}{k + 1} - \frac {\tau^ {k}}{k + 1}} \\ & = & \frac {(k + 1) N _ {H} - M \cdot \tau^ {k}}{\sum_ {i = 0} ^ {k - 1} \tau^ {i}}. \end{array}
$$

## A.2 Proof of Theorem 1

We analyze the behavior of SampleH and SampleL in Algorithm 1 separately by the two lemmas below. We then combine the two results using Equation (7).

First, the following lemma shows that SampleH in Algorithm 1 gives a reliable estimate at high thresholds.

Lemma 1. Let $0 < \epsilon < 1$ be an arbitrary constant. Assume α ≥ log n/n. Then with $c = 4 / ( \log e { \cdot } \epsilon ^ { 2 } )$ and $m _ { H } = c n$ we have

$$
P r (| \hat {J} _ {H} - J _ {H} | \leq \epsilon J _ {H}) \geq 1 - \frac {1}{n}.
$$

Proof. Let X be a random variable denoting the number of pairs in the sample that satisfy τ in SampleH of Algorithm 1. Then X is a binomial random variable with parameters $\left( m _ { H } , \alpha \right) \left[ 1 6 \right]$ . Since m $_ H = c n$ and $\alpha \geq \log n / n$ 7

$$
E (X) \geq c \log n.
$$

For an arbitrary constant $0 < \epsilon < 1$ , by Chernof bounds [16]

$$
P r (| X - E (X) | > \epsilon E (X)) \leq e ^ {- \frac {c \log n \epsilon^ {2}}{4}}.
$$

Letting $c = 4 / ( \log e \cdot \epsilon ^ { 2 } )$ gives

$$
P r (| X - E (X) | > \epsilon E (X)) \leq \frac {1}{n}.
$$

$\begin{array} { r } { E ( X ) = J _ { H } \cdot \frac { m _ { H } } { N _ { H } } } \end{array}$ . Thus, $\begin{array} { r } { \frac { N _ { H } } { m _ { H } } \cdot E ( X ) = \frac { N _ { H } } { m _ { H } } \cdot m _ { H } \cdot \frac { J _ { H } } { N _ { H } } = J _ { H } } \end{array}$ Therefore,

$$
P r (| \frac {N _ {H}}{m _ {H}} \cdot X - J _ {H} | > \epsilon J _ {H}) \leq \frac {1}{n}.
$$

$\hat { J } _ { H } = X \cdot N _ { H } / m _ { H }$ in SampleH of Algorithm 1. Plugging in $\hat { J } _ { H }$ in the above inequality completes the proof.

Second, the following lemma states that SampleL returns a safe lower bound with high probability.

Lemma 2. Assume $\beta < 1 / n$ . Then for suficiently large $^ { n , }$ an arbitrary constant c<sup>0</sup> and $m _ { L } = c ^ { \prime } n _ { \scriptscriptstyle }$ , we have

$$
P r (\hat {J} _ {L} \leq c ^ {\prime}) \geq 1 - \frac {1}{n}.
$$

Proof. Let Y be a random variable denoting the number of pairs in the samples satisfying τ in SampleL of Algorithm 1. We show that Y is not likely to be bigger than $\delta = \log n .$ Y is a binomial random variable with parameters $( m _ { L } , \beta )$

For an arbitrary constant $\epsilon ^ { \prime } \geq 2 e { - 1 }$ , by Chernof bounds [16]

$$
P r (Y > (1 + \epsilon^ {\prime}) E (Y)) \leq 2 ^ {- E (Y) (1 + \epsilon^ {\prime})}.
$$

Therefore, the probability that the loop of SampleL terminates by acquiring enough number of true pairs $( \delta = \log n )$ is as follows:

$$
P r (Y > \delta = \log n) \leq 2 ^ {- \log n} = \frac {1}{n}.
$$

Since $m _ { L } = c ^ { \prime } n$ and $\beta < 1 / n$ 2

$$
E (Y) <   c ^ {\prime}.
$$

If $Y \leq \delta ,$ , SampleL of Algorithm 1 returns ${ \hat { J } } _ { L } = Y$ without scaling it up. Therefore,

$$
P r (\hat {J} _ {L} = E (Y) <   c ^ {\prime}) \geq 1 - \frac {1}{n}.
$$

□

Finally, we prove Theorem 1 using Lemma 1 and Lemma 2. For suficiently large $n , c ^ { \prime } \leq J _ { L }$ . Thus from Lemma 2,

$$
P r (| \hat {J} _ {L} - J _ {L} | \leq J _ {L}) \geq 1 - \frac {1}{n}.
$$

Since $\hat { J } _ { S S } ~ = ~ \hat { J } _ { H } + \hat { J } _ { L }$ and $J = J _ { H } + H _ { L }$ , combining the above inequality with Lemma 1 proves the theorem.

## A.3 Proof of Theorem 2

Let X be a random variable for the number of true pairs in the sample, i.e. n<sub>L</sub>, in SampleL of Algorithm 1. X follows a binomial distribution $B ( m _ { L } , \beta )$ , and $\mu _ { X } = m _ { L } \beta$ and $\sigma _ { X } = \sqrt { m _ { L } \beta ( 1 - \beta ) }$ . Suppose $0 < c _ { s } \le 1$ is used as a dampening factor. Let s be the sampling ratio, $s = m _ { L } / N _ { L } ,$ and Y be the estimate using $c _ { s } . \ Y = c _ { s } / s \cdot X ,$ and $\mu _ { Y } =$ $c _ { s } / s \cdot m _ { L } \beta = c _ { s } N _ { L } \beta = c _ { s } J _ { L }$ and $\sigma _ { Y } = c _ { s } / s \cdot \sqrt { m _ { L } \beta ( 1 - \beta ) }$ Applying Chebyshev’s inequality [16], for any $k \in \mathbb { R } ^ { + }$ 2

$$
P r \left(\left| Y - \mu_ {Y} \right| \geq k \cdot \sigma_ {Y}\right) \leq \frac {1}{k ^ {2}}.
$$

σ<sub>Y</sub> $\cdot / \mu _ { Y } ~ = ~ \sqrt { ( 1 - \beta ) / m _ { L } \beta } .$ . Letting $\epsilon = k \sqrt { ( 1 - \beta ) / m _ { L } \beta }$ gives

$$
P r \left(\frac {| Y - \mu_ {Y} |}{\mu_ {Y}} \geq \epsilon\right) \leq \epsilon^ {- 2} \cdot \frac {1 - \beta}{m _ {L} \beta}.
$$

The lower bound of the estimation is $J _ { L } - ( 1 - \epsilon ) \mu _ { Y }$ . In overestimation, $c _ { s } ( 1 + \epsilon ) J _ { L } \ge J _ { L }$ since otherwise it would be underestimation. Thus, the upper bound of the estimation is $( c _ { s } ( 1 + \epsilon ) - 1 ) J _ { L }$ because $c _ { s } \leq 1 , 1 - ( 1 - \epsilon ) c _ { s } \geq c _ { s } ( 1 + \epsilon ) - 1$ Letting $\epsilon ^ { \prime } = 1 - ( 1 - \epsilon ) c _ { s }$ proves the theorem.

## A.4 Proof of Theorem 3

As we have the same conditions on $\alpha , m _ { H }$ , and $^ { c , }$ Lemma 1 still holds in the low threshold range as well. However, due to the increased $\beta ,$ a diferent analysis needs to be done for SampleL (Lemma 2). We first show that SampleL returns a scaled-up estimate not a safe lower bound with high probability, and then show that the scaled-up estimate is reliable.

Similarly as in Lemma 2, Y is a binomial random variable with parameters $( m _ { L } , \beta )$ . Since $m _ { L } = c ^ { \prime } n$ and $\beta \geq$ log $n / n ,$

$$
E (Y) \geq c ^ {\prime} \log n.
$$

From the given condition, $c ^ { \prime } \geq 2 / ( \log e \cdot \epsilon ^ { 2 } )$ and $c ^ { \prime } \geq 1 / ( 1 -$ $\epsilon ) .$ . Since $( 1 - \epsilon ) E ( Y ) \geq ( 1 - \epsilon ) c ^ { \prime } \log n \geq \log n = \delta$ , by Chernof bounds,

$$
P r (Y \geq \delta) \geq 1 - \frac {1}{n}.
$$

This means that the while-loop (line 2) of SampleL Al gorithm 1 terminates by reaching the desired answer size threshold δ with high probability. Then SampleL returns $\hat { J } _ { L } = Y \cdot J _ { L } / m _ { L }$ . Moreover, since $c ^ { \prime } \geq 4 / ( \log e \cdot \epsilon ^ { 2 } )$ , by Chernof bounds,

$$
P r (| Y - E (Y) | \geq \epsilon E (Y)) \leq \frac {2}{n}.
$$

Therefore,

$$
P r (| \hat {J} _ {L} - J _ {L} | \leq \epsilon J _ {L}) \geq 1 - \frac {2}{n}.
$$

Since $\hat { J } _ { S S } = \hat { J } _ { H } + \hat { J } _ { L }$ and $J = J _ { H } + H _ { L }$ , the above inequality along with Lemma 1 completes the proof.

## B. ADDITIONAL DISCUSSION

## B.1 The Optimal-k for The VSJ Problem

Recall that k is the number of LSH functions for g. Ideally, we want high $P ( T | H )$ and $P ( H | T )$ because the estimate from $S _ { H }$ is more reliable and having more true pairs in $S _ { H }$ reduces the dependency on $S _ { L } . ~ P ( T | H )$ (resp. $P ( H | T ) )$ is analogous to precision (resp. recall) in information retrieval. We note that k value has the following trade-ofs between $P ( { \cal T } | H )$ and $P ( H | T )$

• A larger k increases $P ( { \cal T } | H )$ but decreases $P ( H | T )$ With a suficiently large k, only exactly the same vectors will be in the same bucket. $P ( T | H ) = 1$ in this case. However, only a very small fraction of true pairs is in the same bucket resulting in a very small $P ( H | T )$

• A smaller k decreases $P ( { \cal T } | H )$ but increases $P ( H | T )$ In an extreme case of $k = 0 , \ S _ { H }$ consists of all the pairs of vectors in $V ,$ , and thus $P ( H | T ) = 1$ . However, $P ( T | H ) = P ( T )$ and the LSH scheme does not ofer any benefit.

Observe that $P ( T | H ) \geq P ( T | L )$ from the property of the LSH indexing. The intuition on choosing k is that we want to increase $J _ { H }$ as long as we have good estimates with high probability. Since decreasing k increases $P ( H | T )$ and $J _ { H }$ , this means that we can decrease k as long as $P ( T | H )$ is not too small. Decreasing k also reduces the LSH function computation time. With this intuition, we can formalize the problem of choosing k as follows:

Definition 4 (The Optimal-k Problem). Given a desired error bound $\epsilon > 0$ and the bound on the probabilistic guarantee $p ,$ find the minimum k such that $P ( T | H ) \geq \rho ,$ where $\rho = \rho ( \epsilon , p )$ .

If can we assume a similarity distribution of the database, we can analytically find the optimal k. However, $P ( T | H )$ is dependent on data and the LSH scheme used, and so is the optimal value of k.

## B.2 Extensions

## B.2.1 Using Multiple LSH Tables

The proposed algorithms so far assume only a single LSH table, but a typical LSH index consists of multiple LSH tables. In this section, we describe how we can utilize more than one LSH tables for the estimation purposes. We consider two estimation algorithms using multiple LSH tables: median estimator and virtual bucket estimator.

Median estimator. The median estimator applies LSH-SS to each LSH table independently without modifying LSH SS and merges the estimates. Suppose an LSH index $I _ { G } =$ $\{ D _ { g _ { 1 } } , \ldots , D _ { g _ { \ell } } \}$ with \` tables. From each table $D _ { g _ { i } } , 1 \leq i \leq$ $\ell ,$ we generate an estimate ${ \hat { J } } _ { i }$ with a sample of n pairs. Its estimate, $\hat { J } _ { m }$ , is the median of the estimates, $\hat { J } _ { i } , 1 \leq i \leq \ell .$ This approach makes the algorithm more reliable reducing the probability that $\hat { J } _ { m }$ deviates much from $J .$ From Theorems 1 and 3, the probability that ${ \hat { J } } _ { i }$ difers from J by more than a factor of $1 + \epsilon$ is less than $2 / n$ with the assumed join size and sample size. When taking the median, the probability that more than $\ell / 2 \ \hat { J } _ { i } { ' } _ { 1 }$ s deviate by more than $( 1 + \epsilon ) J$ is at most $2 ^ { - \ell / 2 }$ by the standard estimate of Chernof [16]. This states that $\hat { J } _ { m }$ is within the same factor of error with higher probability than the guarantees in Theorems 1 and 3. The efective sample size increases by a factor of \`. When a sample size that is greater than n is afordable, exploiting multiple LSH tables can make the estimate more reliable. However, dividing a total sample size of n into multiple estimates can impair the accuracy of individual estimates.

Virtual bucket estimator. We consider virtual buckets formed by multiple LSH tables. A pair $( u , v )$ is regarded as in the same bucket if u and v are in the same bucket in any of \` LSH tables. This can improve the accuracy when an existing LSH scheme has a relatively large k than necessary. Recall from the discussions in the previous section that when k is too large, g becomes overly selective and $S _ { H }$ can be too small. Then the true pairs from $S _ { H }$ can be only a small portion of the true pairs. Considering virtual buckets can address this problem by relaxing the bucket conditions; $\boldsymbol { B } ( \boldsymbol { u } ) = \boldsymbol { B } ( \boldsymbol { v } )$ if and only if there exist a $D _ { g _ { i } }$ such that u and v belong to the same bucket $B \in D _ { g _ { i } }$ . With virtual buckets, when we check $B ( u ) = B ( v )$ (or $\neq )$ for $( u , v )$ , we need to check up to \` tables. At lines 3 and 4 of SampleH in Algorithm 1, a pair $( u , v )$ is chosen from V uniformly at random and is discarded if u and v are not in the same bucket in any $D _ { g _ { i } } , 1 \leq i \leq \ell .$ At line 3 of Sample $L , B _ { i } ( u ) \neq B _ { i } ( v )$ is checked for all $D _ { g _ { i } }$ and if $B _ { i } ( u ) = B _ { i } ( v )$ in any $D _ { g _ { i } } , ( u , v )$ is discarded. The analysis remains the same but the set of pairs in same buckets, $S _ { H }$ becomes efectively larger, which gives potentially better accuracy. The runtime increases because of the bucket checking in multiple LSH tables.

## B.2.2 Non-self Joins

In this section, we discuss how to extend the proposed algorithms to handle joins between two collections of vectors $U$ and V. The basic ideas remain the same but we need to make sure that a pair under consideration consists of one vector from each collection.

Definition 5 (The General VSJ Problem). Given two collection of real-valued vectors $V = \{ v _ { 1 } , . . . , v _ { n _ { 1 } } \}$ , $U =$ $\{ u _ { 1 } , . . . , u _ { n _ { 2 } } \}$ and a threshold τ on a similarity measure sim, estimate the number of pairs $J ~ = ~ | \{ ( u , v ) ~ : ~ u ~ \in ~ U , v ~ \in ~$ $V , s i m ( u , v ) \geq \tau \}$

Suppose that we have two LSH tables $D _ { g }$ and $E _ { g }$ that are built on U and $V$ respectively using $g = ( g _ { 1 } , \dotsc , g _ { \ell } )$ . We describe how we modify LSH-S and $L S H  – S S$

LSH-S. We make two changes for $L S H  – S \colon N _ { H }$ computation and sampling. In self-joins, $N _ { H }$ in Equation (1) is the number of pairs in the same bucket: $\begin{array} { r } { N _ { H } = \sum _ { j = 1 } ^ { n _ { g } } \binom { b _ { j } } { 2 } } \end{array}$ . In general joins, $\begin{array} { r } { N _ { H } = \sum _ { i = 1 } ^ { n _ { g } } b _ { j } \cdot c _ { i } } \end{array}$ such that $B _ { j } \in D _ { g } , C _ { i } \in E _ { g }$ $b _ { j }$ is the bucket count of $B _ { j } , \ c _ { i }$ is the bucket count of $C _ { i } .$ and $g ( B _ { j } ) = g ( C _ { i } )$ , where $g ( B )$ denotes the g value that identifies bucket B. For $B _ { i } ,$ if there does not exist a bucket $C _ { i } \in E _ { g }$ such that $g ( C _ { i } ) = g ( B _ { j } ) , c _ { i } = 0$ . Next, a pair $( u , v )$ is sampled uniformly at random such that $u \in U$ and $v \in V ,$

LSH-SS. S is the set of pairs $( u , v ) , u \in U , v \in V$ such that $g ( u ) \ : = \ : g ( v )$ That is, the buckets of u and v have the same g value: $g ( B _ { j } ) = g ( C _ { i } )$ where u $\in \ B _ { j }$ in $D _ { g }$ and $u \in C _ { i }$ in $E _ { g } . ~ S _ { L }$ is the set of pairs $( u , v ) , u \in U , v \in V$ such that $g ( u ) \neq g ( v )$ . To sample a pair from $S _ { H } ,$ , we randomly sample a bucket $B _ { j }$ with weig $\mathfrak { h } t ( B _ { j } ) = b _ { j } \cdot c _ { i }$ where $g ( C _ { i } ) =$ $g ( B _ { j } ) , B _ { j } \in D _ { g }$ and $C _ { i } \in E _ { g }$ We sample u from $B _ { j }$ and v from $C _ { i }$ uniformly at random within the buckets. The resulting pair $( u , v )$ is a uniform random sample from $S _ { H }$ Lines 3 and 4 of SampleH in Algorithm 1 need to be changed accordingly. To sample from $S _ { L } .$ , we sample $u \in U$ and $v \in V$ uniformly at random, and $( u , v )$ is discarded if $g ( u ) = g ( v )$ Line 3 of SampleH needs corresponding changes.

## C. SUPPLEMENTARY EXPERIMENTS

## C.1 Data Set Description

The DBLP data set consists of 794,016 publications is the same as the data set used in [3]. Each vector is constructed from authors and title of a publication. There are about 56,000 distinct words in total and each word is associated with a dimension in a vector. The vector of a publication represents whether the corresponding word is present in the publication. Thus this data set is a binary vector data set. The average number of features is 14, and the smallest is 3 and the biggest is 219. The NYT data set is NYTimes news articles downloaded from UCI Machine Learning Reposi-$\mathrm { { t o r y } ^ { 2 } }$ and consists of 149,649 vectors. Again each dimension represents a word and has the corresponding TF-IDF weight. The dimensionality is about 100k and the average number of feature is 232. The PUBMED data set is constructed from PubMed abstracts and is also downloaded from the

![](images/6dcdf7cc55e4d3a41ce109e15368bfec9389c6a1ecc3adb4a1e11ced53547385.jpg)  
Figure 5: Relative error varying δ (the answer size threshold) in SampleL

![](images/9793684ddc4718dc04427d4f8c111d64ad126e97bc86600e1524b54817d0d3fb.jpg)  
Figure 6: The number of $\tau$ values with $\hat { J } / J \ge 1 0$ (overest.) or $J / \hat { J } \ge 1 0$ (underest.) varying δ

UCI repository. It consists of 400,151 TF-IDF vectors. The dimensionality is about 140k. It takes $4 . 7 , 4 . 6 ,$ , and 5.6 seconds to build LSH indexes from respective in-memory raw data.

## C.2 Impact of Parameters: δ and m

In this section, we study the impact of parameters related with the sample size using the DBLP data set. The goal is to see if our analysis and choice of parameter values are appropriate. Recall that $L S H  – S S$ and RS have the following parameters; m<sub>H</sub> is the sample size in Sampl $: H , \delta$ is the answer size threshold in SampleL, $m _ { L }$ is the (maximum) sample size in $S a m p l e L ,$ and $m _ { R }$ is the sample size of random sampling. In our analysis and experiments, we used the following parameter values: $m _ { H } = m _ { L } = n , \delta = \log n _ { \bot }$ and $m _ { R } = 1 . 5 n$ . Since $m _ { H } , m _ { L }$ and $m _ { R }$ control the overall sample size and δ specifies the answer size threshold, we use two functions $f _ { 1 }$ and $f _ { 2 }$ to control the parameters: $m _ { H } = m _ { L } = f _ { 1 } ( n )$ $m _ { R } = 1 . 5 f _ { 1 } ( n )$ and $\delta = f _ { 2 } ( n )$ . We test the following alternatives:

$f _ { 1 } \colon { \sqrt { n } } , n / \log n , 0 . 5 n , \underline { { n } } , 2 n ,$ and n log n

• f<sub>2</sub>: 0.5 log n, log n, 2 log n, and $\sqrt { n }$

We perform two types of combinations of $f _ { 1 }$ and $f _ { 2 }$ . First, we fix $f _ { 1 } ( n ) = n$ and vary $f _ { 2 } .$ Next, we fix $f _ { 2 } ( n ) = \log n$ and vary $f _ { 1 } .$ . For each combination we show two results: the average absolute error for $\tau = \{ 0 . 1 , 0 . 2 , . . . , 1 . 0 \}$ and the number of τ values with large errors among the 10 τ values. We define an error to be a big overestimation when $\hat { J } / J \geq 1 0 $ and to be a big underestimation when $J / \hat { J } \ge 1 0$

## C.2.1 Answer Size Threshold δ

Figure 5 gives the average (absolute) relative error varying δ and Figure 6 gives the number of τ values that give large errors. $m ( = f _ { 1 } )$ is fixed at n. $\delta > 2$ log n has a big underestimation problem. A large δ may prevent even a reliable estimate from being scaled up and result in a huge loss in contributions from $S _ { L }$ . For instance, $\delta = { \sqrt { n } }$ is too conservative and its estimate is less than 10% of the true size at 4 out of 10 τ values.

![](images/893778b3f63779b891c79e6687a7c1f1f61fbccce6e7ed2b6a0f033efca5fdcb.jpg)  
Figure 7: Relative error varying the sample size m

![](images/3c8b93ff2a866d1dec05fead4edca0d3825763c7a851172b6afe375bef737cb3.jpg)  
Figure 8: The number of τ with $\hat { J } / J \ge 1 0$ (overest.) or $J / \hat { J } \ge 1 0$ (underest.) varying the sample size m. The total number of τ is 10: $\{ 0 . 1 , 0 . 2 , . . . , 1 . 0 \}$

Simple heuristics such as using diferent δ depending on the threshold can improve the performance. For instance using 0.1 log $n \leq \delta \leq 0 .$ 5 log n at high thresholds, e.g. $\tau \geq 0 . 7 .$ 2 greatly improved the runtime without sacrificing accuracy in our experiments. At low thresholds, $\mathrm { e . g . } \ \tau \leq 0 . 3 ,$ using a slightly bigger value of δ, e.g. 2 log n, resulted in better accuracy and variance without increasing the runtime much.

## C.2.2 Sample Size Threshold m

Figure 7 gives the average absolute relative error varying the sample size and Figure 8 gives the number of τ values that give large errors. δ is fixed at log n. $f _ { 1 } < 0 . 5 r$ causes serious underestimations in both algorithms. With $f _ { 1 } =$ n log $n ,$ LSH-SS does not give any large errors, but the runtime roughly increases by log n.

## C.3 Impact of Parameter: $c _ { s }$

Using a larger dampened scale-up factor $c _ { s }$ has less underestimation. In the DBLP data set, setting $c _ { s } = 0 . 5$ reduces underestimation errors from −95% (LSH-SS) to −65% at $\tau = 0 . 6$ However, it can cause overestimation problems with large variance as discussed in Section $5 . 2 . c _ { s } \ = \ 1$ has overestimation errors between 100% and 900% at high thresholds. $c _ { s } = 0 . 5$ gives errors between 16% and 437%, $c _ { s } ~ = ~ 0 . 1$ gives errors less than 62%. The choice of $c _ { s }$ depends on specific application requirements, but if variance is not a concern, $0 . 1 \leq c _ { s } \leq 0 . 5$ can be recommended.

## C.4 PUBMED: Accuracy, Variance, Efficiency

Figure 9 shows the accuracy and variance on the PUBMED data set with $k = 5 .$ The average error of LSH-SS is 73% and that of RS is 117%. LSH-SS shows underestimation tendency but its STD is more than an order of magnitude smaller than that of RS. When the data set is largely dissimilar, smaller k improves accuracy. In such cases, constructing an LSH table on-the-fly can be a viable option.

![](images/c29f086718b4c6dfefa4d18a9de1ce512a3dacea743b078019169bd2c30691fd.jpg)  
(a) Relative error

![](images/5c0445a717d761477adaddaec256e0ddfc89ea0ca14987d592dcdfba9483c936.jpg)  
(b) Variance

Figure 9: Accuracy/Variance on PUBMED

<table><tr><td></td><td colspan="2">NYT</td><td colspan="2">PUBMED</td></tr><tr><td>τ</td><td>α</td><td>β</td><td>α</td><td>β</td></tr><tr><td>0.1</td><td>.710</td><td>1.85E-4</td><td>.0179</td><td>.00593</td></tr><tr><td>0.3</td><td>.710</td><td>3.26E-5</td><td>2.15E-4</td><td>6.37E-7</td></tr><tr><td>0.5</td><td>.708</td><td>9.93E-6</td><td>2.15E-4</td><td>1.40E-8</td></tr><tr><td>0.7</td><td>.705</td><td>3.25E-6</td><td>1.72E-4</td><td>2.19E-9</td></tr><tr><td>0.9</td><td>.696</td><td>6.95E-7</td><td>1.29E-4</td><td>4.98E-10</td></tr><tr><td>high th. orβ</td><td>1.8E-4</td><td>1.17E-5</td><td>6.09E-5</td><td>4.99E-6</td></tr><tr><td>low th. orβ</td><td>1.8E-4</td><td>1.8E-4</td><td>6.09E-5</td><td>6.09E-5</td></tr></table>

Table 2: α and $\beta$ in NYT and PUBMED

<table><tr><td>Symbol</td><td>Description</td></tr><tr><td>U,V</td><td>a collection of vectors, database</td></tr><tr><td>J</td><td>join size</td></tr><tr><td>n</td><td>database size, |V| = n</td></tr><tr><td>m</td><td>sample size</td></tr><tr><td>u,v</td><td>vectors</td></tr><tr><td>τ</td><td>similarity threshold</td></tr><tr><td>h</td><td>an LSH function, e.g. h1(u)</td></tr><tr><td>k</td><td># LSH functions for an LSH table</td></tr><tr><td>l</td><td># LSH tables in an LSH index</td></tr><tr><td>g</td><td>g = (h1, ..., hk), vector of LSH functions</td></tr><tr><td>Dg,Eg</td><td>an LSH table using g</td></tr><tr><td>G</td><td>G = {g1, ..., gl}</td></tr><tr><td>IG</td><td>an LSH index with using G that consists of Dg1, ..., Dgl</td></tr><tr><td>B,C</td><td>a bucket in an LSH table, e.g. B(u): the bucket of u</td></tr><tr><td>bj, ci</td><td>bucket counts, e.g. bj is the bucket count of bucket Bj</td></tr><tr><td>T</td><td>True pairs, (u,v) such that sim(u,v) ≥ τ</td></tr><tr><td>F</td><td>False pairs, (u,v) such that sim(u,v) &lt; τ</td></tr><tr><td>H</td><td>High (expected) sim. pairs that are in the same bucket</td></tr><tr><td>L</td><td>Low (expected) sim. pairs that are not in the same bucket</td></tr><tr><td>P(T|H)</td><td>given (u,v) s.t. B(u) = B(v), prob. of sim(u,v) ≥ τ</td></tr><tr><td>P(H|T)</td><td>given (u,v) s.t. sim(u,v) ≥ τ, prob. of B(u) = B(v)</td></tr><tr><td>P(T|L)</td><td>given (u,v) s.t. B(u) ≠ B(v), prob. of sim(u,v) ≥ τ</td></tr><tr><td>P(L|T)</td><td>given (u,v) s.t. sim(u,v) ≥ τ, prob. of B(u) ≠ B(v)</td></tr><tr><td>S</td><td>stratum. e.g. SH: set of pairs that are in the same bucket</td></tr><tr><td>N</td><td># pairs, e.g. NH: # pairs in the same bucket</td></tr><tr><td>M</td><td># pairs in V, M = {|V|2}</td></tr></table>

Table 3: Summary of Notations