"""Current-state scope/predicate and exact-GT tape regression for all three arms.

Build build_dynamic_triad.sh first, or set DYNAMIC_TRIAD_BINARY to a separately
built runner. Expected sets do not use the runner's postings or GT; <=10 live
records makes every eligible ID part of exact top-10. Tape tests also use k=3
to exercise genuine ranked IDs and compare measured algorithm work by mode.
"""
import csv
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get("DYNAMIC_TRIAD_BINARY", ROOT / "build/dynamic_triad"))


class DynamicTriadSemantics(unittest.TestCase):
    def test_moves_reparents_resurrection_thresholds_and_set_semantics(self):
        binary = BINARY
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
                    self.assertEqual(summary["gt_mode"], "inline")
                    self.assertEqual(summary["gt_distance_calls"],
                                     sum(int(row["eligible_card"]) for row in rows))
                    self.assertEqual([int(row["gt_distance_calls"]) for row in rows],
                                     [int(row["eligible_card"]) for row in rows])
                    # One canonical inline oracle feeds every arm after every
                    # move, reparent, predicate edit and resurrection above.
                    tape = path / "A.queries.csv"
                    replay = path / f"{arm}-precomputed"
                    subprocess.run([str(binary), "--data", str(path), "--trace", str(trace),
                                    "--arm", arm, "--ef", "256", "--scan-threshold", "2",
                                    "--compact-threshold", "0.2", "--out", str(replay),
                                    "--gt-in", str(tape)],
                                   check=True, capture_output=True, text=True)
                    with Path(str(replay) + ".queries.csv").open() as handle:
                        replay_rows = list(csv.DictReader(handle))
                    self.assertEqual(len(replay_rows), len(rows))
                    for inline, precomputed in zip(rows, replay_rows):
                        for field in rows[0]:
                            if field not in ("dir_us", "search_us", "e2e_us", "gt_us",
                                             "gt_distance_calls"):
                                self.assertEqual(precomputed[field], inline[field],
                                                 (arm, inline["event"], field))
                        self.assertEqual(int(precomputed["gt_distance_calls"]), 0)
                    replay_summary = json.loads(Path(str(replay) + ".summary.json").read_text())
                    self.assertEqual(replay_summary["gt_mode"], "precomputed")
                    self.assertEqual(replay_summary["gt_distance_calls"], 0)
                    for field in ("state_violations", "macro_recall", "pair_recall",
                                  "gt_count", "hit_count", "distance_calls", "ann_hops"):
                        self.assertEqual(replay_summary[field], summary[field])

    def test_ranked_tape_validation_and_query_work_invariance(self):
        self.assertTrue(BINARY.exists(), "build the dynamic runner first")
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / "meta.txt").write_text("6 8 2\n")
            # Deliberately non-ID rank order, including a distance tie in qidx=1.
            (path / "base.f32").write_bytes(b"".join(
                struct.pack("<8f", *([float(value)] * 8))
                for value in (4, 0, 2, 1, 3, 5)))
            (path / "test.f32").write_bytes(b"".join(
                struct.pack("<8f", *([value] * 8)) for value in (0.25, 2.5)))
            (path / "tree.txt").write_text("3\n0 -1\n1 0\n2 0\n")
            (path / "bindings.txt").write_text("1\n1\n1\n2\n2\n2\n")
            (path / "attrs.txt").write_text("0 0 0 1\n" * 5 + "0 0 0 0\n")
            events = [
                "Q 0 0 1 -1 -1 -1 0 0 0", "D 1",
                "Q 0 0 1 -1 -1 -1 0 0 0", "I 5 1 0 0 0",
                "Q 0 0 1 -1 -1 -1 0 0 0", "M 0 2",
                "Q 0 0 1 -1 -1 -1 0 0 0", "R 1 2",
                "Q 1 1 2 -1 -1 -1 0 0 0", "V 2 9 2",
                "Q 0 1 2 0 -1 -1 0 0 0", "T 5 3",
                "Q 0 1 2 -1 -1 -1 1 3 0",
                "Q 0 1 2 99 -1 -1 0 0 0",
            ]
            trace = path / "trace.txt"
            trace.write_text(f"{len(events)}\n" + "\n".join(events) + "\n")

            def run(arm, name, tape=None):
                out = path / name
                command = [str(BINARY), "--data", str(path), "--trace", str(trace),
                           "--arm", arm, "--ef", "256", "--scan-threshold", "2",
                           "--compact-threshold", "0.3", "--k", "3", "--out", str(out)]
                if tape is not None:
                    command += ["--gt-in", str(tape)]
                process = subprocess.run(command, capture_output=True, text=True)
                summary = json.loads(Path(str(out) + ".summary.json").read_text())
                with Path(str(out) + ".queries.csv").open() as handle:
                    rows = list(csv.DictReader(handle))
                return process, summary, rows

            process, oracle_summary, oracle = run("A", "oracle")
            self.assertEqual(process.returncode, 0, process.stderr)
            self.assertEqual(oracle[0]["gt_ids"], "1;2;0")
            self.assertEqual(oracle[1]["gt_ids"], "2;0")
            self.assertEqual(oracle[4]["gt_ids"], "2;4;0")
            self.assertEqual(oracle[-2]["gt_ids"], "5")
            self.assertEqual(oracle[-1]["gt_ids"], "")
            self.assertGreater(int(oracle[4]["eligible_card"]), 3)
            fields = list(oracle[0])

            def write_tape(name, rows, columns=None):
                tape = path / f"{name}.csv"
                columns = fields if columns is None else columns
                with tape.open("w", newline="") as handle:
                    writer = csv.DictWriter(handle, fieldnames=columns, extrasaction="ignore")
                    writer.writeheader()
                    writer.writerows(rows)
                return tape

            # The old schema is deliberately supported as an exact oracle tape.
            valid = write_tape("legacy", oracle, fields[:-1])
            modern = path / "oracle.queries.csv"
            bad_tapes = {}
            for field in ("event", "version", "query_index", "qidx", "recursive",
                          "anchor", "anchor_depth", "scope_card", "eligible_card"):
                rows = [dict(row) for row in oracle]
                rows[0][field] = str(int(rows[0][field]) + 1)
                bad_tapes[f"identity-{field}"] = write_tape(f"identity-{field}", rows)
            for name, index, field, value in (
                ("duplicate", 0, "gt_ids", "1;1;0"),
                ("out-of-range", 0, "gt_ids", "1;2;999"),
                ("stale-deleted", 1, "gt_ids", "2;1"),
                ("stale-predicate", 5, "gt_ids", "2;4;0"),
                ("wrong-scope", 0, "gt_ids", "1;2;3"),
                ("missing-id", 0, "gt_ids", "1;2"),
                ("extra-id", 0, "gt_ids", "1;2;0;3"),
                ("negative-id", 0, "gt_ids", "1;2;-1"),
                ("trailing-delimiter", 0, "gt_ids", "1;2;"),
                ("malformed-id", 0, "gt_ids", "1;2;garbage"),
                ("integer-overflow", 0, "event", str(2**64)),
                ("negative-integer", 0, "scope_card", "-1"),
                ("nonfinite-time", 0, "gt_us", "nan"),
                ("gt-under-count", 0, "gt_count", "2"),
                ("gt-over-k", 4, "gt_count", "4"),
                ("invalid-oracle", 0, "invalid_count", "1"),
                ("false-hit-count", 0, "hit_count", "0"),
                ("false-call-count", 0, "gt_distance_calls", "0"),
            ):
                rows = [dict(row) for row in oracle]
                rows[index][field] = value
                bad_tapes[name] = write_tape(name, rows)
            bad_tapes["missing-row"] = write_tape("missing-row", oracle[:-1])
            bad_tapes["empty"] = write_tape("empty", [])
            bad_tapes["extra-row"] = write_tape("extra-row", oracle + [oracle[-1]])
            bad_tapes["bad-schema"] = write_tape("bad-schema", oracle, fields[1:])
            malformed = path / "bad-columns.csv"
            malformed.write_text(",".join(fields) + "\n1,2,3\n")
            bad_tapes["bad-columns"] = malformed
            bad_tapes["missing-file"] = path / "does-not-exist.csv"

            for arm in "ABC":
                process, inline_summary, inline = run(arm, f"{arm}-inline")
                self.assertEqual(process.returncode, 0, process.stderr)
                self.assertEqual(inline_summary["gt_mode"], "inline")
                self.assertEqual(inline_summary["gt_distance_calls"],
                                 sum(int(row["eligible_card"]) for row in inline))
                self.assertEqual(inline_summary["gt_distance_calls"],
                                 oracle_summary["gt_distance_calls"])
                for tape in (valid, modern):
                    with self.subTest(arm=arm, tape=tape.name):
                        process, summary, rows = run(arm, f"{arm}-{tape.stem}", tape)
                        self.assertEqual(process.returncode, 0, process.stderr)
                        self.assertTrue(summary["complete"])
                        self.assertEqual(summary["gt_mode"], "precomputed")
                        self.assertEqual(summary["gt_distance_calls"], 0)
                        self.assertEqual(len(rows), len(inline))
                        for got, want, canonical in zip(rows, inline, oracle):
                            self.assertEqual(got["gt_ids"], canonical["gt_ids"])
                            self.assertEqual(int(got["gt_distance_calls"]), 0)
                            for field in ("result_ids", "distance_calls", "ann_hops",
                                          "scope_card", "eligible_card", "gt_count", "hit_count",
                                          "recall", "valid_count", "invalid_count", "duplicate_count"):
                                self.assertEqual(got[field], want[field], (arm, got["event"], field))
                        self.assertEqual(summary["distance_calls"], inline_summary["distance_calls"])
                        self.assertEqual(summary["state_violations"], 0)
                for name, tape in bad_tapes.items():
                    with self.subTest(arm=arm, malformed=name):
                        process, summary, _ = run(arm, f"{arm}-bad-{name}", tape)
                        self.assertNotEqual(process.returncode, 0)
                        self.assertFalse(summary["complete"])
                        self.assertEqual(summary["gt_mode"], "precomputed")
                        self.assertEqual(summary["gt_distance_calls"], 0)

    def test_disjoint_maintenance_and_real_graph_state(self):
        binary = BINARY
        self.assertTrue(binary.exists(), "run build_dynamic_triad.sh first")
        categories = ("tombstone_compaction", "threshold_build", "threshold_drop")
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / "meta.txt").write_text("6 8 1\n")
            (path / "base.f32").write_bytes(b"".join(
                struct.pack("<8f", *[float(i * 3 + j) for j in range(8)])
                for i in range(6)))
            (path / "test.f32").write_bytes(struct.pack("<8f", *[0.25] * 8))
            (path / "tree.txt").write_text("2\n0 -1\n1 0\n")
            (path / "bindings.txt").write_text("1\n" * 6)
            (path / "attrs.txt").write_text("0 0 0 1\n" * 6)
            # First restore a tombstoned label without rebuilding; then cross
            # dead-ratio, graph->scan, and scan->graph thresholds in that order.
            updates = ["D 0", "I 0 1 0 0 0", "D 0", "D 1",
                       "D 2", "D 3", "I 0 1 0 0 0"]
            live = set(range(6))
            expected = [live.copy()]
            events = ["Q 0 0 1 -1 -1 -1 0 0 0"]
            for update in updates:
                item = int(update.split()[1])
                if update.startswith("D"):
                    live.remove(item)
                else:
                    live.add(item)
                events.extend([update, "Q 0 0 1 -1 -1 -1 0 0 0"])
                expected.append(live.copy())
            trace = path / "trace.txt"
            trace.write_text(f"{len(events)}\n" + "\n".join(events) + "\n")
            for arm in "ABC":
                with self.subTest(arm=arm):
                    out = path / arm
                    subprocess.run([str(binary), "--data", str(path), "--trace", str(trace),
                                    "--arm", arm, "--ef", "256", "--scan-threshold", "2",
                                    "--compact-threshold", "0.3", "--out", str(out)],
                                   check=True, capture_output=True, text=True)
                    with Path(str(out) + ".queries.csv").open() as handle:
                        queries = list(csv.DictReader(handle))
                    self.assertEqual(len(queries), len(expected))
                    for row, want in zip(queries, expected):
                        for field in ("result_ids", "gt_ids"):
                            self.assertEqual({int(x) for x in row[field].split(";") if x},
                                             want, (arm, row["event"], field))
                        self.assertEqual(int(row["invalid_count"]), 0)
                        self.assertEqual(int(row["duplicate_count"]), 0)
                    with Path(str(out) + ".updates.csv").open() as handle:
                        rows = list(csv.DictReader(handle))
                    self.assertEqual(len(rows), len(updates))
                    summary = json.loads(Path(str(out) + ".summary.json").read_text())
                    expected_counts = [(0, 0, 0), (0, 0, 0), (0, 0, 0),
                                       (1, 0, 0), (0, 0, 0), (0, 0, 1), (0, 1, 0)]
                    expected_insertions = [(0, 0, 0)] * 3 + [
                        (4, 0, 0), (0, 0, 0), (0, 0, 0), (0, 3, 0)]
                    for row, counts, insertions in zip(rows, expected_counts, expected_insertions):
                        actual_counts = tuple(int(row[c + "_count"]) for c in categories)
                        actual_insertions = tuple(int(row[c + "_ann_insertions"]) for c in categories)
                        self.assertEqual(actual_counts, counts if arm == "A" else (0, 0, 0))
                        self.assertEqual(actual_insertions, insertions if arm == "A" else (0, 0, 0))
                        self.assertEqual(sum(actual_counts), int(row["compaction_count"]))
                        self.assertLessEqual(sum(actual_insertions), int(row["ann_insertions"]))
                        self.assertAlmostEqual(sum(float(row[c + "_us"]) for c in categories),
                                               float(row["compaction_us"]), delta=1e-5)
                        for category in categories:
                            duration = float(row[category + "_us"])
                            if int(row[category + "_count"]):
                                self.assertGreater(duration, 0)
                            else:
                                self.assertEqual(duration, 0)
                    self.assertEqual(summary["compaction_count"], 3 if arm == "A" else 0)
                    self.assertEqual(sum(summary[c + "_count"] for c in categories),
                                     summary["compaction_count"])
                    self.assertAlmostEqual(sum(summary[c + "_us"] for c in categories),
                                           summary["compaction_total_us"], delta=1e-5)
                    for category in categories:
                        for suffix in ("_us", "_count", "_ann_insertions"):
                            self.assertAlmostEqual(summary[category + suffix],
                                                   sum(float(r[category + suffix]) for r in rows),
                                                   delta=1e-5)
                    self.assertEqual(summary["initial_hnsw_node_count"], 1)
                    self.assertEqual(summary["initial_index_live_count"], 6)
                    self.assertEqual(summary["initial_index_dead_count"], 0)
                    self.assertEqual(summary["final_hnsw_node_count"], 1)
                    self.assertEqual(summary["final_index_live_count"], 3)
                    self.assertEqual(summary["final_index_dead_count"], 0 if arm == "A" else 3)
                    self.assertEqual(summary["state_violations"], 0)
                    self.assertEqual(summary["macro_recall"], 1)


if __name__ == "__main__":
    unittest.main()
