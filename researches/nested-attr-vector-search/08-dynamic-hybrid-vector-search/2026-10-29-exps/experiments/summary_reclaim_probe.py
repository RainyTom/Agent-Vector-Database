#!/usr/bin/env python3
"""Offline probe: can attribute summaries reclaim stale bits under churn?

EMA-style node markers are monotone OR bitmaps: a bit set once is never cleared,
so moving an item (or changing its attribute value) leaves a stale bit behind.
Nothing distinguishes "this node really has a value-v item" from "this node had
one ten thousand updates ago". This probe quantifies that degradation and
compares two alternative semantics:

  or     monotone OR per (node, bucket); 1 write per bucket touched, never cleared
  count  one counter per (node, bucket) (ref-count); exact reclaim, 2 writes
  epoch  bit + last-set update stamp; never reclaimed, but a query may declare
         old bits "uncertain" and either pass them (conservative) or drop them
         (aggressive, with false-negative risk)

The summary is used for multi-attribute filtering: subtree(u) AND attr=value.
A node is a candidate iff its bitmap has any of the predicate's buckets set.
We report (a) bitmap false positives (set bit without live support), which is a
*storage/trust* metric, and (b) query-side cost, i.e. candidate node fraction,
probe-item inflation and wasted probe fraction, which is what a search engine
actually pays. They are not the same number: a false-positive bucket only costs
a probe when the predicate hits that bucket.

Update stream: `churn` fraction f means f*n_items update events, each touching an
item drawn uniformly WITH replacement (an item can move repeatedly and leaves a
stale bit at every node it has left); the destination node is uniform over the
nodes that initially held items. `--attr-change-frac` is the probability that an
event also resamples the item's attributes (a pure move rehangs without changing
values).

Data: nodeann_matrix staging trees/bindings (base.f32 is never opened).
Attributes are synthetic (Cat 32 values, Num in [0,1000] into 16 buckets,
Tag 128 values x 3 per item), with correlation rho as an explicit variable.

Python + numpy only; this is an offline statistics run, not a hot path.
"""
import argparse
import csv
import hashlib
import json
import time
from pathlib import Path

import numpy as np

NBITS = 176                 # 32 Cat + 16 Num buckets + 128 Tag
FAMILIES = (("cat", slice(0, 32)), ("num", slice(32, 48)), ("tag", slice(48, 176)))
CAT_N, TAG_N, NUM_RANGE, NUM_BUCKETS, TAGS_PER_ITEM = 32, 128, 1001, 16, 3

HERE = Path(__file__).resolve().parent
EXP_DIR = HERE.parent                       # .../2026-10-29-exps
ROOT = EXP_DIR.parents[1]                   # nested-attr-vector-search
STAGING = ROOT / "01-directory-aware-acceleration" / "2026-10-29-exps" / "work" / "nodeann_matrix" / "staging"


def sha256_file(path, chunk=1 << 20):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(chunk)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


