# Window / space sensitivity of the exact baselines and the episode catalog

Date: 2026-10-25. Binary: `production/semantic-mobility-search/work/bench` (unmodified,
rebuilt artifact already in `work/`). Data: 6-city Massive-STEPS, 1,838,793 points,
63,084 trajectories, 500 queries, `k=10`.

Three prepared workloads, one output dir each (`work/bench_out_<workload>/`):

| workload | window | spatial | meta |
|---|---|---|---|
| `all6_2160h_s50` | 2160 h (90 d) | 50 km | `window_hours=2160, spatial_km=50` |
| `all6_8760h_s10` | 8760 h (1 y) | 10 km | `window_hours=8760, spatial_km=10` |
| `all6_8760h` | 8760 h (1 y) | none (bbox = globe) | `window_hours=8760, spatial_km=0` |

Modes: `--mode gt` (exact time-first, E1), `--mode space` (exact space-first),
`--mode catalog --topc 300 --cap-hours 48`. All runs single-process, single-threaded,
one run per cell, on the same machine (WSL2, 10 cores, 11 GB RAM, ~8 GB free).

Aggregated numbers are in `data/scale/summary.csv`, produced by `parse_scale.py` from the
per-query CSVs (`time_us`, `cand_users`, `verified`, `dtw`, `hits`, `recall`). Mean/p95
follow the binary's own definitions (p95 = sorted index `floor(0.95*n)`).

## Results

| workload | method | Recall@10 | mean ms | p95 ms | qps | mean cand | mean verified | mean DTW evals |
|---|---|---|---:|---:|---:|---:|---:|---:|
| 90 d / 50 km | gt (time-first) | 1.0000 | 16.331 | 28.255 | 0.06 | 63,084 | 63,084 | 5,047.0 |
| 90 d / 50 km | space-first | 1.0000 | 8.955 | 20.675 | 0.11 | 11,675.5 | 11,675.5 | 5,047.0 |
| 90 d / 50 km | catalog topc300 cap48h | **0.9944** | **5.722** | 13.592 | 0.17 | 25,820.3 | 292.4 | 292.4 |
| 1 y / 10 km | gt (time-first) | 1.0000 | 28.579 | 54.588 | 0.03 | 63,084 | 63,084 | 8,940.0 |
| 1 y / 10 km | space-first | 1.0000 | 15.671 | 39.876 | 0.06 | 11,338.4 | 11,338.4 | 8,940.0 |
| 1 y / 10 km | catalog topc300 cap48h | **0.9946** | **5.351** | 15.246 | 0.19 | 49,962.2 | 291.7 | 291.7 |
| 1 y / no spatial | gt (time-first) | 1.0000 | 83.546 | 176.811 | 0.01 | 63,084 | 63,084 | 56,961.8 |
| 1 y / no spatial | space-first | 1.0000 | 192.616 | 287.773 | 0.01 | 63,084 | 63,084 | 56,961.8 |
| 1 y / no spatial | catalog topc300 cap48h | — | — | — | — | — | — | — |

`qps` is the binary's `1000/mean_us` (single-thread). `mean cand` is the CSV
`cand_users`: for the two exact modes the number of trajectories touched, for the catalog
the number of catalog spans in the duration/time slice (`mean_slice`). `mean verified` is
the number of exact DTW evaluations (`verified` column; identical to `dtw` in every file).

Catalog row for 1 y / no spatial has **no numbers**: the run was killed after 1800 s with no
`SUMMARY` and no `catalog.csv` (see Failure below).

## Raw SUMMARY lines (verbatim)

```
SUMMARY e1_time_first n=500 recall=1 mean_us=16330.6 p95_us=28255.3 qps=0.0612348 mean_cand=63084 mean_dtw=5046.99 empty=0
SUMMARY b2_space_first n=500 recall=1 mean_us=8955.36 p95_us=20675 qps=0.111665 mean_cand=11675.5 mean_dtw=5046.99 empty=0
SUMMARY catalog n=500 recall=0.9944 mean_us=5721.67 p95_us=13591.6 qps=0.174774 mean_cand=25820.3 mean_dtw=292.386 empty=0
CATALOG spans=6213067 build_s=1.09172 bytes=2.98227e+08 mean_slice=25820.3 mean_filter=5085.7 mean_verify=292.386 topc=300 cap_h=48

SUMMARY e1_time_first n=500 recall=1 mean_us=28579.2 p95_us=54588.1 qps=0.0349904 mean_cand=63084 mean_dtw=8939.95 empty=0
SUMMARY b2_space_first n=500 recall=1 mean_us=15671.2 p95_us=39876.3 qps=0.0638112 mean_cand=11338.4 mean_dtw=8939.95 empty=0
SUMMARY catalog n=500 recall=0.9946 mean_us=5351.24 p95_us=15246 qps=0.186873 mean_cand=49962.2 mean_dtw=291.724 empty=0
CATALOG spans=6213067 build_s=0.868259 bytes=2.98227e+08 mean_slice=49962.2 mean_filter=8997.07 mean_verify=291.724 topc=300 cap_h=48

SUMMARY e1_time_first n=500 recall=1 mean_us=83545.7 p95_us=176811 qps=0.0119695 mean_cand=63084 mean_dtw=56961.8 empty=0
SUMMARY b2_space_first n=500 recall=1 mean_us=192616 p95_us=287773 qps=0.00519168 mean_cand=63084 mean_dtw=56961.8 empty=0
```

The catalog on the unconstrained 1-year workload emitted no `SUMMARY` (see below).

