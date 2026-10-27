#!/usr/bin/env python3
"""Build a repro fixture from the released SciHTC dataset (ACM CCS labels).

Source files (public, no application needed):
  /tmp/scihtc_labels/IDs_and_categories/{train,dev,test}_ids_all_categories.csv
  /tmp/SciHTC/HierarchyTree/CCS 2012.html      (for concept-id validation, optional)

SciHTC gives one CCS path per paper: CCS->L1->...->Ln. We materialise the tree
implied by those paths (the paper labels themselves), bind each paper to its
deepest concept (single binding, as in the original DSQ semantics), and emit:
  scihtc_tree.txt      "<n>\n<node> <parent>" lines (node 0 = root CCS)
  scihtc_bindings.txt  one node id per line (line i = paper i)
  workload_scihtc.txt  "<nq> 0" + "<qidx> <rec> <anchor>" lines
  dsm_scihtc.txt       "<nop> 0" + "<MOVE|MERGE> <u> <w>" lines
  meta.json            statistics + honest deviation notes
"""
from __future__ import annotations

import argparse
import csv
import json
import random
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_SRC = Path("/tmp/scihtc_labels/IDs_and_categories")


def load_paths(src: Path):
    rows = []
    for split in ("train", "dev", "test"):
        with open(src / f"{split}_ids_all_categories.csv", encoding="utf-8") as f:
            for r in csv.DictReader(f):
                parts = [p.strip() for p in r["Category"].split("->")][1:]
                parts = [p for p in parts if p]
                # SciHTC/ACM policy: author labels like "Applied computing~Agriculture"
                # break the CCS hierarchy ('~'), so keep only the CCS-consistent
                # prefix of that segment (official experiments do the same).
                cleaned = []
                for seg in parts:
                    if "~" in seg:
                        head = seg.split("~")[0].strip()
                        if head:
                            cleaned.append(head)
                        break
                    cleaned.append(seg)
                if cleaned:
                    rows.append((r["id"], cleaned))
    return rows


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", type=Path, default=DEFAULT_SRC)
    ap.add_argument("--out", type=Path, default=ROOT / "codes" /
                    "directory-aware-vector-attr-search" / "data" / "scihtc")
    ap.add_argument("--n-query", type=int, default=1000)
    ap.add_argument("--n-dsm", type=int, default=2000)
    ap.add_argument("--seed", type=int, default=20261027)
    args = ap.parse_args()
    rng = random.Random(args.seed)

    rows = load_paths(args.src)
    print(f"papers={len(rows)}")

    # ---- build tree from label paths -------------------------------------
    parent = [-1]                       # node 0 = root
    name = [""]
    node_of: dict[str, int] = {"": 0}
    edges: Counter = Counter()          # (parent_name, child_name) multiplicity
    multi_parent: dict[str, set] = defaultdict(set)
    for _, path in rows:
        cur = 0
        for seg in path:
            nid = node_of.get(seg)
            if nid is None:
                nid = len(parent)
                parent.append(cur)
                name.append(seg)
                node_of[seg] = nid
            else:
                # same concept name already exists: check whether this path
                # proposes a different parent (DAG edge collapsed by name match)
                if parent[nid] != cur:
                    multi_parent[seg].add((name[parent[nid]], name[cur]))
            edges[(name[cur], seg)] += 1
            cur = nid

    depth = [0] * len(parent)
    for v in range(1, len(parent)):
        depth[v] = depth[parent[v]] + 1
    bindings = []
    for _, path in rows:
        bindings.append(node_of[path[-1]])

    print(f"tree nodes={len(parent)} max_depth={max(depth)} "
          f"bindings={len(bindings)}")
    print(f"concepts with conflicting parents (DAG collapsed)={len(multi_parent)}")
    if multi_parent:
        for k, v in list(multi_parent.items())[:3]:
            print(f"  {k}: {sorted(v)}")

    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    with open(out / "scihtc_tree.txt", "w", encoding="utf-8") as f:
        f.write(f"{len(parent)}\n")
        for v in range(len(parent)):
            f.write(f"{v} {parent[v]}\n")
    with open(out / "scihtc_bindings.txt", "w", encoding="utf-8") as f:
        for v in bindings:
            f.write(f"{v}\n")

    # ---- workload: anchors by depth, 50% recursive ------------------------
    by_depth = defaultdict(list)
    for v in range(1, len(parent)):
        by_depth[depth[v]].append(v)
    depth_counts = Counter(depth[b] for b in bindings)
    anchors = []
    for d in sorted(by_depth):
        pool = by_depth[d]
        k = max(3, args.n_query // (max(depth) * 2))
        anchors += [rng.choice(pool) for _ in range(k)]
    qs = []
    for i in range(args.n_query):
        a = anchors[i % len(anchors)]
        qs.append((i % 1000, 1 if rng.random() < 0.5 else 0, a))
    with open(out / "workload_scihtc.txt", "w", encoding="utf-8") as f:
        f.write(f"{len(qs)} 0\n")
        for qidx, rec, a in qs:
            f.write(f"{qidx} {rec} {a}\n")

    # ---- DSM ops: random internal nodes, legal MOVE/MERGE ----------------
    def descendants(u):
        out, st = [], [u]
        while st:
            x = st.pop()
            out.append(x)
            for c in children.get(x, ()):
                st.append(c)
        return out

    children = defaultdict(list)
    for v in range(1, len(parent)):
        children[parent[v]].append(v)
    internal = [v for v in range(1, len(parent)) if children[v]]
    ops = []
    guard = 0
    while len(ops) < args.n_dsm and guard < args.n_dsm * 100:
        guard += 1
        u = rng.choice(internal)
        if rng.random() < 0.5:
            sub = set(descendants(u))
            w = rng.randrange(1, len(parent))
            if w in sub:
                continue
            ops.append(("MOVE", u, w))
        else:
            p = parent[u]
            sib = [c for c in children[p] if c != u]
            if not sib:
                continue
            ops.append(("MERGE", u, rng.choice(sib)))
    with open(out / "dsm_scihtc.txt", "w", encoding="utf-8") as f:
        f.write(f"{len(ops)} 0\n")
        for kind, u, w in ops:
            f.write(f"{kind} {u} {w}\n")

    meta = {
        "source": "SciHTC (Sadat & Caragea, EMNLP 2022), public IDs+labels",
        "papers": len(rows), "tree_nodes": len(parent), "max_depth": max(depth),
        "binding_depth_hist": {int(k): int(v) for k, v in sorted(depth_counts.items())},
        "concepts_with_conflicting_parents": len(multi_parent),
        "queries": len(qs), "dsm_ops": len(ops), "seed": args.seed,
        "notes": [
            "tree built from label paths only; CCS DAG multi-parent edges are "
            "collapsed by concept name (conflict count above)",
            "each paper bound to the deepest concept of its released path "
            "(single binding, matches DSQ nrec semantics)",
            "vectors are NOT included: SciHTC text needs an application; run "
            "scope/DSM without vectors, or supply placeholder/real vectors",
        ],
    }
    json.dump(meta, open(out / "meta.json", "w", encoding="utf-8"), indent=2)
    print(json.dumps(meta, indent=2)[:800])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