# ---------------------------------------------------------------- hierarchy ---
def load_hierarchy(data_dir):
    """Parse tree.txt + bindings.txt; return DFS-preorder-reordered structure.

    After reordering, a node's subtree occupies the contiguous position range
    [tin, tin + size), and its items occupy the contiguous item slice
    [item_start[tin], item_start[tin + size]).
    """
    tree = Path(data_dir) / "tree.txt"
    with open(tree) as f:
        n = int(f.readline())
        toks = f.read().split()
    arr = np.asarray(toks, dtype=np.int64).reshape(n, 2)
    assert np.array_equal(np.sort(arr[:, 0]), np.arange(n)), "node ids not 0..n-1"
    parent = arr[:, 1].copy()
    assert np.all(parent[1:] <= np.arange(1, n)), "tree not ordered parent<child"
    assert parent[0] == -1

    # children CSR (children in ascending node id)
    cnode = np.arange(1, n)
    cpar = parent[1:]
    order = np.argsort(cpar, kind="stable")
    child_list = cnode[order]
    child_start = np.zeros(n + 1, dtype=np.int64)
    np.cumsum(np.bincount(cpar, minlength=n), out=child_start[1:])

    tin = np.empty(n, dtype=np.int64)
    tout = np.empty(n, dtype=np.int64)
    node_at = np.empty(n, dtype=np.int64)
    depth = np.zeros(n, dtype=np.int32)
    ptr = child_start.copy()
    stack = [0]
    pos = 0
    tin[0] = 0
    node_at[0] = 0
    while stack:
        u = stack[-1]
        if ptr[u] < child_start[u + 1]:
            v = int(child_list[ptr[u]])
            ptr[u] += 1
            pos += 1
            tin[v] = pos
            node_at[pos] = v
            depth[v] = depth[u] + 1
            stack.append(v)
        else:
            tout[u] = pos + 1
            stack.pop()
    assert pos + 1 == n

    parent_pos = np.where(parent[node_at] >= 0, tin[np.maximum(parent[node_at], 0)], -1)
    depth_pos = depth[node_at].astype(np.int32)
    sub_size = (tout - tin)[node_at].astype(np.int64)

    # ancestor at depth min(2, depth) -- used for the correlated attribute mode
    cur = np.arange(n, dtype=np.int64)
    for _ in range(int(depth_pos.max()) + 1):
        cur = np.where(depth_pos[cur] > 2, parent_pos[cur], cur)
    anc2 = cur

    # items sorted by node position -> contiguous per subtree
    item_node_old = np.loadtxt(Path(data_dir) / "bindings.txt", dtype=np.int64)
    assert item_node_old.min() >= 0 and item_node_old.max() < n
    key = tin[item_node_old]
    sort_idx = np.argsort(key, kind="stable")
    item_node = key[sort_idx]
    node_count = np.bincount(item_node, minlength=n).astype(np.int64)
    item_start = np.zeros(n + 1, dtype=np.int64)
    np.cumsum(node_count, out=item_start[1:])

    return dict(n=n, n_items=len(item_node), depth_pos=depth_pos, sub_size=sub_size,
                anc2=anc2, item_node=item_node, node_count=node_count,
                item_start=item_start)


# --------------------------------------------------------------- attributes ---
def _det_hash(v, mod, mul, add=0):
    return ((v.astype(np.uint64) * np.uint64(mul) + np.uint64(add)) % np.uint64(mod)).astype(np.int16)


def gen_attrs(rng, n, anc2_of_item, rho):
    """Free attributes, then (rho>0) pull Cat and one Tag toward the depth<=2 ancestor."""
    cat = rng.integers(0, CAT_N, n).astype(np.int16)
    num = rng.integers(0, NUM_RANGE, n).astype(np.int16)
    tags = rng.integers(0, TAG_N, (n, TAGS_PER_ITEM)).astype(np.int16)
    purity_target = rho
    if rho > 0:
        det_cat = _det_hash(anc2_of_item, CAT_N, 2654435761)
        det_tag = _det_hash(anc2_of_item, TAG_N, 40503, 17)
        adhere = rng.random(n) < purity_target
        cat = np.where(adhere, det_cat, cat).astype(np.int16)
        tags[:, 0] = np.where(adhere, det_tag, tags[:, 0]).astype(np.int16)
    for k in range(1, TAGS_PER_ITEM):
        for j in range(k):
            dup = tags[:, k] == tags[:, j]
            for _ in range(8):
                if not dup.any():
                    break
                tags[dup, k] = rng.integers(0, TAG_N, int(dup.sum()))
                dup = tags[:, k] == tags[:, j]
    return cat, num, tags


