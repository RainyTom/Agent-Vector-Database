# Pivot filtering原文节选与阅读边界

原始正式全文：https://www.vldb.org/pvldb/vol10/p1058-gao.pdf
DOI：https://doi.org/10.14778/3115404.3115411
版本：PVLDB10(10),2017。只核§2.3、§5.1–5.2及其上下文，不声称全实验复核。
用途：确认距离到pivot的L∞下界、球区域排除及pivot距离区间/树组织已有前序；不把该论文的metric filtering混同为本项目属性过滤。OMNI原始出版入口未提供可读正文，故不冒称已读OMNI2001全文。

## §2.3（PDF2–3）


Using well-chosen pivots, the objects in a metric space can be mapped to data points in a vector space. Given a pivot set *P* = {*p1*, *p2*, …, *pl*}, a metric space (*M*, *d*) can be mapped to a vector space (*R<sup>l</sup>*, *L*¥). Specifically, an object *q* in the metric space is represented as a point f(*q*) = á*d*(*q*, *p1*), *d*(*q*, *p2*), …, *d*(*q*, *pl*)ñ in the vector space. Consider the example in Fig. 1, where the *L2-norm* is used. If *P* = {*o1*, *o6*}, the object set in the original metric space (as illustrated in Fig. 1(a)) can be mapped to the data points in a two-dimensional vector space (as depicted in Fig. 1(b)), in which the *x*-axis denotes *d*(*oi*, *o1*) and the *y*-axis represents *d*(*oi*, *o6*) for any object *oi*. As an example, object *o5* is mapped to point á2, 4ñ. Based on the pivot mapping, the pivot-based filtering [12] can be used to avoid unnecessary similarity computations. **LEMMA 1 (PIVOT FILTERING).** *Given a set P of pivots*, *a query* *object q*, *and a search radius r*, *let SR*(*q*) *be a search region such* *that SR*(*q*) = {á*v1*, *v2*, …, *vl*ñ | 1 £ *i* £ *l* Ù *vi* ³ 0 Ù *vi* Í [*d*(*q*, *pi*) – *r*, *d*(*q*, *pi*) + *r*]}. *If* f(*o*) *locates outside SR*(*q*), *then o* Ï *MRQ*(*q*, *r*). PROOF. Assume, to the contrary, that there exists an object *o* (Î *MRQ*(*q*, *r*)) which satisfies *d*(*q*, *o*) ≤ *r*, but f(*o*) Ï *SR*(*q*) (i.e., $ *pi*

<!-- Page 3 -->

*y y y* *y*

|||o₁|SR(q)||||||
|---|---|---|---|---|---|---|---|---|
|6||6||6||6|||
|5||5||5|Ball partition|5|Hyperplane||
|4||4||4|region R|4|partition region R||
|3||3||3||3|||
|2||2||2||2|||
|1||1||1||R .r 1|||
|0 1 2|3 4 5 6|x 0 1|2 3 4 5|6 x 0 1|2 3 4 56x|0 1|3 4 5|6 x|
|(a) Oringal metric space||(b) Mapped vector space||(a) Range-pivot filtering||(b) Double-pivot filtering|||

6 6 6 6 *o₂* *o₄*

<sub>i</sub> <sub>i</sub> *q o* *o₉* *o₅* *3* *o₉ o₉* *o₉* *q q* *o₄ q r* *o₄* *o₄* *o₈ o₈* *o₈* *ro* *o₇* *o₈* *o₂3 o₂* *o₇* *o₂ o₃* *or₃* *o₇ o₇* <sub>i</sub> *o₆ o₁ o₅ o₆* *o₁ o₅ o₆ o₆ o₁ o₅* 2

**Figure 1. Pivot mapping Figure 2. Pivot filtering Figure 3. LEASA**

Î *P*, *d*(*o*, *pi*) > *d*(*q*, *pi*) + *r* or *d*(*o*, *pi*) < *d*(*q*, *pi*) – *r*). According to the triangle inequality, *d*(*q*, *o*) ³ |*d*(*q*, *pi*) – *d*(*o*, *pi*)| > *r*, which contradicts our assumption. The proof completes. o

