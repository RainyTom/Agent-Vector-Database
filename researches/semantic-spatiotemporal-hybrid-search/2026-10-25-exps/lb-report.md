# Lower-bound pruning for exact episode verification

Date: 2026-10-25. Scope: analysis only; no `production/` changes. Artifacts:
`lb_analysis.py`, `data/lb/lb_stats.csv`, this report.

## 1. Question

Given the exact score of an episode (DTW with steps $(1,1)/(0,1)$ only,
$\lambda=0$, divided by $m$), can a per-episode lower bound skip exact
verification without losing the top-$k$?

Two candidates are measured on every feasible episode:

$$
\mathrm{LB1}(Q,E)=\frac1m\sum_{i=1}^{m}\max\bigl(0,\lVert q_i\rVert-\max_{j}\lVert e_j\rVert\bigr),
$$

$$
\mathrm{LB2}(Q,E)=\frac1m\sum_{i=0}^{m-1}\;\min_{j\in[i,\;n-m+i]}\lVert q_i-e_j\rVert,
\qquad n=v-u+1 .
$$

The LB2 band is the exact reachable interval of the alignment path: with steps
$(1,1)$ and $(0,1)$ the path starts at $(0,0)$, ends at $(m-1,n-1)$ and never
skips a query point, so query index $i$ can only be paid against episode
indices $[i,\,n-m+i]$. (The task statement writes the band with a $\pm1$
index-convention shift; the tight reachable interval is used here, which is
the strongest valid version.) For $n=m$ the band collapses to $j=i$, so
$\mathrm{LB2}$ equals the exact score. Validity is checked numerically on all
90,743 spans (`LB2 <= true`, `LB1 <= true`) and on 200 random span shapes.

## 2. Workload and exactness checks

* Data: `production/semantic-mobility-search/work/all6_8760h_s50`
  (Massive-STEPS, 1.84 M check-ins, 63 k trajectories, $d=128$).
* Queries: `qid 0..29`. Only **10 of the 30** are New-York-scoped
  (0, 2, 5, 13, 15, 16, 17, 23, 28, 29; ground-truth users all have
  `user_city == 3`). The other 20 queries have bboxes and ground-truth users
  in Istanbul/Jakarta/Moscow/Petaling Jaya/São Paulo, so they contribute no
  NY-feasible span and are excluded.
* For each query, **all** feasible spans of NY trajectories are enumerated
  directly from the definition: points in $[w_s,w_e]$, every span point in the
  bbox, $|(t_b-t_a)-\Delta|\le\delta$, $b-a+1\ge m$ (no upper cap on segment
  count; ground truth used $n\le m+2$, which turns out to be inert here).
  The query's source user is excluded, matching `scan.cpp:286` and the
  ground-truth generator; without this, the source episode scores 0.
* Exact score reimplementation validation: top-10 per-trajectory scores match
  `groundtruth.csv` exactly for all 10 queries (`gt_top10_match = 1`,
  atol $10^{-5}$). The DTW recurrence was additionally validated against
  brute-force path enumeration on 200 random $(m,n)$ shapes.
* Totals: 90,743 feasible spans; 35.2 % have $n=m$; maximum $n=38$.

## 3. Results

Per query (τ = 10th-best span score among feasible spans; "prunable" = LB2 ≥ τ;
"verified" = spans actually scored by the sound LB2-ordered scan):

| qid | m | spans | frac n=m | LB2/true p10 | p50 | p90 | LB2/true p50 (n>m) | prunable @τ | verified (scan) | top-10 survive (strict) | LB1 prunable |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0  | 3 | 2,204  | 0.654 | 0.600 | 1.000 | 1.000 | 0.739 | 98.7 % | 29 (1.3 %)  | 100 % | 0 |
| 2  | 5 | 6,363  | 0.353 | 0.439 | 0.825 | 1.000 | 0.687 | 95.9 % | 260 (4.1 %) | 100 % | 0 |
| 5  | 3 | 7,819  | 0.570 | 0.500 | 1.000 | 1.000 | 0.603 | 99.1 % | 75 (1.0 %)  | 100 % | 0 |
| 13 | 3 | 8,982  | 0.408 | 0.370 | 0.750 | 1.000 | 0.585 | 98.2 % | 159 (1.8 %) | 100 % | 0 |
| 15 | 4 | 8,808  | 0.390 | 0.396 | 0.796 | 1.000 | 0.650 | 98.4 % | 138 (1.6 %) | 100 % | 0 |
| 16 | 4 | 39,934 | 0.246 | 0.312 | 0.658 | 1.000 | 0.558 | 98.8 % | 491 (1.2 %) | 100 % | 0 |
| 17 | 3 | 659    | 0.612 | 0.600 | 1.000 | 1.000 | 0.739 | 97.7 % | 16 (2.4 %)  | 100 % | 0 |
| 23 | 3 | 8,229  | 0.482 | 0.365 | 0.750 | 1.000 | 0.585 | 97.6 % | 196 (2.4 %) | 100 % | 0 |
| 28 | 5 | 2,609  | 0.337 | 0.391 | 0.807 | 1.000 | 0.618 | 85.4 % | 381 (14.6 %)| 100 % | 0 |
| 29 | 6 | 5,136  | 0.310 | 0.452 | 0.824 | 1.000 | 0.667 | 92.8 % | 370 (7.2 %) | 100 % | 0 |
| **all** | – | **90,743** | **0.352** | **0.357** | **0.750** | **1.000** | **0.588** | **97.7 %** | **2,115 (2.3 %)** | **100 %** | **0** |

