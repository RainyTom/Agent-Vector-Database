# potion-base-8M workload rebuild and baseline re-run (2026-10-25)

Workload: `production/semantic-mobility-search/work/all6_8760h_s50_potion`
(six cities, `--queries 500 --window-hours 8760 --spatial-km 50 --seed 20261022`).
Bench output: `production/semantic-mobility-search/work/bench_out_potion/`.
Summary table: `summary.csv` (this directory).

## Encoder setup

- venv `production/semantic-mobility-search/venv_potion` (Python 3.14.4, 134 MB):
  `model2vec 0.10.0`, `numpy 2.5.4`. No torch (`import torch` -> `ModuleNotFoundError`).
- Model `models/potion-base-8M` downloaded by curl from
  `https://huggingface.co/minishlab/potion-base-8M/resolve/main/`:
  `model.safetensors` 30,236,760 B, `tokenizer.json` 683,666 B,
  `config.json`, `modules.json`, `special_tokens_map.json`, `tokenizer_config.json`.
- Local load verified: `StaticModel.from_pretrained("models/potion-base-8M")` in 0.303 s,
  `dim = 256`; encode of 3 strings 2.4 ms, outputs unit norm
  (`cos("cafe Starbucks", "cafe Coffee Shop") = 0.836`).

## Embedding construction (`src/prep_potion.py`, new file)

- key = `(venue_category, name)`, encoded as the text `"<category> <name>"`.
- Distinct keys: **179,616** (from 724 categories, 170,025 distinct names over
  1,838,793 used rows). Each key encoded to 256-d potion, then **row-wise** L2
  normalization (`norm(axis=1)`), never a matrix Frobenius norm.
- PCA: column-mean centering, `numpy.linalg.svd` on the (179,616 x 256) float32
  matrix, projection on the top 128 right singular vectors, deterministic sign
  (largest-|component| of each component vector made positive), projections
  re-normalized per row. Cache: `work/all6_8760h_s50_potion/key_emb_128.npz`
  (234 MB), reused on re-run when the key set matches.
- Resulting point embeddings: norms in [0.99999982, 1.00000012]; 178,698 unique
  rows out of 179,616 keys. The 918 duplicate rows (1,760 keys) are legitimate
  tokenizer equivalences such as `Braun Buffel` / `Braun Büffel`,
  `A & W` / `A&W`, `Sate Domba Afrika` / `Sate domba afrika` — the encoder maps
  them to the same vector. No distinct key collapses because of normalization.

### prep.py vs prep_potion.py (same protocol)

| | prep.py (old) | prep_potion.py |
|---|---|---|
| embedding | 64-d PPMI category + 64-d hashed name | 256-d potion per (category, name) -> PCA 128-d |
| cold prep wall time | not measured here | 46.42 s (cache absent) |
| warm prep wall time (cache hit) | — | 42.15 s |
| peak RSS | — | 4.66 GiB cold (4,882,224 KB), 3.25 GiB warm (3,404,828 KB) |

Peak RSS is dominated by the 1.84 M source rows and the (179,616 x 256) SVD;
it is a prep-time cost only, not a retrieval cost.

## Workload identity check (queries and points)

The new binaries have identical sizes to the old workload
(`points.bin` 985,593,056 B, `queries.bin` 977,532 B, `users.bin` 504,676 B,
`queries_cat.bin` 12,008 B, `user_city.bin` 252,344 B).

- `users.bin`, `user_city.bin`, `queries_cat.bin`: **byte-identical** (md5 match)
  to `work/all6_8760h_s50/`.
- `points.bin`: all 1,838,793 records match on `uid, ts, lat, lon, cat`
  (compared with a numpy structured memmap); only the 128-d embedding changes.
  Header `(n=1838793, D=128)`.
- `queries.bin`: header `(500, 128, 6)` and, for all 500 queries, all of
  `m, source, ws, we, dur, tol, bbox, tau` are identical; only the embeddings
  differ.

Query-set verification via dump mode:

```
./work/bench --dir work/all6_8760h_s50        --mode dump --out /tmp/qdump_old_s50
./work/bench --dir work/all6_8760h_s50_potion --mode dump --out /tmp/qdump_potion
diff /tmp/qdump_old_s50/queries.csv /tmp/qdump_potion/queries.csv  -> no difference
```