Since the pre-computed distances f(*o*)s are stored together with object *o*, we can avoid distance computations involving object *o* if f(*o*) Ï*SR*(*q*), based on Lemma 1. Consider the example in Fig. 1(b) where the dotted rectangle represents the search region *SR*(*q*). Here, object *o1* can be pruned as f(*o1*) Ï *SR*(*q*). Also, Lemma 1 can be utilized to prune an entire region (i.e., a minimum bounding box that contains multiple f(*o*)) if it does not intersect *SR*(*q*). To obtain compact regions, two typical techniques, i.e., *ball* *partitioning* and *generalized hyperplane partitioning*, are used [30].

**DEFINITION 3 (BALL PARTITIONING).** *Let Ri.p be the pivot for* *a partition region Ri*, *and let Ri.r be the radius of Ri*. *Then the set* *of objects o* (Î *O*) *in the partition Ri*, *obtained via ball* *partitioning*, *is defined as* {*o* | *o* Î *O* Ù *d*(*o*, *pi*) ≤ *Ri.r*}.

Based on the definition of ball partitioning, a range-pivot filtering technique [30] can be developed as follows.

**LEMMA 2 (RANGE-PIVOT FILTERING).** *Given a ball* *partitioning region Ri*, *a query object q*, *and a search radius r*, *if* *d*(*q*, *Ri.p*) *> Ri.r + r*, *then Ri can be pruned safely*.

PROOF*.* For any object *o* in *Ri*, if *d*(*q*, *Ri.p*) *> Ri.r + r*, then *d*(*q*,

*o*) ≥ *d*(*q*, *Ri.p*) – *d*(*o*, *Ri.p*) > *Ri.r + r* – *d*(*o*, *Ri.p*) due to the triangle inequality. As *d*(*o*, *Ri.p*) ≤ *Ri.r* according to Definition 3, then *d*(*q*, *oj*) > *r*. Hence, any object *o* in *Ri* cannot be in the final result set, and *Ri* can be pruned safely. o Consider the ball partitioning example depicted in Fig. 2(a), where the red solid circle denotes the ball region *Ri* with *Ri*.*p* = *o7*, *Ri*.*r* = *d*(*o7*, *o6*), and *Ri* = {*o6*, *o7*, *o8*}. As *d*(*q*, *Ri.p*) *> Ri.r + r*, *Ri* can be pruned away according to Lemma 2.
#### DEFINITION 4 (GENERALIZED HYPERPLANE PARTITIONING).

