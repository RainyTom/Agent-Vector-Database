"""Launch isolated SIFT1M runs through WSL and update the project experiment index."""

import argparse
import hashlib
import json
import os
import re
import shlex
import subprocess
from datetime import datetime, timedelta, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = Path("code/acorn-hnswlib")
INPUT = Path("dataset/sift1m-workload")
TRUTH = Path("experiments/EXP-SIFT-SEARCH/RUN-TRUTH-001/truth.ids.i64")
SEARCH = "EXP-SIFT-HNSWLIB-SEARCH"
DISTANCE = "EXP-SIFT-HNSWLIB-DISTANCE"
JOBS = {
    "small": (SEARCH, "SMALL", "真实 SIFT 小样本重载与精确预过滤"),
    "main": (SEARCH, "INDEX", "百万 ACORN 和 HNSW 索引构建"),
    "oracle": (DISTANCE, "ORACLE", "12 个百万库 Oracle 分区构建"),
    "figure": (SEARCH, "FIG7A", "Figure 7(a) 全量吞吐"),
    "table": (DISTANCE, "TABLE3", "Table 3 全量距离计数"),
}


def now():
    return datetime.now(timezone(timedelta(hours=8))).strftime("%Y-%m-%d %H:%M UTC+08:00")


def write(path, text):
    path.write_text(text.rstrip() + "\n", encoding="utf-8")


def stamp(text, time):
    return re.sub(r"(?m)^(最后)?更新时间（北京时间）[^\n]*",
                  lambda m: f"{m[1] or ''}更新时间（北京时间）{time}", text)


def link(path, label):
    return f"[{label}]({path.as_posix()})"


def state(message, next_step, time):
    write(Path("STATE.md"), f"""# STATE.md — 当前现场与唯一下一步

最后更新时间（北京时间）{time}

Faiss 的两个正式实验仍待人工审核，详见[实验索引](experiments.md)。

{message}

**唯一下一步**　{next_step}
""")


def update_index(exp, time):
    root = Path("experiments") / exp
    runs = sorted(root.glob("RUN-*/LOG.md"))
    statuses = [re.search(r"(?m)^status: (.+)$", p.read_text(encoding="utf-8-sig"))[1] for p in runs]
    count = {"passed": statuses.count("passed"), "running": statuses.count("running"),
             "failed": statuses.count("failed"), "interrupted": statuses.count("interrupted")}
    formal = "FIG7A" if exp == SEARCH else "TABLE3"
    complete = any(formal in p.parent.name and status == "passed" for p, status in zip(runs, statuses))
    if count["running"]:
        status = "进行中"
    elif complete:
        status = "待人工审核"
    elif statuses[-1] in {"failed", "interrupted"}:
        status = "需调查"
    else:
        status = "进行中"
    names = {"passed": "通过", "running": "运行中", "failed": "失败", "interrupted": "中断"}
    summary = "，".join(f"{value} 个{names[key]}" for key, value in count.items() if value)
    links = " · ".join(link(p, p.parent.name) for p in runs)
    path = Path("experiments.md")
    rows = path.read_text(encoding="utf-8-sig").splitlines()
    idx = next(i for i, row in enumerate(rows) if row.startswith(f"| {exp} |"))
    fields = rows[idx].split("|")
    fields[5:8] = [f" **{status}** ", f" {len(runs)} 个 RUN，{summary} ", f" {links} "]
    rows[idx] = "|".join(fields)
    write(path, stamp("\n".join(rows), time))


def milestone(row_key, status, evidence, time):
    path = Path("REPRODUCTION_PLAN.md")
    rows = path.read_text(encoding="utf-8-sig").splitlines()
    idx = next(i for i, row in enumerate(rows) if row.startswith(f"| {row_key} |"))
    fields = rows[idx].split("|")
    fields[-2] = f" {status}。{link(evidence, 'RUN 记录')} "
    rows[idx] = "|".join(fields)
    write(path, stamp("\n".join(rows), time))


def sha(path):
    with path.open("rb") as source:
        result = hashlib.sha256()
        for block in iter(lambda: source.read(4 << 20), b""):
            result.update(block)
    return result.hexdigest()


