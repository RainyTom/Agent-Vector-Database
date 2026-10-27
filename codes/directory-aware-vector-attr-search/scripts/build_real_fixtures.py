#!/usr/bin/env python3
"""Build Directory-Aware repro fixtures from the released WIKI-Dir / ARXIV-Dir data.

Sources (downloaded from the paper's Google Drive, see data/real/):
  wiki:  dbpedia_dir_2m_corpus_paths.json  {id, path} per entry (single namespace)
  arxiv: arxiv_corpus_metadata.json        {id, categories: [paths], time: 'yyyy/mm/'}
         arxiv_category_query_constraint.json  1000 real category constraints
         arxiv_category_ground_truth.txt       space-separated relevant ids per line

Outputs (fixture format consumed by src/repro.cpp --mode scope):
  <out>/wiki_tree.txt + wiki_bindings.txt + workload_wiki.txt + dsm_wiki_ops.txt
  <out>/subject_tree.txt + subject_bindings.pairs
  <out>/temporal_tree.txt + temporal_bindings.txt
  <out>/workload_arxiv.txt
  <out>/meta.json            tree/workload statistics + GT validation results

Workloads (constraints for general queries were NOT released - the Drive file
named arxiv_query_constraint.json actually contains query vectors, and wiki
query.jsonl ships with empty metadata), so anchors for general queries are
synthesized following the paper's protocol (Fig.10 depth mix for WIKI; uniform
year/month anchors for ARXIV-temporal).  The 1000 ARXIV category constraints
ARE real and are used verbatim (recursive prefix semantics).
"""
import argparse
import json
import random
import sys
from collections import defaultdict

import ijson


class Trie:
    """Path-insertion trie with stable node ids; parent id < child id."""

    def __init__(self):
        self.parent = [-1]            # node -> parent node (-1 root, -2 removed)
        self.node_of = {"": 0}        # path -> node
        self.path_of = [""]           # node -> path
        self.local_cnt = [0]          # node -> direct binding count

    @staticmethod
    def norm(path):
        p = path.strip("/")
        return "/" + p if p else ""

    def insert(self, path):
        """Insert absolute path ('/a/b/' or 'a/b/'), return leaf node id."""
        path = self.norm(path)
        if path in self.node_of:
            return self.node_of[path]
        parts = [p for p in path.split("/") if p]
        cur = 0
        cur_path = ""
        for part in parts:
            cur_path = cur_path + "/" + part
            nxt = self.node_of.get(cur_path)
            if nxt is None:
                nxt = len(self.parent)
                self.parent.append(cur)
                self.node_of[cur_path] = nxt
                self.path_of.append(cur_path)
                self.local_cnt.append(0)
            cur = nxt
        return cur

    def depth(self, v):
        d = 0
        while self.parent[v] != -1:
            v = self.parent[v]
            d += 1
        return d

    def depths(self):
        ds = [0] * len(self.parent)
        for v in range(1, len(self.parent)):
            ds[v] = ds[self.parent[v]] + 1
        return ds

    def children(self):
        ch = [[] for _ in self.parent]
        for v in range(1, len(self.parent)):
            ch[self.parent[v]].append(v)
        return ch

    def subtree_nodes(self, u):
        ch = self.children()
        out, st = [], [u]
        while st:
            v = st.pop()
            out.append(v)
            st.extend(ch[v])
        return out

    def write(self, tree_path):
        with open(tree_path, "w") as f:
            f.write(f"{len(self.parent)}\n")
            for v, p in enumerate(self.parent):
                f.write(f"{v} {p}\n")