*Given a set P of pivots*, *let pi be the corresponding pivot for a* *partition region Ri*. *Then the set of objects o* (Î *O*) *in the partition* *Ri*, *obtained by the generalized hyperplane partitioning*, *is defined* *as* {*o* | *o* Î *O* Ù "*pj* ¹ *pi*, *d*(*o*, *pi*) ≤ *d*(*o*, *pj*)}.

Based on the definition of generalized hyperplane partitioning, a double-pivot filtering technique [30] is developed as follows.

**LEMMA 3 (DOUBLE-PIVOT FILTERING).** *Given two pivots pi and* *pj*, *a query object q*, *and a search radius r*, *if d*(*q*, *pi*) – *d*(*q*, *pj*) > *2* ´ *r*, *then Ri can be pruned safely*, *as pi is the corresponding pivot* *for the partition region Ri*.

PROOF. For every *o* in *Ri*, according to the definition of *Ri*, *d*(*o*, *pi*) ≤ *d*(*o*, *pj*). Based on the triangle inequality, we have *d*(*q*, *pi*) ≤ *d*(*o*, *pi*) + *d*(*q*, *o*) and *d*(*q*, *pj*) ≥ *d*(*o*, *pj*) – *d*(*q*, *o*). Thus, we can derive that *d*(*q*, *pi*) – *d*(*q*, *pj*) ≤ *d*(*o*, *pi*) + *d*(*q*, *o*) – *d*(*o*, *pj*) + *d*(*o*, *q*)

≤ 2 ´ *d*(*q*, *o*) as *d*(*o*, *pi*) ≤ *d*(*o*, *pj*). If *d*(*q*, *pi*) – *d*(*q*, *pj*) > *2* ´ *r*, then *d*(*q*, *o*) > *r*. Therefore, no object *o* (Î *Ri*) can be a real answer object (i.e., *o* Ï *MRQ*(*q*, *r*)), and *Ri* can be pruned safely. o Consider the generalized hyperplane partitioning example in Fig. 2(b). Assume *o2* and *o6* are two pivots, and *Ri* = {*o6*, *o7*, *o8*, *o9*} is the hyperplane partition region corresponding to pivot *o6*. Since *d*(*q*, *o6*) – *d*(*q*, *o2*) > 2 ´ *r*, *Ri* can be discarded safely according to Lemma 3. Lemmas 1 through 3 are pivot filtering techniques. Nonetheless, a distance computation is still needed for verifying each object that cannot be pruned. Hence, a validation technique [12] is proposed to save unnecessary verifications, as stated in Lemma 4 below. **LEMMA 4 (PIVOT VALIDATION).** *Given a pivot set P*, *a query* *object q*, *and a search radius r*, *if there exists*, *for an object o in O*, *a pivot pi* (Î *P*) *satisfying d*(*o*, *pi*) £ *r* – *d*(*q*, *pi*), *then o is validated* *to be an actual answer object for MRQ*(*q*, *r*). PROOF. Given a query object *q*, an object *o*, and a pivot *pi*, *d*(*q*,

*o*) £ *d*(*o*, *pi*) + *d*(*q*, *pi*) because of the triangle inequality. If *d*(*o*, *pi*) £ *r* – *d*(*q*, *pi*), then *d*(*q*, *o*) £ *r* – *d*(*q*, *pi*) + *d*(*q, pi*) = *r*. Thus, *o* is guaranteed to be contained in the final result set. o


## §5.1–5.2（PDF5–6）


The PM-tree combines the pivot mapping and the M-tree, where the M-tree is used to cluster the objects, and the pivot mapping is utilized to avoid unnecessary distance computations. Hence, different from the M-tree introduced in Section 3.3, each

<!-- Page 6 -->

*y* *y* *o₁* 6

|Pivot table||e<sub>2</sub>.r e₂|.ptr e₂. RO e₂.|PD MBB (e₂|6) 5|o₄ M₃|||5|||Cluster-tree|C₁ C₂|M2|MBB minkey maxkey 10 10+||
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
||N₀|N2 r₂|o₆ 8|M₂||o₂|q M₆||4||||||||
|oP₁|e₁|e₂ <sub>6</sub>.r e₆ e|.ptr e₆. RO|e₆. PD MBB|4 (e<sub>6</sub>) 3|M₁ o₅ M₄|o₃|o₉|C₁ 3||C₂ o₉|Pivot table P|N₀|e₁ e₂|<sub>6</sub> e₆ .key e|.ptr|
|o₆|N₁ e₃ e₄|r₆ N₂ e₆ e₅|d o₈ N6 oj oid o|(o<sub>8</sub>,o₆) M₆ d <sub>9</sub>. PD|(o<sub>9</sub>, o₆) 2 1||o₈|M₂ o₇|o₄ 2 o₂ 1|o₈ o₃|o₇|o₁ o₆|N₁ e₃ e₄|N₂ e₆ e₅|N6 10+ e<sub>16</sub>|.ptr .key e₁₆|
|N₃|N₄|N₅ N₆|o₉ 9|r₆ 5 d (o₉,|o₁)|||M₅ o₆|o₁|o₅|o₆|N₃|N₄|N₅₁₃|10+ N|o₉|
|o₁ o₂ o₄|o₃ o₅|o₆ o₇ o₈|o₉||0|1 2|3 5 4|x 6|0 1|3 4 2|x 5 6|e₇|e₈ e₉ e₁₁|e₁₂ e e|e₁₅ e₁₆ 14 6|+ B -tree|
||Pivot table oP₁ o₆|N₁ e₃ N₃ N₄ e₇ e₈ e₉|(a) PM-tree structure (b) MBB Figure 10. PM-tree N₀ e₁ e₂ N₂ e₄ e₅ N₅ e₁₁ e₁₂ e₁₃|R-tree e₆ .ptr e N6 e₆ N₆ e₁₄ e₁₅|<sub>6</sub>.MBB M₆ e₁₆ .ptr e o₉ e₁₆|<sub>16</sub>.MBB R(o₉)|||0 2 d (o<sub>i</sub>, o₁) yo₁ 6 5 o₂ 4 M₁ o₅ 3 2|10 + d +d o₄ o₃ o₈|10+ (o<sub>i</sub>, o<sub>6</sub>) o₉|o₁ o₂ (a) Hyperplane partitioning (b) M-index structure C<sub>1,2</sub> C<sub>1,3</sub>|o₅ o₄ C₁ C₂ ... C<sub>1,n</sub>|o₆ o₇ o₃ Cluster-tree ... C<sub>n</sub> C<sub>2,1</sub>|o₉ o₈ Level =1 ... C<sub>2,3</sub>|RAF Level = 2 C<sub>2,n</sub>|

*id len obj*

|o₁ o₂ o₄ o₃|o₅ o₆ o₇ o₈|o₉|2 8 o₉|M₂ o₆|||Level = 3|
|---|---|---|---|---|---|---|---|
|||||xC|C ... C|C C|... C|

**RAF** *o₁ o₂ o₄ o₃ o₅ o₆ o₇ o₈ o₉* 2 8 *o₉*

**Figure 11. OmniR-tree**

leaf entry of the PM-tree stores the mapped vector (i.e., the pre-computed distances to the pivots) with the real data object. In each intermediate entry, the PM-tree stores a minimum bounding box (MBB) that bounds all the mapped vectors in its child leaf entries. Specifically, given a pivot set *P =* {*pi|* 1 *≤ i ≤ n*}, *MBB*(*e*) = {[*ai*, *bi*] | 1 *≤ i ≤ n*}, where *ai* = *min*{*d*(*o*, *pi*) | *o* Î *e*}, and *bi* = *max*{*d*(*o*, *pi*) | *o* Î *e*}. Fig. 10 depicts an example of PM-tree, with the data distribution shown in Fig. 6(c). **MRQ processing.** To answer MRQ(*q*, *r*), the entries in the PM-tree are visited in depth-first fashion. When an intermediate entry is accessed, we verify its child entries using Lemmas 1 and 2; and when a leaf entry is accessed, we insert the corresponding object into the result set if it is not discarded by Lemma 1. **M*k*NNQ processing.** To answer M*k*NNQ(*q*, *k*), the entries in the PM-tree are traversed in best-first manner, where Lemmas 1 and 2 are employed to eliminate unqualified entries. We first set the search radius to infinity, and then, update the search radius during the search using the visited objects. **Discussion.** The PM-tree stores the data objects in its entries instead of in a separate file, which limits its usability. In particular, for complex objects, the PM-tree needs a large page/node size.

## 5.2 Omni-Family

Unlike the PM-tree, Omni-family stores objects in a separate random access file (RAF), to avoid the impact of the object size. It also utilizes the sequential file, the B<sup>+</sup>-tree, or the R-tree, to index the vectors after the pivot mapping. A sequential file stores the pre-computed distances of objects in order of their identifiers; a B<sup>+</sup>-tree indexes the pre-computed distances for each pivot; and an R-tree indexes the pre-computed distances for all the pivots together. As verified in [17], the OmniR-tree performs the best in most cases. Fig. 11 depicts an example of OmniR-tree, including a pivot table that stores the pivots, an R-tree that indexes the pre-computed distances, and an RAF that stores the objects. The MBB of each R-tree node is shown in Fig. 10(b). **MRQ processing.** To answer MRQ(*q*, *r*), the entries in the R- tree are visited in depth-first fashion. When an intermediate entry is visited, we prune its child entries using Lemma 1; and when a leaf entry is accessed, we compute the actual distance of the corresponding object and insert it into the result set if not pruned.

*o₇* 1*Level = 3* *M₂ o₆* 0 1 2 3 4 5 6 <sub>1,3,2</sub> <sub>1,3,4</sub> <sub>1,n,n-1</sub> <sub>2,3,1</sub> <sub>2,3,4</sub> <sub>2,3,n</sub>

(c) MBB (d) Dynamic cluster-tree
**Figure 12. M-index**

**M*k*NNQ processing.** To answer M*k*NNQ(*q*, *k*), the entries in the R-tree are visited in best-first manner, i.e., in ascending order of their minimum distances to the query object *q*, where Lemma 1 is used to eliminate unqualified entries. Here, we set the search radius to infinity and then update it using the visited objects. **Discussion.** The Omni-family contains the Omni-sequential-file, the OmniB<sup>+</sup>-tree, and the OmniR-tree. Omni-sequential-file can be regarded as disk-based LAESA, which incurs substantial I/O during search as the data is not clustered. The OmniB<sup>+</sup>-tree needs one B<sup>+</sup>-tree for every pivot, resulting in redundant storage and I/O during search. The OmniR-tree utilizes MBBs to cluster the data, and uses the pivot filtering to improve query efficiency.

