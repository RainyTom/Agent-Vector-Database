#!/usr/bin/env python3
"""Lower-bound pruning study for episode retrieval (read-only analysis).

Scope: the 10 New York queries among qid 0..29 of the all6_8760h_s50
workload. For every feasible episode (all constraints of the formal
definition, no upper cap on segment count) on NY trajectories we compute the
exact score and two candidate lower bounds.

Score: DTW with steps (1,1)/(0,1) only (query point i never skipped),
lambda=0, divided by m. Ground truth (`2026-10-24-exps/data/groundtruth.csv`)
was produced with the same score and excludes the query's source user
(scan.cpp); the source user is excluded here too.

  LB1 = (1/m) * sum_i max(0, ||q_i|| - maxnorm(span))
  LB2 = (1/m) * sum_i min_{j in [i, n-m+i]} ||q_i - e_j||   (0-based i, n = span len)

LB2's band follows from the path: the path starts at (0,0), ends at
(m-1,n-1), advances j by 0 or 1 per query step, and never skips i, so query
point i can only align with j in [i, n-m+i]. For n = m the band collapses to
j = i and LB2 equals the exact score.

Writes data/lb/lb_stats.csv. Run from repo root:
  python3 researches/semantic-spatiotemporal-hybrid-search/2026-10-25-exps/lb_analysis.py
"""
from __future__ import annotations

import csv
import heapq
import struct
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
WORK = ROOT / "production" / "semantic-mobility-search" / "work" / "all6_8760h_s50"
GT = ROOT / "researches" / "semantic-spatiotemporal-hybrid-search" / "2026-10-24-exps" / "data" / "groundtruth.csv"
OUT = Path(__file__).resolve().parent / "data" / "lb"
DIM = 128
K = 10
NY_CITY = 3  # cities.txt order is alphabetical: istanbul, jakarta, moscow, ny, ...
EPS = 1e-9


def load_users(path: Path) -> np.ndarray:
    with path.open("rb") as f:
        n = struct.unpack("<i", f.read(4))[0]
        return np.frombuffer(f.read(8 * n), dtype=np.int32).reshape(n, 2)


def load_user_city(path: Path) -> np.ndarray:
    with path.open("rb") as f:
        n, _ = struct.unpack("<ii", f.read(8))
        return np.frombuffer(f.read(4 * n), dtype=np.int32)


def load_queries(path: Path) -> list[dict]:
    out = []
    with path.open("rb") as f:
        n, dim, _ = struct.unpack("<iii", f.read(12))
        assert dim == DIM
        for _ in range(n):
            m, src = struct.unpack("<ii", f.read(8))
            ws, we, dur, tol = struct.unpack("<qqqq", f.read(32))
            bbox = struct.unpack("<ffff", f.read(16))
            tau = np.frombuffer(f.read(8 * m), dtype=np.int64).copy()
            emb = np.frombuffer(f.read(4 * DIM * m), dtype=np.float32).reshape(m, DIM).copy()
            out.append(dict(m=m, src=src, ws=ws, we=we, dur=dur, tol=tol,
                            bbox=bbox, tau=tau, emb=emb))
    return out


def exact_dtw(cost: np.ndarray) -> float:
    """DTW over the cost matrix, steps (1,1) and (0,1) only."""
    m, n = cost.shape
    prev = np.cumsum(cost[0])  # row i=0: only (0,1) steps available
    for i in range(1, m):
        cur = np.full(n, np.inf)
        for j in range(i, n):
            cur[j] = cost[i, j] + min(prev[j - 1], cur[j - 1])
        prev = cur
    return float(prev[-1])


