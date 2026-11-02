#!/usr/bin/env python3
"""Validate isolated DAG runs and report observed, non-interpolated comparisons."""
import argparse
import csv
import json
import math
import re
import statistics
import sys
from collections import defaultdict
from pathlib import Path


SEMANTICS = ("recursive", "direct")
CEILINGS = (200, 650, 1200, 2000, 4000)
VARIANTS = {
    "baseline", "portal4", "portal16", "portal32", "connector2", "connector3",
    "no_reverse", "reverse64", "no_distributed", "prune16", "prune24", "prune32",
    "jump0", "jump2", "jump3", "order_size_asc", "order_size_desc", "build_seed2",
    "build_shuffle", "order_fcfs_dag", "order_fcfs_asc", "order_fcfs_desc",
}
COUNTS = ("distances", "expansions", "graphs", "returned", "rejected_distances",
          "item_checks", "owner_checks", "adjacency_entries", "rejected_edges",
          "duplicate_edges")
EXECUTION_ARMS = ("merged_flat", "merged_flat_owner", "merged_bitmap", "merged_dense",
                  "merged_flat_owner_dense")


class InvalidData(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise InvalidData(message)


def number(row, field, integer=False, optional=False):
    raw = row.get(field, "")
    if raw == "" and optional:
        return None
    try:
        value = int(raw) if integer else float(raw)
    except (ValueError, TypeError):
        raise InvalidData(f"{row['_where']}: {field}不是有效数字: {raw!r}") from None
    require(math.isfinite(value) and value >= 0, f"{row['_where']}: {field}必须为有限非负值")
    return value


def truthy(row, field):
    require(row.get(field, "").lower() in ("1", "true"),
            f"{row['_where']}: {field}必须为true/1")


def load_csv(path, required, keys):
    require(path.is_file(), f"缺少输入: {path}")
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        fields = reader.fieldnames or []
        require(len(fields) == len(set(fields)), f"{path}: 重复CSV列")
        require(set(required) <= set(fields), f"{path}: 缺少列 {sorted(set(required) - set(fields))}")
        result, seen = [], set()
        for line, raw in enumerate(reader, 2):
            require(None not in raw and all(v is not None for v in raw.values()),
                    f"{path}:{line}: CSV列数不符")
            row = {k: v.strip() for k, v in raw.items()}
            row['_where'] = f"{path}:{line}"
            for field in ("query", "ef", "scope_query", "K"):
                if field in row:
                    row[field] = str(number(row, field, integer=True))
            key = tuple(row[k] for k in keys)
            require(key not in seen, f"{path}:{line}: 重复行键 {key}")
            seen.add(key)
            result.append(row)
    require(result, f"{path}: 无数据行")
    return result


def groups(rows, *keys):
    result = defaultdict(list)
    for row in rows:
        result[tuple(row[k] for k in keys)].append(row)
    return result


def percentile(values, fraction):
    if not values:
        return None
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    low, high = math.floor(position), math.ceil(position)
    return ordered[low] + (ordered[high] - ordered[low]) * (position - low)


def distribution(values):
    return {"n": len(values), "mean": statistics.mean(values) if values else None,
            "p50": percentile(values, 0.5), "p95": percentile(values, 0.95)}


def measured_mean(rows, field):
    values = [number(r, field, optional=True) for r in rows]
    values = [v for v in values if v is not None]
    return {"mean": statistics.mean(values) if values else None,
            "measured_queries": len(values), "total_queries": len(rows)}


def summarize(rows):
    nonempty = [r for r in rows if number(r, "truth", integer=True) > 0]
    empty = [r for r in rows if number(r, "truth", integer=True) == 0]
    recalls = [number(r, "hits", integer=True) / number(r, "truth", integer=True) for r in nonempty]
    return {
        "queries": len(rows), "nonempty_queries": len(nonempty), "empty_queries": len(empty),
        "recall_mean": statistics.mean(recalls) if recalls else None,
        "recall_min": min(recalls) if recalls else None,
        "query_median_latency_us": distribution([number(r, "latency_us") for r in nonempty]),
        "empty_query_median_latency_us": distribution([number(r, "latency_us") for r in empty]),
        "nonempty_counts": {field: measured_mean(nonempty, field) for field in COUNTS
                            if any(field in r for r in rows)},
        "empty_counts": {field: measured_mean(empty, field) for field in COUNTS
                         if any(field in r for r in rows)},
        "bitmap_build_us": measured_mean(nonempty, "bitmap_build_us"),
        "graph_structure": {
            field: measured_mean(rows, field)
            for field in ("degree_mean", "local_edges", "connector_edges", "jump_edges")
            if any(field in r for r in rows)
        },
    }


def validate_queries(rows, nq):
    result = {r["query"]: r for r in rows}
    require(len(result) == nq, f"queries: 预期{nq}条，实际{len(result)}")
    for row in rows:
        eligible = number(row, "eligible", integer=True)
        truth = number(row, "truth", integer=True)
        require(truth == min(10, eligible), f"{row['_where']}: truth不等于min(10,eligible)")
        number(row, "source_scope", integer=True)
        # An empty roots list is a valid empty scope.
        if "vector_query_id" in row:
            number(row, "vector_query_id", integer=True)
    return result


def validate_grid(rows, name, queries, expected_efs=None, mandatory_normal=False):
    names = sorted({r[name] for r in rows})
    require(all(names), f"{name}: 空名称")
    efs = sorted({int(r["ef"]) for r in rows})
    require(len(efs) == 5 and min(efs) > 0, f"{name}: 必须为5个正ef，实际{efs}")
    if expected_efs is not None:
        require(efs == expected_efs, f"{name}: ef与arms不一致")
    for (label, ef), selected in groups(rows, name, "ef").items():
        require({r["query"] for r in selected} == set(queries),
                f"{name}={label},ef={ef}: 查询ID集合不完整（包括空查询）")
    require(len(groups(rows, name, "ef")) == len(names) * len(efs), f"{name}: ef网格不完整")
    for row in rows:
        query = queries.get(row["query"])
        require(query is not None, f"{row['_where']}: 未知query")
        for field in ("eligible", "truth"):
            require(number(row, field, integer=True) == number(query, field, integer=True),
                    f"{row['_where']}: {field}与queries不一致")
        hits, truth = number(row, "hits", integer=True), number(row, "truth", integer=True)
        require(hits <= truth, f"{row['_where']}: hits超过truth")
        number(row, "latency_us")
        for field in COUNTS:
            if field in row:
                number(row, field, integer=True, optional=True)
        distances = number(row, "distances", integer=True)
        if "returned" in row and row["returned"] != "":
            returned = number(row, "returned", integer=True)
            require(hits <= returned <= truth, f"{row['_where']}: returned/hits/truth不一致")
        if truth == 0:
            require(distances == hits == 0, f"{row['_where']}: 空查询distances/hits必须为0")
            if row.get("returned", "") != "":
                require(number(row, "returned", integer=True) == 0, f"{row['_where']}: 空查询returned非0")
        if mandatory_normal or "normal_verified" in row:
            truthy(row, "normal_verified")
        if mandatory_normal or "rejected_distances" in row:
            require(number(row, "rejected_distances", integer=True) == 0,
                    f"{row['_where']}: 发现不合格对象距离计算")
        if row.get("arm") == "nodeann_pergraph":
            require(number(row, "ef_local", integer=True) == int(row["ef"]),
                    f"{row['_where']}: native NodeANN必须每图使用相同ef")
    return names, efs


def paired_equality(rows, name, baseline, alternatives, fields):
    index = {(r[name], r["ef"], r["query"]): r for r in rows}
    present = {r[name] for r in rows}
    require(baseline in present, f"缺少配对基线{name}={baseline}")
    result = []
    for alternative in alternatives:
        if alternative not in present:
            continue
        checked = defaultdict(int)
        for (label, ef, query), row in index.items():
            if label != alternative:
                continue
            reference = index[(baseline, ef, query)]
            for field in fields:
                left, right = row.get(field, ""), reference.get(field, "")
                if left == "" or right == "":
                    continue
                require(number(row, field) == number(reference, field),
                        f"{row['_where']}: {alternative}/{baseline} ef={ef},query={query} {field}不一致")
                checked[field] += 1
        result.append({"baseline": baseline, "alternative": alternative,
                       "checked_pairs_by_field": dict(checked), "equal": True})
    return result


def envelope(rows, names, queries):
    query_ids = sorted((q for q, r in queries.items() if int(r["truth"]) > 0), key=int)
    by_query = groups(rows, "arm", "query")
    output = []
    for ceiling in CEILINGS:
        arms = []
        for arm in names:
            selected, missing = [], []
            for query in query_ids:
                feasible = [r for r in by_query[(arm, query)] if int(r["distances"]) <= ceiling]
                if not feasible:
                    missing.append(query)
                    continue
                point = min(feasible, key=lambda r: (-int(r["hits"]) / int(r["truth"]),
                                                     int(r["distances"]), int(r["ef"])))
                selected.append({"query": query, "ef": int(point["ef"]),
                                 "distances": int(point["distances"]),
                                 "recall": int(point["hits"]) / int(point["truth"])})
            coverage = len(selected) / len(query_ids) if query_ids else None
            arms.append({"arm": arm, "status": "supported" if not missing and query_ids else "unsupported",
                         "supported_queries": len(selected), "nonempty_queries": len(query_ids),
                         "coverage": coverage, "unsupported_query_ids": missing,
                         "supported_subset_recall_mean": statistics.mean(p["recall"] for p in selected) if selected else None,
                         "selected_observed_points": selected})
        comparable = bool(query_ids) and all(a["status"] == "supported" for a in arms)
        output.append({"distance_ceiling": ceiling, "all_arms_full_coverage": comparable,
                       "comparison_note": "仅为观测点的次要上包络；上限相同不代表实际工作相同；缺覆盖不可比较子集均值",
                       "arms": arms})
    return output


def scope_summary(rows, queries):
    require({r["query"] for r in rows} == set(queries), "scope报告与queries查询ID不完全匹配")
    for row in rows:
        require(number(row, "mismatch", integer=True) == 0, f"{row['_where']}: scope mask不匹配")
        for field in ("source_scope", "eligible", "truth", "roots"):
            require(row[field] == queries[row["query"]][field],
                    f"{row['_where']}: scope {field}与queries不一致")
        for field in ("dirs", "enumerated_writes", "word_ors"):
            number(row, field, integer=True)
        for field in ("enumerate_us", "bitmap_us"):
            number(row, field)
    return {"exact_query_report_match": True,
            "enumerate_us": distribution([number(r, "enumerate_us") for r in rows]),
            "bitmap_us": distribution([number(r, "bitmap_us") for r in rows]),
            "rows": [{k: v for k, v in r.items() if not k.startswith("_")} for r in rows]}


def amort_summary(rows, queries):
    tested = []
    for row in rows:
        query = queries.get(row["scope_query"])
        require(query is not None, f"{row['_where']}: 未知scope_query")
        require(number(row, "eligible", integer=True) == int(query["eligible"]),
                f"{row['_where']}: eligible与scope query不一致")
        require(number(row, "K", integer=True) > 0, f"{row['_where']}: K必须为正")
        for field in ("correctness_verified", "counts_equal", "allocation_included", "normal_timing"):
            truthy(row, field)
        values = {field: number(row, field) for field in
                  ("char_total_us", "bitmap_total_us", "bitmap_build_us", "per_query_char_us", "per_query_bitmap_us")}
        tested.append({"scope_query": row["scope_query"], "source_scope": query["source_scope"],
                       "eligible": int(row["eligible"]), "K": int(row["K"]), **values,
                       "bitmap_faster_observed": values["bitmap_total_us"] < values["char_total_us"]})
    scopes = []
    for scope in sorted({r["scope_query"] for r in tested}, key=int):
        selected = sorted((r for r in tested if r["scope_query"] == scope), key=lambda r: r["K"])
        winning = [r["K"] for r in selected if r["bitmap_faster_observed"]]
        scopes.append({"scope_query": scope, "first_tested_winning_K": min(winning) if winning else None,
                       "tested_winning_K": winning, "tested_points": selected,
                       "note": "仅报告该scope各实测K；不插值、不保证单调或全局摊销交点"})
    return {"correctness_and_counts_verified": True, "allocation_included": True,
            "normal_timing": True, "scopes": scopes}


def manifests(root, run_dirs):
    paths = [root / "input-sha256.txt"] + [d / "input-sha256.txt" for d in run_dirs]
    found, signatures = [], []
    root_manifest = paths[0].is_file()
    for path in paths:
        if not path.is_file():
            require(root_manifest or path == paths[0], f"缺少输入清单: {path}")
            continue
        entries = {}
        for line in path.read_text(encoding="utf-8").splitlines():
            if not line.strip():
                continue
            match = re.fullmatch(r"([0-9a-fA-F]{64})\s+[* ]?(.+)", line)
            require(match is not None, f"{path}: 非法SHA256清单行")
            digest, name = match.groups()
            require(name not in entries, f"{path}: 重复清单路径{name}")
            entries[name] = digest.lower()
        require(entries, f"{path}: 空清单")
        signatures.append(entries)
        found.append({"path": str(path), "sha256": entries})
    require(found, "未找到input-sha256.txt")
    require(all(s == signatures[0] for s in signatures), "运行间输入SHA256清单不一致")
    return found


def analyze(root):
    run_dirs = [root / f"nq{nq}-{sem}" for nq in (24, 120) for sem in SEMANTICS]
    provenance = manifests(root, run_dirs)
    runs, pairing = {}, []
    query_sets = {}
    common_efs = None
    for nq in (24, 120):
        for sem in SEMANTICS:
            key = f"nq{nq}-{sem}"
            directory = root / key
            qrows = load_csv(directory / f"queries-{sem}.csv",
                             ("query", "source_scope", "vector_query_id", "eligible", "truth",
                              "roots", "roots_semantics"), ("query",))
            queries = validate_queries(qrows, nq)
            query_sets[(nq, sem)] = queries
            require(all(r["roots_semantics"] == sem for r in qrows),
                    f"{key}: roots_semantics与目录语义不一致")
            arms = load_csv(directory / f"arms-{sem}.csv",
                            ("arm", "ef", "ef_local", "query", "eligible", "truth", "hits", "latency_us",
                             "distances", "expansions", "graphs", "bitmap_build_us", "returned",
                             "rejected_distances", "item_checks", "owner_checks", "adjacency_entries", "normal_verified"),
                            ("arm", "ef", "query"))
            names, efs = validate_grid(arms, "arm", queries, common_efs, mandatory_normal=True)
            common_efs = efs
            require({"nodeann_pergraph", "merged", *EXECUTION_ARMS} <= set(names),
                    f"{key}: 缺少native NodeANN或必须的等价执行arms")
            equality = paired_equality(arms, "arm", "merged", EXECUTION_ARMS,
                                       ("distances", "expansions", "hits", "returned"))
            table = []
            for (arm, ef), selected in sorted(groups(arms, "arm", "ef").items(), key=lambda x: (x[0][0], int(x[0][1]))):
                table.append({"arm": arm, "ef": int(ef),
                              "role": "split_ef_control_not_equal_total_work" if arm == "nodeann_shared" else "same_ef_comparison",
                              "ef_local_observed": sorted({int(r["ef_local"]) for r in selected if r["ef_local"] != ""}),
                              **summarize(selected)})
            scope = load_csv(directory / f"scope-{sem}.csv",
                             ("query", "source_scope", "eligible", "truth", "roots", "dirs", "enumerated_writes",
                              "word_ors", "enumerate_us", "bitmap_us", "mismatch"), ("query",))
            amort = load_csv(directory / f"bitmap-amortisation-{sem}.csv",
                             ("scope_query", "eligible", "K", "char_total_us", "bitmap_total_us", "bitmap_build_us",
                              "per_query_char_us", "per_query_bitmap_us", "correctness_verified", "counts_equal",
                              "allocation_included", "normal_timing"), ("scope_query", "K"))
            result = {"nq": nq, "semantics": sem, "efs": efs, "same_ef_primary": table,
                      "observed_only_distance_envelope": envelope(arms, names, queries),
                      "execution_pair_equality": equality, "scope": scope_summary(scope, queries),
                      "bitmap_amortisation": amort_summary(amort, queries)}
            if nq == 24:
                variants = load_csv(directory / f"variants-{sem}.csv",
                                    ("variant", "ef", "query", "eligible", "truth", "hits", "latency_us", "distances"),
                                    ("variant", "ef", "query"))
                variant_names, _ = validate_grid(variants, "variant", queries, efs)
                require(set(variant_names) == VARIANTS, f"{key}: variants必须为源码22种，实际{variant_names}")
                result["robustness"] = {
                    "variant_count": len(variant_names), "ef_count": len(efs),
                    "same_ef": [{"variant": variant, "ef": int(ef), **summarize(selected)}
                                for (variant, ef), selected in sorted(groups(variants, "variant", "ef").items(),
                                                                      key=lambda x: (x[0][0], int(x[0][1])))],
                    "unchanged_graph_pair_equality": paired_equality(
                        variants, "variant", "baseline", ("order_size_asc", "order_size_desc"),
                        ("distances", "expansions", "hits", "returned", "rejected_edges", "duplicate_edges")),
                }
            runs[key] = result
        recursive, direct = query_sets[(nq, "recursive")], query_sets[(nq, "direct")]
        require(set(recursive) == set(direct), f"nq{nq}: 两种语义查询ID不一致")
        checked_fields = ["source_scope"]
        for field in ("vector_query_id", "query_vector_id", "vector_id"):
            if any(field in r for r in list(recursive.values()) + list(direct.values())):
                require(all(field in r and r[field] != "" for r in list(recursive.values()) + list(direct.values())),
                        f"nq{nq}: {field}覆盖不全")
                checked_fields.append(field)
        for query in recursive:
            for field in checked_fields:
                require(recursive[query][field] == direct[query][field],
                        f"nq{nq}: query={query} 两种语义{field}配对不一致")
        pairing.append({"nq": nq, "paired_queries": nq, "equal_fields": checked_fields,
                        "vector_pairing_explicit": any("vector" in f for f in checked_fields)})
    return {"validation_passed": True, "input_root": str(root), "input_manifests": provenance,
            "semantic_pairing": pairing,
            "methodology": {
                "primary": "same ef only; recursive/direct与nq24/nq120分别报告",
                "latency": "每行latency_us是查询重复运行中位数；表内再按非空查询统计mean/p50/p95",
                "empty_queries": "保留并独立统计时间，不计入recall或非空查询时间均值",
                "blank_counts": "未测量，保留null及measured_queries，不以0填补",
                "nodeann": "nodeann_pergraph每图原生ef；nodeann_shared为max(k,ef/G)分配控制，不等于实际总工作",
                "envelope": "逐查询仅选距离<=上限的实测点，最大recall；不插值、不外推；覆盖不匹配不可等工作比较",
                "distance_time": "不把端到端查询时间/距离数称为距离核时间",
                "scope": "scope记录与queries字段精确匹配及mask mismatch=0，不重新计算向量真值",
                "provenance": "记录并比较生成时输入SHA256清单；不重新读取原始向量文件验hash",
            }, "runs": runs}


def print_summary(summary):
    print("验证通过；主比较仅使用相同ef，递归/直接语义及24/120查询分别报告。")
    for key, run in summary["runs"].items():
        print(f"\n[{key}] ef={','.join(map(str, run['efs']))}")
        print("arm | ef | recall均值/最小 | 非空时间mean/p50/p95(µs) | 距离均值 | 空查询数/时间均值(µs)")
        for row in run["same_ef_primary"]:
            def fmt(value):
                return "—" if value is None else f"{value:.4f}"
            timing = row["query_median_latency_us"]
            empty = row["empty_query_median_latency_us"]
            label = row["arm"] + ("[分配控制]" if row["arm"] == "nodeann_shared" else "")
            print(f"{label} | {row['ef']} | {fmt(row['recall_mean'])}/{fmt(row['recall_min'])} | "
                  f"{fmt(timing['mean'])}/{fmt(timing['p50'])}/{fmt(timing['p95'])} | "
                  f"{fmt(row['nonempty_counts']['distances']['mean'])} | {row['empty_queries']}/{fmt(empty['mean'])}")
        for entry in run["observed_only_distance_envelope"]:
            coverage = ", ".join(f"{a['arm']}:{a['supported_queries']}/{a['nonempty_queries']}" for a in entry["arms"])
            print(f"观测包络 ceiling={entry['distance_ceiling']} 覆盖 {coverage}")
        for scope in run["bitmap_amortisation"]["scopes"]:
            print(f"bitmap scope={scope['scope_query']} 实测胜出K={scope['tested_winning_K']}（非全局交点）")
    print("\n原始计数、配对检查、22×5变体及观测点详见JSON；未测量计数为null，不作等实际工作主张。")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dir", required=True, type=Path, help="四个隔离运行子目录的根目录")
    parser.add_argument("--out", required=True, type=Path, help="JSON汇总输出路径")
    args = parser.parse_args()
    try:
        summary = analyze(args.dir)
    except (InvalidData, OSError, csv.Error) as error:
        failure = {"validation_passed": False, "input_root": str(args.dir), "error": str(error)}
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(failure, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"验证失败: {error}", file=sys.stderr)
        return 1
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(summary, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print_summary(summary)
    return 0


if __name__ == "__main__":
    sys.exit(main())