def buckets_of(cat, num, tags):
    b = np.empty((len(cat), 2 + TAGS_PER_ITEM), dtype=np.int64)
    b[:, 0] = cat
    b[:, 1] = 32 + (num.astype(np.int64) * NUM_BUCKETS // NUM_RANGE)
    b[:, 2:] = 48 + tags
    return b


def purity_cat(cat, group):
    """Weighted fraction of items whose Cat is the modal Cat of its ancestor group."""
    uniq, inv = np.unique(group, return_inverse=True)
    tab = np.zeros((len(uniq), CAT_N), dtype=np.int64)
    np.add.at(tab, (inv, cat), 1)
    mode = tab.argmax(1)
    hit = tab[np.arange(len(uniq)), mode]
    tot = tab.sum(1)
    keep = tot >= 5
    if not keep.any():
        return float("nan")
    return float((hit[keep].sum() / tot[keep].sum()))


# --------------------------------------------------------------- predicates ---
def gen_predicates(rng, h, cat0, num0, tags0, n_pred):
    n = h["n"]
    item_start, sub_size = h["item_start"], h["sub_size"]
    sub_items0 = item_start[np.minimum(np.arange(n) + sub_size, n)] - item_start[:n]
    elig = np.where((sub_items0 >= 200) & (sub_items0 <= 8000) & (sub_size > 1))[0]
    if len(elig) < 50:  # fall back to the widest reasonable subtrees
        order = np.argsort(-sub_items0)
        elig = order[: max(50, min(n, 400))]
    anchor = rng.choice(elig, size=n_pred, replace=len(elig) < n_pred)

    item_node = h["item_node"]
    num_b = (num0.astype(np.int64) * NUM_BUCKETS // NUM_RANGE)
    preds = []
    for i, u in enumerate(anchor):
        u = int(u)
        size = int(sub_size[u])
        a, b = int(item_start[u]), int(item_start[u + size])
        kind = i % 3
        ok = False
        for _ in range(40):
            if kind == 0:
                v = int(rng.integers(0, CAT_N))
                hits = int(np.count_nonzero(cat0[a:b] == v))
                if hits == 0:
                    continue
                pred = dict(u=u, size=size, type="cat", v=v,
                            buckets=np.array([v], dtype=np.int64), hits0=hits)
            elif kind == 1:
                lo = int(rng.integers(0, NUM_BUCKETS - 1))
                w = int(rng.choice([1, 2, 4]))
                hi = min(NUM_BUCKETS, lo + w)
                hits = int(np.count_nonzero((num_b[a:b] >= lo) & (num_b[a:b] < hi)))
                if hits == 0:
                    continue
                pred = dict(u=u, size=size, type="num", lo=lo, hi=hi,
                            buckets=np.arange(32 + lo, 32 + hi, dtype=np.int64), hits0=hits)
            else:
                t1, t2 = (int(x) for x in rng.choice(TAG_N, size=2, replace=False))
                ts = tags0[a:b]
                hits = int(np.count_nonzero((ts == t1).any(1) | (ts == t2).any(1)))
                if hits == 0:
                    continue
                pred = dict(u=u, size=size, type="tag", tags=(t1, t2),
                            buckets=np.array([48 + t1, 48 + t2], dtype=np.int64), hits0=hits)
            ok = True
            break
        if ok:
            preds.append(pred)
    return preds


def item_condition_mask(pred, cat, num_b, tags, a, b):
    if pred["type"] == "cat":
        return cat[a:b] == pred["v"]
    if pred["type"] == "num":
        nb = num_b[a:b]
        return (nb >= pred["lo"]) & (nb < pred["hi"])
    t1, t2 = pred["tags"]
    ts = tags[a:b]
    return (ts == t1).any(1) | (ts == t2).any(1)


# ------------------------------------------------------------------ queries ---
def eval_queries(preds, bits, count, epoch, cur, thrs, node_count, item_start,
                 node_sorted, sub_size, cat, num_b, tags):
    """Return per-semantics aggregate query cost over the fixed predicate set.

    `node_sorted` / `item_start` describe the *current* item->node assignment
    (rebuilt every checkpoint), so a subtree's items are again contiguous.
    """
    out, by_type = {}, {}
    acc = {}

    def add(key, cand_node_frac, cand_item_frac, probe, true_n, waste, missed, sub_items):
        s = acc.setdefault(key, [0] * 10)
        s[0] += 1                      # predicates counted
        s[1] += cand_node_frac
        s[2] += cand_item_frac
        s[3] += probe                  # probe items
        s[4] += true_n                 # truly matching items
        s[5] += waste
        s[6] += missed
        s[7] += sub_items              # items in scope
        s[8] += probe / true_n         # macro ratio
        s[9] += probe / max(sub_items, 1)

    for pred in preds:
        u, size = pred["u"], pred["size"]
        a, b = int(item_start[u]), int(item_start[u + size])
        alive = node_count[u:u + size] > 0
        n_alive = int(alive.sum())
        if n_alive == 0:
            continue
        sub_items = int(node_count[u:u + size].sum())
        truth = item_condition_mask(pred, cat, num_b, tags, a, b)
        true_n = int(truth.sum())
        if true_n == 0:
            continue
        bs = pred["buckets"]
        ibuck = node_sorted[a:b] - u

        def report(key, cand):
            probe = int(node_count[u:u + size][cand].sum())
            hit = int(np.count_nonzero(truth & cand[ibuck]))
            vals = (float(cand[alive].sum()) / n_alive,
                    float(probe) / max(sub_items, 1),
                    probe, true_n,
                    1.0 - float(hit) / max(probe, 1),
                    1.0 - float(hit) / true_n,
                    sub_items)
            add(key, *vals)
            add(f"{key}|{pred['type']}", *vals)

        report("or", bits[u:u + size][:, bs].any(1))
        report("count", (count[u:u + size][:, bs] > 0).any(1))
        ep = epoch[u:u + size][:, bs]
        for thr in thrs:
            report(f"epoch_cons@{thr}", (ep >= 0).any(1))
            report(f"epoch_aggr@{thr}", ((ep >= 0) & (ep >= cur - thr)).any(1))

    def derive(s):
        m = max(s[0], 1)
        return dict(n_pred=s[0], cand_node_frac=s[1] / m, cand_item_frac=s[2] / m,
                    probe_items_per_true=s[3] / max(s[4], 1),   # item-weighted
                    probe_items_per_true_macro=s[8] / m,
                    probe_items_per_scope=s[9] / m,
                    waste_frac=s[5] / m, missed_true_frac=s[6] / m)

    for key, s in acc.items():
        base, _, typ = key.partition("|")
        if typ:
            by_type[key] = derive(s)
        else:
            out[key] = derive(s)
    return out, by_type


def fp_stats(bits, count, epoch, cur, thrs, n):
    """Bitmap false positives: set bits with no live support (per attribute family)."""
    live_all = count > 0
    set_all = bits.astype(bool)
    res = dict(set_bits_per_node=float(set_all.sum()) / n,
               stale_bits_per_node=float((set_all & ~live_all).sum()) / n)
    for name, sl in FAMILIES:
        s = set_all[:, sl]
        lv = live_all[:, sl]
        res[f"set_{name}"] = int(s.sum())
        res[f"live_{name}"] = int(lv.sum())
        res[f"fp_{name}"] = float((s & ~lv).sum()) / max(int(s.sum()), 1)
    res["fp_all"] = float((set_all & ~live_all).sum()) / max(int(set_all.sum()), 1)
    for thr in thrs:
        trusted = (epoch >= 0) & (epoch >= cur - thr)
        res[f"trusted_frac@{thr}"] = float(trusted.sum()) / max(int((epoch >= 0).sum()), 1)
        res[f"fp_trusted@{thr}"] = float((trusted & ~live_all).sum()) / max(int(trusted.sum()), 1)
        res[f"trusted_bits_per_node@{thr}"] = float(trusted.sum()) / n
    return res


# -------------------------------------------------------------------- main ---
def run_dataset(name, data_dir, rhos, fracs, attr_change_frac, seed, n_pred, thrs,
                out_rows, summary, by_type):
    t0 = time.time()
    h = load_hierarchy(data_dir)
    n, n_items = h["n"], h["n_items"]
    tree_hash = sha256_file(Path(data_dir) / "tree.txt")
    bind_hash = sha256_file(Path(data_dir) / "bindings.txt")
    print(f"[{name}] n_nodes={n} n_items={n_items} depth_max={int(h['depth_pos'].max())} "
          f"tree_sha256={tree_hash[:12]} bind_sha256={bind_hash[:12]}", flush=True)

    item_node = h["item_node"].copy()
    base_cnt = h["node_count"].copy()
    pool = np.where(base_cnt > 0)[0]
    sub_size = h["sub_size"]
    anc2_item = h["anc2"][item_node]

    for rho in rhos:
        t1 = time.time()
        rng = np.random.default_rng(seed ^ (int(rho * 1000) << 8))
        cat0, num0, tags0 = gen_attrs(rng, n_items, anc2_item, rho)
        purity = purity_cat(cat0, anc2_item) if rho > 0 else float("nan")

        # ---- churn plan: a stream of n_items updates sampled WITH replacement
        # (an item can move repeatedly, leaving a stale trail at every node it left)
        rngp = np.random.default_rng(seed + 7919)
        upd_item = rngp.integers(0, n_items, n_items)     # stream slot -> item
        dst = pool[rngp.integers(0, len(pool), n_items)]  # stream slot -> destination node
        change = rngp.random(n_items) < attr_change_frac
        anc_dst = h["anc2"][dst]
        if rho > 0:
            det_cat = _det_hash(anc_dst, CAT_N, 2654435761)
            det_tag = _det_hash(anc_dst, TAG_N, 40503, 17)
            adhere = rngp.random(n_items) < rho
            inc_cat = np.where(adhere, det_cat, rngp.integers(0, CAT_N, n_items)).astype(np.int16)
            inc_tag = np.where(adhere, det_tag, rngp.integers(0, TAG_N, n_items)).astype(np.int16)
        else:
            inc_cat = rngp.integers(0, CAT_N, n_items).astype(np.int16)
            inc_tag = rngp.integers(0, TAG_N, n_items).astype(np.int16)
        inc_num = rngp.integers(0, NUM_RANGE, n_items).astype(np.int16)
        inc_tags = rngp.integers(0, TAG_N, (n_items, TAGS_PER_ITEM)).astype(np.int16)
        inc_tags[:, 0] = inc_tag
        for k in range(1, TAGS_PER_ITEM):
            for j in range(k):
                dup = inc_tags[:, k] == inc_tags[:, j]
                for _ in range(8):
                    if not dup.any():
                        break
                    inc_tags[dup, k] = rngp.integers(0, TAG_N, int(dup.sum()))
                    dup = inc_tags[:, k] == inc_tags[:, j]

        rngfix = np.random.default_rng(seed + 13)
        preds = gen_predicates(np.random.default_rng(seed + 31), h, cat0, num0, tags0, n_pred)

        # ---- summaries ----
        bits = np.zeros((n, NBITS), dtype=np.uint8)      # or / epoch presence
        count = np.zeros((n, NBITS), dtype=np.int32)     # exact live ref-count
        epoch = np.full((n, NBITS), -1, dtype=np.int32)  # last-set update index
        item_node_cur = item_node.copy()
        cat, num, tags = cat0.copy(), num0.copy(), tags0.copy()

        b0 = buckets_of(cat0, num0, tags0)
        flat = item_node[:, None] * NBITS + b0
        bits.reshape(-1)[flat.ravel()] = 1
        np.add.at(count.reshape(-1), flat.ravel(), 1)
        epoch.reshape(-1)[flat.ravel()] = 0
        del b0, flat

        bits_f, count_f, epoch_f = bits.reshape(-1), count.reshape(-1), epoch.reshape(-1)
        targets = [int(round(f * n_items)) for f in fracs]
        applied = 0
        ops_or = ops_count = ops_epoch = 0
        ti = 0
        CH = 200_000

        def checkpoint(cp, cur, applied):
            # items move, so rebuild the node->items grouping from *current* bindings
            perm = np.argsort(item_node_cur, kind="stable")
            node_sorted = item_node_cur[perm]
            cnt = np.bincount(node_sorted, minlength=n).astype(np.int64)
            # invariants: every item sits in exactly one node and owns exactly 5 bucket counts
            assert int(cnt.sum()) == n_items, (cnt.sum(), n_items)
            assert int(count.sum()) == (2 + TAGS_PER_ITEM) * n_items, count.sum()
            assert int(count.min()) >= 0
            qstart = np.zeros(n + 1, dtype=np.int64)
            np.cumsum(cnt, out=qstart[1:])
            qcat, qnum, qtags = cat[perm], num[perm], tags[perm]
            num_b = (qnum.astype(np.int64) * NUM_BUCKETS // NUM_RANGE)
            q, qbt = eval_queries(preds, bits, count, epoch, cur, thrs, cnt,
                                  qstart, node_sorted, sub_size, qcat, num_b, qtags)
            fp = fp_stats(bits, count, epoch, cur, thrs, n)
            live_vals = cnt[cnt > 0]
            live_frac = float(len(live_vals)) / n
            mean_card = float(live_vals.mean()) if len(live_vals) else 0.0
            p99_card = float(np.percentile(live_vals, 99)) if len(live_vals) else 0.0
            max_card = int(live_vals.max()) if len(live_vals) else 0
            cnt_ref_probe = q["count"]["probe_items_per_true"] if "count" in q else 1.0
            cnt_ref_node = q["count"]["cand_node_frac"] if "count" in q else 0.0
            upd = max(applied, 1)
            for key, v in qbt.items():
                sem, typ = key.split("|")
                by_type.append(dict(dataset=name, rho=rho, churn=cp, n_updates=applied,
                                    semantics=sem, attr_type=typ, **v))
            for sem in ["or", "count"] + [f"epoch_cons@{t}" for t in thrs] + [f"epoch_aggr@{t}" for t in thrs]:
                if sem not in q:
                    continue
                base = "or" if sem == "or" else ("count" if sem == "count" else "epoch")
                row = dict(dataset=name, rho=rho, semantics=base, mode=sem.split("@")[0].split("_")[-1]
                           if "@" in sem else sem,
                           age_thresh=(int(sem.split("@")[1]) if "@" in sem else ""),
                           churn=cp, n_updates=applied, n_pred=q[sem]["n_pred"],
                           live_node_frac=live_frac, mean_items_per_live_node=mean_card,
                           p99_items_per_live_node=p99_card,
                           max_items_per_live_node=max_card,
                           cand_node_frac=q[sem]["cand_node_frac"],
                           cand_node_excess_vs_count=q[sem]["cand_node_frac"] - cnt_ref_node,
                           probe_ratio_over_count=q[sem]["probe_items_per_true"] / max(cnt_ref_probe, 1e-12),
                           cand_item_frac=q[sem]["cand_item_frac"],
                           probe_items_per_true=q[sem]["probe_items_per_true"],
                           probe_items_per_true_macro=q[sem]["probe_items_per_true_macro"],
                           probe_items_per_scope=q[sem]["probe_items_per_scope"],
                           waste_frac=q[sem]["waste_frac"],
                           missed_true_frac=q[sem]["missed_true_frac"],
                           fp_all=(fp["fp_all"] if base != "count" else 0.0),
                           fp_cat=(fp["fp_cat"] if base != "count" else 0.0),
                           fp_num=(fp["fp_num"] if base != "count" else 0.0),
                           fp_tag=(fp["fp_tag"] if base != "count" else 0.0),
                           fp_trusted=(fp.get(f"fp_trusted@{sem.split('@')[1]}", fp["fp_all"])
                                       if base == "epoch" else (0.0 if base == "count" else fp["fp_all"])),
                           set_bits_per_node=fp["set_bits_per_node"],
                           stale_bits_per_node=fp["stale_bits_per_node"],
                           epoch_trusted_frac=(fp.get(f"trusted_frac@{sem.split('@')[1]}", 1.0)
                                               if base == "epoch" else (1.0 if base == "or" else "")),
                           maint_ops_per_update=(ops_or if base == "or" else
                                                 ops_count if base == "count" else ops_epoch) / upd,
                           maint_ops_per_bucket=(1.0 if base in ("or", "epoch") else 2.0),
                           maint_ops_per_update_effective=(
                               (ops_or if base == "or" else
                                ops_count if base == "count" else
                                ops_epoch + int((epoch >= 0).sum())) / upd),
                           space_bytes_per_node=SPACE[base],
                           space_total_mb=SPACE[base] * n / 1e6,
                           epoch_sweep_bits_total=(int((epoch >= 0).sum()) if base == "epoch" else ""))
                out_rows.append(row)

        if targets and targets[0] == 0:
            checkpoint(fracs[0], 0, 0)
            ti = 1

        # apply exactly up to each checkpoint target (never overshoot a boundary).
        # The stream may hit the same item several times inside one segment, so each
        # segment is replayed in "occurrence rounds": round r holds the r-th touch of
        # every item, all distinct, and rounds run in update order per item.
        for k, target in enumerate(targets):
            if k < ti:
                continue
            while applied < target:
                seg_end = min(target, applied + CH)
                slots = np.arange(applied, seg_end)
                items = upd_item[slots]
                order = np.argsort(items, kind="stable")
                s_items = items[order]
                new_grp = np.empty(len(items), dtype=bool)
                new_grp[0] = True
                new_grp[1:] = s_items[1:] != s_items[:-1]
                grp_start = np.maximum.accumulate(np.where(new_grp, np.arange(len(items)), 0))
                rank = np.empty(len(items), dtype=np.int64)
                rank[order] = np.arange(len(items)) - grp_start
                for r in range(int(rank.max()) + 1):
                    sel = np.nonzero(rank == r)[0]          # ascending slot order
                    idx = items[sel]
                    old_n = item_node_cur[idx]
                    new_n = dst[slots[sel]]
                    ch = change[slots[sel]]
                    old_b = buckets_of(cat[idx], num[idx], tags[idx])
                    ncat = np.where(ch, inc_cat[slots[sel]], cat[idx]).astype(np.int16)
                    nnum = np.where(ch, inc_num[slots[sel]], num[idx]).astype(np.int16)
                    ntags = np.where(ch[:, None], inc_tags[slots[sel]], tags[idx]).astype(np.int16)
                    for kk in range(1, TAGS_PER_ITEM):
                        for jj in range(kk):
                            dup = ntags[:, kk] == ntags[:, jj]
                            for _ in range(8):
                                if not dup.any():
                                    break
                                ntags[dup, kk] = rngfix.integers(0, TAG_N, int(dup.sum()))
                                dup = ntags[:, kk] == ntags[:, jj]
                    new_b = buckets_of(ncat, nnum, ntags)
                    old_flat = (old_n[:, None] * NBITS + old_b).ravel()
                    new_flat = (new_n[:, None] * NBITS + new_b).ravel()
                    bits_f[new_flat] = 1
                    np.add.at(count_f, old_flat, -1)
                    np.add.at(count_f, new_flat, 1)
                    stamps = np.repeat((slots[sel] + 1).astype(np.int32), 2 + TAGS_PER_ITEM)
                    np.maximum.at(epoch_f, new_flat, stamps)
                    item_node_cur[idx] = new_n
                    cat[idx] = ncat
                    num[idx] = nnum
                    tags[idx] = ntags
                    ops_or += (2 + TAGS_PER_ITEM) * len(idx)
                    ops_count += 2 * (2 + TAGS_PER_ITEM) * len(idx)
                    ops_epoch += (2 + TAGS_PER_ITEM) * len(idx)
                applied = seg_end
            checkpoint(fracs[k], applied, applied)

        summary.append(dict(dataset=name, rho=rho,
                            purity_cat=(purity if purity == purity else None),
                            n_nodes=n, n_items=n_items,
                            tree_sha256=tree_hash, bindings_sha256=bind_hash,
                            space_bytes_per_node=SPACE,
                            maint_ops_per_update=dict(or_=2 + TAGS_PER_ITEM,
                                                      count=2 * (2 + TAGS_PER_ITEM),
                                                      epoch=2 + TAGS_PER_ITEM),
                            n_predicates=len(preds)))
        print(f"[{name} rho={rho}] purity={purity:.3f} preds={len(preds)} "
              f"elapsed={time.time() - t1:.1f}s", flush=True)
    print(f"[{name}] done in {time.time() - t0:.1f}s", flush=True)


SPACE = {"or": 24, "count": 176 * 2, "epoch": 24 + 176 * 4}   # bytes/node: 6xuint32 | 176xuint16 | bits+176xuint32


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--datasets", default="wiki,d4_balanced")
    ap.add_argument("--rho", default="0.0,0.8")
    ap.add_argument("--churn-fracs", default="0,0.1,0.2,0.5,0.8")
    ap.add_argument("--attr-change-frac", type=float, default=1.0)
    ap.add_argument("--seed", type=int, default=20261029)
    ap.add_argument("--predicates", type=int, default=200)
    ap.add_argument("--age-thresholds", default="10,100,1000,100000")
    ap.add_argument("--out", default=str(EXP_DIR / "data"))
    ap.add_argument("--suffix", default="")
    a = ap.parse_args()

    rhos = [float(x) for x in a.rho.split(",")]
    fracs = [float(x) for x in a.churn_fracs.split(",")]
    thrs = [int(x) for x in a.age_thresholds.split(",")]
    out_dir = Path(a.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    rows, summary, by_type = [], [], []
    for ds in a.datasets.split(","):
        run_dataset(ds, STAGING / ds, rhos, fracs, a.attr_change_frac, a.seed,
                    a.predicates, thrs, rows, summary, by_type)

    csv_path = out_dir / f"summary_reclaim_curve{a.suffix}.csv"
    with open(csv_path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)

    meta = dict(script=Path(__file__).name, seed=a.seed, attr_change_frac=a.attr_change_frac,
                churn_fracs=fracs, rho=rhos, age_thresholds=thrs, predicates=a.predicates, suffix=a.suffix,
                nbits=NBITS, layout=dict(cat=[0, 32], num=[32, 48], tag=[48, 176]),
                stream_note="update stream length = n_items; the touched item is drawn UNIFORMLY WITH "
                            "REPLACEMENT, so churn=f means f*n_items update events and an item may move "
                            "several times; destination node is uniform over nodes that initially held items",
                num_bucketing=f"bucket=num*{NUM_BUCKETS}//{NUM_RANGE}",
                space_note="or=6xuint32(176b,24B); count=176xuint16(352B, uint16 caps at 65535 items/bucket); "
                           "epoch=176 bits + 176xuint32 stamps (728B)",
                op_note="one bucket-write: or=1, count=2 (dec old + inc new), epoch=1 stamp write "
                        "(+ optional age sweep whose cost = current set bits, reported as epoch_sweep_bits_total); "
                        "maint_ops_per_update_effective adds that sweep cost for epoch and equals "
                        "maint_ops_per_update for or/count",
                query_note="candidate = subtree node whose bitmap has any predicate bucket set; "
                           "probe_items_per_true = items in candidate nodes / truly matching items; "
                           "epoch_cons passes stale bits (== or), epoch_aggr drops them (may miss truth)",
                runs=summary, query_cost_by_type=by_type)
    json_path = out_dir / f"summary_reclaim_summary{a.suffix}.json"
    with open(json_path, "w") as f:
        json.dump(meta, f, indent=2)

    # ---- key tables ----
    print("\n== query cost: probe_items_per_true (item-weighted) ==")
    print(f"{'dataset':>12} {'rho':>4} {'churn':>5} {'or':>7} {'count':>7} "
          f"{'or/count':>8} {'e@1000c':>8} {'e@1000a':>8} {'miss@1000a':>10}")
    for ds in a.datasets.split(","):
        for rho in rhos:
            for cp in fracs:
                sub = {(r["semantics"], str(r["age_thresh"]), r["mode"]): r for r in rows
                       if r["dataset"] == ds and r["rho"] == rho and r["churn"] == cp}
                g = lambda k: sub.get(k, {}).get("probe_items_per_true", float("nan"))
                ov, cv = g(("or", "", "or")), g(("count", "", "count"))
                miss = sub.get(("epoch", "1000", "aggr"), {}).get("missed_true_frac", float("nan"))
                print(f"{ds:>12} {rho:>4} {cp:>5} {ov:>7.3f} {cv:>7.3f} {ov / cv:>8.3f} "
                      f"{g(('epoch','1000','cons')):>8.3f} {g(('epoch','1000','aggr')):>8.3f} {miss:>10.3f}")

    print("\n== bitmap false positives (fp_all) and set bits/node ==")
    print(f"{'dataset':>12} {'rho':>4} {'churn':>5} {'fp_or':>7} {'fp_count':>8} "
          f"{'set/node':>8} {'stale/node':>10} {'items/node':>10}")
    for ds in a.datasets.split(","):
        for rho in rhos:
            for cp in fracs:
                r = next(x for x in rows if x["dataset"] == ds and x["rho"] == rho
                         and x["churn"] == cp and x["semantics"] == "or")
                print(f"{ds:>12} {rho:>4} {cp:>5} {r['fp_all']:>7.3f} {0.0:>8.3f} "
                      f"{r['set_bits_per_node']:>8.3f} {r['stale_bits_per_node']:>10.3f} "
                      f"{r['mean_items_per_live_node']:>10.2f}")

    print("\n== churn=0.8, probe_items_per_true by attribute type (or / count) ==")
    print(f"{'dataset':>12} {'rho':>4} {'type':>5} {'or':>7} {'count':>7} {'candnode_or':>11}")
    for ds in a.datasets.split(","):
        for rho in rhos:
            for typ in ("cat", "num", "tag"):
                o = next((x for x in by_type if x["dataset"] == ds and x["rho"] == rho
                          and x["churn"] == max(fracs) and x["semantics"] == "or"
                          and x["attr_type"] == typ), None)
                c = next((x for x in by_type if x["dataset"] == ds and x["rho"] == rho
                          and x["churn"] == max(fracs) and x["semantics"] == "count"
                          and x["attr_type"] == typ), None)
                if o and c:
                    print(f"{ds:>12} {rho:>4} {typ:>5} {o['probe_items_per_true']:>7.3f} "
                          f"{c['probe_items_per_true']:>7.3f} {o['cand_node_frac']:>11.3f}")

    print(f"\nwrote {csv_path} ({len(rows)} rows) and {json_path}")


if __name__ == "__main__":
    main()