def feasible_spans(q: dict, user_ids: np.ndarray, off: np.ndarray,
                   length: np.ndarray, ts: np.ndarray, lat: np.ndarray,
                   lon: np.ndarray) -> list[tuple[int, int, int]]:
    """All (dense user id, a, b) absolute point-index spans the definition allows."""
    m = q["m"]
    ws, we, dur, tol = q["ws"], q["we"], q["dur"], q["tol"]
    min_lat, max_lat, min_lon, max_lon = q["bbox"]
    triples = []
    for s in range(len(off)):
        if int(user_ids[s]) == int(q["src"]):
            continue  # ground truth excludes the source trajectory
        i0, ln = int(off[s]), int(length[s])
        if ln < m:
            continue
        t = ts[i0:i0 + ln]
        ok = ((t >= ws) & (t <= we)
              & (lat[i0:i0 + ln] >= min_lat) & (lat[i0:i0 + ln] <= max_lat)
              & (lon[i0:i0 + ln] >= min_lon) & (lon[i0:i0 + ln] <= max_lon))
        if not ok.any():
            continue
        d = np.diff(ok.astype(np.int8))
        starts = (np.flatnonzero(d == 1) + 1).tolist()
        ends = (np.flatnonzero(d == -1) + 1).tolist()
        if ok[0]:
            starts.insert(0, 0)
        if ok[-1]:
            ends.append(ln)
        for rs, re in zip(starts, ends):
            nrun = re - rs
            if nrun < m:
                continue
            tsr = t[rs:re]
            a_rel = np.arange(0, nrun - m + 1)
            lo_b = np.searchsorted(tsr, tsr[a_rel] + dur - tol, "left")
            hi_b = np.searchsorted(tsr, tsr[a_rel] + dur + tol, "right") - 1
            b_lo = np.maximum(lo_b, a_rel + m - 1)
            b_hi = np.minimum(hi_b, nrun - 1)
            for ar, b0, b1 in zip(a_rel, b_lo, b_hi):
                if b1 < b0:
                    continue
                base = i0 + rs
                for b in range(int(b0), int(b1) + 1):
                    triples.append((int(user_ids[s]), base + int(ar), base + b))
    return triples


def pct(x, q):
    x = x[np.isfinite(x)]
    return float(np.percentile(x, q)) if len(x) else float("nan")