def build_wiki(src, out, rng):
    print("[wiki] streaming corpus_paths.json ...", flush=True)
    trie = Trie()
    bindings = open(f"{out}/wiki_bindings.txt", "w")
    n = 0
    max_id = -1
    with open(f"{src}/wiki/dbpedia_dir_2m_corpus_paths.json", "rb") as f:
        for rec in ijson.items(f, "item"):
            e, path = rec["id"], rec["path"]
            v = trie.insert(path)
            trie.local_cnt[v] += 1
            bindings.write(f"{v}\n")     # line i = entry i -> single binding
            n += 1
            if e > max_id:
                max_id = e
    bindings.close()
    assert max_id == n - 1, f"wiki ids not contiguous: max={max_id} n={n}"
    trie.write(f"{out}/wiki_tree.txt")

    depths = trie.depths()
    by_depth = defaultdict(list)
    for v in range(1, len(trie.parent)):
        by_depth[depths[v]].append(v)

    # paper Fig.10 mix: depth 1/2/4/9 with counts 83/39/72/114 (of 308 labeled)
    weights = {1: 83, 2: 39, 4: 72, 9: 114}
    total_w = sum(weights.values())
    nq = 456
    qs = []
    assigned = 0
    for d, w in weights.items():
        # Cumulative rounding keeps the final total exactly nq.
        k = round(nq * (assigned + w) / total_w) - round(nq * assigned / total_w)
        assigned += w
        for _ in range(k):
            a = rng.choice(by_depth[d])
            qs.append((0, rng.random() < 0.5, a))
    rng.shuffle(qs)
    with open(f"{out}/workload_wiki.txt", "w") as f:
        f.write(f"{len(qs)} 0\n")
        for i, (qidx, rec, a) in enumerate(qs):
            f.write(f"{i} {1 if rec else 0} {a}\n")

    # DSM ops: 2000, 50/50 MOVE/MERGE, entry-weighted
    ch = trie.children()
    leaves_entry_nodes = [v for v in range(len(trie.parent)) if trie.local_cnt[v] > 0]
    ops = []
    while len(ops) < 2000:
        if rng.random() < 0.5:
            # MOVE: u = ancestor (1..9 hops up) of an entry-weighted dir,
            # w = uniform dir not under u
            v = rng.choice(leaves_entry_nodes)
            hops = rng.randint(1, min(9, depths[v]))
            u = v
            for _ in range(hops):
                u = trie.parent[u]
            if u == 0:
                continue
            sub = set(trie.subtree_nodes(u))
            w = rng.randrange(len(trie.parent))
            if w in sub or w == 0:
                continue
            ops.append(("MOVE", u, w))
        else:
            # MERGE: u entry-weighted, w random sibling
            v = rng.choice(leaves_entry_nodes)
            u = v
            p = trie.parent[u]
            sib = [c for c in ch[p] if c != u]
            if not sib:
                continue
            ops.append(("MERGE", u, rng.choice(sib)))
    with open(f"{out}/dsm_wiki_ops.txt", "w") as f:
        f.write(f"{len(ops)} 0\n")
        for kind, u, w in ops:
            f.write(f"{kind} {u} {w}\n")

    meta = {
        "entries": n,
        "dirs": len(trie.parent),
        "max_depth": max(depths),
        "avg_entry_depth": None,
        "workload_queries": len(qs),
        "dsm_ops": len(ops),
    }
    print(f"[wiki] entries={n} dirs={len(trie.parent)} max_depth={max(depths)}",
          flush=True)
    return meta


