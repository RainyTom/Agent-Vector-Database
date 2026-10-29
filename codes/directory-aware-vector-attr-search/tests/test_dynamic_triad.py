"""Current-state scope/predicate regression across all three dynamic index arms.

Build build_dynamic_triad.sh first. Expected sets are computed without the runner's
postings or GT; <=10 live records makes every eligible ID part of exact top-10.
No Python vector distance or search implementation is used.
"""
import csv
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DynamicTriadSemantics(unittest.TestCase):
    def test_moves_reparents_resurrection_thresholds_and_set_semantics(self):
        binary = ROOT / "build/dynamic_triad"
        self.assertTrue(binary.exists(), "run build_dynamic_triad.sh first")
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            n, dim = 12, 8
            (path / "meta.txt").write_text(f"{n} {dim} 1\n")
            (path / "base.f32").write_bytes(b"".join(
                struct.pack("<8f", *[float(i * 3 + j) for j in range(dim)])
                for i in range(n)))
            (path / "test.f32").write_bytes(struct.pack("<8f", *[0.25] * dim))
            parent = [-1, 0, 0, 1, 1, 2, 2]
            nodes = [3, 3, 4, 4, 5, 5, 1, 2, 0, 3, 4, 5]
            attrs = [[i % 2, i, (1 << (i % 3)) | 1, i < 10]
                     for i in range(n)]
            (path / "tree.txt").write_text("7\n" + "".join(
                f"{i} {p}\n" for i, p in enumerate(parent)))
            (path / "bindings.txt").write_text("\n".join(map(str, nodes)) + "\n")
            (path / "attrs.txt").write_text("".join(
                f"{c} {v} {t} {int(a)}\n" for c, v, t, a in attrs))
            events, expected = [], []

            def inside(node, anchor, rec):
                if not rec:
                    return node == anchor
                while node >= 0:
                    if node == anchor:
                        return True
                    node = parent[node]
                return False

            def query(anchor, rec, cat=-1, lo=-1, hi=-1, mode=0, mask=0, combine=0):
                events.append(f"Q 0 {rec} {anchor} {cat} {lo} {hi} {mode} {mask} {combine}")
                eligible = set()
                for item, (category, number, tags, alive) in enumerate(attrs):
                    if not alive or not inside(nodes[item], anchor, rec):
                        continue
                    predicates = []
                    if cat >= 0:
                        predicates.append(category == cat)
                    if lo >= 0 or hi >= 0:
                        predicates.append(lo <= number <= hi)
                    if mode:
                        predicates.append({1: (tags & mask) == mask,
                                           2: bool(tags & mask),
                                           3: tags == mask,
                                           4: (tags & mask) == tags}[mode])
                    if not predicates or (any(predicates) if combine else all(predicates)):
                        eligible.add(item)
                self.assertLessEqual(len(eligible), 10)
                expected.append(eligible)

            def observe():
                for anchor in range(7):
                    for rec in (0, 1):
                        query(anchor, rec)
                for mode in range(1, 5):
                    for combine in (0, 1):
                        query(0, 1, cat=1, lo=2, hi=8, mode=mode,
                              mask=3, combine=combine)
                    query(0, 1, mode=mode, mask=0)  # Empty-set quantifiers differ.
                query(0, 1, lo=0, hi=0)
                query(0, 1, lo=42, hi=42)
                query(0, 1, cat=99)  # zero-result convention

            observe()
            for item, dst in ((0, 5), (0, 3)):
                events.append(f"M {item} {dst}")
                nodes[item] = dst
                observe()
            for item, active in ((1, False), (10, True), (10, False), (1, True)):
                if active:
                    c, v, t, _ = attrs[item]
                    events.append(f"I {item} {nodes[item]} {c} {v} {t}")
                else:
                    events.append(f"D {item}")
                attrs[item][3] = active
                observe()
            events.append("V 2 3 42")
            attrs[2][:2] = [3, 42]
            events.append("T 3 2")
            attrs[3][2] = 2
            observe()
            events.append("T 3 0")
            attrs[3][2] = 0
            observe()
            events.append("B 4 5")
            for item in range(n):
                if attrs[item][3] and inside(nodes[item], 4, True):
                    nodes[item] = 5
            observe()
            for destination in (2, 0):
                events.append(f"R 1 {destination}")
                parent[1] = destination
                observe()
            for item in (0, 9):
                events.append(f"D {item}")
                attrs[item][3] = False
            for item in (11, 0):
                c, v, t, _ = attrs[item]
                events.append(f"I {item} 3 {c} {v} {t}")
                nodes[item], attrs[item][3] = 3, True
                observe()
            trace = path / "trace.txt"
            trace.write_text(str(len(events)) + "\n" + "\n".join(events) + "\n")
            for arm in "ABC":
                with self.subTest(arm=arm):
                    out = path / arm
                    subprocess.run([str(binary), "--data", str(path), "--trace", str(trace),
                                    "--arm", arm, "--ef", "256", "--scan-threshold", "2",
                                    "--compact-threshold", "0.2", "--out", str(out)],
                                   check=True, capture_output=True, text=True)
                    with Path(str(out) + ".queries.csv").open() as handle:
                        rows = list(csv.DictReader(handle))
                    self.assertEqual(len(rows), len(expected))
                    for row, want in zip(rows, expected):
                        got = {int(x) for x in row["result_ids"].split(";") if x}
                        self.assertEqual(got, want, (arm, row["event"]))
                        gt = {int(x) for x in row["gt_ids"].split(";") if x}
                        self.assertEqual(gt, want)
                        self.assertEqual(int(row["invalid_count"]), 0)
                        self.assertEqual(int(row["duplicate_count"]), 0)
                    summary = json.loads(Path(str(out) + ".summary.json").read_text())
                    self.assertEqual(summary["state_violations"], 0)
                    self.assertEqual(summary["macro_recall"], 1.0)
                    self.assertEqual(summary["pair_recall"], 1.0)


if __name__ == "__main__":
    unittest.main()