def verified_inputs(log):
    manifest = INPUT / "manifest.json"
    payload = json.loads(manifest.read_text(encoding="utf-8-sig"))
    truth_manifest = json.loads(TRUTH.with_name("truth-manifest.json").read_text(encoding="utf-8-sig"))
    expected = {
        Path("code/hnswlib-master.zip"): "fb7f8a10e85ac8a9c5e099236f12900917544136b093a5d8bbd85c7ca841416b",
        manifest: truth_manifest["workload_manifest_sha256"],
        TRUTH: truth_manifest["sha256"][TRUTH.name],
    }
    for split in payload["splits"].values():
        expected.update({INPUT / filename: digest for filename, digest in split["sha256"].items()})
    for path, digest in expected.items():
        if sha(path) != digest:
            raise RuntimeError(f"Input checksum mismatch: {path}")
        print(f"input_verified {path.as_posix()} sha256={digest}", file=log)
    for path in sorted(SRC.glob("*")):
        if path.suffix in {".cpp", ".h", ".py"}:
            print(f"source {path.as_posix()} sha256={sha(path)}", file=log)


def bash(script, log):
    cmd = ["wsl.exe", "--cd", str(ROOT), "-e", "bash", "-lc", script]
    print(f"command: {subprocess.list2cmdline(cmd)}", file=log, flush=True)
    subprocess.run(cmd, check=True, stdout=log, stderr=subprocess.STDOUT)


def latest(exp, prefix):
    runs = sorted((Path("experiments") / exp).glob(f"RUN-{prefix}-*/LOG.md"))
    for log in reversed(runs):
        if "\nstatus: passed\n" in log.read_text(encoding="utf-8-sig"):
            return log.parent
    raise FileNotFoundError(f"No passed {exp}/{prefix} run")


