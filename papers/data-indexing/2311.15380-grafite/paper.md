URL: https://arxiv.org/html/2311.15380v2
Content-Type: text/html
Method: native

---

---
meta-keywords: range filter,  Bloom filter,  range search,  data structure
meta-viewport: width=device-width, initial-scale=1, shrink-to-fit=no
title: "Grafite: Taming Adversarial Queries with Optimal Range Filters"
---

![](/static/base/1.0.1/images/icons/smileybones-small.svg) arXiv is now an independent nonprofit! [Learn more](https://info.arxiv.org/about) ×

[License: CC BY-NC-ND 4.0](https://info.arxiv.org/help/license/index.html#licenses-available)

arXiv:2311.15380v2 [cs.DS] 19 Mar 2024

# Grafite: Taming Adversarial Queries with Optimal Range FiltersNote: This work has been accepted for publication in Proceedings of the ACM on Management of Data (SIGMOD 2024).

CCS: Theory of computation Bloom filters and hashingCCS: Information systems Unidimensional range search

Marco Costa
Affiliation: University of Pisa, Italy email: <m.costa22@studenti.unipi.it> , Paolo Ferragina
[](https://orcid.org/0000-0003-1353-360X "ORCID 0000-0003-1353-360X") Affiliation: University of Pisa, Italy email: <paolo.ferragina@unipi.it> and Giorgio Vinciguerra
[](https://orcid.org/0000-0003-0328-7791 "ORCID 0000-0003-0328-7791") Affiliation: University of Pisa, Italy email: <giorgio.vinciguerra@unipi.it>

© none

###### Abstract.

Range filters allow checking whether a query range intersects a given set of keys with a chance of returning a false positive answer, thus generalising the functionality of Bloom filters from point to range queries.
Existing practical range filters have addressed this problem heuristically, resulting in high false positive rates and query times when dealing with adversarial inputs, such as in the common scenario where queries are correlated with the keys.

We introduce Grafite, a novel range filter that solves these issues with a simple design and clear theoretical guarantees that hold regardless of the input data and query distribution: given a fixed space budget of BB bits per key, the query time is O⁡(1)O(1), and the false positive probability is upper bounded by ℓ/2B−2\ell/2^{B-2}, where ℓ\ell is the query range size.
Our experimental evaluation shows that Grafite is the only range filter to date to achieve robust and predictable false positive rates across all combinations of datasets, query workloads, and range sizes, while providing faster queries and construction times, and dominating all competitors in the case of correlated queries.

As a further contribution, we introduce a very simple heuristic range filter whose performance on uncorrelated queries is very close to or better than the one achieved by the best heuristic range filters proposed in the literature so far.

###### Keywords:

range filter, Bloom filter, range search, data structure

## 1. Introduction

Filters are data structures that allow checking whether a query key belongs to a given set of keys, with a chance of returning a false positive answer in exchange for a small space occupancy, i.e. much smaller than the storage of the full set.

Due to their compactness and the guarantee of not returning false negatives, filters are often kept in main memory and used to prevent unnecessary and costly accesses and searches in the set.
For example, they can avoid unnecessary network communications if a remote server does not contain the sought resource, or they can avoid unnecessary disk reads when the set is stored on disk.
In fact, since their introduction by Bloom ([Bloom, 1970](#bib.bib6)) in 1970s, filters have been successfully used in networking ([Broder and Mitzenmacher, 2004](#bib.bib7)), distributed systems ([Tarkoma et al., 2012](#bib.bib36)), databases ([Dayan et al., 2018](#bib.bib11)), bioinformatics tools ([Chikhi et al., 2022](#bib.bib8)), and search engines ([Goodwin et al., 2017](#bib.bib18)), to mention just a few applications.

While the vast majority of filters are capable of answering approximate membership (point) queries ([Luo et al., 2019](#bib.bib25); [Fan et al., 2014](#bib.bib16); [Graf and Lemire, 2020](#bib.bib20); [Dillinger et al., 2022](#bib.bib14); [Porat, 2009](#bib.bib35); [Pagh et al., 2005](#bib.bib33); [Dayan et al., 2023](#bib.bib12)), a new line of research, started a decade ago ([Alexiou et al., 2013](#bib.bib2)), focused on their generalisation to *range queries*, which occur frequently in big data systems such as key-value stores ([Goswami et al., 2014](#bib.bib19); [Zhang et al., 2018](#bib.bib41); [Luo et al., 2020](#bib.bib26); [Vaidya et al., 2022](#bib.bib37); [Knorr et al., 2022](#bib.bib22)). In this case, the filtering problem can be formally stated as follows.

*Figure 1. Grafite is the only range filter to date that is both effective (low false positive rate) and efficient (low query time) as the endpoints of the query range get closer to the data.*

###### Problem 1 (Approximate range emptiness).

Given a set SS of nn keys drawn from an integer universe [u]={0,…,u−1}[u]=\\{0,\ldots,u-1\\}, build a space-efficient data structure, called *range filter*, that answers range emptiness queries of the form [a,b]∩S≠∅​?[a,b]\cap S\neq\emptyset\\,? for any a≤ba\leq b in [u][u]. The range filter is allowed to return “not empty” when actually [a,b]∩S=∅[a,b]\cap S=\emptyset (i.e. a false-positive error) with probability at most ε\varepsilon.

From a theoretical point of view, this problem was solved optimally by Goswami et al. ([Goswami et al., 2014](#bib.bib19)), which first proved a space lower bound of Ω⁡(log⁡Lε)−O⁡(1)\Omega(\log\tfrac{L}{\varepsilon})-O(1) bits per key, where LL is an upper bound on the query range size, and then they gave a data structure for the ww-bit word RAM model matching this space up to a lower order additive term and offering constant-time queries when w=Ω⁡(log⁡n​Lε)w=\Omega(\log\tfrac{nL}{\varepsilon}).

From a practical point of view, the literature offers a vast choice of range filters, such as ARF ([Alexiou et al., 2013](#bib.bib2)), SuRF ([Zhang et al., 2018](#bib.bib41)), Rosetta ([Luo et al., 2020](#bib.bib26)), SNARF ([Vaidya et al., 2022](#bib.bib37)), Proteus ([Knorr et al., 2022](#bib.bib22)), bloomRF ([Mößner et al., 2023](#bib.bib28)), and REncoder ([Wang et al., 2023](#bib.bib39)).
These solutions, reviewed in Section [2](#S2 "2. Related Work ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters"), adopt totally different approaches to range filtering, thus offering a large number of trade-offs among space, *empirical* probability of a false positive error (henceforth, false positive rate), query time, and construction time.
This notwithstanding, there is still one fundamental challenge that the literature has not yet been able to address:

No practical solution is robust enough to efficiently handle all input data and query distributions.

Existing practical range filters, indeed, adopt heuristic designs that sacrifice performance guarantees to improve upon some specific inputs.
In fact, these range filters hardly guarantee a bounded false positive probability ε\varepsilon for a given amount of space, thus, strictly speaking, they do not solve the approximate range emptiness problem unless some specific (and strong) assumptions on the kind of query workload and input data distribution are met.

As a consequence of this, there exist *adversarial distributions* that can drive the false positive rate arbitrarily close to 1, thus making the filter useless, if not dangerous for the big data systems making use of it (e.g. because of increased disk or network activity that the filter was actually deployed to prevent).
The importance of this issue has also been stressed by Knorr et al. ([Knorr et al., 2022](#bib.bib22)), who after presenting a formal framework of existing range filters, conclude that “no current design can handle [adversarial workloads] practically, suggesting the need for further expansion of the range filter design space.”

Notably, the vast majority of range filters suffer from the so-called *correlation* between keys and queries, that is, they provide little or no filtering at all when an endpoint of the query range is close to one of the keys in the input set, which is quite disappointing given the commonness of such a workload in applications that care about the local properties of data (such as in time series applications where we need to check if some events occurred in a time frame) ([Luo et al., 2020](#bib.bib26)), or given that malicious users can artificially issue these queries with just the knowledge of (a subset of) the keys.
To demonstrate this issue, we show in Figure [1](#S1.F1 "Figure 1 ‣ 1. Introduction ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters") how existing range filters quickly reach high false positive rates as the endpoints of the query range get closer to the input keys, denoted as “correlation degree” on the horizontal axis (and detailed in our experimental section).
This holds true for SuRF, SNARF, REncoder, and Proteus, the latter even being auto-tuned on (i.e. overfitted to) the query workload.
The only exception is Rosetta which has a constant false positive rate but a query time that is up to orders of magnitude higher compared to the other filters.

Apart from the lack of robustness, there is another challenge:

Current range filters are complex to evaluate and deploy because of their complicated design.

As stated above, existing range filters adopt complex design choices aimed at increasing their efficacy on some specific inputs.
For instance, SuRF ([Zhang et al., 2018](#bib.bib41)) encodes a trie with input keys truncated at their distinguishing prefix (thus providing better filtering when there is no correlation), while SNARF ([Vaidya et al., 2022](#bib.bib37)) maps each input key to a 1-bit in a bitvector via a model learned from the data (thus providing better filtering when there are no outliers or poisoned data ([Kornaropoulos et al., 2022](#bib.bib23))).

These designs, coupled with the lack of guaranteed bounds on the false positive rate, hinder our understanding of how the range filter will behave once deployed to production unless future data and queries will follow exactly the same distribution of the test data on which the *empirical* false positive rate was originally observed.
The ability to auto-tune on a sample of queries and input keys, as in Proteus ([Knorr et al., 2022](#bib.bib22)), only partially eases the hard job of integrating a range filter into a real system, as there is still the necessity to keep a proper set of sample queries (thus also allocating further space) and to detect when the filter needs to be rebuilt because of workload shifts (thus introducing additional delays and requiring to keep the input data in memories close to where the range filter is built).

Instead, we aspire to a practical range filter that, similarly to Bloom filters, works robustly out of the box regardless of the input data and future queries, while hiding the complexities of its design and exposing just simple knobs such as the false positive probability ε\varepsilon or a space budget.

Our contributions.

- •

  We introduce *Grafite*, a novel practical range filter that solves the lack of robustness and the high complexity of current solutions. Unlike all the practical range filters to date, Grafite offers clear guarantees that hold regardless of the input data and query distributions: given a fixed space budget of BB bits per key, the query time is O⁡(1)O(1), and the false positive probability is upper bounded by min⁡{1,ℓ/2B−2}\min\\{1,\ell/2^{B-2}\\}, where ℓ\ell is the query range size. Perhaps surprisingly, this is achieved via a simple design that maps the input keys into a smaller universe via a properly designed hash function ([Goswami et al., 2014](#bib.bib19)), stores the resulting hash codes space-efficiently ([Elias, 1974](#bib.bib15); [Fano, 1971](#bib.bib17)), and checks hash codes for inclusion in a range via an efficient query algorithm.

- •

  We provide a comprehensive related work section and propose the first theoretical comparison of the space-time performance of range filters, showing the superiority of Grafite over prior solutions.

- •

  We perform the largest experimental comparison among range filters, both in terms of dataset size and in the number of tested solutions, which shows that *all* the existing filters provide little to no filtering or a high query time in the case of correlated query workloads.
  Instead, Grafite is the only range filter to date to achieve a robust and predictable false positive rate across all combinations of datasets, query workloads, and range sizes, while also providing faster queries and construction times, and dominating all competitors in the case of correlated query workloads.

- •

  For datasets and uncorrelated query workloads previously tested in the literature, we show that there exists a very simple heuristic filter design — that we name *Bucketing* — that essentially matches the filtering effectiveness of all the existing heuristic range filters, which are however significantly more complex and incur in higher query and construction times. This demonstrates that, if we give up on robustness guarantees, the approximate range emptiness problem can sometimes be addressed with a very simple solution.

Paper outline. Section [2](#S2 "2. Related Work ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters") discusses existing range filters.
Section [3](#S3 "3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters") introduces Grafite.
Section [4](#S4 "4. Bucketing: A Heuristic Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters") introduces Bucketing.
Section [5](#S5 "5. Theoretical Comparison ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters") compares the space-time bounds of Grafite with those of existing range filters.
Section [6](#S6 "6. Experiments ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters") experiments with Grafite, Bucketing and existing range filters.
Section [7](#S7 "7. Conclusion ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters") concludes the paper and suggests some open problems.

## 2. Related Work

Consider an upper bound LL on the query range size b−a+1b-a+1. We can provide a trivial solution to the approximate range emptiness problem by using point filters which, given a false positive probability of γ\gamma, can be implemented in n​log⁡1γ+O⁡(n)n\log\tfrac{1}{\gamma}+O(n) bits of space and O⁡(1)O(1) query time ([Pagh et al., 2005](#bib.bib33); [Porat, 2009](#bib.bib35)).
Indeed, by building a point filter on the input set SS with false positive probability γ=ε/L\gamma=\varepsilon/L, we can check the existence of any element of [a,b][a,b] in SS by executing at most LL point queries. This solution takes n​log⁡Lε+O⁡(n)n\log\tfrac{L}{\varepsilon}+O(n) bits of space, O⁡(L)O(L) query time, and the false positive probability is at most ε\varepsilon by union bound.

The question is now how far is this trivial solution from being optimal. The answer was given by Goswami et al. ([Goswami et al., 2014](#bib.bib19)), which proved the following lower bound.

###### Theorem 2.1 (([Goswami et al., 2014](#bib.bib19))).

Any data structure solving approximate range emptiness queries of fixed length L≤u/(5​n)L\leq u/(5n) on nn keys drawn from an integer universe [u]={0,…,u−1}[u]=\\{0,\ldots,u-1\\} with a false positive probability of ε\varepsilon must use at least n​log⁡(L1−O⁡(ε)ε)−O⁡(n)n\log\big(\tfrac{L^{1-O(\varepsilon)}}{\varepsilon}\big)-O(n) bits of space.

This is a disappointing result because it states that, for a sufficiently small ε\varepsilon, at least log⁡Lε\log\tfrac{L}{\varepsilon} bits per key are needed.
Hence, the larger is LL and/or the smaller is ε\varepsilon, the larger is the space required by any range filter.
Note that we can restrict L≤u​ε/nL\leq u\varepsilon/n, since otherwise it is more convenient to store the input keys in space close to log⁡un\log\tfrac{u}{n} bits per key (e.g. with an Elias-Fano encoding ([Elias, 1974](#bib.bib15); [Fano, 1971](#bib.bib17))) thus solving the problem without false positives (i.e. ε=0\varepsilon=0).

Furthermore, Theorem [2.1](#S2.Thmtheorem1 "Theorem 2.1 (( , )). ‣ 2. Related Work ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters") implies that we cannot improve the space occupancy of the trivial solution stated above, but it challenges us to find a solution that matches its same space bound whilst improving the unattractive O⁡(L)O(L) query time. In this respect, Goswami et al. ([Goswami et al., 2014](#bib.bib19)) also introduce a data structure that solves the range emptiness problem in n​log⁡Lε+o⁡(n​log⁡Lε)+O⁡(n)n\log\tfrac{L}{\varepsilon}+o{(n\log\tfrac{L}{\varepsilon})}+O(n) bits of space, while offering O⁡((log⁡n​Lε)/w)O((\log\tfrac{nL}{\varepsilon})/w) query time in the ww-bit word RAM model, thus achieving O⁡(1)O(1) query time when w=Ω⁡(log⁡n​Lε)w=\Omega(\log\tfrac{nL}{\varepsilon}).
The overall approach is mainly theoretic in nature and thus very complicated to implement. Nevertheless, the idea in ([Goswami et al., 2014](#bib.bib19)) to reduce the original universe UU into a smaller universe h⁡(U)h(U) via a proper hash function hh is effective, and it will be used in Grafite too.

We now turn our attention to practical range filters.

Prefix Bloom Filter. A Prefix Bloom Filter hashes key prefixes of a predetermined bit-length ll within a Bloom filter ([Dharmapurikar et al., 2003](#bib.bib13); [Matsunobu et al., 2020](#bib.bib27)).
Since each prefix encodes a range of the universe of size 2log⁡u−l2^{\log u-l}, the filter can answer a range emptiness query by probing each range (i.e. configuration of ll bits) that overlaps with the query range, and returning “empty” if all the probes return false, “not empty” otherwise.
We do not further consider Prefix Bloom Filters because they are generalised by Rosetta ([Luo et al., 2020](#bib.bib26)) and Proteus ([Knorr et al., 2022](#bib.bib22)), which are described below and used in our experimental comparison.

ARF. The Adaptive Range Filter (ARF) ([Alexiou et al., 2013](#bib.bib2)) is based on a compactly-encoded binary tree whose leaves represent ranges of the universe and are associated with a flag indicating whether there is at least one key in that range.
Internal nodes allow navigating to the leaf containing the left endpoint aa of the query range [a,b][a,b], and the leaves to its right are inspected until either one of them has a true flag, thus the answer is “not empty”, or the leaf covering bb is reached and has a false flag, thus the answer is “empty”.
ARF adapts to the data and query distribution by learning from false positive queries and adjusting its shape accordingly.
As reported in ([Zhang et al., 2018](#bib.bib41)), ARF can be up to 1300×\times larger than SuRF, described next, while also exhibiting a higher false positive rate. Thus, we do not further consider ARF.

SuRF. The Succinct Range Filter (SuRF) ([Zhang et al., 2018](#bib.bib41); [Zhang et al., 2020](#bib.bib42); [Zhang et al., 2021](#bib.bib43)) is built upon a compactly-encoded trie, called Fast Succinct Trie, that stores, for each key s∈Ss\in S, the shortest prefix psp_{s} of ss such that ss can be uniquely identified among all the strings in SS, followed by a number mm of suffix bits following that key prefix.
For improving the filter performance on just point queries, these mm bits can also be set to a hash of the key.
A range emptiness query on [a,b][a,b] is answered by looking in the truncated trie for the smallest key kk (which can include its suffix bits if the search reaches a leaf) such that kk is lexicographically ≥a\geq a. The result of the query is given by the result of the lexicographic comparison k≤bk\leq b.
We use SuRF in our experimental comparison.

Rosetta. The Robust Space-Time Optimized Range Filter (Rosetta) ([Luo et al., 2020](#bib.bib26)) consists of log⁡u\log u Bloom filters organised in levels. For every key, each of its prefixes of length k=1,…,log⁡uk=1,\dots,\log u is inserted into the Bloom filter at level kk.
A range emptiness query is answered by decomposing the query range into dyadic intervals, which are used to probe the corresponding Bloom filters.
If all the probes return a negative answer, the query range is empty. Otherwise, each range that returned a positive answer is recursively decomposed at the next level.
In practice, Rosetta tunes itself to minimise the false positive rate under a given space budget by allocating more bits to the Bloom filters that are probed more frequently, based on a sample of the query workload.
We use Rosetta in our experimental comparison.

SNARF. The Sparse Numerical Array-Based Range Filter (SNARF) ([Vaidya et al., 2022](#bib.bib37)) uses a bit array BB of K​nKn bits, where KK is a suitably large parameter that impact on the false positive rate, and a function f⁡(x)=⌊MCDF​(x)⋅K​n⌋f(x)=\lfloor\text{MCDF}{(x)}\cdot Kn\rfloor, where MCDF is a monotonic estimate of the CDF of the keys in SS, i.e. ⌊MCDF​(x)⋅n⌋\lfloor\text{MCDF}(x)\cdot n\rfloor is an estimate of the rank of xx in SS. The function ff is built by taking one key every tt keys in the sorted SS, and using these key samples as endpoints of linear splines. The bit array BB, initially empty, is filled with B⁡[f⁡(x)]=1B[f(x)]=\texttt{1} for each x∈Sx\in S, and then compressed. A range emptiness query [a,b][a,b] returns “not empty” if and only if there is at least a 1-bit in the range B⁡[f⁡(a),f⁡(b)]B[f(a),f(b)].
We use SNARF in our experimental comparison.

Proteus. Proteus ([Knorr et al., 2022](#bib.bib22)) combines the trie-based prefix filtering of the Fast Succinct Trie with the filtering of the Prefix Bloom Filter.
Differently from SuRF, Proteus does not encode in the trie a unique prefix for every key but rather all unique key prefixes of a fixed length l1l_{1}, and it implements a single (prefix) Bloom filter for all key prefixes of length l2>l1l_{2}>l_{1}.
If the range emptiness query is not resolved after descending the trie up to the prefix length l1l_{1}, i.e. if there are matching leaves so that we cannot yet return “not empty”, then the Prefix Bloom Filter is probed for each length-l2l_{2} prefix extending the length-l1l_{1} prefix of each matching leaf, returning “empty” if all these probes return false, “not empty” otherwise.
The values of l1l_{1} and l2l_{2} are determined by an algorithm that minimises the false positive rate given the input keys, a sample query workload, and a space budget.
We use Proteus in our experimental comparison.

bloomRF. The Bloom Range Filter (bloomRF) ([Mößner et al., 2023](#bib.bib28)) hashes a key into a hash code composed of positions that are used to set bits to 1 in a bit array BB.
The hash code is such that equal key prefixes have equal hash code prefixes (thus encoding range information in the hash code), and its position components preserve the order of prefixes (thus improving data locality).
A query range is decomposed into dyadic intervals whose emptiness is determined by checking in BB the appropriate bits computed via the hash code above.
We could not experiment with bloomRF because its implementation is not yet open source,11 1 Personal communication with the authors. but we comment on it in the theoretical comparison of Section [5](#S5 "5. Theoretical Comparison ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters").

REncoder. The Range Encoder (REncoder) ([Wang et al., 2023](#bib.bib39)) too consists of a bit array BB, initially empty. It splits each input key into a 4-bit suffix ss and the remaining prefix pp.
The suffix ss is conceptually represented by a leaf in a complete binary tree with 16 leaves, whose nodes in the path from that leaf to the root are marked with a 1, and the remaining nodes are marked with a 0.
Intuitively, nodes represent ranges of the universe and the bit marks record the presence of keys in a range.
The bit marks are then concatenated to form a 32-bit value, which is written into B⁡[hi​(p),hi​(p)+31]B[h_{i}(p),h_{i}(p)+31] via an OR operation, where hih_{i} is a hash function, for i=1,…,ki=1,\dots,k. The process is then repeated on the prefix pp of the key, and it stops when the whole key has been processed.
A query range is decomposed into dyadic intervals whose emptiness is determined via traversals of binary trees, which are recovered from BB via AND operations.
We use REncoder in our experimental comparison.

We conclude this section by mentioning that the problem of supporting efficient in-place insertions has only been touched upon in the literature. Indeed, current range filters are difficult to update efficiently due to their use of static compactly-encoded tries (SuRF and Proteus), or learned functions and compressed bitvectors (SNARF).
Some other range filters like Prefix Bloom Filters, Rosetta, bloomRF and REncoder, instead, could be easier to update with new keys due to their design (loosely) based on Bloom filters, but the impact of insertions on the false positive rate has not yet been explored. Since in this paper we do not deal with these issues, we leave it as an open problem ([Dayan et al., 2023](#bib.bib12)).

## 3. Grafite: An Optimal Range Filter

We now introduce Grafite, which eventually solves the lack of robustness in state-of-the-art range filters.
We start from the idea of Goswami et al. ([Goswami et al., 2014](#bib.bib19)) to solve the approximate range emptiness problem through hashing, and we take this idea into a simpler, practical and yet more succinct solution that is closer to the lower bound of Theorem [2.1](#S2.Thmtheorem1 "Theorem 2.1 (( , )). ‣ 2. Related Work ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters").

Hashing input keys. Recall we are given a set SS of nn keys in a universe [u]={0,…,u−1}[u]=\\{0,\dots,u-1\\}, a false positive probability ε\varepsilon, and an upper bound LL on the query range size.

Set r=n​L/εr=nL/\varepsilon, and let q:[u/r]→[r]q\colon[u/r]\to[r] be a hash function taken from a pairwise-independent family QQ, i.e. a set of hash functions Q={q:[u/r]→[r]}Q=\\{q\colon[u/r]\to[r]\\} such that, for any pair of distinct keys (x1,x2)∈[u/r]2(x_{1},x_{2})\in[u/r]^{2} and any pair of (not necessarily distinct) hash codes (y1,y2)∈[r]2(y_{1},y_{2})\in[r]^{2}, we have

|  | Prq∈Q[q(x1)=y1∧q(x2)=y2]=1r2.\Pr_{q\in Q}\left[q(x_{1})=y_{1}\land q(x_{2})=y_{2}\right]=\frac{1}{r^{2}}. |  |
| --- | --------------------------------------------------------------------------------------------------------- | --- |

The simplest technique for constructing such a hash function ([Wegman and Carter, 1981](#bib.bib40)) is to select a large prime number p>rp>r and two random numbers c1,c2<pc_{1},c_{2}<p such that c1≠0c_{1}\neq 0, and then define the hash function as q⁡(x)=((c1​x+c2)modp)modrq(x)=((c_{1}x+c_{2})\bmod{p})\bmod{r}.

In addition to qq, we define a hash function hh that preserves the locality of the hashed items and has a small collision probability ([Goswami et al., 2014](#bib.bib19)):

| (1) |  | h⁡(x)=(q⁡(⌊x/r⌋)+x)modr.h(x)=\left(q{\left(\lfloor x/r\rfloor\right)}+x\right)\bmod{r}. |  |
| --- | --- | --------------------------------------------------------------------------------------- | --- |

We use hh to transform the set S={x1,…,xn}S=\\{x_{1},\dots,x_{n}\\} of input keys from the original universe [u][u] to the set h⁡(S)={h⁡(x1),…,h⁡(xn)}h(S)=\\{h(x_{1}),\dots,h(x_{n})\\} of hash codes in the reduced universe [r][r], and then we store h⁡(S)h(S) via a compact non-approximate range emptiness data structure.

We will describe the data structure in a moment. For now, we notice that because of the hash function hh, a range emptiness query [a,b]∩S≠∅​?[a,b]\cap S\neq\emptyset\\,? can be answered by verifying the existence of a value h⁡(x)∈h⁡(S)h(x)\in h(S) such that

| (2) |  | {h⁡(a)≤h⁡(x)≤h⁡(b)if ​h​(a)≤h⁡(b),h⁡(x)≤h⁡(b)∨h⁡(x)≥h⁡(a)otherwise.\begin{cases}h(a)\leq h(x)\leq h(b)&\text{if }h(a)\leq h(b),\\\\ h(x)\leq h(b)\lor h(x)\geq h(a)&\text{otherwise.}\end{cases} |  |
| --- | --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | --- |

The first case is straightforward, while the second one occurs if there is an overlap of the hashed endpoints (i.e. h⁡(a)>h⁡(b)h(a)>h(b)) as a consequence of the modulo and the reduced universe.
If a value h⁡(x)h(x) which satisfies ([2](#S3.E2 "In 3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")) is found, we answer ‘‘not empty’’. Otherwise, we answer ‘‘empty’’.22 2 In the special case the query range [a,b][a,b] crosses one of the u/ru/r boundaries, i.e. ⌊a/r⌋\lfloor a/r\rfloor = ⌊b/r⌋−1\lfloor b/r\rfloor-1, we split it into [a,a′][a,a^{\prime}] and [b′,b][b^{\prime},b], where a′a^{\prime} is the largest value such that ⌊a′/r⌋=⌊a/r⌋\lfloor a^{\prime}/r\rfloor=\lfloor a/r\rfloor and b′b^{\prime} is the smallest value such that ⌊b′/r⌋=⌊b/r⌋\lfloor b^{\prime}/r\rfloor=\lfloor b/r\rfloor, namely, b′=b−(bmodr)b^{\prime}=b-(b\bmod r) and a′=b′−1a^{\prime}=b^{\prime}-1. Then, the two range emptiness queries are solved by applying the conditions ([2](#S3.E2 "In 3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")).

It should be clear that there can be no false negatives. For the false positives, there is the following result (which is a straightforward generalisation of a result in ([Goswami et al., 2014](#bib.bib19))).

###### Lemma 3.1 (([Goswami et al., 2014](#bib.bib19))).

The approach based on the hash function ([1](#S3.E1 "In 3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")) and the conditions ([2](#S3.E2 "In 3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")) guarantees a false positive probability of at most ε\varepsilon for query ranges of size LL, and at most ℓ​ε/L\ell\varepsilon/L for ranges of size ℓ≤L\ell\leq L.

###### Proof.

A false positive occurs when no key in SS is in the query range II but there is a hash collision between a key x∈Sx\in S and a point y∈Iy\in I.
From ([Goswami et al., 2014](#bib.bib19), Lemma 3.1), such a collision happens with probability Pr[h(x)=h(y)]≤1/r{\Pr[h(x)=h(y)]\leq 1/r}.
The false positive probability is then given by a union bound over all possible collisions between keys in SS, which are nn, and points in II, which are ℓ≤L\ell\leq L, thus it is

|  | ∑x∈S∑y∈IPr[h(x)=h(y)]≤∑x∈S∑y∈I1r=n​ℓr=n​ℓn​Lε=ℓ​εL≤ε.∎\sum_{x\in S}{\sum_{y\in I}{\Pr\left[h(x)\\!=\\!h(y)\right]}}\leq\sum_{x\in S}{\sum_{y\in I}\frac{1}{r}}=\frac{n\ell}{r}=\frac{n\ell}{\tfrac{nL}{\varepsilon}}=\frac{\ell\varepsilon}{L}\leq\varepsilon.\qed |  |
| --- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --- |

Storing hash codes succinctly. Having defined how approximate range emptiness can be achieved through hashing, the following step is to store the hash codes h⁡(S)h(S).
Goswami et al. ([Goswami et al., 2014](#bib.bib19)) store the hash codes together with a sophisticated prefix search data structure from ([Belazzougui et al., 2010](#bib.bib5)) to check hash codes for inclusion in a query range.
Our study proposes a much simpler data structure that builds on the classic Elias-Fano integer code ([Elias, 1974](#bib.bib15); [Fano, 1971](#bib.bib17)) together with an efficient procedure to check hash codes for inclusion in a range, explained below.
As we will show, we will obtain a practical range filter, which actually has an even better space than the solution of ([Goswami et al., 2014](#bib.bib19)).

*Figure 2. An example of Grafite storing the compressed hash codes 𝟔,𝟏𝟒,𝟑𝟐,𝟓𝟏,𝟓𝟑,𝟓𝟓,𝟔𝟔,𝟕𝟎,𝟗𝟏,𝟗𝟒6,14,32,51,53,55,66,70,91,94 (see Example [3.2](#S3.Thmtheorem2 "Example 3.2. ‣ 3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")), and some steps needed for answering a range emptiness query (see Example [3.3](#S3.Thmtheorem3 "Example 3.3. ‣ 3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")).*

Let z1,…,znz_{1},\dots,z_{n} be the deduplicated sorted set of hash codes in h⁡(S)h(S).33 3 Due to collisions there can be fewer than nn distinct hash codes, but we prefer using nn in our description and bounds for simplicity. The space without the kk duplicates is k​log⁡Lεk\log\tfrac{L}{\varepsilon} bits lower, where the expected value of kk can be shown to be ε⁡(n−1)/(2​L)\varepsilon(n-1)/(2L) with classic hash tables analyses under the simple uniform hashing assumption ([Cormen et al., 2022](#bib.bib10)). We split the length-⌈log⁡r⌉\lceil\log r\rceil binary representation of each ziz_{i} into a low part zi𝑙𝑜\cramped{z_{i}^{\mathit{lo}}} consisting of the l=⌊log⁡rn⌋=⌊log⁡Lε⌋l=\lfloor\log\tfrac{r}{n}\rfloor=\lfloor\log\tfrac{L}{\varepsilon}\rfloor least significant bits of ziz_{i}, and a high part ziℎ𝑖\cramped{z_{i}^{\mathit{hi}}} consisting of the remaining ⌈log⁡r⌉−l\lceil\log r\rceil-l most significant bits of ziz_{i}.
The low parts are concatenated into a vector V⁡[1,n]V[1,n] of ll-bit cells, which thus takes n​l=n​⌊log⁡Lε⌋nl=n\lfloor\log\tfrac{L}{\varepsilon}\rfloor bits overall.
The high parts are encoded in a bitvector H⁡[1,znℎ𝑖+n+1]H[1,\cramped{z_{n}^{\mathit{hi}}}+n+1] where the positions ziℎ𝑖+i\cramped{z_{i}^{\mathit{hi}}}+i are set to 1, and the remaining positions are set to 0.
This completes the succinct encoding of h⁡(S)h(S), whose bit-size can be shown to be upper bounded by n​log⁡Lε+2​nn\log\tfrac{L}{\varepsilon}+2n.

###### Example 3.2.

Suppose we are given the set S={9,48,50,191,226,269,335,446,487,511}S=\\{9,48,50,191,\allowbreak 226,269,335,446,487,511\\} with n=10n=10 keys, and the values L=4L=4, ε=0.4\varepsilon=0.4, thus giving r=n​L/ε=100r=nL/\varepsilon=100. Say we use the hash function h⁡(x)=(q⁡(⌊x/r⌋)+x)modrh(x)=(q(\lfloor x/r\rfloor)+x)\bmod r, where we choose q⁡(x)=((c1​x+c2)modp)modrq(x)=((c_{1}x+c_{2})\bmod p)\bmod r with parameters p=231−1p=2^{31}-1, c1=10c_{1}=10, and c2=5c_{2}=5, thus giving the set of hash codes h⁡(S)={14,53,55,6,51,94,70,91,32,66}h(S)=\\{14,53,55,6,51,\allowbreak 94,70,91,32,66\\}.
Figure [2](#S3.F2 "Figure 2 ‣ 3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters") shows the binary representation of the integers z1=6,…,94=znz_{1}=6,\dots,94=z_{n} corresponding to the sorted set h⁡(S)h(S) at the top (where the low l=⌊log⁡Lε⌋=3l=\lfloor\log\tfrac{L}{\varepsilon}\rfloor=3 bits and the remaining 44 bits are highlighted with different colours) and their Elias-Fano encoding via the vectors HH and VV at the bottom.

Searching hash codes efficiently. We now describe how to search within the hash codes h⁡(S)h(S) so that both conditions in ([2](#S3.E2 "In 3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")) can be checked efficiently.

For the first branch in ([2](#S3.E2 "In 3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")), we need to augment the Elias-Fano encoding with an operation that checks for the existence of an h⁡(x)h(x) such that h⁡(a)≤h⁡(x)≤h⁡(b)h(a)\leq h(x)\leq h(b).
To this end, we use the well-known 𝑝𝑟𝑒𝑑𝑒𝑐𝑒𝑠𝑠𝑜𝑟⁡(y)\mathit{predecessor}(y) operation, which given y∈[r]y\in[r], returns the largest element zkz_{k} smaller than or equal to yy ([Navarro and Rojas-Ledesma, 2020](#bib.bib30)).
We first compute zk=𝑝𝑟𝑒𝑑𝑒𝑐𝑒𝑠𝑠𝑜𝑟⁡(h⁡(b))z_{k}=\mathit{predecessor}(h(b)) and then check if zk≥h⁡(a)z_{k}\geq h(a). If this is the case, then there is at least a hash code zk=h⁡(x)z_{k}=h(x) in the range [h⁡(a),h⁡(b)][h(a),h(b)], where x∈Sx\in S. Thus the first branch is satisfied, and the answer to the approximate range emptiness query is “not empty”. If not, i.e. zk<h⁡(a)≤h⁡(b)z_{k}<h(a)\leq h(b), then the first branch is not satisfied and the answer is “empty”.

The 𝑝𝑟𝑒𝑑𝑒𝑐𝑒𝑠𝑠𝑜𝑟⁡(y)\mathit{predecessor}(y) operation is implemented by first identifying the range of hash codes zi,…,zjz_{i},\dots,z_{j} that share the same high part yℎ𝑖\cramped{y^{\mathit{hi}}} of yy, and then by binary searching in the subarray V⁡[i,j]=[zi𝑙𝑜,…,zj𝑙𝑜]V[i,j]=[\cramped{z_{i}^{\mathit{lo}}},\dots,\cramped{z_{j}^{\mathit{lo}}}] for the predecessor of y𝑙𝑜\cramped{y^{\mathit{lo}}}.
Since V⁡[i,j]V[i,j] might contain at most 2l\cramped{2^{l}} configurations of ll-bit integers, the binary search runs in O⁡(log⁡2l)=O⁡(log⁡Lε)O(\log 2^{l})=O(\log\tfrac{L}{\varepsilon}) time.
Let us detail these steps for completeness ([Okanohara and Sadakane, 2007](#bib.bib31); [Vigna, 2013](#bib.bib38); [Ottaviano and Venturini, 2014](#bib.bib32); [Navarro, 2016](#bib.bib29)).
For identifying the range [i,j][i,j], we need the 𝑠𝑒𝑙𝑒𝑐𝑡b​(k)\mathit{select}_{b}(k) operation, which returns the position of the kkth bb-bit in HH, for b∈{0,1}b\in\\{\texttt{0},\texttt{1}\\} (we define 𝑠𝑒𝑙𝑒𝑐𝑡b​(0)=0\mathit{select}_{b}(0)=0). This operation can be implemented in O⁡(1)O(1) time using o⁡(|H|)=o⁡(n)o(|H|)=o(n) bits ([Jacobson, 1989](#bib.bib21); [Clark, 1996](#bib.bib9)).
Then, by definition of HH, the hash codes zi,…,zjz_{i},\dots,z_{j} with the same high part p=yℎ𝑖p=\cramped{y^{\mathit{hi}}} form a contiguous sequence of 1-bits in the subarray H⁡[p+i,p+j]H[p+i,p+j] followed by a 0 in H⁡[p+j+1]H[p+j+1].
Thus, the subarray H⁡[p+i,p+j+1]=1j−i+1​0H[p+i,p+j+1]=\texttt{1}^{j-i+1}\texttt{0} corresponds to the subarray H⁡[𝑠𝑒𝑙𝑒𝑐𝑡0​(p)+1,𝑠𝑒𝑙𝑒𝑐𝑡0​(p+1)]H[\mathit{select}_{\texttt{0}}(p)+1,\mathit{select}_{\texttt{0}}(p+1)], and hence the range [i,j][i,j] is given by setting the endpoints of these subarrays equal, thus yielding i=𝑠𝑒𝑙𝑒𝑐𝑡0​(p)−p+1i=\mathit{select}_{\texttt{0}}(p)-p+1 and j=𝑠𝑒𝑙𝑒𝑐𝑡0​(p+1)−p−1j=\mathit{select}_{\texttt{0}}(p+1)-p-1.
Finally, in the corner case that the binary search in V⁡[i,j]V[i,j] finds that the predecessor of yy is zi−1z_{i-1}, we recover this element via a random access operation: the high part of zi−1z_{i-1} is retrieved as zi−1ℎ𝑖=𝑠𝑒𝑙𝑒𝑐𝑡1​(i−1)−(i−1)\cramped{z_{i-1}^{\mathit{hi}}}=\mathit{select}_{1}(i-1)-(i-1), and the low part is readily available as zi−1𝑙𝑜=V⁡[i−1]\cramped{z_{i-1}^{\mathit{lo}}}=V[i-1].

For the second branch in ([2](#S3.E2 "In 3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")), it is enough to random access the smallest and largest value in h⁡(S)h(S), i.e. z1z_{1} and znz_{n}, and do the comparison z1≤h⁡(b)∨zn≥h⁡(a)z_{1}\leq h(b)\lor z_{n}\geq h(a).

Algorithms [1](#algorithm1 "In 3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters") and [2](#algorithm2 "In 3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters") contain the pseudocode for the construction and query algorithms on Grafite, respectively. For the construction, we notice that BuildEliasFano runs in linear time, while Sort takes the time to sort nn integers of length ⌈log⁡r⌉\lceil\log r\rceil, for which there exist very efficient sequential and parallel algorithms ([Axtmann et al., 2022](#bib.bib4)).

###### Example 3.3.

Let us solve the range emptiness query for the range [44,47][44,47] on the Grafite instance of Example [3.2](#S3.Thmtheorem2 "Example 3.2. ‣ 3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters") and Figure [2](#S3.F2 "Figure 2 ‣ 3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters"). First, we compute h⁡(a)=h⁡(44)=49h(a)=h(44)=49 and h⁡(b)=h⁡(47)=52h(b)=h(47)=52. Then, since h⁡(a)≤h⁡(b)h(a)\leq h(b), we need to compute zk=𝑝𝑟𝑒𝑑𝑒𝑐𝑒𝑠𝑠𝑜𝑟⁡(h⁡(b))z_{k}=\mathit{predecessor}(h(b)) and do the check zk≥h⁡(a)z_{k}\geq h(a). As depicted in Figure [2](#S3.F2 "Figure 2 ‣ 3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters"), we find such a zkz_{k} by: ![1] taking the high part p=𝟶𝟷𝟷𝟶2=6p=\mathtt{0110}_{2}=6 of h⁡(b)=52h(b)=52 and computing 𝑠𝑒𝑙𝑒𝑐𝑡0​(p)=9\mathit{select}_{0}(p)=9 and 𝑠𝑒𝑙𝑒𝑐𝑡0​(p+1)=13\mathit{select}_{0}(p+1)=13; ![2] computing i=𝑠𝑒𝑙𝑒𝑐𝑡0​(p)−p+1=4i=\mathit{select}_{0}(p)-p+1=4 and j=𝑠𝑒𝑙𝑒𝑐𝑡0​(p+1)−p−1=6j=\mathit{select}_{0}(p+1)-p-1=6; ![3] doing a binary search in V⁡[i,j]V[i,j] for the predecessor of the low part 𝟷𝟶𝟶2=4\mathtt{100}_{2}=4 of h⁡(b)h(b), which yields V⁡[4]=𝟶𝟷𝟷2V[4]=\mathtt{011}_{2}. This reveals that 𝑝𝑟𝑒𝑑𝑒𝑐𝑒𝑠𝑠𝑜𝑟⁡(h⁡(b))=z4=51\mathit{predecessor}(h(b))=z_{4}=51, and since z4≥h⁡(a)=49z_{4}\geq h(a)=49, we return “not empty”, which is a false positive error since [44,47]∩S=∅[44,47]\cap S=\emptyset.

Combining the above description of Grafite with Lemma [3.1](#S3.Thmtheorem1 "Lemma 3.1 (( , )). ‣ 3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters"), and recalling the restriction on the range size L≤u​ε/nL\leq u\varepsilon/n (see Theorem [2.1](#S2.Thmtheorem1 "Theorem 2.1 (( , )). ‣ 2. Related Work ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters") and the discussion below it), we can state the following result.

###### Theorem 3.4.

Given a set of nn keys from a universe [u][u], ε∈(0,1)\varepsilon\in(0,1), and L∈[1,u​ε/n]L\in[1,u\varepsilon/n], Grafite takes n​log⁡Lε+2​n+o⁡(n)n\log\tfrac{L}{\varepsilon}+2n+o(n) bits of space and answers approximate range emptiness queries in time O⁡(log⁡Lε)O(\log\tfrac{L}{\varepsilon}) with a false positive probability of at most ε\varepsilon for query ranges of size LL, and at most ℓ​ε/L\ell\varepsilon/L for query ranges of size ℓ≤L\ell\leq L.

Since n​log⁡Lε+2​n+o⁡(n)=n​log⁡Lε+O⁡(n)n\log\tfrac{L}{\varepsilon}+2n+o(n)=n\log\tfrac{L}{\varepsilon}+O(n), Grafite matches the space lower bound of the approximate range emptiness problem (Theorem [2.1](#S2.Thmtheorem1 "Theorem 2.1 (( , )). ‣ 2. Related Work ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")), thus it is space-optimal, while offering constant time queries whenever L/ε=O⁡(1)L/\varepsilon=O(1).

*Algorithm 1 Construction of Grafite.*

function *Construct(*S⁡[1,n]S[1,n], LL, ε\varepsilon*)*

    G.r←n​L/εG.r\leftarrow nL/\varepsilon

    G.h←G.h\leftarrow\\, The hash function ([1](#S3.E1 "In 3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters"))

    V←An empty array of size ​nV\leftarrow\text{An empty array of size }n

    for *i=1i=1 to nn* do

        V⁡[i]←G.h⁡(S⁡[i])V[i]\leftarrow G.h{(S[i])}

    Sort​(V)\textsc{Sort}{(V)}

    G.𝑒𝑓←BuildEliasFano​(V)G.\mathit{ef}\leftarrow\textsc{BuildEliasFano}(V)

    return GG

*Algorithm 2 Range emptiness query in Grafite.*

function *RangeEmptinessQuery(*GG, aa, bb*)*

    ha←G.h⁡(a)h_{a}\leftarrow G.h{(a)}

    hb←G.h⁡(b)h_{b}\leftarrow G.h{(b)}

    if *ha>hbh_{a}>h_{b}* then

        return “Not empty” iff G.𝑒𝑓.𝑓𝑖𝑟𝑠𝑡≤hb∨G.𝑒𝑓.𝑙𝑎𝑠𝑡≥haG.\mathit{ef}.\mathit{first}\leq h_{b}\lor G.\mathit{ef}.\mathit{last}\geq h_{a}

    return “Not empty” iff G.𝑒𝑓.𝑝𝑟𝑒𝑑𝑒𝑐𝑒𝑠𝑠𝑜𝑟⁡(hb)≥haG.\mathit{ef}.\mathit{predecessor}{(h_{b})}\geq h_{a}

Notice that Grafite has several important features.
First, the false positive probability is bounded *regardless* of the input set SS and query workload, thus solving the first challenge faced by known practical range filters mentioned in Section [1](#S1 "1. Introduction ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters").
Second, the query time is independent of nn (and uu), thus making Grafite efficient even for large sets of input keys.
Third, Grafite does not require any sophisticated tuning procedure, but it can be used out of the box by just specifying ε\varepsilon and LL.

We stress that, after LL has been set, Grafite can answer on both query ranges of size ℓ\ell smaller than LL (with a smaller chance of false positives than ε\varepsilon) and larger than LL (with a higher chance of false positives than ε\varepsilon), because the presence of LL in Theorem [3.4](#S3.Thmtheorem4 "Theorem 3.4. ‣ 3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters") is technical and serves to make the false positive probability ≤ε\leq\varepsilon.
As a matter of fact, since the space usage is log⁡Lε+2\log\tfrac{L}{\varepsilon}+2 bits per key,44 4 The o⁡(1)o(1) term we omit here can be just 0.0350.035 bits per key in practice ([Kurpicz, 2022](#bib.bib24)). we can build Grafite by just setting the space budget to a constant BB, hence ε=L/2B−2\varepsilon=L/2^{B-2}, and we can answer range emptiness queries with a false positive probability of at most

|  | ℓ​εL=ℓ​L/2B−2L=ℓ2B−2,\frac{\ell\varepsilon}{L}=\frac{\ell L/2^{B-2}}{L}=\frac{\ell}{2^{B-2}}, |  |
| --- | --------------------------------------------------------------------------------------------- | --- |

in time

|  | O⁡(log⁡Lε)=O⁡(log⁡LL/2B−2)=O⁡(log⁡2B−2)=O⁡(B),O\left(\log\frac{L}{\varepsilon}\right)=O\left(\log\frac{L}{L/2^{B-2}}\right)=O\left(\log 2^{B-2}\right)=O(B), |  |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ | --- |

thus proving the following result (which solves the second challenge faced by known practical range filters mentioned in Section [1](#S1 "1. Introduction ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")).

###### Corollary 3.5.

Given a set of nn keys and a budget of B=O⁡(1)B=O(1) bits per key, Grafite answers approximate range emptiness queries in O⁡(1)O(1) time with a false positive probability of at most min⁡{1,ℓ/2B−2}\min\\{1,\ell/2^{B-2}\\}, where ℓ\ell is the query range size.

Observe that, a similar derivation of Corollary [3.5](#S3.Thmtheorem5 "Corollary 3.5. ‣ 3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters") with the data structure of Goswami et al. ([Goswami et al., 2014](#bib.bib19)) would lead to a false positive probability higher (actually, strictly higher, due to the lower-order terms we omit) than ℓ/2B−3\ell/2^{B-3}, which is worse than the one achieved by Grafite (see also Section [5](#S5 "5. Theoretical Comparison ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")).

Finally, we mention that instead of returning a boolean answer, Grafite can return an approximate count of the keys that intersect the given query range without any change in its space or query time complexity, thus potentially being a practical and efficient solution for this interesting problem too ([Alstrup et al., 2001](#bib.bib3)). It suffices to return the difference between the *ranks* at the hashed endpoints of the query range (possibly adjusting the result with the expected number of collisions in the range, as per Footnote [3](#footnote3 "footnote 3 ‣ 3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")), where the rank of a hashed element can be found easily during the predecessor operation on the Elias-Fano sequence.

## 4. Bucketing: A Heuristic Range Filter

We now introduce a very simple heuristic range filter named Bucketing. Bucketing has the same weakness of known heuristic range filters, namely, it provides little or no filtering on correlated query workloads, thus its purpose is not to compete with Grafite, which instead provides robust and consistent filtering effectiveness regardless of the input set and query workload.
Rather, Bucketing will serve us to show experimentally that, on certain inputs experimented in the literature, one does not need to resort to the sophisticated heuristic filter designs proposed in the literature, because simpler solutions can experimentally match or improve their filtering effectiveness while being more efficient to query and construct.

Given a set S={x1,…,xn}S=\\{x_{1},\dots,x_{n}\\} of nn keys in the universe [u][u], and given an integer s≥1s\geq 1, we split the universe into u/su/s buckets of size ss.
Then, we create a bitvector CC of size u/su/s that indicates with 1 in position ii if there exists at least a key x∈Sx\in S that falls in the iith bucket.
That is, CC is initially empty, and we set C⁡[x/s]=1C[x/s]=\texttt{1} for each x∈Sx\in S (we omit floors for simplicity).

Let tt be the number of 1-bits in CC, which depends on the distribution of the input data.
Clearly, tt cannot be more than the size of CC or than the number of elements in SS, thus t≤min⁡{u/s,n}t\leq\min\\{u/s,n\\}.
By compressing CC with the Elias-Fano encoding, the total space occupancy is t⁡(log⁡ut​s+2)t(\log\tfrac{u}{ts}+2) bits.
The construction can actually be done without creating CC by just considering the deduplicated list of the 1-bit positions x1/s,…,xn/sx_{1}/s,\dots,x_{n}/s.

The parameter ss allows us to trade the space with the coarseness of such a lossy encoding of SS.
Indeed, when s=1{s=1}, we are losslessly encoding the input set (i.e. t=nt=n) and the space is n⁡(log⁡un+2)n(\log\tfrac{u}{n}+2) bits, whereas if s=us=u then a single bucket exists for the whole set (i.e. t=1t=1) and its single entry in CC is 1, thus the space is 0.

Similarly to Grafite (Section [3](#S3 "3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")), we augment the Elias-Fano encoding of CC with select data structures that occupy o⁡(t)o(t) bits and allow us to compute the 𝑝𝑟𝑒𝑑𝑒𝑐𝑒𝑠𝑠𝑜𝑟\mathit{predecessor} operation in O⁡(log⁡ut​s)O(\log\tfrac{u}{ts}) time. Then, given a query range [a,b][a,b], if 𝑝𝑟𝑒𝑑𝑒𝑐𝑒𝑠𝑠𝑜𝑟⁡(b/s)≥a/s\mathit{predecessor}(b/s)\geq a/s is true, then C⁡[a/s,b/s]C[a/s,b/s] contains at least a 1-bit and we answer “not empty”. Otherwise, we answer “empty”.

It goes without saying that false negatives are not possible and that a false positive happens when [a,b]∩S=∅[a,b]\cap S=\emptyset but there is a key k∈Sk\in S such that k<ak<a and kk falls into bucket number a/sa/s, or symmetrically if k>bk>b and kk falls into bucket number b/sb/s.
Similarly to other heuristic filters, a bound on the false positive rate that holds regardless of the input data and query distributions cannot be proved.
Moreover, we expect this approach to provide no filtering as the correlation increases, due to endpoints of the query range falling in non-empty buckets.

## 5. Theoretical Comparison

*Table 1. Summary of known and new theoretical results achieved by range filters. Recall that 𝒏n is the number of input keys from a universe of size 𝒖u, 𝑳L is an upper bound on the query range size, and 𝜺\varepsilon is the false positive probability.*

|                                                                                                                                 |                                                                                                                                                        | Space in bits                                                                            | Query time                                                | Practical impl. |
| ------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------- | --------------------------------------------------------- | --------------- |
| Heuristic   {\left.\vbox{\vrule height=0.0pt,width=0.0pt}\textnormal{{\rotatebox[origin={c}]{90.0}{Heuristic}}\\;}\right\\{     | SuRF ([Zhang et al., 2018](#bib.bib41))                                                                                                                | (10+m)​n+10​z+o⁡(n+z)(10+m)n+10z+o(n+z)*                                                 | O⁡(log⁡u)O(\log u)                                        | ✓\checkmark     |
|                                                                                                                                 | SNARF ([Vaidya et al., 2022](#bib.bib37))                                                                                                              | n​log⁡K+2.4​nn\log K+2.4n†                                                               | Ω⁡(log⁡n)\Omega(\log{n})                                  | ✓\checkmark     |
|                                                                                                                                 | Proteus ([Knorr et al., 2022](#bib.bib22))                                                                                                             | ?                                                                                        | ?                                                         | ✓\checkmark     |
|                                                                                                                                 | bloomRF ([Mößner et al., 2023](#bib.bib28))                                                                                                            | ?                                                                                        | O⁡(log⁡un)O(\log\tfrac{u}{n})                             | ✓\checkmark     |
|                                                                                                                                 | Bucketing (this paper)                                                                                                                                 | t​log⁡ut​s+2​t+o⁡(t)t\log\tfrac{u}{ts}+2t+o(t)‡                                          | O⁡(log⁡ut​s)O(\log\tfrac{u}{ts})                          | ✓\checkmark     |
| FPR-bounded   {\left.\vbox{\vrule height=0.0pt,width=0.0pt}\textnormal{{\rotatebox[origin={c}]{90.0}{FPR-bounded}}\\;}\right\\{ | Theoretical baseline (cf. Section [2](#S2 "2. Related Work ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters"))                         | n​log⁡Lε+O⁡(n)n\log\tfrac{L}{\varepsilon}+O(n)                                           | O⁡(L)O(L)                                                 |                 |
|                                                                                                                                 | Goswami et al. ([Goswami et al., 2014](#bib.bib19))                                                                                                    | n​log⁡Lε+3​n+o⁡(n​log⁡Lε)n\log\tfrac{L}{\varepsilon}+3n+o(n\log\tfrac{L}{\varepsilon})   | O⁡((log⁡n​Lε)/w)O((\log\tfrac{nL}{\varepsilon})/w)        |                 |
|                                                                                                                                 | Rosetta ([Luo et al., 2020](#bib.bib26))                                                                                                               | 1.44⋅n​log⁡Lε1.44\cdot n\log\tfrac{L}{\varepsilon}                                       | Ω⁡((log⁡L)​log⁡(2−ε))\Omega((\log L)\log(2-\varepsilon))§ | ✓\checkmark     |
|                                                                                                                                 | Grafite (this paper)                                                                                                                                   | n​log⁡Lε+2​n+o⁡(n)n\log\tfrac{L}{\varepsilon}+2n+o(n)                                    | O⁡(log⁡Lε)O(\log\tfrac{L}{\varepsilon})                   | ✓\checkmark     |
|                                                                                                                                 | Lower bound (Theorem [2.1](#S2.Thmtheorem1 "Theorem 2.1 (( , )). ‣ 2. Related Work ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")) | n​log⁡(L1−O⁡(ε)ε)−O⁡(n)n\log{\left(\frac{L^{1-O(\varepsilon)}}{\varepsilon}\right)}-O(n) |                                                           |                 |

- \*

  ​Using the space-efficient LOUDS-Sparse encoding, where zz is the number of internal nodes, and mm is the given number of suffix bits.

- †

  ​K≥1K\geq 1 is a suitably large parameter of SNARF.

- ‡

  ​ss is a positive integer parameter (higher values encode the input set more coarsely), and t∈[1,min⁡{n,u/s}]t\in[1,\min\\{n,u/s\\}] depends on the input data.

- §

  ​Expected time. Worst-case time is O⁡(L​log⁡1ε)O(L\log\tfrac{1}{\varepsilon}).

We now compare the space-time bounds of Grafite (Theorem [3.4](#S3.Thmtheorem4 "Theorem 3.4. ‣ 3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")) with those of the state-of-the-art range filters discussed in Section [2](#S2 "2. Related Work ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters").
We distinguish two kinds of range filters, the ones that provide a *bounded* false positive probability ε\varepsilon thus solving the approximate range emptiness problem formulated in Section [1](#S1 "1. Introduction ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters"), and the *heuristic* ones, which do not provide any guarantee unless some assumptions on the input data and query distribution are met.
Therefore, the space complexity of these latter range filters cannot and will not be compared with that of Grafite (unless under said assumptions).

Table [1](#S5.T1 "Table 1 ‣ 5. Theoretical Comparison ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters") summarises known and new bounds.
Some complex time bounds are simplified with the Ω\Omega-notation, which still allows comparing them with Grafite.
All time bounds do not include the O⁡((log⁡u)/w)O((\log u)/w) time to process the two endpoints of the query range, which is typically neglected because any solution has to read them.

Goswami et al.’s solution. The data structure proposed for the ww-bit word RAM model by Goswami et al. takes n​log⁡Lε+O⁡(n)+o⁡(n​log⁡Lε)n\log\tfrac{L}{\varepsilon}+O(n)+o(n\log\tfrac{L}{\varepsilon}) bits, where the O⁡(n)O(n) term hides 3​n+o⁡(n)3n+o(n) bits ([Goswami et al., 2014](#bib.bib19), §3.2).
Hence, Grafite is better in space than this data structure by an additive term n+o⁡(n​log⁡Lε)n+o(n\log\tfrac{L}{\varepsilon}), thus it is closer to the space lower bound of approximate range emptiness data structures (Theorem [2.1](#S2.Thmtheorem1 "Theorem 2.1 (( , )). ‣ 2. Related Work ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")).

On the other hand, the query time of Grafite is O⁡(log⁡Lε)O(\log\tfrac{L}{\varepsilon}) while the query time of Goswami et al.’s data structure is O⁡((log⁡n​Lε)/w)O((\log\tfrac{nL}{\varepsilon})/w).
The former is higher than the latter when n=O⁡((L/ε)w−1)n=O((L/\varepsilon)^{w-1}).
In the case L/ε=O⁡(1)L/\varepsilon=O(1), then queries in both data structures take O⁡(1)O(1) time because it is usually assumed w=Ω⁡(log⁡n)w=\Omega(\log n).

Rosetta. Rosetta allows tuning the false positive probability of its per-level Bloom filters.
We use the tuning from ([Luo et al., 2020](#bib.bib26), §3.1) that achieves approximately 1.44⋅n​log⁡Lε1.44\cdot n\log\tfrac{L}{\varepsilon} bits of space by setting the probability of false positives to ε\varepsilon for the last-level Bloom filter and to 1/(2−ε)1/(2-\varepsilon) for each other upper-level Bloom filter.
The space of Grafite is better, since 1.44⋅log⁡Lε<log⁡Lε+21.44\cdot\log\tfrac{L}{\varepsilon}<\log\tfrac{L}{\varepsilon}+2 if and only if L<23.36​εL<23.36\varepsilon.

For the query time, the worst-case number of Bloom filter probes done by Rosetta is O⁡(L)O(L) and the expected number is Ω⁡(log⁡L)\Omega(\log L), as per the analysis in ([Luo et al., 2020](#bib.bib26), §3.2).
The probe time of the last-level Bloom filter is Θ⁡(log⁡1ε)\Theta(\log\tfrac{1}{\varepsilon}), which is higher than the Θ⁡(log⁡(2−ε))\Theta(\log(2-\varepsilon)) probe time of each other upper-level Bloom filter.
So the worst-case query time of Rosetta is O⁡(L​log⁡1ε)O(L\log\tfrac{1}{\varepsilon}), which is worse than the query time of Grafite, and the expected query time of Rosetta is Ω⁡((log⁡L)​log⁡(2−ε))\Omega((\log L)\log(2-\varepsilon)), which is equivalent to the query time of Grafite if ε\varepsilon is a constant.

SuRF. Let zz be the number of internal nodes in the Fast Succinct Trie at the core of SuRF, and recall it stores one leaf and mm suffix bits for each of the nn input keys.
The trie uses the LOUDS-Dense encoding for the upper levels and LOUDS-Sparse for the lower levels.
Following ([Zhang et al., 2018](#bib.bib41), §2.5), we assume the more space-efficient LOUDS-Sparse encoding is used, in which each node takes 10 bits.
Considering the o⁡(z+n)o(z+n) bits for the rank/select data structures, the total space sums up to n​m+10​(n+z)+o⁡(n+z)=(10+m)​n+10​z+o⁡(n+z)nm+10(n+z)+o(n+z)=(10+m)n+10z+o(n+z) bits.
From this analysis (confirmed by experiments), we infer that SuRF needs at least 10 bits per key, which can be restrictive in applications with a low space budget.

The query time of SuRF is given by the time to traverse the trie and then compare the suffix bits. Thus, for a trie of height hh, the time is O⁡(h)O(h) if the suffix bits fit into a machine word (thus they can be accessed in constant time), and if each branching step takes constant time, e.g. because the trie has a constant bounded fan-out.
Since the input keys are of length O⁡(log⁡u)O(\log u), the query time is O⁡(h)=O⁡(log⁡u)O(h)=O(\log u).
Even for a fairly large L≤u​ε/nL\leq u\varepsilon/n (cf. Theorem [3.4](#S3.Thmtheorem4 "Theorem 3.4. ‣ 3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")), Grafite is faster than SuRF because it has query time O⁡(log⁡Lε)=O⁡(log⁡un)O(\log\tfrac{L}{\varepsilon})=O(\log\tfrac{u}{n}).

SNARF. SNARF was shown to take approximately n​log⁡K+2.4​nn\log K+2.4n bits, where KK is a suitably large parameter impacting on the false positive rate ([Vaidya et al., 2022](#bib.bib37), §5).
For the query time, the paper does not give a precise analysis, but we notice that it requires performing a binary search on the sample of n/tn/t keys (where tt is a constant) to identify the correct spline model, followed by decoding the compressed bit array. Due to the binary search, SNARF takes time Ω⁡(log⁡nt)=Ω⁡(log⁡n)\Omega(\log\tfrac{n}{t})=\Omega(\log n), which is already at least asymptotically the query time of Grafite.
Indeed, Grafite binary searches on a range with min⁡{n,L/ε}\min\\{n,L/\varepsilon\\} keys.

Under the assumption of uniform keys, uniform query workload, and u≫n​Ku\gg nK, SNARF was shown to have a false positive probability of (u/K)/(u−n​L)(u/K)/(u-nL) for query ranges of size LL ([Vaidya et al., 2022](#bib.bib37), §3).
This is approximated to 1/K1/K under the additional assumption of n​L≪unL\ll u.
In such a restricted setting, SNARF takes log⁡L−0.4\log{L}-0.4 bits per key less than Grafite with ε\varepsilon set to 1/K1/K.
On the flip side, SNARF suffers a high false positive rate in correlated workloads (see Section [6.2](#S6.SS2 "6.2. Robustness of Range Filters ‣ 6. Experiments ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")).

Proteus. The Proteus paper ([Knorr et al., 2022](#bib.bib22)) does not provide a closed formula for the space and the query time taken by this data structure.
Indeed, Proteus tunes its configuration parameters (l1,l2)(l_{1},l_{2}) via an algorithm whose inputs are the keys, a query workload, and a space budget (cf. ([Knorr et al., 2022](#bib.bib22), Alg. 1)). This makes it difficult to provide a satisfactory space bound other than for the extreme configurations that turn it into either a full Fast Succinct Trie, or a full Prefix Bloom Filter.

For the query time, we could not derive a satisfactory analysis either, but we observe that Proteus uses a Fast Succinct Trie on prefixes of uniform depth l1l_{1} and a Prefix Bloom Filter for prefixes of length l2>l1l_{2}>l_{1}, thus it might require a trie traversal plus several queries to the Prefix Bloom Filter (our experiments will show that Proteus is much slower than Grafite).

bloomRF. The authors of bloomRF build a model of the false positive rate given a space budget in ([Mößner et al., 2023](#bib.bib28), §5–6).
We prefer to not report this model here due to its complicated design and assumptions, but we content ourselves to notice that it is influenced by the input data distribution (cf. the constant CC in ([Mößner et al., 2023](#bib.bib28))), thus making bloomRF a heuristic solution.

The query time of bloomRF is given by the time to compute k=⌈log⁡un/Δ⌉k=\lceil\log\tfrac{u}{n}/\Delta\rceil hash functions, where Δ≥1\Delta\geq 1 is a parameter of the data structure ([Mößner et al., 2023](#bib.bib28), §6). Thus bloomRF has query time O⁡(log⁡un)O(\log\tfrac{u}{n}), which is no better than Grafite for the same considerations we make above for SuRF.

REncoder. The authors of REncoder show in ([Wang et al., 2023](#bib.bib39), §4) that, under some assumptions, a false positive probability of ε\varepsilon can be obtained using O⁡(n⁡(k+log⁡1ε))O(n(k+\log\tfrac{1}{\varepsilon})) bits of space, where kk is the number of hash functions used in REncoder.
This result is hard to compare with Grafite due to the lack of LL in the space bound (which seems to conflict with the lower bound of Theorem [2.1](#S2.Thmtheorem1 "Theorem 2.1 (( , )). ‣ 2. Related Work ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")), due to the big-OO, and due to the use of kk, which also impacts on the query time (no precise indications on how to set kk are given).
In any case, the analysis in ([Wang et al., 2023](#bib.bib39), §4.C) suggests that REncoder too is affected by correlated workloads, which is confirmed by our experiments of Section [6.2](#S6.SS2 "6.2. Robustness of Range Filters ‣ 6. Experiments ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters").

## 6. Experiments

We now perform the largest experimental comparison among range filters, both in terms of dataset size and in the number of tested solutions, and we show that:

1. (1)

   The vast majority of existing range filters provide no filtering or much degraded filtering and query performance in the case of correlated query workloads. Instead, Grafite is among the (few) robust range filters, and it offers the overall best false positive rate (FPR) and query time already starting from mildly correlated query workloads.

2. (2)

   On uncorrelated query workloads, Bucketing offers, simultaneously, a filtering effectiveness that is very close to or better than the one achieved by the best-performing heuristic range filters, 5–13×\times faster queries, and 5–24×\times faster construction than them.

3. (3)

   Among robust range filters, Grafite is the best choice because it offers, simultaneously, the best FPR by up to 5 orders of magnitude, 9–92×\times faster queries, and 4–10×\times faster construction.

Then, we conclude this experimental section by summarising our findings in terms of recommendations on which range filter to adopt for an application (Section [6.7](#S6.SS7 "6.7. Discussion and Recommendations ‣ 6. Experiments ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")).

### 6.1. Experimental Setup

All the experiments are run on a machine equipped with a 1.80 GHz Intel Xeon E5-2650Lv3 CPU and 64 GB of RAM. The code of Grafite and the competitors is in C++ and is compiled with gcc-11.
Our source code is available at <https://github.com/marcocosta97/grafite>.

Competitors. As motivated in Section [2](#S2 "2. Related Work ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters"), we compare Grafite and Bucketing with the following state-of-the-art range filters: SuRF ([Zhang et al., 2018](#bib.bib41)), Rosetta ([Luo et al., 2020](#bib.bib26)), SNARF ([Vaidya et al., 2022](#bib.bib37)),55 5 During the experiments we found that SNARF returns some false negatives, which is contrary to the definition of (range) filter. The authors believe this is due to computation overflows in the learned model, recommending the use of SNARF on significantly smaller input numbers (personal communication, Kapil Vaidya, 17 April 2023). We still experiment with SNARF, with the hope the false negatives do not affect its performance. Proteus ([Knorr et al., 2022](#bib.bib22)) and REncoder ([Wang et al., 2023](#bib.bib39)), including its variants REncoderSE and REncoderSS. This makes our study the largest one in terms of number of considered competitors.

Rosetta, Proteus and REncoderSE are auto-tuned on a sample of the queries with the procedures designed by the respective authors.
For SuRF, we use real suffixes when testing against range queries and hashed key suffixes when testing against point queries, as suggested by ([Zhang et al., 2018](#bib.bib41)).

Datasets. We use synthetic and real-world datasets used in previous range filters evaluations ([Zhang et al., 2018](#bib.bib41); [Luo et al., 2020](#bib.bib26); [Vaidya et al., 2022](#bib.bib37); [Knorr et al., 2022](#bib.bib22); [Wang et al., 2023](#bib.bib39)):

- •

  Uniform: 200M keys chosen uniformly at random from [0,264)[0,2^{64}).

- •

  Books: 200M keys representing Amazon book sale popularity.

- •

  Osm: 200M coordinates of locations from Open Street Map.

By using up to the entire dataset to build the range filters, we double the scale of the previously largest evaluation ([Vaidya et al., 2022](#bib.bib37)).

We build each range filter with space budgets ranging between ≈\approx 8 and 28 bits per key, which covers a large spectrum of trade-offs ([Luo et al., 2020](#bib.bib26); [Wang et al., 2023](#bib.bib39)).66 6 SuRF takes no less than 10 bits per key, as per our analysis in Section [5](#S5 "5. Theoretical Comparison ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters"), and some configurations are not shown due to crashes. Some configurations of Proteus results in the same design thus giving overlapped points in our figures. We ensure that the space of a range filter does not exceed an explicit encoding of the input keys, namely log⁡un+2\log\tfrac{u}{n}+2 bits per key via an Elias-Fano encoding, since this approach would solve the problem without false positives as discussed after Theorem [2.1](#S2.Thmtheorem1 "Theorem 2.1 (( , )). ‣ 2. Related Work ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters").

*Figure 3. The majority of range filters provide no filtering (Bucketing, SNARF, SuRF, REncoderSS) or much degraded filtering and query performance (Proteus, REncoderSE) as the key-query correlation increases. An adversary could exploit this weakness to make an attack on the availability of the system employing these heuristic range filters. Instead, Grafite and Rosetta are robust range filters, while REncoder is robust for large range queries. Grafite offers significantly better query time and FPR than Rosetta and REncoder.*

Query workloads. Following the literature ([Vaidya et al., 2022](#bib.bib37); [Knorr et al., 2022](#bib.bib22); [Luo et al., 2020](#bib.bib26); [Wang et al., 2023](#bib.bib39)), we execute 10M range emptiness queries of the form [x,x+L−1][x,x+L-1] in a single thread.
We distinguish between batches of *point queries* in which L=20L=2^{0}, *small range queries* in which L=25L=2^{5}, and large range queries in which L=210L=2^{10}.

For the *synthetic* dataset (Uniform), the left endpoint xx is chosen according to the following strategies:

- •

  Uncorrelated: xx is chosen uniformly at random from [0,264)[0,2^{64}).

- •

  Correlated: a key kk is chosen uniformly at random from the dataset, and then xx is chosen uniformly at random from [k,k+230​(1−D)][k,k+2^{30(1-D)}], where DD is the *correlation degree* that ranges from 0 (uncorrelated) to 1 (correlated) ([Vaidya et al., 2022](#bib.bib37)). If not explicitly varied, we set D=0.8D=0.8.

For the real datasets, the left endpoint xx is a key extracted (and removed) from the dataset. Notice that this query workload may be a mix of correlated and uncorrelated query ranges, depending on the distribution of the original input keys, from which xx is extracted.

In all the above strategies, we enforce the generation of *empty* queries by discarding the query ranges that intersect the dataset.
This way, we evaluate the false positive rate (FPR) as the ratio between the number of “not empty” answers and the size of the batch.
In a separate experiment, we also test the query time of range filters on *non-empty* queries. Note that the query time does not include the time to access a slow resource, such as a disk or a network drive, where the dataset might be stored. This time can vary greatly depending on the FPR and the hardware, or it might even be absent if the application requires no further check of a “not empty” answer (i.e. checking whether it is a true positive or not).

Other datasets and query workloads. We ought to report that we have also experimented with (i) a dataset generated from a normal distribution (mean of 2632^{63}, standard deviation of 264×0.12^{64}\times 0.1, which allows covering the universe and generating large range queries) in combination with the Uncorrelated and Correlated query workloads, and (ii) the Uniform dataset in combination with a normal query workload.
In all these cases, consistently with previously published evaluations ([Knorr et al., 2022](#bib.bib22); [Vaidya et al., 2022](#bib.bib37)), we found no interesting change in the relative performance of range filters compared to using Uniform only, so we do not show them.

We have also experimented with the Fb dataset used in ([Wang et al., 2023](#bib.bib39); [Vaidya et al., 2022](#bib.bib37); [Knorr et al., 2022](#bib.bib22)) but we found it to be too simple to be included in our evaluation because the mean value of the keys is ≈238\approx 2^{38}, and if we exclude the last 21 keys (that are larger than 2382^{38}), then an Elias-Fano encoding of the dataset would provide no false positives in just log⁡238200⋅106+2≈12\log\tfrac{2^{38}}{200\cdot 10^{6}}+2\approx 12 bits per key.
Indeed, we report that Grafite, due to its optimal design, provides an FPR of 0 on Fb when given a budget of only 12 bits per key, while the other range filters may still give false positives (as shown also in the papers above).

### 6.2. Robustness of Range Filters

Our first experiment aims to differentiate robust range filters from heuristic ones, thereby emphasising the necessity of treating them separately due to their distinct guarantees.
We consider the Uniform dataset and the Correlated query workload where the correlation degree DD is varied from 0 to 1, using a space budget for the range filters fixed to 20 bits per key.

The results in Figure [3](#S6.F3 "Figure 3 ‣ 6.1. Experimental Setup ‣ 6. Experiments ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters") show that the FPR of Grafite and Rosetta is not affected by correlation, so we classify them as robust range filters. Grafite offers a better FPR than Rosetta by up to two orders of magnitude.
The FPR of REncoder is affected by correlation, but this effect diminishes for larger range sizes. Grafite offers a better FPR than REncoder by up to four orders of magnitude.

The FPR of Proteus too suffers from increased correlation, but it does not reach 1. In the case of slightly correlated (i.e. D<0.5D<0.5) large range queries, Proteus shows a smaller FPR than Grafite, while Grafite has a better FPR in all the other cases.
We stress that Proteus is auto-tuned on the input keys and the query workload, so it has an advantage due to overfitting. In applications where the workload shifts, it might not retain this advantage.

The FPR of SuRF, SNARF and Bucketing approaches 1 for correlation degrees beyond 0.4, thus failing to provide any kind of filtering (the drop of FPR of SuRF in point queries is expected because it ends up comparing hashed key suffixes).
The same holds for REncoderSS and REncoderSE for correlation degrees beyond 0.7 (the latter in the case of large range queries).

For what concerns the query time, Grafite is the fastest effective range filter across the various query range sizes and correlation degrees (Bucketing is the fastest range filter, but it is not always effective, as commented above).
The query time of Proteus, Rosetta and REncoder increases for increasingly large query ranges, up to about 3 orders of magnitude more with respect to Grafite.
The query time of Proteus, REncoderSE and REncoderSS is affected by the correlation degree, which is another reason to classify them as non-robust.

In summary, with the exception of Grafite, Rosetta and, to a lesser extent, REncoder, the vast majority of range filters provide *no* filtering (SNARF, SuRF, REncoderSS) or *much degraded* filtering and query performance (Proteus, REncoderSE) in the case of correlated query workloads.
This is a significant concern given the importance of these workloads in applications that care about the local properties of data ([Luo et al., 2020](#bib.bib26)) or given that malicious users could exploit this weakness to increase the network or disk accesses the range filters are deployed to prevent, thus posing a *risk* on the availability of a data system.
Instead, Grafite is the overall best range filter in terms of FPR and query time already starting from mildly correlated query workloads, independently of the query range size.

Given the large number of competitors and the widely different guarantees they provide, our next experiments will focus separately on *heuristic range filters*, namely SNARF, SuRF, Proteus, REncoderSS, and REncoderSE, and on *robust range filters*, namely Grafite, Rosetta, and REncoder.

### 6.3. Evaluation of Heuristic Range Filters

Figure [4](#S6.F4 "Figure 4 ‣ 6.3. Evaluation of Heuristic Range Filters ‣ 6. Experiments ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters") shows the results of our experiments with heuristic range filters.
Each column of the plot corresponds to a query range size (point, small and large), and each row corresponds to a dataset (the first two rows are the Correlated and Uncorrelated query workloads we experiment on Uniform data, and the other two rows correspond to the Books and Osm datasets).
At the right of each row, we show a table with the query time of each range filter, averaged over the various space configurations and query range sizes, and next to each query time we show its ratio with respect to the fastest range filter.

| Range filter | Avg ns/query          |
| ------------ | --------------------- |
| Bucketing    | 67 (1.0×\times)       |
| SuRF         | 275 (4.1×\times)      |
| SNARF        | 576 (8.6×\times)      |
| REncoderSS   | 732 (10.93×\times)    |
| REncoderSE   | 4074 (60.81×\times)   |
| Proteus      | 28638 (427.43×\times) |
|              |                       |
| Bucketing    | 70 (1.0×\times)       |
| Proteus      | 227 (3.24×\times)     |
| SuRF         | 228 (3.26×\times)     |
| REncoderSE   | 351 (5.01×\times)     |
| REncoderSS   | 352 (5.03×\times)     |
| SNARF        | 895 (12.79×\times)    |
|              |                       |
| Bucketing    | 277 (1.0×\times)      |
| SuRF         | 423 (1.53×\times)     |
| REncoderSS   | 527 (1.9×\times)      |
| REncoderSE   | 528 (1.91×\times)     |
| Proteus      | 573 (2.07×\times)     |
| SNARF        | 1817 (6.56×\times)    |
|              |                       |
| Bucketing    | 456 (1.0×\times)      |
| REncoderSE   | 466 (1.02×\times)     |
| REncoderSS   | 467 (1.02×\times)     |
| SuRF         | 567 (1.24×\times)     |
| Proteus      | 719 (1.58×\times)     |
| SNARF        | 2908 (6.38×\times)    |

*Figure 4. Comparison among heuristic range filters. In the first row, only Proteus and REncoderSE provide some range query filtering (albeit unsatisfactorily, as discussed in Section [6.2](#S6.SS2 "6.2. Robustness of Range Filters ‣ 6. Experiments ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")) because they are auto-tuned on the correlated query workload. In the other rows, a simple solution like Bucketing provides very close or better FPR, and much better query time than all the other heuristic range filters. We remark that, unlike the other range filters, SNARF suffers from false negatives (see Footnote [5](#footnote5 "footnote 5 ‣ 6.1. Experimental Setup ‣ 6. Experiments ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")).*

In Correlated, in line with the experiment of Section [6.2](#S6.SS2 "6.2. Robustness of Range Filters ‣ 6. Experiments ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters"), heuristic range filters provide no filtering (SNARF, REncoderSS, and SuRF, whose performance on point queries is commented in Section [6.2](#S6.SS2 "6.2. Robustness of Range Filters ‣ 6. Experiments ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")) or little filtering (Proteus and REncoderSE). These last two filters are actually advantaged by being auto-tuned on the query workload, which might not be realistic in some applications due to rapidly-changing workloads or due to the additional space needed by query logs (which we did not account in their space usage).

For the other datasets, we notice that the filtering effectiveness of Bucketing essentially matches (on Uncorrelated and Books) or is very close (Osm) to the one of the best-performing heuristic range filter that is typically either SNARF (which, however, suffers from false negatives, see Footnote [5](#footnote5 "footnote 5 ‣ 6.1. Experimental Setup ‣ 6. Experiments ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")) or REncoderSE/SS, while simultaneously providing up to 13×\times faster queries than SNARF and up to 5×\times faster queries than REncoderSE/SS. Moreover, Bucketing provides the best construction times, as we will show in Section [6.6](#S6.SS6 "6.6. Construction Efficiency ‣ 6. Experiments ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters").

### 6.4. Evaluation of Robust Range Filters

| Range filter | Avg ns/query         |
| ------------ | -------------------- |
| Grafite      | 305 (1.00×\times)    |
| REncoder     | 2905 (9.52×\times)   |
| Rosetta      | 24902 (81.65×\times) |
|              |                      |
| Grafite      | 270 (1.00×\times)    |
| REncoder     | 3001 (11.11×\times)  |
| Rosetta      | 24913 (92.27×\times) |
|              |                      |
| Grafite      | 274 (1.00×\times)    |
| REncoder     | 2410 (8.80×\times)   |
| Rosetta      | 24716 (90.20×\times) |
|              |                      |
| Grafite      | 276 (1.00×\times)    |
| REncoder     | 2845 (10.31×\times)  |
| Rosetta      | 24853 (90.05×\times) |

*Figure 5. Grafite dominates all other robust range filters by providing up to 5 orders of magnitude better FPR and up to 92×\times faster queries. These substantial improvements, coupled with its performance guarantees (Corollary [3.5](#S3.Thmtheorem5 "Corollary 3.5. ‣ 3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")), make Grafite the range filter of choice in applications handling a variety of data distributions and query workloads, even adversarial ones.*

We now experiment with robust range filters, namely Grafite, Rosetta, and REncoder (note this last one is slightly less robust in the case of small range queries, as discussed in Section [6.2](#S6.SS2 "6.2. Robustness of Range Filters ‣ 6. Experiments ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")).

As Figure [5](#S6.F5 "Figure 5 ‣ 6.4. Evaluation of Robust Range Filters ‣ 6. Experiments ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters") shows, in all datasets and query range sizes, Grafite dominates Rosetta and REncoder both in terms of FPR and query time.
In particular, in terms of FPR, Grafite is up to 4 orders of magnitude more effective than REncoder, and up to 5 orders of magnitude more effective than Rosetta.
In terms of query time, Grafite is 9.5–11.1×\times faster than REncoder, and 81.7–92.3×\times faster than Rosetta.
Besides, we observe that Grafite has the most predictable FPR across all combinations of datasets, query workloads, and range sizes.

This consistent and substantial improvement of the state of the art corroborates the theoretical advantage of Grafite over prior solutions (Section [5](#S5 "5. Theoretical Comparison ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")), and demonstrates its potential to become the range filter of choice in applications handling a variety of data distributions and query workloads (even adversarial ones).

### 6.5. Performance on Non-Empty Queries

*Figure 6. The query time of range filters can vary a lot also in the case of non-empty queries, as shown in these plots with a logarithmic time axis. Grafite and Bucketing provide the best query times among robust and heuristic range filters, respectively.*

We now experiment with queries that intersect the input dataset to show their impact on the query time.
We use the Uniform dataset and create a query range [x,x+L−1][x,x+L-1] by first picking a key kk randomly from the dataset, and then picking the left endpoint xx randomly in [k−L+1,k][k-L+1,k].

Figure [6](#S6.F6 "Figure 6 ‣ 6.5. Performance on Non-Empty Queries ‣ 6. Experiments ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters") shows the results: among heuristic range filters, Bucketing provides up to 3 orders of magnitude faster queries than the others; among robust range filters, Grafite provides the fastest queries, up to 1 order of magnitude faster than REncoder and up to 2 orders of magnitude faster than Rosetta.

A remark is necessary at this point. Even though filters are typically used in applications to prevent unnecessary (due to empty queries) network or disk accesses, they also increase CPU usage (regardless of the actual emptiness of the queried range).
In some cases, high CPU usage might not compensate for the reduction in access frequency to a slow resource, thus making the choice of a query-efficient range filter preferable, even if it has a higher FPR.
For example, Rosetta and Proteus in Figure [6](#S6.F6 "Figure 6 ‣ 6.5. Performance on Non-Empty Queries ‣ 6. Experiments ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters") take up to 61.2 and 101.5 microseconds per query, respectively, which is comparable to the access latency of an SSD.
In other cases, the opposite might be true, i.e. the cost of accessing a slow resource might be too high to be able to afford a range filter with a lower FPR but better CPU usage; thus the choice of which range filter to use ultimately depends on the specific application.

### 6.6. Construction Efficiency

*Figure 7. Grafite has the best construction time among robust range filters (Rosetta and REncoder). Bucketing has the best construction time among heuristic range filters.*

Figure [7](#S6.F7 "Figure 7 ‣ 6.6. Construction Efficiency ‣ 6. Experiments ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters") shows the construction time of the various range filters as the number of keys increases from 10510^{5} to 10810^{8}.
We use the Uniform dataset (other datasets do not change our conclusions) and average the construction time over the different space budgets.
We do not show REncoderSE and SS because their construction time is identical to that of REncoder.
For both Rosetta and Proteus, the plot shows with a light colour the impact of the tuning process, which was evaluated with an Uncorrelated query workload of n/10n/10 small range queries.

Among heuristic range filters, Bucketing is the fastest to construct, from 1.8 to 30.2×\times faster than the others. In particular, compared to its closest competitors in terms of FPR (see Section [6.3](#S6.SS3 "6.3. Evaluation of Heuristic Range Filters ‣ 6. Experiments ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")), Bucketing is 8.6–23.9×\times faster to construct than REncoderSE and 4.9–6.5×\times faster than SNARF.

Among robust range filters, Grafite is the fastest to construct, 6.7–10.3×\times faster than Rosetta and 3.8–7.9×\times faster than REncoder.

Furthermore, the construction time of both Grafite and Bucketing is linear for increasing values of nn (note the plot shows the construction time per key), which makes them highly scalable.

Actually, Grafite could achieve an even better construction time by using other sorting algorithms ([Axtmann et al., 2022](#bib.bib4)) in place of the spreadsort algorithm we are using, or by enabling multi-threaded sorting (not shown in the plots for fairness with the single-threaded competitors).
For example, by using the parallel block_indirect_sort algorithm from the Boost library, the construction time for 200M keys with just 2 threads is reduced from 28.0 to 18.8 s (a 1.5×\times speedup), with 4 threads to 15.8 s (1.8×\times speedup), with 8 threads to 14.0 s (2.0×\times speedup)

### 6.7. Discussion and Recommendations

We now summarise our experimental findings by providing some guidance on which range filter to adopt for an application.

First and foremost, one should determine whether guarantees on the filtering effectiveness and query performance are needed regardless of the input data and future queries. If the answer is affirmative, then range filters providing a bounded FPR for a given space budget (and vice versa) are the choice, and among them Grafite is the best option.

For example, if future queries are correlated (i.e. close) to the input keys, the existing heuristic range filters provide little to no filtering, thus impacting the overall performance of the system (and possibly cloud costs) due to the network or disk accesses the filters are deployed to prevent.
Correlated queries are common in practice ([Luo et al., 2020](#bib.bib26)), and malicious users can artificially issue them with just the knowledge of (a subset of) the keys.
In these cases, Grafite is again the best option since it is unaffected by correlated queries.

If the application has no or infrequent correlated queries, and the query distribution does not change after the range filter is evaluated on a query sample and deployed, we recommend considering also Bucketing, Proteus, REncoderSS (and possibly SNARF, but refer to Footnote [5](#footnote5 "footnote 5 ‣ 6.1. Experimental Setup ‣ 6. Experiments ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")), which could provide better filtering effectiveness than Grafite.
For example, Proteus can auto-tune itself and obtain a good FPR (see the rightmost plot in Figure [3](#S6.F3 "Figure 3 ‣ 6.1. Experimental Setup ‣ 6. Experiments ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")). REncoderSS can offer a good FPR without any auto-tuning in some cases with small range queries (see last two rows of Figure [4](#S6.F4 "Figure 4 ‣ 6.3. Evaluation of Heuristic Range Filters ‣ 6. Experiments ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")). Bucketing always offered the best query and construction times in our experiments, and very good FPR in many cases (see Figure [4](#S6.F4 "Figure 4 ‣ 6.3. Evaluation of Heuristic Range Filters ‣ 6. Experiments ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")).
Other than the FPR, query and construction times, deciding which range filter to adopt in a real application should consider factors like the cost of a false positive (e.g., in terms of latency or cloud costs) and the frequency of queries (which impact on the CPU usage). Thus the best choice ultimately depends on the peculiarities of the application.

## 7. Conclusion

We introduced Grafite, a range filter that solves the lack of robustness in current practical solutions by providing strong theoretical guarantees on the false positive probability, optimal space usage, and very efficient and effective performance across many datasets, query workloads, and range sizes.
We also introduced Bucketing, which simplifies the design of existing heuristic range filters while empirically providing very close or better filtering effectiveness, and much faster query and construction times, thus possibly resulting in a simple substitute for them.

For future work, we mention a more in-depth study of the Bucketing approach, which could be made workload-aware (e.g. by creating larger buckets for key ranges that are queried less frequently), or combined with Grafite.
It is also worth engineering and experimenting with an extension of Grafite to string keys, for example by treating strings as integers and choosing rr as a power of two, say r=2kr=2^{k} for some k>0k>0, so that the hash function ([1](#S3.E1 "In 3. Grafite: An Optimal Range Filter ‣ Grafite: Taming Adversarial Queries with Optimal Range Filters")) can be efficiently implemented via bitwise and arithmetic operations as h⁡(x)=(q⁡(x≫k)+x)&(r−1)h(x)=(q(x\\,\mathtt{\gg}\\,k)+x)\\,\mathtt{\\&}\\,(r-1), where qq could be chosen to be a practical hash function for strings like xxHash. Another open problem is to support the insertion of new keys in Grafite and Bucketing, for which dynamic Elias-Fano representations could help ([Pibiri and Venturini, 2017](#bib.bib34)).
Finally, we mention again that Grafite can easily return an approximate count of the keys that intersect the given query range without any change in its space or query time complexity, thus potentially being a practical and efficient solution for this other interesting problem too ([Alstrup et al., 2001](#bib.bib3)).

###### Acknowledgements.

We thank Mayank Goswami and Rasmus Pagh for clarifications on ([Goswami et al., 2014](#bib.bib19)), Subarna Chatterjee for clarifications on ([Luo et al., 2020](#bib.bib26)), and Hans-Peter Lehmann for noticing a special case in our query algorithm.
This work was supported by the European Union – Horizon 2020 Program under the scheme “INFRAIA-01-2018-2019 – Integrating Activities for Advanced Communities”, Grant Agreement n. 871042, “SoBigData++: European Integrated Infrastructure for Social Mining and Big Data Analytics” (http://www.sobigdata.eu), by the NextGenerationEU – National Recovery and Resilience Plan (Piano Nazionale di Ripresa e Resilienza, PNRR) – Project: “SoBigData.it - Strengthening the Italian RI for Social Mining and Big Data Analytics” – Prot. IR0000013 – Avviso n. 3264 del 28/12/2021, by the spoke “FutureHPC & BigData” of the ICSC – Centro Nazionale di Ricerca in High-Performance Computing, Big Data and Quantum Computing funded by European Union – NextGenerationEU – PNRR, by the Italian Ministry of University and Research “Progetti di Rilevante Interesse Nazionale” project: “Multicriteria data structures and algorithms” (grant n. 2017WR7SHH).

## References

- Alexiou et al. (2013) Karolina Alexiou, Donald Kossmann, and Per-Åke Larson. 2013.

    Adaptive Range Filters for Cold Data: Avoiding Trips to Siberia.

    *PVLDB*6, 14 (2013), 1714–1725.

    <https://doi.org/10.14778/2556549.2556556>
- Alstrup et al. (2001) Stephen Alstrup, Gerth Stølting Brodal, and Theis Rauhe. 2001.

    Optimal static range reporting in one dimension. In *Proc. 33rd Annual ACM Symposium on Theory of Computing (STOC)*. 476–482.

    <https://doi.org/10.1145/380752.380842>
- Axtmann et al. (2022) Michael Axtmann, Sascha Witt, Daniel Ferizovic, and Peter Sanders. 2022.

    Engineering In-place (Shared-memory) Sorting Algorithms.

    *ACM Trans. Parallel Comput.*9, 1 (2022), 2:1–2:62.

    <https://doi.org/10.1145/3505286>
- Belazzougui et al. (2010) Djamal Belazzougui, Paolo Boldi, Rasmus Pagh, and Sebastiano Vigna. 2010.

    Fast Prefix Search in Little Space, with Applications. In *Proc.18th Annual European Symposium on Algorithms (ESA)*. 427–438.

    <https://doi.org/10.1007/978-3-642-15775-2_37>
- Bloom (1970) Burton H. Bloom. 1970.

    Space/Time Trade-offs in Hash Coding with Allowable Errors.

    *Commun. ACM*13, 7 (1970), 422–426.

    <https://doi.org/10.1145/362686.362692>
- Broder and Mitzenmacher (2004) Andrei Broder and Michael Mitzenmacher. 2004.

    Network Applications of Bloom Filters: A Survey.

    *Internet mathematics*1, 4 (2004), 485–509.

- Chikhi et al. (2022) Rayan Chikhi, Jan Holub, and Paul Medvedev. 2022.

    Data Structures to Represent a Set of *k*-long DNA Sequences.

    *ACM Comput. Surv.*54, 1 (2022), 17:1–17:22.

    <https://doi.org/10.1145/3445967>
- Clark (1996) David Richard Clark. 1996.

    *Compact Pat Trees*.

    Ph. D. Dissertation. University of Waterloo, Canada.

- Cormen et al. (2022) Thomas H. Cormen, Charles E. Leiserson, Ronald L. Rivest, and Clifford Stein. 2022.

    *Introduction to Algorithms*(4 ed.).

    The MIT Press.

- Dayan et al. (2018) Niv Dayan, Manos Athanassoulis, and Stratos Idreos. 2018.

    Optimal Bloom Filters and Adaptive Merging for LSM-Trees.

    *ACM Trans. Database Syst.*43, 4 (2018), 16:1–16:48.

    <https://doi.org/10.1145/3276980>
- Dayan et al. (2023) Niv Dayan, Ioana Bercea, Pedro Reviriego, and Rasmus Pagh. 2023.

    InfiniFilter: Expanding Filters to Infinity and Beyond.

    *Proc. ACM Manag. Data*1, 2, Article 140 (jun 2023), 27 pages.

    <https://doi.org/10.1145/3589285>
- Dharmapurikar et al. (2003) Sarang Dharmapurikar, Praveen Krishnamurthy, and David E. Taylor. 2003.

    Longest Prefix Matching Using Bloom Filters. In *Proc. ACM Conference on Applications, Technologies, Architectures, and Protocols for Computer Communications (SIGCOMM)*. 201–212.

- Dillinger et al. (2022) Peter C. Dillinger, Lorenz Hübschle-Schneider, Peter Sanders, and Stefan Walzer. 2022.

    Fast Succinct Retrieval and Approximate Membership Using Ribbon. In *Proc. 20th International Symposium on Experimental Algorithms (SEA)*, Vol. 233. 4:1–4:20.

    <https://doi.org/10.4230/LIPICS.SEA.2022.4>
- Elias (1974) Peter Elias. 1974.

    Efficient Storage and Retrieval by Content and Address of Static Files.

    *J. ACM*21, 2 (1974), 246–260.

- Fan et al. (2014) Bin Fan, David G. Andersen, Michael Kaminsky, and Michael Mitzenmacher. 2014.

    Cuckoo Filter: Practically Better Than Bloom. In *Proc. 10th ACM International on Conference on emerging Networking Experiments and Technologies (CoNEXT)*. 75–88.

    <https://doi.org/10.1145/2674005.2674994>
- Fano (1971) Robert Mario Fano. 1971.

    *On the Number of Bits Required to Implement an Associative Memory*.

    Massachusetts Institute of Technology, Project MAC.

- Goodwin et al. (2017) Bob Goodwin, Michael Hopcroft, Dan Luu, Alex Clemmer, Mihaela Curmei, Sameh Elnikety, and Yuxiong He. 2017.

    BitFunnel: Revisiting Signatures for Search. In *Proc. 40th International ACM Conference on Research and Development in Information Retrieval (SIGIR)*. 605–614.

    <https://doi.org/10.1145/3077136.3080789>
- Goswami et al. (2014) Mayank Goswami, Allan Grønlund, Kasper Green Larsen, and Rasmus Pagh. 2014.

    Approximate Range Emptiness in Constant Time and Optimal Space. In *Proc. 26th Annual ACM-SIAM Symposium on Discrete Algorithms (SODA)*. 769–775.

- Graf and Lemire (2020) Thomas Mueller Graf and Daniel Lemire. 2020.

    Xor Filters.

    *ACM J. Exp. Algorithmics*25 (2020), 1–16.

    <https://doi.org/10.1145/3376122>
- Jacobson (1989) Guy Jacobson. 1989.

    Space-efficient Static Trees and Graphs. In *Proc. 30th IEEE Symposium on Foundations of Computer Science (FOCS)*. 549–554.

    <https://doi.org/10.1109/SFCS.1989.63533>
- Knorr et al. (2022) Eric R. Knorr, Baptiste Lemaire, Andrew Lim, Siqiang Luo, Huanchen Zhang, Stratos Idreos, and Michael Mitzenmacher. 2022.

    Proteus: A Self-Designing Range Filter. In *Proc. 48th ACM International Conference on Management of Data (SIGMOD)*. 1670–1684.

    Code available at <https://github.com/Eric-R-Knorr/Proteus>.

- Kornaropoulos et al. (2022) Evgenios M. Kornaropoulos, Silei Ren, and Roberto Tamassia. 2022.

    The Price of Tailoring the Index to Your Data: Poisoning Attacks on Learned Index Structures. In *Proc. 48th International Conference on Management of Data (SIGMOD)*. 1331–1344.

    <https://doi.org/10.1145/3514221.3517867>
- Kurpicz (2022) Florian Kurpicz. 2022.

    Engineering Compact Data Structures for Rank and Select Queries on Bit Vectors. In *Proc. 29th International Symposium on String Processing and Information Retrieval (SPIRE)*. 257–272.

    <https://doi.org/10.1007/978-3-031-20643-6_19>
- Luo et al. (2019) Lailong Luo, Deke Guo, Richard T. B. Ma, Ori Rottenstreich, and Xueshan Luo. 2019.

    Optimizing Bloom Filter: Challenges, Solutions, and Comparisons.

    *IEEE Commun. Surv. Tutorials*21, 2 (2019), 1912–1949.

    <https://doi.org/10.1109/COMST.2018.2889329>
- Luo et al. (2020) Siqiang Luo, Subarna Chatterjee, Rafael Ketsetsidis, Niv Dayan, Wilson Qin, and Stratos Idreos. 2020.

    Rosetta: A Robust Space-Time Optimized Range Filter for Key-Value Stores. In *Proc. 46th ACM International Conference on Management of Data (SIGMOD)*. 2071–2086.

- Matsunobu et al. (2020) Yoshinori Matsunobu, Siying Dong, and Herman Lee. 2020.

    MyRocks: LSM-tree database storage engine serving facebook’s social graph.

    *PVLDB*13, 12 (2020), 3217–3230.

- Mößner et al. (2023) Bernhard Mößner, Christian Riegger, Arthur Bernhardt, and Ilia Petrov. 2023.

    bloomRF: On Performing Range-Queries in Bloom-Filters with Piecewise-Monotone Hash Functions and Prefix Hashing. In *Proc. 26th International Conference on Extending Database Technology (EDBT)*. 131–143.

    <https://doi.org/10.48786/edbt.2023.11>
- Navarro (2016) Gonzalo Navarro. 2016.

    *Compact data structures: a practical approach*.

    Cambridge University Press.

- Navarro and Rojas-Ledesma (2020) Gonzalo Navarro and Javiel Rojas-Ledesma. 2020.

    Predecessor Search.

    *ACM Comput. Surv.*53, 5, Article 105 (2020), 35 pages.

    <https://doi.org/10.1145/3409371>
- Okanohara and Sadakane (2007) Daisuke Okanohara and Kunihiko Sadakane. 2007.

    Practical Entropy-Compressed Rank/Select Dictionary. In *Proc. 9th Workshop on Algorithm Engineering and Experiments (ALENEX)*. 60–70.

    <https://doi.org/10.1137/1.9781611972870.6>
- Ottaviano and Venturini (2014) Giuseppe Ottaviano and Rossano Venturini. 2014.

    Partitioned Elias-Fano indexes. In *Proc. 37th International ACM Conference on Research and Development in Information Retrieval (SIGIR)*. 273–282.

    <https://doi.org/10.1145/2600428.2609615>
- Pagh et al. (2005) Anna Pagh, Rasmus Pagh, and S. Srinivasa Rao. 2005.

    An Optimal Bloom Filter Replacement. In *Proc. 16th Annual ACM-SIAM Symposium on Discrete Algorithms (SODA)*. 823–829.

- Pibiri and Venturini (2017) Giulio Ermanno Pibiri and Rossano Venturini. 2017.

    Dynamic Elias-Fano Representation. In *Proc. 28th Annual Symposium on Combinatorial Pattern Matching (CPM)*. 30:1–30:14.

    <https://doi.org/10.4230/LIPIcs.CPM.2017.30>
- Porat (2009) Ely Porat. 2009.

    An Optimal Bloom Filter Replacement Based on Matrix Solving. In *Proc. 4th International Computer Science Symposium in Russia on Computer Science - Theory and Applications (CSR)*. 263–273.

    <https://doi.org/10.1007/978-3-642-03351-3_25>
- Tarkoma et al. (2012) Sasu Tarkoma, Christian Esteve Rothenberg, and Eemil Lagerspetz. 2012.

    Theory and Practice of Bloom Filters for Distributed Systems.

    *IEEE Commun. Surv. Tutorials*14, 1 (2012), 131–155.

    <https://doi.org/10.1109/SURV.2011.031611.00024>
- Vaidya et al. (2022) Kapil Vaidya, Subarna Chatterjee, Eric R. Knorr, Michael Mitzenmacher, Stratos Idreos, and Tim Kraska. 2022.

    SNARF: A Learning-Enhanced Range Filter.

    *PVLDB*15, 8 (2022), 1632–1644.

    <https://doi.org/10.14778/3529337.3529347> Code available at <https://github.com/kapilvaidya24/SNARF>.

- Vigna (2013) Sebastiano Vigna. 2013.

    Quasi-Succinct Indices. In *Proc. 6th ACM International Conference on Web Search and Data Mining (WSDM)*. 83–92.

    <https://doi.org/10.1145/2433396.2433409>
- Wang et al. (2023) Ziwei Wang, Zheng Zhong, Jiarui Guo, Yuhan Wu, Haoyu Li, Tong Yang, Yaofeng Tu, Huanchen Zhang, and Bin Cui. 2023.

    REncoder: A Space-Time Efficient Range Filter with Local Encoder. In *Proc. 39th IEEE International Conference on Data Engineering (ICDE)*.

    <https://doi.org/10.1109/ICDE55515.2023.00158>
- Wegman and Carter (1981) Mark N. Wegman and J. Lawrence Carter. 1981.

    New Hash Functions and Their Use in Authentication and Set Equality.

    *J. Comput. System Sci.*22, 3 (June 1981), 265–279.

    <https://doi.org/10.1016/0022-0000(81)90033-7>
- Zhang et al. (2018) Huanchen Zhang, Hyeontaek Lim, Viktor Leis, David G. Andersen, Michael Kaminsky, Kimberly Keeton, and Andrew Pavlo. 2018.

    SuRF: Practical Range Query Filtering with Fast Succinct Tries. In *Proc. 44th ACM International Conference on Management of Data (SIGMOD)*. 323–336.

    Code available at <https://github.com/efficient/SuRF>.

- Zhang et al. (2020) Huanchen Zhang, Hyeontaek Lim, Viktor Leis, David G. Andersen, Michael Kaminsky, Kimberly Keeton, and Andrew Pavlo. 2020.

    Succinct Range Filters.

    *ACM Trans. Database Syst.*45, 2 (2020), 5:1–5:31.

    <https://doi.org/10.1145/3375660>
- Zhang et al. (2021) Huanchen Zhang, Hyeontaek Lim, Viktor Leis, David G. Andersen, Michael Kaminsky, Kimberly Keeton, and Andrew Pavlo. 2021.

    Succinct range filters.

    *Commun. ACM*64, 4 (2021), 166–173.

    <https://doi.org/10.1145/3450262>

[](javascript:toggleReadingMode(); "Disable reading mode, show header and footer")