## Runtime notes

Wall clock and peak RSS from `/usr/bin/time -v` (each run includes loading ~986 MB
`points.bin`):

| workload | mode | wall s | peak RSS |
|---|---|---:|---:|
| 90 d / 50 km | gt | 9.81 | 996 MB |
| 90 d / 50 km | space | 5.49 | 986 MB |
| 90 d / 50 km | catalog | 7.49 | 1.90 GB |
| 1 y / 10 km | gt | 15.74 | 1.00 GB |
| 1 y / 10 km | space | 8.84 | 986 MB |
| 1 y / 10 km | catalog | 6.53 | 1.90 GB |
| 1 y / no spatial | gt | 43.28 | 1.11 GB |
| 1 y / no spatial | space | 97.80 | 986 MB |
| 1 y / no spatial | catalog | >1800 (killed) | — |

- Catalog memory is ~2x the exact modes (spans + duration/bucket/grid maps: 6,213,067
  spans × 48 B = 298 MB raw, plus ~3 auxiliary containers).
- Catalog `mean_us` excludes one-time build (`build_s` 0.87–1.09 s). Amortized over the
  500-query batch that is 1.7–2.2 ms/query, i.e. 30–40% on top of the reported 5.4–5.7 ms;
  for a cold single query the build dominates. Build cost is identical across all three
  workloads (same points, query-independent), as the identical `spans=6213067` shows.
- Both exact modes evaluate exactly the same number of DTWs in every workload
  (5,047 / 8,940 / 56,962 per query on average). The exact-mode speedup therefore comes
  purely from how many trajectories are touched (11.7k vs 63k) plus per-trajectory bbox
  checks, not from doing less DTW work.
- With no spatial constraint, space-first is **slower** than time-first (192.6 ms vs
  83.5 ms): the spatial bbox filter degenerates and its candidate bookkeeping is pure
  overhead, while the time window still prunes by timestamp.
- `recall=1` for both exact modes is by construction (same problem, same scoring);
  space-first matching gt exactly is a correctness check, not an accuracy result.

## Failure: catalog on 1 y / no spatial

Command:
`./bench --mode catalog --dir all6_8760h --out bench_out_all6_8760h --k 10 --topc 300 --cap-hours 48`
was killed by the 1800 s timeout. It produced `groundtruth.csv`, `e1_time_first.csv` and
`b2_space_first.csv` in `bench_out_all6_8760h/`, but no `catalog.csv` and no `SUMMARY` line.
Nothing was fabricated for that cell.

Root cause (from `src/bench.cpp` `run_catalog`, code read only, not modified): stage 2
calls `grid_collect(nullptr, &space_n)` unconditionally, which enumerates the full
Cartesian product of query-bbox grid cells × months × duration buckets, doing one
`unordered_map` probe per combination, even when the answer is only used for a size
comparison. With no spatial constraint the prepared queries carry the global bbox
(`-90..90, -180..180`, confirmed by `--mode dump`), so at `cell_deg=0.05` each query spans
3601 × 7201 ≈ 25.9 M cells. Across the 500 queries this is:

- median 3.03e9 grid probes per query, max 1.71e10, **total 1.97e12** (computed from
  `queries.csv` and the binary's `cell_deg=0.05`, `bucket_hours=24`, `DUR_BUCKET=900 s`);
- for comparison, the constrained workloads need 1.46e4 (50 km) and 3.15e3 (10 km)
  probes per query — a factor of ~1e5–1e6 less.

At that rate the full 500-query run needs hours, not minutes; 1800 s was not enough.
This is a scaling defect of the catalog's *query-side* grid enumeration, not of the
catalog build (which is the same 6.2 M spans and ~1 s in all workloads).

## What these numbers support

- With a spatial constraint, the catalog is 2.9x (90 d/50 km) and 5.3x (1 y/10 km) faster
  than exact time-first at Recall@10 0.994, with p95 ≈ 2.4–2.8x mean (the same ratio the
  two exact modes show).
- A 1-year window by itself costs little (catalog 5.35 ms at 1 y/10 km vs 5.72 ms at
  90 d/50 km); exact scan grows with the window (16.3 → 28.6 ms) and grows much more
  once the spatial constraint is dropped (83.5 ms time-first, 192.6 ms space-first).
- The two constrained workloads differ in both window and radius, so this design cannot
  attribute their difference to either factor alone. The clean signal is the third row:
  removing the spatial constraint removes the catalog's advantage entirely — the current
  implementation does not even finish. The catalog is therefore best described as a
  spatial/time-pruned precomputation, not a general speedup over exact scan for
  unconstrained long-window queries.
- Prior reported figures (15.4 ms / 0.995 for exact space-first, 10.3 ms catalog) were
  measured on a different workload/machine configuration; the same code here gives
  15.7 ms for 1 y/10 km and 5.4 ms for the catalog. Use this report's table as the
  same-machine comparison.

## Reproduce

```bash
cd production/semantic-mobility-search/work
for w in all6_2160h_s50 all6_8760h_s10 all6_8760h; do
  mkdir -p bench_out_$w
  ./bench --mode gt    --dir $w --out bench_out_$w --k 10
  ./bench --mode space --dir $w --out bench_out_$w --k 10
  ./bench --mode catalog --dir $w --out bench_out_$w --k 10 --topc 300 --cap-hours 48
done
python3 ../../../researches/semantic-spatiotemporal-hybrid-search/2026-10-25-exps/parse_scale.py
```