So `ws/we/bbox/source` (and `m/dur/tol/tau`) match the old workload exactly;
the only changed input is the semantic embedding channel.

## Bench runs (existing `work/bench` binary, unmodified)

```
./work/bench --dir work/all6_8760h_s50_potion --mode gt      --out work/bench_out_potion
./work/bench --dir work/all6_8760h_s50_potion --mode space   --out work/bench_out_potion
./work/bench --dir work/all6_8760h_s50_potion --mode catalog --topc 300 --cap-hours 48 --out work/bench_out_potion
./work/bench --dir work/all6_8760h_s50_potion --mode ann-post --topn 300 --cand 1250 --out work/bench_out_potion
```

`ann-post` was run in addition to the three specified modes so that the four
standard baselines (e1, b2, b3, catalog) are all present; groundtruth.csv from
the gt run was used for the recall of b2/catalog/b3.

Raw SUMMARY lines from the binary:

```
SUMMARY e1_time_first n=500 recall=1 mean_us=25159.7 p95_us=50176.6 qps=0.0397461 mean_cand=63084 mean_dtw=11612.1 empty=0
SUMMARY b2_space_first n=500 recall=1 mean_us=15263.3 p95_us=37462.5 qps=0.0655166 mean_cand=11675.5 mean_dtw=11612.1 empty=0
SUMMARY catalog n=500 recall=0.9894 mean_us=10367.4 p95_us=23302.5 qps=0.0964561 mean_cand=54597.3 mean_dtw=293.506 empty=0
SUMMARY b3_ann_postfilter n=500 recall=0.3128 mean_us=3226.42 p95_us=10455.2 qps=0.309941 mean_cand=603.85 mean_dtw=1734.73 empty=9
BUILD_S 52.7557
CATALOG spans=6213067 build_s=0.85053 bytes=2.98227e+08 mean_slice=54597.3 mean_filter=11673.3 mean_verify=293.506 topc=300 cap_h=48
```

Note: the binary's `qps` field is `1000/mean_us` (a kQPS value printed under the
name qps). `summary.csv` uses `1e6/mean_us`, consistent with the earlier
`2026-10-24-exps/data/summary.csv`.

### summary.csv (from bench_out_potion/*.csv)

| method | recall@10 | mean_us | p95_us | qps | mean_cand | mean_dtw | empty |
|---|---|---|---|---|---|---|---|
| e1_time_first | 1.0000 | 25159.7 | 50176.6 | 39.75 | 63084.0 | 11612.1 | 0 |
| b2_space_first | 1.0000 | 15263.3 | 37462.5 | 65.52 | 11675.5 | 11612.1 | 0 |
| catalog (topc=300, cap=48h) | 0.9894 | 10367.4 | 23302.5 | 96.46 | 54597.3 | 293.5 | 0 |
| b3_ann_postfilter (topn=300, cand=1250) | 0.3128 | 3226.4 | 10455.2 | 309.94 | 603.9 | 1734.7 | 9 |

Resource use of the bench runs (wall, peak RSS):

| mode | wall | peak RSS |
|---|---|---|
| gt | 13.68 s | 984 MiB |
| space | 8.39 s | 962 MiB |
| catalog | 8.31 s | 1.82 GiB (span build) |
| ann-post | 55.10 s | 2.27 GiB (HNSW build 52.76 s) |

## Comparison with the old pilot embedding (context only)

Same protocol, old pilot embedding (`2026-10-24-exps/data/summary.csv`,
`all6_8760h_s50`): e1 mean_us 24,414.9 -> 25,159.7 (+3.1%), b2 15,440.9 ->
15,263.3 (-1.2%). Candidate sets and DTW counts are unchanged (e1/b2
recall = 1 by construction), so these differences are within run-to-run noise;
the semantic channel changes *which* candidates approximate methods return.

Catalog recall with potion is 0.9894, i.e. its DTW upper-bound pruning loses a
true neighbour on ~1% of queries; no old-embedding catalog run exists for this
exact `all6_8760h_s50` workload (`work/bench_out_all6_8760h_s50/` was never
produced), so no old-vs-new catalog delta is claimed here.

## Failures

None. No crashes, no empty-result regressions beyond the 9 empty queries already
inherent to b3 at topn=300 (mean_verify 293.5 < 300 on average). All four runs
read the new workload as D=128 without modification to the C++ binary.
