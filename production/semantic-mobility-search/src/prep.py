#!/usr/bin/env python3
"""Prepare user-level semantic trajectories, embeddings, and episode queries.

Data: Massive-STEPS check-in CSV (New York by default).
Embedding (pilot, deterministic):
  - 64-d category co-occurrence PPMI + truncated SVD;
  - 64-d hashed bag-of-words of the venue name;
  - concatenated and L2-normalized (d=128).

The embedding is a reproducible pilot representation, not a claim that it is
the best semantic model. Swap in a text encoder later without changing the
retrieval protocol.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import random
import re
import struct
from collections import Counter, defaultdict
from datetime import datetime, timedelta, timezone
from pathlib import Path

import numpy as np

TOKEN_RE = re.compile(r"[a-z0-9]+")
D_CAT = 64
D_NAME = 64
D = D_CAT + D_NAME
MAX_EPISODE = 48 * 3600


def parse_ts(s: str) -> int:
    # Massive-STEPS timestamps are naive local wall-clock strings.
    dt = datetime.strptime(s, "%Y-%m-%d %H:%M:%S")
    return int(dt.replace(tzinfo=timezone.utc).timestamp())


def load_rows(paths: list[Path], cities: list[str] | None = None) -> list[dict]:
    rows: list[dict] = []
    for i, path in enumerate(paths):
        city = cities[i] if cities and i < len(cities) else path.stem.split("_")[0]
        with path.open(newline="") as f:
            for r in csv.DictReader(f):
                r["city"] = city
                rows.append(r)
    return rows


def build_embeddings(rows: list[dict]) -> tuple[dict[str, np.ndarray], np.ndarray, dict[str, int]]:
    cats = sorted({r["venue_category"] for r in rows})
    cat_id = {c: i for i, c in enumerate(cats)}
    n_cat = len(cats)

    # Category transition counts within trails. PPMI + SVD captures functional
    # similarity (e.g. cafe/coffee shop) from co-occurrence, not spelling.
    trans = np.zeros((n_cat, n_cat), dtype=np.float64)
    by_trail: dict[tuple[str, str], list[str]] = defaultdict(list)
    for r in rows:
        by_trail[(r.get("city", ""), r["trail_id"])].append(r["venue_category"])
    for seq in by_trail.values():
        for a, b in zip(seq, seq[1:]):
            if a != b:
                trans[cat_id[a], cat_id[b]] += 1.0
                trans[cat_id[b], cat_id[a]] += 1.0
    total = trans.sum()
    row = trans.sum(axis=1, keepdims=True)
    col = trans.sum(axis=0, keepdims=True)
    with np.errstate(divide="ignore", invalid="ignore"):
        pmi = np.log((trans * total) / (row * col))
    ppmi = np.nan_to_num(np.maximum(pmi, 0.0), nan=0.0, posinf=0.0, neginf=0.0)
    # Truncated SVD; deterministic sign convention by fixing the largest
    # absolute component of each left singular vector to be positive.
    u, s, _ = np.linalg.svd(ppmi, full_matrices=False)
    k = min(D_CAT, len(s))
    cat_emb = u[:, :k] * np.sqrt(s[:k])
    if k < D_CAT:
        cat_emb = np.pad(cat_emb, ((0, 0), (0, D_CAT - k)))
    for j in range(cat_emb.shape[1]):
        idx = int(np.argmax(np.abs(cat_emb[:, j])))
        if cat_emb[idx, j] < 0:
            cat_emb[:, j] *= -1.0
    cat_emb = l2(cat_emb)

    # Hashed name embedding: deterministic signed token hashing, 64 dims.
    name_cache: dict[str, np.ndarray] = {}
    for r in rows:
        name = r["name"]
        if name in name_cache:
            continue
        v = np.zeros(D_NAME, dtype=np.float64)
        for tok in TOKEN_RE.findall(name.lower()):
            h = hashlib.blake2b(tok.encode(), digest_size=8).digest()
            x = int.from_bytes(h, "little")
            idx = x % D_NAME
            sign = 1.0 if (x >> 8) & 1 else -1.0
            v[idx] += sign
        name_cache[name] = l2(v)

    # Combine per category and per venue name; venue embeddings are looked up
    # by category/name at point construction time.
    cat_vec = {c: np.concatenate([cat_emb[i], np.zeros(D_NAME)]) for c, i in cat_id.items()}
    name_vec = name_cache
    return cat_vec, name_vec, cat_id


def l2(v: np.ndarray) -> np.ndarray:
    n = np.linalg.norm(v)
    return v / n if n > 0 else v


def venue_embedding(cat_vec, name_vec, cat: str, name: str) -> np.ndarray:
    # Category and name halves are each unit-norm; concatenation then L2
    # normalization keeps the two signals at comparable scale.
    v = np.concatenate([cat_vec[cat][:D_CAT], name_vec[name]])
    return l2(v)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", type=Path, action="append", required=True,
                    help="repeat for multiple city CSVs")
    ap.add_argument("--city", action="append", default=None,
                    help="optional city label per --csv, same order")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--queries", type=int, default=500)
    ap.add_argument("--seed", type=int, default=20261022)
    ap.add_argument("--k", type=int, default=10)
    ap.add_argument("--spatial-km", type=float, default=0.0,
                    help="0 = no spatial constraint; otherwise bbox buffer in km")
    ap.add_argument("--window-hours", type=float, default=24.0,
                    help="query window length starting at episode start")
    args = ap.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    rows = load_rows(args.csv, args.city)
    n_raw = len(rows)
    rows = [r for r in rows if r["latitude"] and r["longitude"] and r["name"] and r["venue_category"]]
    if not rows:
        raise SystemExit("no rows with complete coordinates/name/category")
    cat_vec, name_vec, cat_id = build_embeddings(rows)

    # Group by (city, user) and sort by time. User ids are local to a city, so
    # the city key is required; the C++ side only needs a dense index.
    by_user: dict[tuple[str, int], list[dict]] = defaultdict(list)
    for r in rows:
        by_user[(r["city"], int(r["user_id"]))].append(r)
    users = sorted(by_user)
    points: list[tuple[int, int, float, float, int, np.ndarray]] = []
    user_off: list[int] = []
    user_len: list[int] = []
    for uid, ukey in enumerate(users):
        seq = sorted(by_user[ukey], key=lambda r: r["timestamp"])
        user_off.append(len(points))
        user_len.append(len(seq))
        for r in seq:
            points.append((
                uid,
                parse_ts(r["timestamp"]),
                float(r["latitude"]),
                float(r["longitude"]),
                cat_id[r["venue_category"]],
                venue_embedding(cat_vec, name_vec, r["venue_category"], r["name"]),
            ))

    # Write points.bin
    with (args.out / "points.bin").open("wb") as f:
        f.write(struct.pack("<ii", len(points), D))
        for uid, ts, lat, lon, cat, emb in points:
            f.write(struct.pack("<iqffi", uid, ts, lat, lon, cat))
            f.write(emb.astype("<f4").tobytes())

    # Write users.bin
    with (args.out / "users.bin").open("wb") as f:
        f.write(struct.pack("<i", len(users)))
        for off, ln in zip(user_off, user_len):
            f.write(struct.pack("<ii", off, ln))

    # Query generation: same-calendar-day episodes, m=3..6, duration <= 24h.
    rng = random.Random(args.seed)
    candidates: list[tuple[int, int, int]] = []  # (user_idx, start_idx, m)
    for ui, uid in enumerate(users):
        seq = sorted(by_user[uid], key=lambda r: r["timestamp"])
        n = len(seq)
        if n < 3:
            continue
        for a in range(n):
            t_a = parse_ts(seq[a]["timestamp"])
            day0 = t_a - (t_a % 86400)
            day1 = day0 + 86400
            for m in range(3, 7):
                b = a + m - 1
                if b >= n:
                    break
                t_b = parse_ts(seq[b]["timestamp"])
                if t_b >= day1:
                    break
                if t_b - t_a > MAX_EPISODE:
                    break
                candidates.append((ui, a, m))
    rng.shuffle(candidates)
    candidates = candidates[: args.queries]

    queries = []
    for ui, a, m in candidates:
        uid = users[ui]
        seq = sorted(by_user[uid], key=lambda r: r["timestamp"])
        ts = [parse_ts(r["timestamp"]) for r in seq]
        t_a, t_b = ts[a], ts[a + m - 1]
        ws = t_a
        we = t_a + int(args.window_hours * 3600)
        dur = t_b - t_a
        tol = max(1800, int(0.25 * dur))
        lats = [float(seq[a + i]["latitude"]) for i in range(m)]
        lons = [float(seq[a + i]["longitude"]) for i in range(m)]
        if args.spatial_km > 0:
            # Approximate degree buffer: 1 deg lat ~= 111.32 km.
            buf_lat = args.spatial_km / 111.32
            buf_lon = args.spatial_km / (111.32 * max(0.2, math.cos(math.radians(sum(lats) / m))))
            min_lat, max_lat = min(lats) - buf_lat, max(lats) + buf_lat
            min_lon, max_lon = min(lons) - buf_lon, max(lons) + buf_lon
        else:
            min_lat, max_lat, min_lon, max_lon = -90.0, 90.0, -180.0, 180.0
        tau = [ts[a + i] - t_a for i in range(m)]
        cats = [cat_id[seq[a + i]["venue_category"]] for i in range(m)]
        emb = np.stack([
            venue_embedding(cat_vec, name_vec, seq[a + i]["venue_category"], seq[a + i]["name"])
            for i in range(m)
        ])
        queries.append({
            "source_user": ui,
            "source_city": uid[0],
            "m": m,
            "ws": ws,
            "we": we,
            "dur": dur,
            "tol": tol,
            "bbox": [min_lat, max_lat, min_lon, max_lon],
            "tau": tau,
            "cats": cats,
            "emb": emb,
        })

    with (args.out / "queries.bin").open("wb") as f:
        f.write(struct.pack("<iii", len(queries), D, 6))
        for q in queries:
            f.write(struct.pack("<iiqqq", q["m"], q["source_user"], q["ws"], q["we"], q["dur"]))
            f.write(struct.pack("<q", q["tol"]))
            f.write(struct.pack("<ffff", *q["bbox"]))
            for t in q["tau"]:
                f.write(struct.pack("<q", t))
            f.write(q["emb"].astype("<f4").tobytes())

    with (args.out / "queries_cat.bin").open("wb") as f:
        f.write(struct.pack("<ii", len(queries), 6))
        for q in queries:
            for j in range(6):
                f.write(struct.pack("<i", q["cats"][j] if j < q["m"] else -1))

    meta = {
        "csv": str(args.csv),
        "rows_raw": n_raw,
        "rows_used": len(rows),
        "rows_dropped_incomplete": n_raw - len(rows),
        "points": len(points),
        "users": len(users),
        "cities": sorted({u[0] for u in users}),
        "city_users": {c: sum(1 for u in users if u[0] == c) for c in sorted({u[0] for u in users})},
        "categories": len(cat_id),
        "dim": D,
        "queries": len(queries),
        "k": args.k,
        "spatial_km": args.spatial_km,
        "window_hours": args.window_hours,
        "seed": args.seed,
        "time_min": min(p[1] for p in points),
        "time_max": max(p[1] for p in points),
        "user_len_mean": float(np.mean(user_len)),
        "user_len_median": int(np.median(user_len)),
        "user_len_max": int(np.max(user_len)),
    }
    (args.out / "meta.json").write_text(json.dumps(meta, indent=2) + "\n")
    print(json.dumps(meta, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