def main() -> None:
    ua = load_users(WORK / "users.bin")
    uc = load_user_city(WORK / "user_city.bin")
    queries = load_queries(WORK / "queries.bin")

    rec = np.dtype([("user", "<i4"), ("ts", "<i8"), ("lat", "<f4"),
                    ("lon", "<f4"), ("cat", "<i4"), ("emb", "<f4", (DIM,))])
    ny = np.flatnonzero(uc == NY_CITY)
    assert ny[-1] - ny[0] + 1 == len(ny), "NY users not contiguous"
    lo = int(ua[ny[0], 0])
    hi = int(ua[ny[-1], 0] + ua[ny[-1], 1])
    mm = np.memmap(WORK / "points.bin", dtype=rec, mode="r", offset=8, shape=(hi,))
    blk = mm[lo:hi]
    off = ua[ny, 0] - lo
    length = ua[ny, 1]
    ts = np.asarray(blk["ts"], dtype=np.int64)
    lat = np.asarray(blk["lat"])
    lon = np.asarray(blk["lon"])
    emb = np.asarray(blk["emb"], dtype=np.float32)
    norm = np.linalg.norm(emb.astype(np.float64), axis=1)

    gt = list(csv.DictReader(GT.open()))
    qid_ny = sorted({int(r["qid"]) for r in gt
                     if int(r["qid"]) < 30 and int(uc[int(r["user"])]) == NY_CITY})
    assert qid_ny == [0, 2, 5, 13, 15, 16, 17, 23, 28, 29], qid_ny

    rows, pooled = [], []
    for qid in qid_ny:
        q = queries[qid]
        m = q["m"]
        Q = q["emb"]
        qn = np.linalg.norm(Q.astype(np.float64), axis=1)
        spans = feasible_spans(q, ny, off, length, ts, lat, lon)
        n_s = len(spans)
        true = np.empty(n_s)
        lb1 = np.empty(n_s)
        lb2 = np.empty(n_s)
        lens = np.empty(n_s, dtype=np.int64)
        users = np.empty(n_s, dtype=np.int64)
        for idx, (u, a, b) in enumerate(spans):
            E = emb[a:b + 1].astype(np.float64)
            n = b - a + 1
            cost = np.sqrt(np.maximum(0.0, ((Q[:, None, :] - E[None, :, :]) ** 2).sum(-1)))
            true[idx] = exact_dtw(cost) / m
            lb1[idx] = np.maximum(0.0, qn - norm[a:b + 1].max()).sum() / m
            band = np.full((m, n), np.inf)
            for i in range(m):
                band[i, i:n - m + i + 1] = cost[i, i:n - m + i + 1]
            lb2[idx] = band.min(axis=1).sum() / m
            lens[idx] = n
            users[idx] = u

        assert np.all(lb2 <= true + 1e-6), (qid, float((lb2 - true).max()))
        assert np.all(lb1 <= true + 1e-6), (qid, float((lb1 - true).max()))

        nz = true > EPS
        ratio2 = lb2[nz] / true[nz]
        ratio1 = lb1[nz] / true[nz]
        ratio2_gtm = lb2[nz & (lens > m)] / true[nz & (lens > m)]
        # exactness of LB2 when n == m
        same = lens == m
        frac_eq_true = float(np.mean(np.abs(lb2[same] - true[same]) <= 1e-9))

        k_eff = min(K, n_s)
        order = np.lexsort((np.array([s[2] for s in spans]),
                            np.array([s[1] for s in spans]), users, true))
        top = order[:k_eff]
        tau = true[order[k_eff - 1]]
        pr2_ge = int(np.sum(lb2 >= tau))
        pr2_gt = int(np.sum(lb2 > tau))
        pr1_ge = int(np.sum(lb1 >= tau))
        # Pruning rule "prune iff LB >= tau" would also drop a true top-k item
        # whose LB sits exactly on tau; the strict rule is the sound one for
        # distinct scores.
        surv2_ge = float(np.mean(lb2[top] < tau))
        surv2_gt = float(np.mean(lb2[top] <= tau))
        surv1_ge = float(np.mean(lb1[top] < tau))

        # Realistic stopping rule: scan spans in ascending LB2, keep top-k by
        # true score, stop when the next LB2 cannot beat the current k-th.
        o = np.argsort(lb2, kind="stable")
        rank_lb2 = np.empty(n_s, dtype=np.int64)
        rank_lb2[o] = np.arange(n_s)
        heap = []  # negatives of the k smallest true scores seen so far
        verified_idx = []
        for idx in o:
            if len(heap) >= K and lb2[idx] >= -heap[0]:
                break
            heapq.heappush(heap, -true[idx])
            if len(heap) > K:
                heapq.heappop(heap)
            verified_idx.append(idx)
        verified = len(verified_idx)
        vmask = np.zeros(n_s, dtype=bool)
        vmask[verified_idx] = True
        assert np.allclose(sorted(-x for x in heap), np.sort(true[top]), atol=1e-9), qid

        # Cost model (arithmetic units): a d-dim distance = d+1 = 129, a DP
        # cell = 2. The band-restricted exact DTW needs m*(n-m+1) distances
        # and as many DP cells; the reference dtw() evaluates all m*n cells.
        band = m * (lens - m + 1)
        mn = m * lens
        cost_ref = float(np.sum(129.0 * mn + 2.0 * mn))
        # Survivors reuse the band distances and run the DP over the band only.
        cost_lb2_ref = float(np.sum(129.0 * band) + np.sum(2.0 * band[vmask]))
        cost_opt = float(np.sum(129.0 * band + 2.0 * band))
        cost_lb2_opt = float(np.sum(129.0 * band) + np.sum(2.0 * band[vmask]))
        ratio_ref = cost_lb2_ref / cost_ref
        ratio_opt = cost_lb2_opt / cost_opt
        ratio_band_alone = cost_opt / cost_ref

        # How informative is LB2 as an ordering?
        rt = np.argsort(np.argsort(true))
        rl = np.argsort(np.argsort(lb2))
        spearman = float(np.corrcoef(rt, rl)[0, 1])
        gtm = lens > m
        if int(gtm.sum()) > 2:
            rgt = np.argsort(np.argsort(true[gtm]))
            rlg = np.argsort(np.argsort(lb2[gtm]))
            spearman_gtm = float(np.corrcoef(rgt, rlg)[0, 1])
        else:
            spearman_gtm = float("nan")
        frac_topk_n_eq_m = float(np.mean(lens[top] == m))
        n_lt_tau = int(np.sum(lb2 < tau))
        n_le_tau = int(np.sum(lb2 <= tau))
        survivor_precision = len(set(top.tolist()) & set(np.flatnonzero(lb2 <= tau).tolist())) / n_le_tau
        last_topk_lb2_rank_pct = float(rank_lb2[top].max() / n_s)

        # Official (per-trajectory) semantics: best episode per user.
        best_per_user = {}
        for i, u in enumerate(users):
            best_per_user[u] = min(best_per_user.get(u, np.inf), true[i])
        us = np.fromiter(best_per_user.keys(), dtype=np.int64)
        bs = np.fromiter(best_per_user.values(), dtype=float)
        o2 = np.lexsort((us, bs))
        tau_traj = bs[o2[min(K, len(o2)) - 1]]
        prt_ge = int(np.sum(lb2 >= tau_traj))
        prt_gt = int(np.sum(lb2 > tau_traj))

        gt_scores = sorted(float(r["score"]) for r in gt if int(r["qid"]) == qid)
        gt_match = int(np.allclose(gt_scores, np.sort(bs)[:K], atol=1e-5))

        rows.append(dict(
            qid=qid, m=m, spans=n_s,
            spans_n_eq_m=int(same.sum()), frac_n_eq_m=float(same.mean()),
            ratio2_p10=pct(ratio2, 10), ratio2_p50=pct(ratio2, 50), ratio2_p90=pct(ratio2, 90),
            ratio2_gtm_p10=pct(ratio2_gtm, 10), ratio2_gtm_p50=pct(ratio2_gtm, 50),
            ratio2_gtm_p90=pct(ratio2_gtm, 90),
            frac_lb2_eq_true_n_eq_m=frac_eq_true,
            ratio1_p50=pct(ratio1, 50), ratio1_p90=pct(ratio1, 90),
            ratio1_max=float(np.max(ratio1)) if len(ratio1) else 0.0,
            kth_span_score=tau, kth_traj_score=tau_traj,
            prunable_lb2_ge=pr2_ge, prunable_frac_lb2=pr2_ge / n_s,
            prunable_lb2_gt=pr2_gt, prunable_frac_lb2_gt=pr2_gt / n_s,
            prunable_lb1_ge=pr1_ge, prunable_frac_lb1=pr1_ge / n_s,
            survive_top10_lb2_ge=surv2_ge, survive_top10_lb2_gt=surv2_gt,
            survive_top10_lb1_ge=surv1_ge,
            verified_lb2_order=verified, frac_verified_lb2_order=verified / n_s,
            cost_ratio_lb2_vs_reference=ratio_ref,
            cost_ratio_lb2_vs_band_exact=ratio_opt,
            cost_ratio_band_exact_vs_reference=ratio_band_alone,
            survivors_oracle_le_tau=n_le_tau,
            survivor_precision_at_oracle=survivor_precision,
            last_topk_lb2_rank_pct=last_topk_lb2_rank_pct,
            spearman_lb2_true=spearman, spearman_lb2_true_n_gt_m=spearman_gtm,
            frac_topk_n_eq_m=frac_topk_n_eq_m,
            prunable_spans_trajtau_lb2=prt_ge,
            prunable_frac_spans_trajtau_lb2=prt_ge / n_s,
            prunable_spans_trajtau_lb2_gt=prt_gt,
            top1_score=float(true[order[0]]),
            gt_top10_match=gt_match,
        ))
        pooled.append((ratio1, ratio2, ratio2_gtm, lens, true, np.full(n_s, m), lb1, lb2))
        print(f"qid {qid}: m={m} spans={n_s} frac_n=m {same.mean():.3f} "
              f"lb2ratio p10/p50/p90 {pct(ratio2,10):.3f}/{pct(ratio2,50):.3f}/{pct(ratio2,90):.3f} "
              f"tau={tau:.4f} prunable_ge={pr2_ge} ({pr2_ge/n_s:.3f}) "
              f"verified={verified} surv(>)={surv2_gt:.2f} gt_match={gt_match}", flush=True)

    OUT.mkdir(parents=True, exist_ok=True)
    fields = list(rows[0].keys())
    tot = sum(r["spans"] for r in rows)
    r1 = np.concatenate([p[0] for p in pooled])
    r2 = np.concatenate([p[1] for p in pooled])
    r2g = np.concatenate([p[2] for p in pooled])
    tr = np.concatenate([p[4] for p in pooled])
    lb2_all = np.concatenate([p[7] for p in pooled])
    rt = np.argsort(np.argsort(tr))
    rl = np.argsort(np.argsort(lb2_all))
    spearman_all = float(np.corrcoef(rt, rl)[0, 1])
    surv_oracle_tot = sum(r["survivors_oracle_le_tau"] for r in rows)
    pooled_row = {
        "qid": "all", "m": "",
        "spans": tot,
        "spans_n_eq_m": sum(r["spans_n_eq_m"] for r in rows),
        "frac_n_eq_m": sum(r["spans_n_eq_m"] for r in rows) / tot,
        "ratio2_p10": pct(r2, 10), "ratio2_p50": pct(r2, 50), "ratio2_p90": pct(r2, 90),
        "ratio2_gtm_p10": pct(r2g, 10), "ratio2_gtm_p50": pct(r2g, 50),
        "ratio2_gtm_p90": pct(r2g, 90),
        "frac_lb2_eq_true_n_eq_m": sum(r["frac_lb2_eq_true_n_eq_m"] * r["spans_n_eq_m"]
                                       for r in rows) / max(1, sum(r["spans_n_eq_m"] for r in rows)),
        "ratio1_p50": pct(r1, 50), "ratio1_p90": pct(r1, 90),
        "ratio1_max": float(r1.max()),
        "kth_span_score": "", "kth_traj_score": "",
        "prunable_lb2_ge": sum(r["prunable_lb2_ge"] for r in rows),
        "prunable_frac_lb2": sum(r["prunable_lb2_ge"] for r in rows) / tot,
        "prunable_lb2_gt": sum(r["prunable_lb2_gt"] for r in rows),
        "prunable_frac_lb2_gt": sum(r["prunable_lb2_gt"] for r in rows) / tot,
        "prunable_lb1_ge": sum(r["prunable_lb1_ge"] for r in rows),
        "prunable_frac_lb1": 0.0,
        "survive_top10_lb2_ge": float(np.mean([r["survive_top10_lb2_ge"] for r in rows])),
        "survive_top10_lb2_gt": float(np.mean([r["survive_top10_lb2_gt"] for r in rows])),
        "survive_top10_lb1_ge": float(np.mean([r["survive_top10_lb1_ge"] for r in rows])),
        "verified_lb2_order": sum(r["verified_lb2_order"] for r in rows),
        "frac_verified_lb2_order": sum(r["verified_lb2_order"] for r in rows) / tot,
        "cost_ratio_lb2_vs_reference": sum(r["cost_ratio_lb2_vs_reference"] * r["spans"] for r in rows) / tot,
        "cost_ratio_lb2_vs_band_exact": sum(r["cost_ratio_lb2_vs_band_exact"] * r["spans"] for r in rows) / tot,
        "cost_ratio_band_exact_vs_reference": sum(r["cost_ratio_band_exact_vs_reference"] * r["spans"] for r in rows) / tot,
        "survivors_oracle_le_tau": surv_oracle_tot,
        "survivor_precision_at_oracle": K * len(rows) / surv_oracle_tot,
        "last_topk_lb2_rank_pct": float(np.mean([r["last_topk_lb2_rank_pct"] for r in rows])),
        "spearman_lb2_true": spearman_all,
        "spearman_lb2_true_n_gt_m": float(np.nanmean([r["spearman_lb2_true_n_gt_m"] for r in rows])),
        "frac_topk_n_eq_m": float(np.mean([r["frac_topk_n_eq_m"] for r in rows])),
        "prunable_spans_trajtau_lb2": sum(r["prunable_spans_trajtau_lb2"] for r in rows),
        "prunable_frac_spans_trajtau_lb2": sum(r["prunable_spans_trajtau_lb2"] for r in rows) / tot,
        "prunable_spans_trajtau_lb2_gt": sum(r["prunable_spans_trajtau_lb2_gt"] for r in rows),
        "top1_score": min(r["top1_score"] for r in rows),
        "gt_top10_match": min(r["gt_top10_match"] for r in rows),
    }
    with (OUT / "lb_stats.csv").open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for r in [*rows, pooled_row]:
            w.writerow({k: (f"{v:.6g}" if isinstance(v, float) else v) for k, v in r.items()})

    print(f"\npooled: queries={len(rows)} spans={tot}")
    print("lb2 ratio p10/p50/p90", pct(r2, 10), pct(r2, 50), pct(r2, 90))
    print("lb2 ratio (n>m) p10/p50/p90", pct(r2g, 10), pct(r2g, 50), pct(r2g, 90))
    print("lb1 ratio p50/p90/max", pct(r1, 50), pct(r1, 90), float(r1.max()))
    print("prunable lb2>=tau", pooled_row["prunable_lb2_ge"],
          pooled_row["prunable_frac_lb2"])
    print("survivors (lb2<=tau)", surv_oracle_tot,
          "precision", pooled_row["survivor_precision_at_oracle"])
    print("verified under LB2 order", pooled_row["verified_lb2_order"],
          pooled_row["frac_verified_lb2_order"])
    print("spearman lb2/true", spearman_all,
          "mean last-top10 rank pct", pooled_row["last_topk_lb2_rank_pct"])
    print("true score p10/p50/p90", pct(tr, 10), pct(tr, 50), pct(tr, 90))
    lens = np.concatenate([p[3] for p in pooled])
    ms = np.concatenate([p[5] for p in pooled])
    print("frac n=m", float(np.mean(lens == ms)), "max n", int(lens.max()))
    print("cost ratio LB2 vs reference full-matrix DTW",
          pooled_row["cost_ratio_lb2_vs_reference"])
    print("cost ratio LB2 vs band-restricted exact DTW",
          pooled_row["cost_ratio_lb2_vs_band_exact"])
    print("cost ratio band-restricted exact vs reference DTW",
          pooled_row["cost_ratio_band_exact_vs_reference"])


if __name__ == "__main__":
    main()
