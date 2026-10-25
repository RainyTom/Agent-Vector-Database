#!/usr/bin/env python3
"""Prepare user-level semantic trajectories, embeddings, and episode queries.

Data: Massive-STEPS check-in CSV (six cities).
Embedding (potion-base-8M static encoder):
  - key = (venue_category, name), encoded as "category name" -> 256-d potion;
  - each distinct key is L2-normalized individually;
  - PCA to 128-d via numpy SVD on the (n_keys, 256) matrix with deterministic
    sign convention (largest-|component| positive), projections re-normalized;
  - key -> 128-d vector map cached in the output directory.

Point/query binary formats are byte-compatible with prep.py (D=128): this file
only replaces the embedding computation.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import random
import struct
from collections import Counter, defaultdict
from datetime import datetime, timedelta, timezone
from pathlib import Path

import numpy as np

D_RAW = 256
D = 128
MODEL_DIR = Path(__file__).resolve().parent.parent / "models" / "potion-base-8M"
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


KEY_SEP = "\x1f"


def _l2_rows(m: np.ndarray) -> np.ndarray:
    n = np.linalg.norm(m, axis=1, keepdims=True)
    return m / np.where(n > 0.0, n, 1.0)


def _load_key_cache(cache_path: Path, keys: list[tuple[str, str]]) -> np.ndarray | None:
    if not cache_path.exists():
        return None
    with np.load(cache_path, allow_pickle=False) as z:
        packed = z["keys"].tolist()
        vecs = z["vecs"]
    if [tuple(k.split(KEY_SEP)) for k in packed] != keys or vecs.shape != (len(keys), D):
        return None
    return vecs.astype(np.float32)


def build_key_embeddings(
    rows: list[dict], cache_path: Path, model_dir: Path = MODEL_DIR
) -> tuple[dict[tuple[str, str], np.ndarray], dict[str, int]]:
    """Encode distinct (venue_category, name) keys with potion, then PCA to D.

    Returns the key -> 128-d unit vector map and the category id map.
    """
    cat_id = {c: i for i, c in enumerate(sorted({r["venue_category"] for r in rows}))}
    keys = sorted({(r["venue_category"], r["name"]) for r in rows})

    vecs = _load_key_cache(cache_path, keys)
    if vecs is None:
        # Import lazily so the module stays importable without model2vec.
        from model2vec import StaticModel

        model = StaticModel.from_pretrained(str(model_dir))
        texts = [f"{cat} {name}" for cat, name in keys]
        raw = np.empty((len(texts), D_RAW), dtype=np.float32)
        BATCH = 32768
        for i in range(0, len(texts), BATCH):
            raw[i:i + BATCH] = np.asarray(
                model.encode(texts[i:i + BATCH], show_progress_bar=False), dtype=np.float32
            )
        # Per-row L2 normalization, NOT a matrix Frobenius norm: an encoder
        # output has one norm per key, and normalizing the whole matrix would
        # collapse every key toward a shared direction.
        raw = _l2_rows(raw)

        # PCA: center, SVD, project onto the top D right singular vectors.
        # Deterministic sign convention: the largest-|component| of each
        # component vector is made positive.
        mean = raw.mean(axis=0, keepdims=True)
        x = raw - mean
        _, _, vt = np.linalg.svd(x, full_matrices=False)
        comp = vt[:D].copy()
        for j in range(comp.shape[0]):
            idx = int(np.argmax(np.abs(comp[j])))
            if comp[j, idx] < 0.0:
                comp[j] *= -1.0
        vecs = _l2_rows(x @ comp.T)

        tmp = cache_path.with_suffix(".tmp.npz")
        np.savez(tmp, keys=np.array([KEY_SEP.join(k) for k in keys]), vecs=vecs)
        tmp.replace(cache_path)

    return {k: vecs[i] for i, k in enumerate(keys)}, cat_id


def venue_embedding(key_vec: dict[tuple[str, str], np.ndarray], cat: str, name: str) -> np.ndarray:
    return key_vec[(cat, name)]


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
    key_vec, cat_id = build_key_embeddings(rows, args.out / "key_emb_128.npz")

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
                venue_embedding(key_vec, r["venue_category"], r["name"]),
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

    # Write user_city.bin: dense user index -> city id, plus city names.
    city_names = sorted({u[0] for u in users})
    city_id = {c: i for i, c in enumerate(city_names)}
    with (args.out / "user_city.bin").open("wb") as f:
        f.write(struct.pack("<ii", len(users), len(city_names)))
        for u in users:
            f.write(struct.pack("<i", city_id[u[0]]))
    (args.out / "cities.txt").write_text("\n".join(city_names) + "\n")

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
            venue_embedding(key_vec, seq[a + i]["venue_category"], seq[a + i]["name"])
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