Pooled LB1/true ratio: p50 $=2.0\times10^{-10}$, p90 $=3.3\times10^{-9}$,
max $=3.3\times10^{-8}$; 0 of 90,743 spans prunable at τ.

Additional pooled facts (all in `data/lb/lb_stats.csv`):

* Fraction of $n=m$ spans with $\mathrm{LB2}$ exactly equal to the true score:
  **1.000** (LB2 is not merely a bound there, it is the answer).
* Trajectory-level threshold (10th-best per-trajectory score, the official
  semantics) gives `prunable_frac_spans_trajtau_lb2` $=0.972$ pooled
  (0.816–0.991 per query).
* The sound LB2-ordered scan (sort by LB2, stop when
  $\mathrm{LB2} \ge$ current 10th-best true) verifies **2,115 / 90,743 = 2.3 %**
  of spans and returns the exact span-level top-10 on every query.
* Oracle survivor set ($\mathrm{LB2}\le\tau$): 2,118 spans, of which 100 are the
  true top-10 → precision **4.7 %**. Rank correlation of LB2 with true score:
  $-0.38$ pooled, $-0.15$ within $n>m$ spans (per query $-0.39$ to $+0.19$).
* 100 % of the true top-10 spans have $n=m$ on every query.
* Cost model (arithmetic units: 129 per 128-d distance, 2 per DP cell; the
  reachable band has $m(n-m+1)$ cells, the reference `dtw()` evaluates the
  full $m\times n$ matrix):
  a band-restricted exact DTW costs **53.9 %** of the reference; the LB2
  pipeline (band distances everywhere, band-only DP for the 2.3 % survivors)
  costs **53.2 %** of the reference and **98.6 %** of the band-restricted
  verifier. The bound therefore adds **0.7 percentage points** — 1.4 % of the
  verifier's own cost — on top of the free band restriction.

### Top-10 survival

Every true top-10 span has $\mathrm{LB2}\le$ true $\le\tau$. With the sound
rule "prune iff $\mathrm{LB}>\tau$" survival is **100 %** for LB2 and LB1.
With the literal rule "prune iff $\mathrm{LB}\ge\tau$" survival is 90 % for
LB2: the 10th span has $n=m$, hence $\mathrm{LB2}=\text{true}=\tau$, and is
dropped by the boundary comparison. That is a tie-handling artifact, not a
bound violation; the strict rule is the one that preserves the exact answer.

## 4. Verdict

**LB2 is valid and, at the span level, tight enough to prune.** It is exact
for the 35 % of spans with $n=m$ and retains a median 0.59 of the true score
for $n>m$; it removes 97.7 % of spans at the true top-10 threshold, and a
monotone LB2-ordered scan verifies only 2.3 % of spans while reproducing the
exact top-10. So, in answer to the direct question: **the bound is not too
loose to prune.**

**LB1 is unusable.** All query and segment embeddings are unit-norm
(PPMI-SVD rows; measured norms $0.9999999$–$1.0000001$), so
$\lVert q_i\rVert-\max_j\lVert e_j\rVert\approx0$ and LB1 is numerically zero
on every span. It prunes nothing.

**But "prunable" does not mean much cheaper.** LB2 has to evaluate exactly
the reachable band cells, $m(n-m+1)$ distances — the same cells a
band-restricted exact DTW needs. The only work that pruning removes is the DP
pass over those cells, which is $2/(d+1+2)\approx1.5\%$ of the band cost. In
the arithmetic model above, the LB2 pipeline costs 98.6 % of a
band-restricted exact verifier: 97.7 % of the DP passes are skipped, but that
is only ~1.4 % of the verification arithmetic. Against the *current*
`dtw()` — which evaluates the full $m\times n$ matrix including infeasible
cells — the pipeline looks 46 % cheaper, but 46.1 of those points come from
restricting evaluation to the band (a bound-free optimisation), not from the
bound itself. The bound also carries weak ordering information for $n>m$
spans (rank correlation $-0.15$, survivor precision 4.7 %); its strength comes
from being exact on the $n=m$ spans, which are the entire true top-10, and
from the extreme top-10 threshold.

Practical consequence: LB2 pays off only if the band distances are already
available from another stage — the ANN top-n distances computed per query
point (the direction taken by `lb.cpp`), or a per-user distance cache. In that
setting it removes 97.7 % of the remaining per-span work. For a from-scratch
exact scan, implement the band restriction first (46 % for free) and treat
LB2 as a secondary filter; tighten it for $n>m$ (e.g. a monotone
one-point-per-query DP bound that charges every segment) before relying on it
for a speedup.

## 5. Reproduction

```bash
python3 researches/semantic-spatiotemporal-hybrid-search/2026-10-25-exps/lb_analysis.py
```

Runtime ≈ 8 s (memory-maps only the contiguous NY block of `points.bin`,
~135 MB). Output: `data/lb/lb_stats.csv` with one row per query plus a pooled
`all` row. No `production/` files were modified.