def build_arxiv(src, out, rng):
    print("[arxiv] loading corpus_metadata.json ...", flush=True)
    md = json.load(open(f"{src}/arxiv/arxiv_corpus_metadata.json"))
    n = len(md)

    subj = Trie()
    time = Trie()
    subj_bind = open(f"{out}/subject_bindings.pairs", "w")
    time_bind = open(f"{out}/temporal_bindings.txt", "w")
    for m in md:
        e = m["id"]
        cats = m["categories"]
        for c in cats:
            v = subj.insert(c)
            subj.local_cnt[v] += 1
            subj_bind.write(f"{e} {v}\n")
        tv = time.insert(m["time"])
        time.local_cnt[tv] += 1
        time_bind.write(f"{tv}\n")
    subj_bind.close()
    time_bind.close()
    subj.write(f"{out}/subject_tree.txt")
    time.write(f"{out}/temporal_tree.txt")

    depths_s = subj.depths()
    depths_t = time.depths()

    # real category constraints (recursive prefix semantics)
    cc = json.load(open(f"{src}/arxiv/arxiv_category_query_constraint.json"))
    qs = []
    missing = 0
    for q in cc:
        path = subj.norm(q["constraints"]["category"])
        v = subj.node_of.get(path)
        if v is None:
            missing += 1
            continue
        qs.append((q["query_id"], 0, 1, v))       # ns=0 subject, rec=1
    # non-recursive subject: anchors at leaf dirs with real direct bindings
    leaf_dirs = [v for v in range(1, len(subj.parent))
                 if subj.local_cnt[v] > 0 and depths_s[v] >= 2]
    for i in range(500):
        qs.append((1000 + i, 0, 0, rng.choice(leaf_dirs)))
    # temporal: year anchors rec=1, month anchors rec=0
    years = [v for v in range(1, len(time.parent)) if depths_t[v] == 1]
    months = [v for v in range(1, len(time.parent)) if depths_t[v] == 2]
    for i in range(250):
        qs.append((1500 + i, 1, 1, rng.choice(years)))
    for i in range(250):
        qs.append((1750 + i, 1, 0, rng.choice(months)))
    rng.shuffle(qs)
    with open(f"{out}/workload_arxiv.txt", "w") as f:
        f.write(f"{len(qs)} 1\n")
        for qidx, ns, rec, a in qs:
            f.write(f"{qidx} {ns} {rec} {a}\n")

    # GT validation: category GT ids must lie inside the prefix subtree
    print("[arxiv] validating category ground truth against prefix scopes ...",
          flush=True)
    entries_of = defaultdict(list)
    for m in md:
        for c in m["categories"]:
            entries_of[subj.node_of[subj.norm(c)]].append(m["id"])
    ch = subj.children()
    gt_checked = 0
    with open(f"{src}/arxiv/arxiv_category_ground_truth.txt") as f:
        gt_lines = f.read().splitlines()
    bad = 0
    for q in cc:
        v = subj.node_of.get(subj.norm(q["constraints"]["category"]))
        if v is None:
            continue
        scope = set()
        st = [v]
        while st:
            x = st.pop()
            if x in entries_of:
                scope.update(entries_of[x])
            st.extend(ch[x])
        ids = [int(t) for t in gt_lines[q["query_id"]].split()]
        gt_checked += 1
        if not set(ids) <= scope:
            bad += 1
            if bad <= 3:
                print(f"  GT mismatch q={q['query_id']} path={q['constraints']['category']}"
                      f" gt_n={len(ids)} out_of_scope={len(set(ids)-scope)}")
    print(f"[arxiv] GT check: {gt_checked - bad}/{gt_checked} queries fully inside scope",
          flush=True)

    meta = {
        "entries": n,
        "subject_dirs": len(subj.parent),
        "temporal_dirs": len(time.parent),
        "subject_max_depth": max(depths_s),
        "temporal_max_depth": max(depths_t),
        "constraints_missing_in_tree": missing,
        "workload_queries": len(qs),
        "gt_queries_checked": gt_checked,
        "gt_queries_mismatch": bad,
    }
    print(f"[arxiv] entries={n} subj_dirs={len(subj.parent)} time_dirs={len(time.parent)}",
          flush=True)
    return meta


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default="data/real")
    ap.add_argument("--out", default="data/real_fixtures")
    ap.add_argument("--seed", type=int, default=20261026)
    ap.add_argument("--arxiv-only", action="store_true")
    ap.add_argument("--wiki-only", action="store_true")
    args = ap.parse_args()
    rng = random.Random(args.seed)
    import os
    os.makedirs(args.out, exist_ok=True)
    meta = {"seed": args.seed}
    if args.arxiv_only:
        meta["arxiv"] = build_arxiv(args.src, args.out, rng)
    elif args.wiki_only:
        meta["wiki"] = build_wiki(args.src, args.out, rng)
    else:
        meta["wiki"] = build_wiki(args.src, args.out, rng)
        meta["arxiv"] = build_arxiv(args.src, args.out, rng)
    with open(f"{args.out}/meta.json", "w") as f:
        json.dump(meta, f, indent=2)
    print("meta:", json.dumps(meta, indent=2))


if __name__ == "__main__":
    main()
