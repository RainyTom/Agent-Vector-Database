#!/usr/bin/env python3
"""Run the C++ dynamic triad, retaining failed quality points and raw evidence.

  python scripts/run_dynamic_triad.py generate --smoke --suite screening
  python scripts/run_dynamic_triad.py --manifest MANIFEST --dry-run
  python scripts/run_dynamic_triad.py --manifest MANIFEST --jobs 2 --repeats 3

This is MULTI-PROCESS orchestration of serialized single-thread C++ replays,
not concurrent query/update throughput. Every arm/repeat uses the same exact
trace. No GT, indexing, search, or benchmark timings are implemented in Python.
Resume requires exact config, executable, input-data, trace, and raw-output
hashes; raw CSVs are always revalidated. Recall failures remain in results.
--gt-mode precomputed first completes independent C++ inline oracles, then
replays every measured arm against the same hash-bound query CSV tape.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import shlex
import statistics
import subprocess
import sys

from generate_dynamic_triad import (ROOT, FORMAT_VERSION, json_hash,
                                     select_configs, sha256_file, write_json)

QUERY_COLUMNS = ("event", "version", "query_index", "qidx", "recursive", "anchor",
                 "anchor_depth", "scope_card", "eligible_card", "dir_us", "search_us",
                 "e2e_us", "gt_us", "gt_count", "hit_count", "recall", "distance_calls",
                 "ann_hops", "local_indexes", "valid_count", "invalid_count",
                 "duplicate_count", "gt_ids", "result_ids", "fanout", "scan_nodes")
UPDATE_COLUMNS = ("event", "version", "op", "latency_us", "affected_items",
                  "affected_nodes", "metadata_writes", "ann_insertions", "ann_deletions",
                  "compaction_us", "compaction_count")
SUMMARY_COLUMNS = ("arm", "ef", "scan_threshold", "n_vectors", "n_nodes", "n_queries",
                   "n_updates", "build_s", "index_bytes", "peak_rss_bytes",
                   "query_p50_us", "query_p95_us", "query_p99_us", "update_p50_us",
                   "update_p95_us", "update_p99_us", "query_total_us", "update_total_us",
                   "total_workload_us", "gt_total_us", "compaction_total_us", "compaction_count",
                   "metadata_writes", "ann_insertions", "ann_deletions", "macro_recall",
                   "pair_recall", "invalid_results", "duplicate_results", "state_violations",
                   "distance_calls", "ann_hops", "fanout_total", "scan_nodes_total")
RAW_SUFFIXES = (".queries.csv", ".updates.csv", ".summary.json")
SINGLE_THREAD_ENV = {name: "1" for name in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS",
                                            "MKL_NUM_THREADS", "NUMEXPR_NUM_THREADS",
                                            "VECLIB_MAXIMUM_THREADS")}


def read_json(path):
    return json.loads(Path(path).read_text())


def raw_hashes(prefix):
    return {suffix: sha256_file(str(prefix) + suffix) for suffix in RAW_SUFFIXES
            if Path(str(prefix) + suffix).is_file()}


def trace_layout(path, n_live, n_vectors):
    """Read event identity/version/liveness; C++ alone evaluates predicates."""
    queries, updates = [], []
    version = 0
    alive = [item < n_live for item in range(n_vectors)]
    live_count = n_live
    events = 0
    with Path(path).open() as handle:
        declared = int(handle.readline())
        for event_number, line in enumerate(handle, 1):
            fields = line.split()
            if not fields:
                raise ValueError(f"blank trace event {event_number}")
            op = fields[0]
            values = tuple(map(int, fields[1:]))
            if op == "Q":
                if len(values) != 9:
                    raise ValueError("wrong query field count")
                queries.append((event_number, version, len(queries), *values[:3], live_count))
            elif op in "VTMBRDI":
                if op in ("V", "T", "M", "D", "I"):
                    item = values[0]
                    if item < 0 or item >= n_vectors:
                        raise ValueError("trace references out-of-range item")
                    if op == "I":
                        if alive[item]:
                            raise ValueError("trace inserts live item")
                        alive[item] = True
                        live_count += 1
                    elif not alive[item]:
                        raise ValueError("trace mutates dead item")
                    elif op == "D":
                        alive[item] = False
                        live_count -= 1
                version += 1
                updates.append((event_number, version, op))
            else:
                raise ValueError(f"unknown trace event {op}")
            events += 1
    if declared != events:
        raise ValueError(f"declared {declared} events, found {events}")
    return queries, updates


def ids(value):
    return [int(part) for part in value.split(";") if part]


def close(left, right, relative=1e-5):
    return abs(left - right) <= max(1e-5, relative * max(abs(left), abs(right)))


def validate_raw(prefix, config, arm, expected, k, gt_mode="inline", require_gt=False):
    """Validate every raw row, not merely a plausible summary file."""
    errors = []
    summary = {}
    comparison = hashlib.sha256()
    query_count, update_count = 0, 0
    recall_sum = 0.0
    hits, ground_truth = 0, 0
    query_us, update_us = 0.0, 0.0
    gt_us, gt_calls = 0.0, 0
    try:
        summary = read_json(str(prefix) + ".summary.json")
        missing = set(SUMMARY_COLUMNS) - summary.keys()
        if missing:
            errors.append("summary missing fields: " + ",".join(sorted(missing)))
        if summary.get("arm") != arm:
            errors.append("summary arm mismatch")
        if summary.get("complete") is False:
            errors.append("C++ reports incomplete replay: " + str(summary.get("error", "")))
        if require_gt and summary.get("complete") is not True:
            errors.append("C++ must explicitly report a complete replay")
        if require_gt or "gt_mode" in summary:
            if summary.get("gt_mode") != gt_mode:
                errors.append(f"summary gt_mode must be {gt_mode}")
            value = summary.get("gt_distance_calls")
            if not isinstance(value, int) or isinstance(value, bool) or value < 0:
                errors.append("invalid summary gt_distance_calls")
        for key in SUMMARY_COLUMNS:
            value = summary.get(key)
            if key != "arm" and (not isinstance(value, (int, float)) or
                                 not math.isfinite(value) or value < 0):
                errors.append(f"invalid numeric summary field {key}")
        for key in ("invalid_results", "duplicate_results", "state_violations"):
            if summary.get(key) != 0:
                errors.append(f"nonzero {key}")
        for key in ("n_queries", "n_updates", "n_vectors", "n_nodes"):
            if summary.get(key) != config[key]:
                errors.append(f"summary {key} does not match input manifest")
        with Path(str(prefix) + ".queries.csv").open(newline="") as handle:
            reader = csv.DictReader(handle)
            missing = set(QUERY_COLUMNS) - set(reader.fieldnames or ())
            if missing:
                raise ValueError("query CSV missing: " + ",".join(sorted(missing)))
            for index, row in enumerate(reader):
                query_count += 1
                if index >= len(expected[0]):
                    raise ValueError("unexpected extra query row")
                actual = tuple(int(row[key]) for key in
                               ("event", "version", "query_index", "qidx", "recursive", "anchor"))
                if actual != expected[0][index][:6]:
                    raise ValueError(f"query {index}: event/version/input identity mismatch")
                gt, result = ids(row["gt_ids"]), ids(row["result_ids"])
                gt_count, hit_count = int(row["gt_count"]), int(row["hit_count"])
                if len(gt) != gt_count or len(set(gt)) != len(gt) or gt_count > k:
                    raise ValueError(f"query {index}: inconsistent GT ID/count")
                if any(item < 0 or item >= config["n_vectors"] for item in gt + result):
                    raise ValueError(f"query {index}: out-of-range item ID")
                if len(set(result)) != len(result) or int(row["duplicate_count"]) != 0:
                    raise ValueError(f"query {index}: duplicate results")
                if int(row["invalid_count"]) != 0 or int(row["valid_count"]) != len(result):
                    raise ValueError(f"query {index}: invalid/result count mismatch")
                if len(result) > k or len(result) > int(row["eligible_card"]):
                    raise ValueError(f"query {index}: excess results")
                if gt_count != min(k, int(row["eligible_card"])):
                    raise ValueError(f"query {index}: GT count inconsistent with eligible cardinality")
                current_live = expected[0][index][6]
                if not (0 <= int(row["eligible_card"]) <= int(row["scope_card"]) <= current_live):
                    raise ValueError(f"query {index}: invalid scope/eligible/live cardinality")
                if actual[4:] == (1, 0) and int(row["scope_card"]) != current_live:
                    raise ValueError(f"query {index}: root scope differs from committed live count")
                if hit_count != len(set(gt).intersection(result)):
                    raise ValueError(f"query {index}: hit count inconsistent with IDs")
                recall = float(row["recall"])
                if not math.isfinite(recall) or not close(recall, hit_count / gt_count if gt_count else 1.0):
                    raise ValueError(f"query {index}: recall inconsistent with hits/GT")
                for key in ("dir_us", "search_us", "e2e_us", "gt_us", "distance_calls",
                            "ann_hops", "local_indexes", "fanout", "scan_nodes"):
                    value = float(row[key])
                    if not math.isfinite(value) or value < 0:
                        raise ValueError(f"query {index}: invalid {key}")
                expected_calls = int(row["eligible_card"]) if gt_mode == "inline" else 0
                if "gt_distance_calls" in row and int(row["gt_distance_calls"]) != expected_calls:
                    raise ValueError(f"query {index}: GT distance call count mismatch")
                gt_calls += expected_calls
                gt_us += float(row["gt_us"])
                # Current-state GT and scope must agree across all arms. GT
                # output ordering may differ, so compare canonical sorted IDs.
                identity = [*actual, int(row["anchor_depth"]), int(row["scope_card"]),
                            int(row["eligible_card"]), gt_count, sorted(gt)]
                comparison.update(json.dumps(identity, separators=(",", ":")).encode() + b"\n")
                recall_sum += recall
                hits += hit_count
                ground_truth += gt_count
                query_us += float(row["e2e_us"])
        with Path(str(prefix) + ".updates.csv").open(newline="") as handle:
            reader = csv.DictReader(handle)
            missing = set(UPDATE_COLUMNS) - set(reader.fieldnames or ())
            if missing:
                raise ValueError("update CSV missing: " + ",".join(sorted(missing)))
            for index, row in enumerate(reader):
                update_count += 1
                if index >= len(expected[1]):
                    raise ValueError("unexpected extra update row")
                identity = (int(row["event"]), int(row["version"]), row["op"])
                if identity != expected[1][index]:
                    raise ValueError(f"update {index}: event/version/op mismatch")
                for key in UPDATE_COLUMNS[3:]:
                    value = float(row[key])
                    if not math.isfinite(value) or value < 0:
                        raise ValueError(f"update {index}: invalid {key}")
                update_us += float(row["latency_us"])
        if query_count != config["n_queries"] or update_count != config["n_updates"]:
            errors.append("raw CSV row counts do not match trace")
        macro = recall_sum / query_count if query_count else 1.0
        pair = hits / ground_truth if ground_truth else 1.0
        for key, actual in (("macro_recall", macro), ("pair_recall", pair),
                            ("query_total_us", query_us), ("update_total_us", update_us),
                            ("total_workload_us", query_us + update_us)):
            if key in summary and not close(float(summary[key]), actual):
                errors.append(f"summary {key} inconsistent with raw rows")
        if require_gt or "gt_distance_calls" in summary:
            if summary.get("gt_distance_calls") != gt_calls:
                errors.append("summary GT distance calls inconsistent with raw rows/mode")
            if not close(float(summary.get("gt_total_us", -1)), gt_us):
                errors.append("summary GT time inconsistent with raw rows")
    except (OSError, ValueError, KeyError, TypeError, csv.Error) as error:
        errors.append(str(error))
    quality = (not errors and float(summary.get("macro_recall", 0)) >= 0.999 and
               float(summary.get("pair_recall", 0)) >= 0.999)
    return summary, errors, quality, comparison.hexdigest()


def validate_tape_binding(job):
    """Reject a changed tape or oracle stamp before using/resuming a measurement."""
    tape_hash = sha256_file(job["gt_tape"])
    if tape_hash != job["gt_tape_sha256"]:
        raise ValueError("oracle tape SHA mismatch")
    stamp = read_json(job["oracle_record"])
    if (sha256_file(job["oracle_record"]) != job["oracle_record_sha256"] or
            stamp.get("identity_sha256") != job["oracle_identity_sha256"] or
            stamp.get("gt_tape_sha256") != tape_hash or
            stamp.get("raw_sha256", {}).get(".queries.csv") != tape_hash or
            stamp.get("complete") is not True or stamp.get("returncode") != 0):
        raise ValueError("oracle tape/stamp provenance mismatch")


def execute(job, args, expected):
    prefix = Path(job["raw_prefix"])
    prefix.parent.mkdir(parents=True, exist_ok=True)
    stamp = Path(str(prefix) + ".run.json")
    oracle = job["stage"] == "oracle"
    cached = False
    returncode = None
    process_error = None
    preserve_stamp = False
    previous = None
    try:
        if job["gt_mode"] == "precomputed":
            validate_tape_binding(job)
        existing_oracle = oracle and (stamp.exists() or
                                     any(prefix.parent.glob(prefix.name + ".*")))
        if (args.resume or existing_oracle) and stamp.is_file():
            previous = read_json(stamp)
            cached = (previous.get("identity_sha256") == job["identity_sha256"] and
                      previous.get("raw_sha256") == raw_hashes(prefix) and
                      len(previous.get("raw_sha256", {})) == len(RAW_SUFFIXES) and
                      previous.get("returncode") == 0)
            if oracle:
                preserve_stamp = True
                cached = (cached and previous.get("complete") is True and
                          all(previous.get(key) == value for key, value in job.items()) and
                          previous.get("gt_tape") == str(prefix) + ".queries.csv" and
                          previous.get("gt_tape_sha256") ==
                          previous.get("raw_sha256", {}).get(".queries.csv"))
                if not cached:
                    raise ValueError("oracle cache identity/raw/tape/stamp mismatch; refusing fallback")
            if cached:
                returncode = 0
        elif existing_oracle:
            preserve_stamp = True
            raise ValueError("oracle cache missing run stamp; refusing fallback")
    except (OSError, ValueError, TypeError) as error:
        if oracle:
            preserve_stamp = stamp.exists()
        process_error = str(error)
    if not cached and not process_error:
        try:
            with Path(str(prefix) + ".stdout.log").open("w") as stdout, \
                    Path(str(prefix) + ".stderr.log").open("w") as stderr:
                process = subprocess.run(job["command"], stdout=stdout, stderr=stderr,
                                         env=dict(os.environ, **SINGLE_THREAD_ENV),
                                         timeout=args.timeout or None, check=False)
                returncode = process.returncode
        except (OSError, subprocess.TimeoutExpired) as error:
            process_error = str(error)
    summary, errors, ann_quality, fingerprint = validate_raw(
        prefix, job["config"], job["arm"], expected, args.k,
        gt_mode=job["gt_mode"], require_gt=oracle or job["gt_mode"] == "precomputed")
    for key in ("ef", "scan_threshold"):
        if summary.get(key) != job["runner_settings"][key]:
            errors.append(f"summary {key} differs from requested settings")
    if job["gt_mode"] == "precomputed":
        try:
            validate_tape_binding(job)
        except (OSError, ValueError, TypeError) as error:
            errors.append(str(error))
        if fingerprint != job["oracle_current_state_gt_sha256"]:
            errors.append("benchmark current-state GT/scope differs from oracle")
    if oracle and cached and fingerprint != previous.get("current_state_gt_sha256"):
        errors.append("oracle current-state GT fingerprint differs from stamp")
    if oracle and cached:
        if (previous.get("summary") != summary or
                previous.get("gt_total_us") != summary.get("gt_total_us") or
                previous.get("gt_distance_calls") != summary.get("gt_distance_calls")):
            errors.append("oracle summary/time/calls differ from stamp")
    if process_error:
        errors.append(process_error)
    if returncode != 0:
        errors.append(f"runner exit status {returncode}")
    quality = not errors and (oracle or ann_quality)
    hashes = raw_hashes(prefix)
    record = dict(job, summary=summary, quality_pass=quality,
                  validation_errors=errors, returncode=returncode, resumed=cached,
                  current_state_gt_sha256=fingerprint,
                  status="failed" if errors else ("ok" if quality else "quality_failed"),
                  raw_sha256=hashes)
    if oracle:
        record.update(complete=not errors, ann_quality_pass=ann_quality,
                      gt_tape=str(prefix) + ".queries.csv",
                      gt_tape_sha256=hashes.get(".queries.csv"),
                      gt_total_us=summary.get("gt_total_us"),
                      gt_distance_calls=summary.get("gt_distance_calls"))
    if not preserve_stamp:
        write_json(stamp, record)
    return record


def run_jobs(jobs, args, layouts):
    records = []
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = {executor.submit(execute, job, args, layouts[job["config_id"]]): job
                   for job in jobs}
        for future in as_completed(futures):
            job = futures[future]
            try:
                result = future.result()
            except Exception as error:
                result = dict(job, summary={}, quality_pass=False, complete=False,
                              validation_errors=[f"driver exception: {error}"],
                              returncode=None, resumed=False, status="failed",
                              current_state_gt_sha256="", raw_sha256={})
            records.append(result)
            print(f"{result['config_id']} stage={result['stage']} "
                  f"repeat={result['repeat']} arm={result['arm']} {result['status']}", flush=True)
    return sorted(records, key=lambda run: (run["config_id"], run["repeat"], run["arm"]))


def compare_arms(runs):
    groups = {}
    for run in runs:
        groups.setdefault((run["config_id"], run["repeat"]), []).append(run)
    for group in groups.values():
        fingerprints = {run["current_state_gt_sha256"] for run in group
                        if not run["validation_errors"]}
        if len(fingerprints) > 1:
            for run in group:
                run["validation_errors"].append("cross-arm current-state GT/scope/version mismatch")
                run["quality_pass"] = False
                run["status"] = "failed"


def aggregate_runs(runs, repeats):
    groups = {}
    for run in runs:
        groups.setdefault((run["config_id"], run["arm"]), []).append(run)
    result = []
    for (config_id, arm), group in sorted(groups.items()):
        # Failed recall measurements are included, never silently selected out.
        # Incomplete executions produce null aggregate metrics, not fabricated
        # partial means that could look like a successful performance result.
        complete = len(group) == repeats and all(not run["validation_errors"] for run in group)
        keys = set.intersection(*(set(run["summary"]) for run in group)) if group else set()
        means, stdev = {}, {}
        for key in sorted(keys):
            values = [run["summary"][key] for run in group]
            if all(isinstance(value, (int, float)) and not isinstance(value, bool) and
                   math.isfinite(value) for value in values):
                means[key] = statistics.mean(values) if complete else None
                stdev[key] = statistics.stdev(values) if complete and len(values) > 1 else None
        result.append({"config_id": config_id, "arm": arm, "config": group[0]["config"],
                       "repeats": len(group), "complete": complete,
                       "quality_pass": complete and all(run["quality_pass"] for run in group),
                       "summary": means, "summary_stdev": stdev,
                       "failed_runs": sum(bool(run["validation_errors"]) for run in group),
                       "quality_failed_runs": sum(not run["quality_pass"] for run in group)})
    return result


def save_results(output, document):
    output.mkdir(parents=True, exist_ok=True)
    write_json(output / "results.json", document)
    flattened = []
    for run in document["runs"]:
        row = {"config_id": run["config_id"], "repeat": run["repeat"], "arm": run["arm"],
               "status": run["status"], "quality_pass": run["quality_pass"],
               "raw_prefix": run["raw_prefix"], "trace_sha256": run["trace_sha256"],
               "binary_sha256": run["binary_sha256"], "config_sha256": run["config_sha256"],
               "validation_errors": "; ".join(run["validation_errors"])}
        for key in ("gt_mode", "gt_tape", "gt_tape_sha256", "oracle_record",
                    "oracle_record_sha256", "oracle_identity_sha256", "oracle_gt_total_us",
                    "oracle_gt_distance_calls"):
            if key in run:
                row[key] = run[key]
        row.update({key: value for key, value in run["config"].items()
                    if isinstance(value, (str, int, float, bool)) or value is None})
        row.update(run["summary"])
        flattened.append(row)
    columns = sorted(set().union(*(row.keys() for row in flattened))) if flattened else []
    with (output / "results.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=columns)
        writer.writeheader()
        writer.writerows(flattened)

def command_records(execution, oracles, jobs, planned):
    def describe(job):
        return {**{key: job.get(key) for key in
                   ("config_id", "arm", "repeat", "stage", "gt_mode", "trace_sha256",
                    "identity_sha256", "gt_tape", "gt_tape_sha256", "oracle_record",
                    "oracle_identity_sha256")},
                "argv": job["command"], "shell": shlex.join(job["command"]),
                "provenance_status": "planned" if planned else "verified"}
    return {"execution": execution, "stage_order": ["oracle", "benchmark"] if oracles else ["benchmark"],
            "oracle_commands": [describe(job) for job in oracles],
            "commands": [describe(job) for job in jobs]}



def parser():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--manifest", type=Path, required=True)
    ap.add_argument("--binary", type=Path, default=ROOT / "codes/directory-aware-vector-attr-search/build/dynamic_triad")
    ap.add_argument("--gt-mode", choices=("inline", "precomputed"), default="inline",
                    help="inline exact GT (legacy default), or two-stage hash-bound C++ GT tapes")
    ap.add_argument("--select", action="append", default=[], help="config ID glob or sweep name")
    ap.add_argument("--limit", type=int)
    ap.add_argument("--arms", nargs="+", choices=("A", "B", "C"), default=["A", "B", "C"])
    ap.add_argument("--repeats", type=int, default=3)
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--ef", type=int, default=256)
    ap.add_argument("--k", type=int, default=10)
    ap.add_argument("--scan-threshold", type=int,
                    help="explicit override; default frozen policy: dim128=1024, dim1024=512")
    ap.add_argument("--compact-threshold", type=float, default=0.2)
    ap.add_argument("--timeout", type=float, default=0, help="per-process seconds; 0 disables")
    ap.add_argument("--tracked-root", type=Path, help="exact small config/commands/results directory")
    ap.add_argument("--work-root", type=Path, help="exact raw-output root (must be ignored by git)")
    ap.add_argument("--dry-run", action="store_true", help="print commands, perform no execution or writes")
    ap.add_argument("--resume", action="store_true", help="reuse only hash-identical inputs/executable/raw files")
    return ap


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv and argv[0] == "generate":
        from generate_dynamic_triad import main as generate
        return generate(argv[1:])
    args = parser().parse_args(argv)
    if args.jobs < 1 or args.repeats < 1 or args.ef < 1 or args.k < 1:
        raise ValueError("jobs/repeats/ef/k must be positive")
    if args.scan_threshold is not None and args.scan_threshold < 0:
        raise ValueError("scan threshold must be nonnegative")
    if not 0 < args.compact_threshold <= 1 or args.timeout < 0:
        raise ValueError("compact threshold must be (0,1]; timeout nonnegative")
    manifest_path = args.manifest.resolve()
    manifest = read_json(manifest_path)
    if manifest.get("format_version") != FORMAT_VERSION or not manifest.get("generated"):
        raise ValueError("need a generated compatible manifest, not a planning-only dry run")
    configs = select_configs(manifest["configs"], args.select, args.limit)
    args.arms = list(dict.fromkeys(args.arms))
    binary = args.binary.resolve()
    binary_hash = sha256_file(binary) if binary.is_file() else None
    if not args.dry_run and (binary_hash is None or not os.access(binary, os.X_OK)):
        raise ValueError(f"executable not available: {binary}")
    settings = {"gt_mode": args.gt_mode, "ef": args.ef, "k": args.k,
                "scan_threshold": args.scan_threshold,
                "scan_threshold_by_dim": {"128": 1024, "1024": 512},
                "compact_threshold": args.compact_threshold,
                "single_thread_environment": SINGLE_THREAD_ENV,
                "visibility": "single-thread serialized synchronous commits, zero staleness"}
    settings_hash = json_hash(settings)[:12]
    output = (args.tracked_root or Path(manifest["output_dir"]) /
              f"runs-{manifest_path.stem}-{settings_hash}").resolve()
    work = (args.work_root or Path(manifest["work_dir"]) / "runs" /
            manifest_path.stem / settings_hash).resolve()
    jobs = []
    oracles = []
    oracle_keys = set()
    layouts = {}
    for config in configs:
        threshold = args.scan_threshold
        if threshold is None:
            threshold = settings["scan_threshold_by_dim"].get(str(config["dim"]))
            if threshold is None:
                raise ValueError(f"no frozen threshold for dim={config['dim']}; specify --scan-threshold")
        effective_settings = dict(settings, scan_threshold=threshold)
        if not args.dry_run:
            if sha256_file(config["trace"]) != config["trace_sha256"]:
                raise ValueError(f"trace changed since generation: {config['id']}")
            hashes = {name: sha256_file(Path(config["data_dir"]) / name)
                      for name in config["data_file_sha256"]}
            if hashes != config["data_file_sha256"] or json_hash(hashes) != config["data_sha256"]:
                raise ValueError(f"input data changed since generation: {config['id']}")
            layouts[config["id"]] = trace_layout(config["trace"], config["n_live"], config["n_vectors"])
            if (len(layouts[config["id"]][0]) != config["n_queries"] or
                    len(layouts[config["id"]][1]) != config["n_updates"]):
                raise ValueError(f"trace counts do not match manifest: {config['id']}")
        config_hash = json_hash({"config": config, "runner_settings": effective_settings})
        identity = {"config_sha256": config_hash, "trace_sha256": config["trace_sha256"],
                    "data_sha256": config["data_sha256"], "binary_sha256": binary_hash,
                    "gt_mode": args.gt_mode}
        base_command = [str(binary), "--data", config["data_dir"], "--trace", config["trace"],
                        "--ef", str(args.ef), "--k", str(args.k),
                        "--scan-threshold", str(threshold),
                        "--compact-threshold", str(args.compact_threshold)]
        oracle = None
        if args.gt_mode == "precomputed":
            oracle_settings = dict(effective_settings, gt_mode="inline")
            oracle_identity = dict(
                identity, gt_mode="inline", stage="oracle", arm="A",
                config_sha256=json_hash({"config": config, "runner_settings": oracle_settings}))
            oracle_key = json_hash(oracle_identity)
            oracle_prefix = work / "oracles" / oracle_key / "oracle"
            oracle = dict(oracle_identity, config_id=config["id"], config=config, repeat=0,
                          runner_settings=oracle_settings, identity_sha256=oracle_key,
                          raw_prefix=str(oracle_prefix),
                          command=[*base_command, "--arm", "A", "--out", str(oracle_prefix)])
            if oracle_key not in oracle_keys:
                oracles.append(oracle)
                oracle_keys.add(oracle_key)
        # Stage-two identities are completed with the actual tape hash only
        # after every oracle has finished; dry runs never invent that hash.
        identity_short = json_hash(identity)[:16]
        for repeat in range(1, args.repeats + 1):
            for arm in args.arms:
                prefix = work / config["id"] / identity_short / f"repeat-{repeat:02}" / arm
                command = [*base_command, "--arm", arm, "--out", str(prefix)]
                job = dict(identity, config_id=config["id"], config=config, arm=arm,
                           repeat=repeat, stage="benchmark", command=command, raw_prefix=str(prefix),
                           runner_settings=effective_settings,
                           identity_sha256=json_hash(dict(identity, arm=arm, repeat=repeat)))
                if oracle is not None:
                    tape = oracle["raw_prefix"] + ".queries.csv"
                    command.extend(["--gt-in", tape])
                    job.update(gt_tape=tape, gt_tape_sha256=None,
                               oracle_record=oracle["raw_prefix"] + ".run.json",
                               oracle_identity_sha256=oracle["identity_sha256"],
                               identity_sha256=None)
                jobs.append(job)
    execution = {"manifest": str(manifest_path), "manifest_sha256": sha256_file(manifest_path),
                 "settings": settings, "settings_sha256": json_hash(settings),
                 "binary": str(binary), "binary_sha256": binary_hash,
                 "jobs": args.jobs, "repeats": args.repeats, "arms": args.arms,
                 "mode": "multi-process replay; NOT concurrent updates/queries within a process",
                 "selection": {"patterns": args.select, "limit": args.limit, "count": len(configs)},
                 "tracked_root": str(output), "work_root": str(work),
                 "quality_gate": {"macro_recall": 0.999, "pair_recall": 0.999},
                 "driver_command": [sys.executable, str(Path(__file__).resolve()), *argv]}
    commands = command_records(execution, oracles, jobs, planned=True)
    if args.dry_run:
        print(json.dumps(commands, indent=2, sort_keys=True))
        return 0
    if oracles:
        ignored = subprocess.run(
            ["git", "check-ignore", "--quiet", "--no-index",
             str(work / "oracles" / "probe" / "oracle.queries.csv")],
            cwd=ROOT, check=False)
        if ignored.returncode != 0:
            raise ValueError(f"oracle raw work root must be git-ignored: {work}")
    write_json(output / "config.json", {"execution": execution, "configs": configs})
    write_json(output / "commands.json", commands)
    oracle_runs = run_jobs(oracles, args, layouts) if oracles else []
    if oracles:
        write_json(output / "oracles.json", {
            "format_version": FORMAT_VERSION, "execution": execution, "oracles": oracle_runs})
        if any(not record.get("complete") for record in oracle_runs):
            save_results(output, {"format_version": FORMAT_VERSION, "execution": execution,
                                 "oracles": oracle_runs, "oracle_stage_complete": False,
                                 "runs": [], "aggregates": []})
            print(json.dumps({"results": str(output / "results.json"),
                              "oracle_failures": sum(not record.get("complete") for record in oracle_runs),
                              "runs": 0, "all_pass": False}, sort_keys=True))
            return 1
        by_identity = {record["identity_sha256"]: record for record in oracle_runs}
        for job in jobs:
            oracle = by_identity[job["oracle_identity_sha256"]]
            job.update(gt_tape_sha256=oracle["gt_tape_sha256"],
                       oracle_record_sha256=sha256_file(job["oracle_record"]),
                       oracle_current_state_gt_sha256=oracle["current_state_gt_sha256"],
                       oracle_gt_total_us=oracle["gt_total_us"],
                       oracle_gt_distance_calls=oracle["gt_distance_calls"])
            job["identity_sha256"] = json_hash({
                key: job[key] for key in
                ("config_sha256", "trace_sha256", "data_sha256", "binary_sha256",
                 "gt_mode", "gt_tape_sha256", "oracle_identity_sha256", "arm", "repeat")})
    commands = command_records(execution, oracles, jobs, planned=False)
    write_json(output / "commands.json", commands)
    runs = run_jobs(jobs, args, layouts)
    compare_arms(runs)
    document = {"format_version": FORMAT_VERSION, "execution": execution, "runs": runs,
                "oracles": oracle_runs, "oracle_stage_complete": True,
                "aggregates": aggregate_runs(runs, args.repeats)}
    save_results(output, document)
    failed = sum(not run["quality_pass"] for run in runs)
    print(json.dumps({"results": str(output / "results.json"), "runs": len(runs),
                      "quality_failed_runs": failed, "all_pass": failed == 0}, sort_keys=True))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
