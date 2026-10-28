#!/usr/bin/env python3
"""Multi-process experiment driver for the 2026-10-29 NodeANN-first hnswlib matrix.

Orchestrates, per dataset (WIKI + six SIFT1M hierarchy variants):
  1. mode=exact run  -> doubles as unified-RawL2Space ground truth (ids CSV) and
                        the B3 diagnostic oracle (timed, excluded from frontier).
  2. mode=nodeann --scan-threshold 0 (B0) and --scan-threshold T (B1) over an
     ef grid, `repeats` independent processes each.

Runner CLI contract (fixed by RunnerInfra):
  nodeann_research --data DIR --dim D --mode nodeann|exact \
      --scan-threshold T --ef N --threads P --csv PREFIX
  -> writes PREFIX.perquery.csv and PREFIX.summary.json next to PREFIX.

Parallelism: one multiprocessing worker per (dataset, method, ef, repeat) task;
every child pins OMP threads to 1 and passes --threads P (build-only threads).
No two tasks ever share an output prefix.

Writes ONLY under <exps-root>/work/nodeann_matrix/ plus <exps-root>/figures/
(figure step lives in scripts/plot_nodeann_matrix.py). Produces:
  work/nodeann_matrix/summary.csv        one row per executed run
  work/nodeann_matrix/summary_agg.csv    median over repeats per (dataset,method,ef)
  work/nodeann_matrix/perquery.csv       all per-query rows, annotated
  work/nodeann_matrix/manifest.json      command, hashes, revisions, CPU/threads
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import multiprocessing
import os
import platform
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

# --------------------------------------------------------------------------
# Fixed constants from the 2026-10-29 plan
# --------------------------------------------------------------------------
HNSWLIB_PINNED_REV = "ca426729609b3221563047ceb269cc1213e0d376"
DEFAULT_EFS = [16, 32, 64, 128, 256, 512]
DEFAULT_REPEATS = 3
K = 10  # top-k for GT / result lists

EXPS_ROOT = Path(__file__).resolve().parents[1]           # .../2026-10-29-exps
REPO_ROOT = Path(__file__).resolve().parents[5]           # repository root
CODES_DIR = REPO_ROOT / "codes" / "directory-aware-vector-attr-search"
DEFAULT_RUNNER = CODES_DIR / "build" / "nodeann_research"
WIKI_DATA = CODES_DIR / "data" / "wiki_full"
WORK_ROOT = EXPS_ROOT / "work"
DEFAULT_WORK_DIR = WORK_ROOT / "nodeann_matrix"

# Unified filenames the runner expects inside --data DIR.
STAGE_NAMES = ["base.f32", "test.f32", "tree.txt", "bindings.txt", "workload.txt"]

SIFT_VARIANTS = ["d4_balanced", "d4_skewed", "d6_balanced",
                 "d6_skewed", "d12_balanced", "d12_skewed"]
SIFT_DEPTH = {"d4": 4, "d6": 6, "d12": 12}
SIFT_LOAD = {"balanced": "balanced", "skewed": "skewed"}


@dataclass(frozen=True)
class Dataset:
    ds_id: str
    dim: int
    depth: str = ""      # SIFT hierarchy depth label ("d4"), "" for WIKI
    load: str = ""       # "balanced" | "skewed" | ""
    # logical name -> source path (pre-staging)
    sources: dict = field(default_factory=dict)

    @property
    def is_sift(self) -> bool:
        return bool(self.depth)


def dataset_registry() -> dict[str, Dataset]:
    reg = {
        "wiki": Dataset(
            ds_id="wiki", dim=1024,
            sources={
                "base.f32": WIKI_DATA / "base.f32",
                "test.f32": WIKI_DATA / "test.f32",
                "tree.txt": WIKI_DATA / "wiki_tree.txt",
                "bindings.txt": WIKI_DATA / "wiki_bindings.txt",
                "workload.txt": WIKI_DATA / "workload_wiki.txt",
            },
        ),
    }
    for var in SIFT_VARIANTS:
        vdir = WORK_ROOT / "sift_hierarchies" / var
        depth, load = var.split("_", 1)
        reg[var] = Dataset(
            ds_id=var, dim=128, depth=depth, load=SIFT_LOAD[load],
            sources={
                "base.f32": WORK_ROOT / "sift1m" / "base.f32",
                "test.f32": WORK_ROOT / "sift1m" / "test.f32",
                "tree.txt": vdir / "tree.txt",
                "bindings.txt": vdir / "bindings.txt",
                "workload.txt": vdir / "workload.txt",
            },
        )
    return reg


# --------------------------------------------------------------------------
# Helpers
# --------------------------------------------------------------------------
def sha256_file(path: Path, chunk: int = 1 << 20) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(chunk)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def git_rev(path: Path) -> str:
    try:
        out = subprocess.run(
            ["git", "-C", str(path), "rev-parse", "HEAD"],
            capture_output=True, text=True, timeout=30)
        return out.stdout.strip() if out.returncode == 0 else "unknown"
    except Exception:
        return "unknown"


def cpu_model() -> str:
    try:
        with open("/proc/cpuinfo") as f:
            for line in f:
                if line.startswith("model name"):
                    return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or "unknown"


def _child_env_init() -> None:
    """Worker initializer: forbid nested threading in every child process."""
    for var in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS",
                "NUMEXPR_NUM_THREADS", "VECLIB_MAXIMUM_THREADS"):
        os.environ[var] = "1"


def parse_ids(raw: str) -> list[int]:
    if not raw:
        return []
    out = []
    for tok in raw.split(";"):
        tok = tok.strip()
        if tok:
            out.append(int(tok))
    return out


def compute_recalls(run_rows: list[dict], gt_by_qidx: dict[int, list[int]],
                    gt_count_by_qidx: dict[int, int]) -> dict:
    """Recall of a nodeann/exact perquery.csv against exact-mode GT ids.

    Per-query recall = |returned ∩ gt| / gt_count (set-based; ties already
    resolved identically by the shared distance). Empty scope (gt_count == 0)
    counts as hit only when nothing valid was returned.
    """
    hits_total = gt_total = 0
    rec_vals, nonrec_vals = [], []
    for row in run_rows:
        qidx = int(row["qidx"])
        ret = [i for i in parse_ids(row["result_ids"]) if i >= 0]
        gt = gt_by_qidx.get(qidx, [])
        gtc = gt_count_by_qidx.get(qidx, 0)
        if gtc == 0:
            hits, recall = (1, 1.0) if not ret else (0, 0.0)
        else:
            hits = len(set(ret) & set(gt))
            recall = hits / gtc
            hits_total += hits
            gt_total += gtc
        row["hits"] = hits
        row["recall"] = f"{recall:.6f}"
        (rec_vals if row["recursive"] == "1" else nonrec_vals).append(recall)
    all_vals = rec_vals + nonrec_vals
    return {
        "macro_recall": statistics.mean(all_vals) if all_vals else -1.0,
        "global_recall": (hits_total / gt_total) if gt_total else 1.0,
        "macro_recall_recursive": statistics.mean(rec_vals) if rec_vals else -1.0,
        "macro_recall_nonrec": statistics.mean(nonrec_vals) if nonrec_vals else -1.0,
    }


# --------------------------------------------------------------------------
# Task execution
# --------------------------------------------------------------------------
@dataclass
class Task:
    dataset: str
    dim: int
    method: str          # "b3" (exact), "b0", "b1"
    ef: int
    scan_threshold: int
    build_threads: int
    repeat: int          # 0 for single exact run
    staging_dir: str
    prefix: str          # output path prefix (unique per task)
    runner: str
    timeout_s: int


def run_task(task: Task) -> dict:
    """Executed in a worker process. Returns a result dict (must be picklable)."""
    res = {
        "dataset": task.dataset, "method": task.method, "ef": task.ef,
        "scan_threshold": task.scan_threshold, "build_threads": task.build_threads,
        "repeat": task.repeat, "run_dir": str(Path(task.prefix).parent),
        "status": "failed", "error": "", "started_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
    }
    mode = "exact" if task.method == "b3" else "nodeann"
    Path(task.prefix).parent.mkdir(parents=True, exist_ok=True)
    cmd = [task.runner, "--data", task.staging_dir, "--dim", str(task.dim),
           "--mode", mode, "--scan-threshold", str(task.scan_threshold),
           "--ef", str(task.ef), "--threads", str(task.build_threads),
           "--csv", task.prefix]
    res["command"] = " ".join(cmd)
    t0 = time.monotonic()
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True,
                              timeout=task.timeout_s)
    except subprocess.TimeoutExpired:
        res["error"] = f"timeout after {task.timeout_s}s"
        res["elapsed_s"] = f"{time.monotonic() - t0:.1f}"
        return res
    res["elapsed_s"] = f"{time.monotonic() - t0:.1f}"
    res["returncode"] = proc.returncode
    if proc.returncode != 0:
        tail = (proc.stderr or proc.stdout or "").strip().splitlines()[-15:]
        res["error"] = f"exit {proc.returncode}: " + " | ".join(tail)
        return res

    summary_path = Path(task.prefix + ".summary.json")
    perquery_path = Path(task.prefix + ".perquery.csv")
    if not summary_path.exists() or not perquery_path.exists():
        res["error"] = "runner exited 0 but summary.json/perquery.csv missing"
        return res
    try:
        with open(summary_path) as f:
            s = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        res["error"] = f"cannot read summary.json: {e}"
        return res
    res["status"] = "ok"
    res["summary"] = s
    res["perquery_path"] = str(perquery_path)
    return res


def stage_dataset(ds: Dataset, staging_dir: Path) -> dict:
    """Symlink unified filenames -> real inputs; return sha256 of real files."""
    staging_dir.mkdir(parents=True, exist_ok=True)
    hashes = {}
    for name in STAGE_NAMES:
        src = ds.sources[name]
        dst = staging_dir / name
        if dst.is_symlink() or dst.exists():
            dst.unlink()
        dst.symlink_to(src.resolve())
        hashes[name] = sha256_file(src)
    return hashes


# --------------------------------------------------------------------------
# Preflight
# --------------------------------------------------------------------------
def preflight(args, reg: dict[str, Dataset]) -> list[str]:
    problems = []
    runner = Path(args.runner)
    if not runner.is_file():
        problems.append(f"runner binary missing: {runner} "
                        f"(build it first, e.g. via codes build_nodeann.sh)")
    elif not os.access(runner, os.X_OK):
        problems.append(f"runner not executable: {runner}")
    for ds_id in args.datasets:
        ds = reg[ds_id]
        for name in STAGE_NAMES:
            p = ds.sources[name]
            if not p.is_file():
                problems.append(f"dataset '{ds_id}' input missing: {name} -> {p}")
    if problems:
        print("PREFLIGHT FAILED — fix the following before running:", file=sys.stderr)
        for p in problems:
            print(f"  - {p}", file=sys.stderr)
    return problems


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------
def build_task_list(args, reg) -> tuple[list[Task], list[Task]]:
    """Returns (exact_tasks, nodeann_tasks). Exact tasks gate nodeann ones."""
    work_dir = Path(args.work_dir)
    exact_tasks, nodeann_tasks = [], []
    for ds_id in args.datasets:
        ds = reg[ds_id]
        staging = work_dir / "staging" / ds_id
        prefix = work_dir / "runs" / ds_id / "b3_exact" / "run"
        exact_tasks.append(Task(ds_id, ds.dim, "b3", 0, 0, args.build_threads,
                                0, str(staging), str(prefix), args.runner,
                                args.task_timeout))
        tscan = args.tscan_128 if ds.dim == 128 else args.tscan_1024
        for method in args.methods:
            for ef in args.efs:
                for rep in range(1, args.repeats + 1):
                    st = 0 if method == "b0" else tscan
                    label = f"{method}_ef{ef}_rep{rep}"
                    prefix = work_dir / "runs" / ds_id / label / "run"
                    nodeann_tasks.append(Task(ds_id, ds.dim, method, ef, st,
                                              args.build_threads, rep,
                                              str(staging), str(prefix),
                                              args.runner, args.task_timeout))
    return exact_tasks, nodeann_tasks


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--runner", default=str(DEFAULT_RUNNER),
                    help="nodeann_research binary path")
    ap.add_argument("--work-dir", default=str(DEFAULT_WORK_DIR),
                    help="output root (default: <exps-root>/work/nodeann_matrix)")
    ap.add_argument("--datasets", nargs="+",
                    default=["wiki"] + SIFT_VARIANTS,
                    help="subset of datasets to run")
    ap.add_argument("--methods", nargs="+", default=["b0", "b1"],
                    choices=["b0", "b1"], help="ANN methods (B3/exact always runs)")
    ap.add_argument("--efs", nargs="+", type=int, default=DEFAULT_EFS,
                    help="efSearch grid for B0/B1")
    ap.add_argument("--repeats", type=int, default=DEFAULT_REPEATS,
                    help="independent process repeats per (dataset,method,ef)")
    ap.add_argument("--build-threads", type=int, default=1,
                    help="runner --threads (build-only parallelism per process)")
    ap.add_argument("--jobs", type=int, default=min(16, os.cpu_count() or 1),
                    help="max concurrent worker processes")
    ap.add_argument("--tscan-128", type=int, default=64,
                    help="B1 small-node exact-scan threshold for d=128 "
                         "(placeholder until the microbenchmark freezes it)")
    ap.add_argument("--tscan-1024", type=int, default=64,
                    help="B1 threshold for d=1024 (placeholder, same caveat)")
    ap.add_argument("--tscan-frozen", action="store_true",
                    help="assert that the T_scan microbenchmark has frozen the "
                         "thresholds above; without it a warning is emitted")
    ap.add_argument("--reuse-exact", action="store_true",
                    help="reuse existing exact-run outputs as GT/B3; FAIL if "
                         "they are missing (idempotent reuse is the default "
                         "when outputs exist)")
    ap.add_argument("--force-exact", action="store_true",
                    help="always re-run mode=exact, even if outputs exist")
    ap.add_argument("--task-timeout", type=int, default=7200,
                    help="per-task wall-clock timeout in seconds")
    ap.add_argument("--dry-run", action="store_true",
                    help="print the planned task list and exit")
    args = ap.parse_args()

    if args.reuse_exact and args.force_exact:
        ap.error("--reuse-exact and --force-exact are mutually exclusive")

    if not args.tscan_frozen:
        print("WARNING: T_scan thresholds are placeholders "
              f"(d=128: {args.tscan_128}, d=1024: {args.tscan_1024}); "
              "the plan requires freezing them via the d-specific "
              "microbenchmark before B1 results count. "
              "Pass --tscan-frozen to silence.", file=sys.stderr)

    reg = dataset_registry()
    unknown = [d for d in args.datasets if d not in reg]
    if unknown:
        ap.error(f"unknown datasets: {unknown}; choices: {sorted(reg)}")

    problems = preflight(args, reg)
    if problems:
        return 2

    work_dir = Path(args.work_dir)
    runs_dir = work_dir / "runs"
    runs_dir.mkdir(parents=True, exist_ok=True)

    exact_tasks, nodeann_tasks = build_task_list(args, reg)
    all_tasks = exact_tasks + nodeann_tasks
    if args.dry_run:
        print(f"planned {len(all_tasks)} tasks "
              f"({len(exact_tasks)} exact/GT-B3, {len(nodeann_tasks)} nodeann):")
        for t in all_tasks:
            print(f"  {t.dataset:14s} {t.method:4s} ef={t.ef:<4d} "
                  f"tscan={t.scan_threshold:<4d} rep={t.repeat} -> {t.prefix}")
        return 0

    t_start = time.time()
    print(f"[{time.strftime('%H:%M:%S')}] staging datasets ...", flush=True)
    input_hashes = {}
    for ds_id in args.datasets:
        input_hashes[ds_id] = stage_dataset(
            reg[ds_id], work_dir / "staging" / ds_id)

    # ---- Phase 1: exact (GT + B3 diagnostic), parallel over datasets ----
    results: list[dict] = []
    gt_ok: set[str] = set()
    exact_to_run: list[Task] = []
    for t in exact_tasks:
        summary_p = Path(t.prefix + ".summary.json")
        perquery_p = Path(t.prefix + ".perquery.csv")
        have = summary_p.is_file() and perquery_p.is_file()
        if have and not args.force_exact:
            if args.reuse_exact:
                reason = "reuse-exact"
            else:
                reason = ("existing outputs reused (idempotent default; "
                          "pass --force-exact to regenerate GT/B3)")
            try:
                with open(summary_p) as f:
                    s = json.load(f)
                results.append({"dataset": t.dataset, "method": "b3", "ef": 0,
                                "scan_threshold": 0,
                                "build_threads": t.build_threads, "repeat": 0,
                                "run_dir": str(Path(t.prefix).parent),
                                "status": "ok", "summary": s,
                                "perquery_path": str(perquery_p),
                                "started_at": "", "elapsed_s": "",
                                "command": f"<{reason}>"})
                gt_ok.add(t.dataset)
                print(f"  REUSE {t.dataset:13s} b3_exact ({reason})", flush=True)
                continue
            except (OSError, json.JSONDecodeError) as e:
                print(f"  WARN  {t.dataset:13s} existing exact outputs unreadable "
                      f"({e}); re-running", flush=True)
        elif not have and args.reuse_exact:
            print(f"  FAIL {t.dataset:14s} b3_exact: --reuse-exact given but "
                  f"{summary_p} / {perquery_p} missing", flush=True)
            results.append({"dataset": t.dataset, "method": "b3", "ef": 0,
                            "scan_threshold": 0, "build_threads": t.build_threads,
                            "repeat": 0, "run_dir": str(Path(t.prefix).parent),
                            "status": "failed",
                            "error": f"--reuse-exact but outputs missing: "
                                     f"{summary_p}", "started_at": "",
                            "elapsed_s": ""})
            continue
        exact_to_run.append(t)

    if exact_to_run:
        print(f"[{time.strftime('%H:%M:%S')}] phase 1: exact GT/B3 "
              f"({len(exact_to_run)} tasks, jobs={args.jobs})", flush=True)
        with multiprocessing.Pool(processes=min(args.jobs, len(exact_to_run)),
                                  initializer=_child_env_init,
                                  maxtasksperchild=1) as pool:
            for res in pool.imap_unordered(run_task, exact_to_run):
                results.append(res)
                if res["status"] == "ok":
                    gt_ok.add(res["dataset"])
                    print(f"  OK   {res['dataset']:14s} b3_exact "
                          f"({res.get('elapsed_s','?')}s)", flush=True)
                else:
                    print(f"  FAIL {res['dataset']:14s} b3_exact: {res['error']}",
                          flush=True)

    # GT tables per dataset (from exact perquery.csv)
    gt_tables: dict[str, tuple[dict, dict]] = {}
    for ds_id in gt_ok:
        prefix = work_dir / "runs" / ds_id / "b3_exact" / "run"
        with open(str(prefix) + ".perquery.csv") as f:
            rows = list(csv.DictReader(f))
        gt_by_qidx = {int(r["qidx"]): [i for i in parse_ids(r["result_ids"]) if i >= 0]
                      for r in rows}
        gt_count_by_qidx = {int(r["qidx"]): int(r["gt_count"]) for r in rows}
        gt_tables[ds_id] = (gt_by_qidx, gt_count_by_qidx)

    # ---- Phase 2: nodeann matrix ----
    all_runnable = [t for t in nodeann_tasks if t.dataset in gt_ok]
    blocked = [t for t in nodeann_tasks if t.dataset not in gt_ok]
    runnable = []
    if not args.force_exact:
        for t in all_runnable:
            summary_p = Path(t.prefix + ".summary.json")
            perquery_p = Path(t.prefix + ".perquery.csv")
            if summary_p.is_file() and perquery_p.is_file():
                try:
                    with open(summary_p) as f:
                        s = json.load(f)
                    results.append({"dataset": t.dataset, "method": t.method,
                                    "ef": t.ef, "scan_threshold": t.scan_threshold,
                                    "build_threads": t.build_threads,
                                    "repeat": t.repeat,
                                    "run_dir": str(Path(t.prefix).parent),
                                    "status": "ok", "summary": s,
                                    "perquery_path": str(perquery_p),
                                    "started_at": "", "elapsed_s": "",
                                    "command": "<existing outputs reused>"})
                    print(f"  REUSE {t.dataset:13s} {t.method:3s} "
                          f"ef={t.ef:<4d} rep={t.repeat}", flush=True)
                    continue
                except (OSError, json.JSONDecodeError):
                    pass
            runnable.append(t)
    else:
        runnable = list(all_runnable)
    for t in blocked:
        results.append({"dataset": t.dataset, "method": t.method, "ef": t.ef,
                        "scan_threshold": t.scan_threshold,
                        "build_threads": t.build_threads, "repeat": t.repeat,
                        "run_dir": str(Path(t.prefix).parent), "status": "blocked",
                        "error": "exact/GT run failed or missing",
                        "started_at": "", "elapsed_s": ""})
    if runnable:
        print(f"[{time.strftime('%H:%M:%S')}] phase 2: nodeann matrix "
              f"({len(runnable)} tasks, jobs={args.jobs})", flush=True)
        done = 0
        with multiprocessing.Pool(processes=min(args.jobs, len(runnable)),
                                  initializer=_child_env_init,
                                  maxtasksperchild=1) as pool:
            for res in pool.imap_unordered(run_task, runnable):
                results.append(res)
                done += 1
                tag = "OK  " if res["status"] == "ok" else "FAIL"
                print(f"  {tag} {res['dataset']:14s} {res['method']:3s} "
                      f"ef={res['ef']:<4d} rep={res['repeat']} "
                      f"({res.get('elapsed_s','?')}s) [{done}/{len(runnable)}]",
                      flush=True)
                if res["status"] != "ok":
                    print(f"       error: {res['error']}", flush=True)

    # ---- Aggregate ----
    print(f"[{time.strftime('%H:%M:%S')}] aggregating ...", flush=True)
    write_outputs(args, reg, results, gt_tables, input_hashes, t_start)
    return 0 if all(r["status"] == "ok" for r in results) else 1


# --------------------------------------------------------------------------
# Aggregation / output writers
# --------------------------------------------------------------------------
SUMMARY_FIELDS = [
    "run_id", "dataset", "dim", "depth", "load", "method", "is_frontier",
    "ef", "scan_threshold", "build_threads", "repeat", "seed",
    "status", "build_wall_s", "build_cpu_s", "peak_rss_bytes",
    "serialized_bytes", "serialized_bytes_missing",
    "mean_e2e_ms", "p50_e2e_ms", "p95_e2e_ms", "p99_e2e_ms",
    "macro_recall", "global_recall",
    "macro_recall_recursive", "macro_recall_nonrec",
    "total_distance_calls", "total_hnsw_hops",
    "n_base", "n_test", "n_nodes", "n_nonempty_nodes",
    "tscan_frozen", "runner", "run_dir", "command",
    "started_at", "elapsed_s", "error",
]

PERQUERY_EXTRA = ["dataset", "dim", "method", "ef", "scan_threshold", "repeat"]
RUNNER_PERQUERY_COLS = [
    "query_index", "qidx", "recursive", "anchor", "anchor_depth", "scope_card",
    "candidate_nodes", "scan_nodes", "hnsw_nodes", "scan_items", "hnsw_items",
    "distance_calls", "hnsw_hops", "local_results", "heap_offers", "heap_pops",
    "dir_us", "scan_us", "hnsw_us", "merge_us", "e2e_ms",
    "result_ids", "result_dists", "hit_count", "gt_count",
]


def _num(v):
    """Coerce runner summary values to float; '' when absent."""
    try:
        return float(v)
    except (TypeError, ValueError):
        return ""


def write_outputs(args, reg, results, gt_tables, input_hashes, t_start) -> None:
    work_dir = Path(args.work_dir)
    summary_rows, agg_groups, perquery_out = [], {}, []
    seed_seen = "fixed-in-runner"

    for res in sorted(results, key=lambda r: (r["dataset"], r["method"],
                                              r["ef"], r["repeat"])):
        ds = reg[res["dataset"]]
        row = {k: "" for k in SUMMARY_FIELDS}
        row.update({
            "run_id": Path(res["run_dir"]).name + f"_r{res['repeat']}",
            "dataset": res["dataset"], "dim": ds.dim,
            "depth": ds.depth, "load": ds.load,
            "method": res["method"],
            "is_frontier": "0" if res["method"] == "b3" else "1",
            "ef": res["ef"], "scan_threshold": res["scan_threshold"],
            "build_threads": res["build_threads"], "repeat": res["repeat"],
            "seed": seed_seen, "status": res["status"],
            "tscan_frozen": int(args.tscan_frozen),
            "runner": args.runner, "run_dir": res["run_dir"],
            "command": res.get("command", ""),
            "started_at": res.get("started_at", ""),
            "elapsed_s": res.get("elapsed_s", ""), "error": res.get("error", ""),
        })
        if res["status"] == "ok":
            s = res["summary"]
            if "seed" in s:
                seed_seen = str(s["seed"])
            serialized = _num(s.get("serialized_bytes"))
            row.update({
                "build_wall_s": _num(s.get("build_wall_s")),
                "build_cpu_s": _num(s.get("build_cpu_s")),
                "peak_rss_bytes": _num(s.get("peak_rss_bytes")),
                "serialized_bytes": serialized,
                "serialized_bytes_missing": int(serialized == ""),
                "n_base": s.get("n_base", ""), "n_test": s.get("n_test", ""),
                "n_nodes": s.get("n_nodes", ""),
                "n_nonempty_nodes": s.get("n_nonempty_nodes", ""),
                "mean_e2e_ms": _num(s.get("mean_e2e_ms")),
                "p50_e2e_ms": _num(s.get("p50_e2e_ms")),
                "p95_e2e_ms": _num(s.get("p95_e2e_ms")),
                "p99_e2e_ms": _num(s.get("p99_e2e_ms")),
                "total_distance_calls": _num(s.get("total_distance_calls")),
                "total_hnsw_hops": _num(s.get("total_hnsw_hops")),
            })
            if res["method"] == "b3":
                # exact run defines the GT: recall is 1 by construction
                row.update({"macro_recall": 1.0, "global_recall": 1.0,
                            "macro_recall_recursive": 1.0,
                            "macro_recall_nonrec": 1.0})
            else:
                gt_by_qidx, gt_count_by_qidx = gt_tables[res["dataset"]]
                with open(res["perquery_path"]) as f:
                    rows = list(csv.DictReader(f))
                rec = compute_recalls(rows, gt_by_qidx, gt_count_by_qidx)
                row.update({k: round(v, 6) if isinstance(v, float) else v
                            for k, v in rec.items()})
                for r in rows:
                    perquery_out.append({
                        "dataset": res["dataset"], "dim": ds.dim,
                        "method": res["method"], "ef": res["ef"],
                        "scan_threshold": res["scan_threshold"],
                        "repeat": res["repeat"], **r})
        summary_rows.append(row)
        if res["status"] == "ok":
            key = (res["dataset"], res["method"], res["ef"], res["scan_threshold"])
            agg_groups.setdefault(key, []).append(row)

    summary_path = work_dir / "summary.csv"
    with open(summary_path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=SUMMARY_FIELDS)
        w.writeheader()
        w.writerows(summary_rows)

    def med(rows, col):
        vals = [float(r[col]) for r in rows if r[col] != ""]
        return round(statistics.median(vals), 6) if vals else ""

    agg_fields = [
        "dataset", "dim", "depth", "load", "method", "ef", "scan_threshold",
        "n_reps", "is_frontier", "median_mean_e2e_ms", "median_p50_e2e_ms",
        "median_p95_e2e_ms", "median_p99_e2e_ms", "median_macro_recall",
        "median_global_recall", "median_macro_recall_recursive",
        "median_macro_recall_nonrec", "median_build_wall_s",
        "median_build_cpu_s", "median_peak_rss_bytes",
        "median_serialized_bytes", "median_total_distance_calls",
        "median_total_hnsw_hops",
    ]
    agg_rows = []
    for (ds_id, method, ef, st), rows in sorted(agg_groups.items()):
        ds = reg[ds_id]
        agg_rows.append({
            "dataset": ds_id, "dim": ds.dim, "depth": ds.depth, "load": ds.load,
            "method": method, "ef": ef, "scan_threshold": st,
            "n_reps": len(rows),
            "is_frontier": "0" if method == "b3" else "1",
            "median_mean_e2e_ms": med(rows, "mean_e2e_ms"),
            "median_p50_e2e_ms": med(rows, "p50_e2e_ms"),
            "median_p95_e2e_ms": med(rows, "p95_e2e_ms"),
            "median_p99_e2e_ms": med(rows, "p99_e2e_ms"),
            "median_macro_recall": med(rows, "macro_recall"),
            "median_global_recall": med(rows, "global_recall"),
            "median_macro_recall_recursive": med(rows, "macro_recall_recursive"),
            "median_macro_recall_nonrec": med(rows, "macro_recall_nonrec"),
            "median_build_wall_s": med(rows, "build_wall_s"),
            "median_build_cpu_s": med(rows, "build_cpu_s"),
            "median_peak_rss_bytes": med(rows, "peak_rss_bytes"),
            "median_serialized_bytes": med(rows, "serialized_bytes"),
            "median_total_distance_calls": med(rows, "total_distance_calls"),
            "median_total_hnsw_hops": med(rows, "total_hnsw_hops"),
        })
    agg_path = work_dir / "summary_agg.csv"
    with open(agg_path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=agg_fields)
        w.writeheader()
        w.writerows(agg_rows)

    perquery_path = work_dir / "perquery.csv"
    with open(perquery_path, "w", newline="") as f:
        fields = PERQUERY_EXTRA + RUNNER_PERQUERY_COLS + ["hits", "recall"]
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(perquery_out)

    # ---- manifest.json ----
    gt_hashes = {}
    for ds_id in gt_tables:
        p = work_dir / "runs" / ds_id / "b3_exact" / "run.perquery.csv"
        if p.is_file():
            gt_hashes[ds_id] = sha256_file(p)
    n_ok = sum(1 for r in results if r["status"] == "ok")
    manifest = {
        "driver": Path(__file__).name,
        "created_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "command": " ".join([sys.argv[0]] + [shlex_quote(a) for a in sys.argv[1:]]),
        "cwd": os.getcwd(),
        "python": platform.python_version(),
        "platform": platform.platform(),
        "cpu_model": cpu_model(),
        "logical_cpus": os.cpu_count(),
        "git_revision": git_rev(REPO_ROOT),
        "hnswlib_pinned_revision": HNSWLIB_PINNED_REV,
        "hnswlib_checkout_revision": git_rev(CODES_DIR / "deps" / "hnswlib"),
        "runner_path": str(Path(args.runner).resolve()),
        "runner_sha256": (sha256_file(Path(args.runner))
                          if Path(args.runner).is_file() else None),
        "threads": {"search": 1, "build_per_process": args.build_threads,
                    "max_worker_processes": args.jobs,
                    "child_env": {v: "1" for v in
                                  ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS",
                                   "MKL_NUM_THREADS", "NUMEXPR_NUM_THREADS")}},
        "seed": seed_seen,
        "k": K,
        "tscan": {"d128": args.tscan_128, "d1024": args.tscan_1024,
                  "frozen": bool(args.tscan_frozen)},
        "ef_grid": args.efs,
        "repeats": args.repeats,
        "methods": ["b3_exact"] + args.methods,
        "datasets": [
            {"id": ds_id, "dim": reg[ds_id].dim, "depth": reg[ds_id].depth,
             "load": reg[ds_id].load, "input_sha256": input_hashes[ds_id],
             "gt_perquery_sha256": gt_hashes.get(ds_id)}
            for ds_id in args.datasets],
        "outputs": {"summary": str(summary_path), "summary_agg": str(agg_path),
                    "perquery": str(perquery_path)},
        "task_timeout_s": args.task_timeout,
        "results": {"planned": len(results), "ok": n_ok,
                    "failed": sum(1 for r in results if r["status"] == "failed"),
                    "blocked": sum(1 for r in results if r["status"] == "blocked")},
        "driver_wall_s": round(time.time() - t_start, 1),
    }
    manifest_path = work_dir / "manifest.json"
    tmp = work_dir / ".manifest.json.tmp"
    with open(tmp, "w") as f:
        json.dump(manifest, f, indent=2)
    os.replace(tmp, manifest_path)
    print(f"wrote {summary_path}\nwrote {agg_path}\nwrote {perquery_path}\n"
          f"wrote {manifest_path}")
    print(f"results: {manifest['results']}")


def shlex_quote(s: str) -> str:
    import shlex
    return shlex.quote(s)


if __name__ == "__main__":
    sys.exit(main())