def run(job):
    exp, prefix, label = JOBS[job]
    parent = Path("experiments") / exp
    number = 1 + max([int(p.name.rsplit("-", 1)[1]) for p in parent.glob(f"RUN-{prefix}-*")] or [0])
    folder = parent / f"RUN-{prefix}-{number:03d}"
    folder.mkdir(parents=True)
    record = folder / "LOG.md"
    start = now()
    extra = ""

    def save(status, end=""):
        end = f'end: "{end}"\n' if end else ""
        write(record, f"""---
id: {folder.name}
experiment: {exp}
status: {status}
start: "{start}"
{end}upstream: ca426729609b3221563047ceb269cc1213e0d376
---

# {label}

| 条件 | 配置 |
|---|---|
| 环境 | WSL2 Ubuntu、GCC、单线程 CPU |
| 输入 | 固定 SIFT1M 工作负载。{ "小样本为前 12,000 条基础向量与 120 条查询。" if job == "small" else "正式测量每点为 10,000 条查询。" } |
| 证据 | [运行日志](run.log) |

{extra}
""")

    save("running")
    update_index(exp, start)
    state(f"hnswlib 正在执行{label}。{link(record, '当前 RUN')}。",
          "核对该 RUN 的退出状态与原始产物。", start)
    print(f"started {folder.as_posix()}", flush=True)
    status = "failed"
    try:
        with (folder / "run.log").open("w", encoding="utf-8", buffering=1) as log:
            try:
                verified_inputs(log)
                binary = f"/tmp/acorn-hnswlib-{os.getpid()}"
                env = "export OMP_NUM_THREADS=1 OMP_DYNAMIC=FALSE OPENBLAS_NUM_THREADS=1"
                compile_cmd = (f"g++ -std=c++17 -O3 -march=native -pthread -I {SRC.as_posix()}/upstream "
                               f"{SRC.as_posix()}/acorn_index.cpp {SRC.as_posix()}/benchmark.cpp -o {binary}")
                commands = [
                    "set -euo pipefail",
                    "uname -r; head -n 2 /etc/os-release; g++ --version | sed -n '1p'; lscpu | sed -n '/^Model name:/p'",
                    env, compile_cmd, f"sha256sum {binary}; ldd {binary}", "set -x",
                ]
                timing = f"/usr/bin/time -f 'elapsed_seconds=%e peak_rss_kib=%M' {binary}"
                source = INPUT.as_posix()
                target = folder.as_posix()
                if job == "small":
                    latest(SEARCH, "VALIDATE")
                    commands += ["small=$(mktemp -d); mkdir \"$small/main\" \"$small/oracle\"",
                                 f'{timing} build-main {source} "$small/main" 12000',
                                 f'{timing} build-oracle {source} "$small/oracle" 12000']
                elif job == "main":
                    latest(SEARCH, "SMALL")
                    commands += [f"{timing} build-main {source} {target}"]
                elif job == "oracle":
                    latest(SEARCH, "SMALL")
                    commands += [f"{timing} build-oracle {source} {target}"]
                else:
                    main = latest(SEARCH, "INDEX").as_posix()
                    oracle = " " + latest(DISTANCE, "ORACLE").as_posix() if job == "table" else ""
                    commands += [f"{timing} {job} {source} {TRUTH.as_posix()} {main}{oracle} {target}"]
                bash("\n".join(commands), log)
                for file in sorted(folder.iterdir()):
                    if file.suffix in {".index", ".csv", ".i64", ".u64"}:
                        print(f"output {file.name} sha256={sha(file)}", file=log)
            except BaseException as error:
                print(f"failure={error!r}", file=log)
                raise
        if job in {"figure", "table"}:
            from report import report
            extra = report(job, folder)
        else:
            detail = {
                "small": "3 个主索引和 12 个 Oracle 分区均通过保存重载检查。120 条查询的精确预过滤与独立逐坐标穷举一致。",
                "main": "ACORN-γ、ACORN-1、普通 HNSW 各索引 1,000,000 条向量，图结构和每种方法 120 条查询的重载检查通过。",
                "oracle": "12 个 Oracle 分区逐个通过重载检查，合计恰好覆盖 1,000,000 个原始全局 ID。",
            }[job]
            extra = f"| 验收项 | 结果 |\n|---|---|\n| 构建与重载 | {detail} |"
        status = "passed"
    except KeyboardInterrupt:
        status = "interrupted"
        raise
    except Exception as error:
        extra = f"运行在该阶段中止。错误见 [运行日志](run.log)。\n\n`{error}`"
        raise
    finally:
        end = now()
        save(status, end)
        update_index(exp, end)
        if status == "passed":
            if job == "small":
                milestone("精确预过滤", "真实 SIFT 小样本独立穷举核对通过", record, end)
            elif job == "main":
                milestone("ACORN-γ、ACORN-1 移植", "固定图与真实小样本对照通过，百万索引已构建并通过重载", record, end)
            elif job == "oracle":
                milestone("HNSW 后过滤和 Oracle Partition", "已构建百万后过滤索引和 12 个 Oracle 分区，并通过重载核验", record, end)
            elif job == "figure":
                milestone(SEARCH, "52 个测点通过独立复算，待人工审核", record, end)
                milestone("精确预过滤", "全量 10,000 条有序结果与独立真值一致", record, end)
            elif job == "table":
                milestone(DISTANCE, "全量距离计数和召回通过独立复算，待人工审核", record, end)
            next_steps = {
                "small": "构建并重载 hnswlib 百万 ACORN 和 HNSW 后过滤索引。",
                "main": "按 12 类标签构建并核对 Oracle 分区。",
                "oracle": "执行 hnswlib Figure 7(a) 的 52 个正式测点。",
                "figure": "执行 hnswlib Table 3 的全量距离计数。",
                "table": "人工审核 hnswlib Figure 7(a) 与 Table 3 的对照图表及口径差异。",
            }
            state(f"hnswlib 已完成{label}。{link(record, '本次 RUN')}。", next_steps[job], end)
        else:
            state(f"hnswlib 的{label}阶段未通过。{link(record, '本次 RUN')}。",
                  "按运行日志定位失败原因，修正后使用新 RUN 复验。", end)
        print(f"{status} {folder.as_posix()}", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("jobs", nargs="+", choices=JOBS)
    args = parser.parse_args()
    os.chdir(ROOT)
    for name in args.jobs:
        run(name)
