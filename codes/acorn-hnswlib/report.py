"""Independently verify hnswlib measurements and render Faiss comparisons."""

import csv
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont


FIGURE_OLD = Path("experiments/EXP-SIFT-SEARCH/RUN-FIG7A-001")
TABLE_OLD = Path("experiments/EXP-SIFT-DISTANCE/RUN-TABLE3-001")
TRUTH = Path("experiments/EXP-SIFT-SEARCH/RUN-TRUTH-001/truth.ids.i64")
INPUT = Path("dataset/sift1m-workload")
WIDTHS = [*range(10, 800, 50), 800]
DISPLAY = {
    "ACORN-gamma": ("ACORN-γ", "#1c6595"), "ACORN-1": ("ACORN-1", "#137864"),
    "HNSW-postfilter": ("HNSW post-filter", "#a75234"),
    "exact-prefilter": ("Exact pre-filter", "#694988"), "Oracle-Partition": ("Oracle Partition", "#65764d"),
}
PAPER = {"Oracle-Partition": 398.0, "ACORN-gamma": 611.0, "ACORN-1": 999.6, "HNSW-postfilter": 1837.8}


def rows(path):
    with path.open(newline="", encoding="utf-8-sig") as source:
        return list(csv.DictReader(source))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def audit(folder, counting):
    points = rows(folder / "points.csv")
    truth = np.fromfile(TRUTH, dtype="<i8").reshape(10000, 10)
    labels = np.fromfile(INPUT / "train.labels", dtype="u1")
    predicates = np.fromfile(INPUT / "test.labels", dtype="u1")
    raw = np.memmap(folder / "neighbors.i64", mode="r", dtype="<i8").reshape(len(points), *truth.shape)
    calls = (np.fromfile(folder / "distance-counts.u64", dtype="<u8").reshape(len(points), len(truth))
             if counting else None)
    check_ef = {(int(r["point"]), int(r["predicate"])): r for r in rows(folder / "effective-ef.csv")}
    label_counts = np.bincount(labels)
    method_widths = {}
    used = 0
    for point, (row, ids) in enumerate(zip(points, raw)):
        method, ef = row["method"], int(row["ef"])
        method_widths.setdefault(method, []).append(ef)
        require(int(row["point"]) == point and int(row["queries"]) == len(truth), "Point metadata mismatch")
        require(np.all((ids >= -1) & (ids < len(labels))), "Returned ID out of range")
        missing = ids == -1
        require(np.all((labels[np.maximum(ids, 0)] == predicates[:, None]) | missing), "Predicate mismatch")
        ordered = np.sort(ids, axis=1)
        require(not np.any((ordered[:, 1:] >= 0) & (ordered[:, 1:] == ordered[:, :-1])), "Duplicate neighbor")
        hits = int((ids[:, :, None] == truth[:, None, :]).any(axis=2).sum())
        require(hits == int(row["hits"]) and hits / truth.size == float(row["recall_at_10"]), "Recall mismatch")
        require(int(missing.sum()) == int(row["missing"]) and
                int(missing.any(axis=1).sum()) == int(row["underfilled"]), "Missing result mismatch")
        require(int(row["invalid"]) == int(row["duplicates"]) == 0, "Invalid result counters")
        require(abs(len(truth) / float(row["seconds"]) - float(row["qps"])) < 1e-3, "QPS mismatch")
        if method == "HNSW-postfilter":
            for p in range(1, 13):
                budget = int((10 * len(labels) + label_counts[p] - 1) // label_counts[p])
                settings = check_ef[point, p]
                require(settings["method"] == method and int(settings["ef"]) == ef and
                        int(settings["candidate_budget"]) == budget and
                        int(settings["effective_ef"]) == max(ef, budget), "Effective ef mismatch")
                used += 1
        if method == "exact-prefilter":
            require(np.array_equal(ids, truth), "Exact prefilter differs from ordered truth")
        if counting:
            total = int(calls[point].sum())
            require(np.all(calls[point] > 0) and total == int(row["distance_calls"]) and
                    total / len(truth) == float(row["mean_distance_calls"]), "Distance count mismatch")
    require(used == len(check_ef), "Unexpected effective ef data")
    required = {"ACORN-gamma", "ACORN-1", "HNSW-postfilter"}
    require(set(method_widths) == required | {"Oracle-Partition" if counting else "exact-prefilter"},
            "Missing or unexpected method")
    for method, widths in method_widths.items():
        require(len(widths) == len(set(widths)), "Duplicate ef")
        expected = [10, 60] if counting else [0] if method == "exact-prefilter" else WIDTHS
        require((widths[:2] if counting else widths) == expected, "Search width schedule mismatch")
    if counting:
        search = folder.parent.parent / "EXP-SIFT-HNSWLIB-SEARCH"
        references = [p.parent for p in sorted(search.glob("RUN-FIG7A-*/LOG.md"))
                      if "\nstatus: passed\n" in p.read_text(encoding="utf-8-sig")]
        require(bool(references), "An audited uninstrumented Figure 7(a) run is required")
        reference = references[-1]
        baseline = {(r["method"], r["ef"]): int(r["point"]) for r in rows(reference / "points.csv")}
        plain = np.memmap(reference / "neighbors.i64", mode="r", dtype="<i8").reshape(-1, *truth.shape)
        compared = 0
        for row, result in zip(points, raw):
            reference_point = baseline.get((row["method"], row["ef"]))
            if reference_point is not None:
                require(np.array_equal(result, plain[reference_point]), "Full-run IDs differ from uninstrumented search")
                compared += 1
        require(compared >= 6, "Missing full-run instrumented comparisons")
        print(f"Independent count-on/off ID comparisons: {compared} full points", flush=True)
    return points


def canvas(width, height, title, subtitle):
    image = Image.new("RGB", (width, height), "white")
    draw = ImageDraw.Draw(image)

    def text(x, y, value, size=22, fill="#1c2c3e", bold=False, anchor=None):
        name = "segoeuib.ttf" if bold else "segoeui.ttf"
        font = ImageFont.truetype(f"C:/Windows/Fonts/{name}", size)
        draw.text((x, y), value, font=font, fill=fill, anchor=anchor)

    text(46, 24, title, 39, bold=True)
    text(48, 83, subtitle, 22, fill="#4b6273")
    return image, draw, text


def figure(folder, points):
    previous = rows(FIGURE_OLD / "points.csv")
    datasets = [points, previous]
    image, draw, text = canvas(1370, 700, "Figure 7(a)  |  SIFT1M",
                                "10,000 queries per setting  ·  one thread  ·  one pass")
    all_rows = points + previous
    lo = math.floor(math.log10(min(float(r["qps"]) for r in all_rows)) * 2) / 2
    hi = math.ceil(math.log10(max(float(r["qps"]) for r in all_rows)) * 2) / 2
    recall_lo = math.floor(min(float(r["recall_at_10"]) for r in all_rows) * 10) / 10
    methods = ("ACORN-gamma", "ACORN-1", "HNSW-postfilter", "exact-prefilter")
    for panel, (dataset, title) in enumerate(zip(datasets, ("hnswlib", "Faiss reference"))):
        left, right, top, bottom = 106 + panel * 655, 661 + panel * 655, 202, 548
        text(left, 146, title, 27, bold=True)

        def x(qps):
            return left + (math.log10(qps) - lo) / (hi - lo) * (right - left)

        def y(recall):
            return bottom - (recall - recall_lo) / (1 - recall_lo) * (bottom - top)

        for i in range(round(recall_lo * 100), 101, 10):
            yy = y(i / 100)
            draw.line((left, yy, right, yy), fill="#e1e8ee", width=2)
            text(left - 11, yy, str(i), 18, anchor="rm")
        for tick in (30, 100, 300, 1000, 3000, 10000):
            if lo <= math.log10(tick) <= hi:
                xx = x(tick)
                draw.line((xx, top, xx, bottom), fill="#e1e8ee", width=2)
                text(xx, bottom + 10, f"{tick:,}", 17, anchor="mt")
        for method in methods:
            color = DISPLAY[method][1]
            graph = [(float(r["qps"]), float(r["recall_at_10"])) for r in dataset if r["method"] == method]
            frontier = []
            for qps, recall in sorted(graph, reverse=True):
                if not frontier or recall > frontier[-1][1]:
                    frontier.append((qps, recall))
            if len(frontier) > 1:
                draw.line([(x(qps), y(recall)) for qps, recall in frontier], fill=color, width=4)
            for qps, recall in graph:
                xx, yy = x(qps), y(recall)
                draw.ellipse((xx - 4, yy - 4, xx + 4, yy + 4), fill=color)
        text((left + right) // 2, 594, "Queries per second (log scale)", 20, anchor="mt")
    text(48, 170, "Recall@10 (%)", 17)
    for position, method in zip((197, 410, 616, 901), methods):
        name, color = DISPLAY[method]
        draw.rectangle((position, 646, position + 25, 652), fill=color)
        text(position + 32, 633, name, 21)
    with (folder / "figure7a-hnswlib.png").open("xb") as out:
        image.save(out, format="PNG")

    def best(dataset, method, threshold):
        result = [float(r["qps"]) for r in dataset
                  if r["method"] == method and float(r["recall_at_10"]) >= threshold]
        return f"{max(result):,.1f}" if result else "未达到"

    records = []
    for method in methods:
        maximum = max(float(r["recall_at_10"]) for r in points if r["method"] == method)
        cells = [best(d, method, target) for target in (0.8, 0.9) for d in datasets]
        records.append(f"| {DISPLAY[method][0]} | {maximum:.3%} | {' | '.join(cells)} |")
    return """![同轴比较 hnswlib 与 Faiss 的 SIFT1M 查询曲线](figure7a-hnswlib.png)

| 方法 | hnswlib 最高召回 | ≥80% hnswlib QPS | ≥80% Faiss QPS | ≥90% hnswlib QPS | ≥90% Faiss QPS |
|---|---:|---:|---:|---:|---:|
""" + "\n".join(records) + """

52 个测点均以全部 10,000 条查询独立复算。精确预过滤有序 ID 与共同真值逐项一致。表内 QPS 是达到各门槛的最快实测点。

已复用同一工作负载及计时边界。hnswlib 后过滤使用原生实际搜索宽度和重建的 HNSW 图。各点每次一条、单线程、只测一次，论文的 QPS 为 50 次均值。Faiss 图来自此前的独立运行。
"""


def target(points, method):
    values = sorted((int(r["ef"]), float(r["recall_at_10"]), float(r["mean_distance_calls"]))
                    for r in points if r["method"] == method)
    exact = next((row for row in values if row[1] == 0.8), None)
    if exact:
        return exact[2], [exact], False
    for a, b in zip(values, values[1:]):
        if b[0] == a[0] + 1 and a[1] < 0.8 < b[1]:
            weight = (0.8 - a[1]) / (b[1] - a[1])
            return a[2] + weight * (b[2] - a[2]), [a, b], True
    return None, [min(values, key=lambda r: (abs(r[1] - 0.8), r[0]))], False


def table(folder, points):
    previous = rows(TABLE_OLD / "points.csv")
    estimates = {method: target(points, method) for method in PAPER}
    old = {method: target(previous, method) for method in PAPER}
    image, draw, text = canvas(1260, 690, "Table 3  |  Distance calls at 80% Recall@10",
                                "SIFT1M  ·  10,000 filtered queries per setting  ·  fewer calls are better")
    colors = ("#28658d", "#839eb5", "#dce4ed")
    for index, name in enumerate(("hnswlib", "Faiss", "Paper")):
        left = 48 + 160 * index
        draw.rectangle((left, 142, left + 25, 161), fill=colors[index])
        text(left + 35, 135, name, 20)
    maximum = max([*PAPER.values()] + [t[0] for t in estimates.values() if t[0]] +
                  [t[0] for t in old.values() if t[0]])
    unit = 500 if maximum < 3000 else 1000
    bound = (math.ceil(maximum / unit) + 0.30) * unit

    def x(value):
        return 270 + value / bound * 883

    for value in range(0, int(bound) + 1, unit):
        xx = x(value)
        draw.line((xx, 185, xx, 578), fill="#e1e8ee", width=2)
        text(xx, 590, f"{value:,}", 17, anchor="mt")
    records = []
    oracle = estimates["Oracle-Partition"][0]
    for index, (method, paper) in enumerate(PAPER.items()):
        estimate, bracket, interpolated = estimates[method]
        historic, _, historic_interpolated = old[method]
        top = 193 + 96 * index
        text(250, top + 38, DISPLAY[method][0], 23, anchor="rm")
        for band, (value, star) in enumerate(((estimate, interpolated), (historic, historic_interpolated),
                                               (paper, False))):
            yy = top + 25 * band
            if value is None:
                text(x(0) + 8, yy + 9, "80% outside measured range", 15, anchor="lm")
            else:
                draw.rectangle((x(0), yy, x(value), yy + 18), fill=colors[band])
                text(x(value) + 8, yy + 9, f"{value:,.1f}" + ("*" if star else ""), 16, anchor="lm")
        nearby = " · ".join(f"{ef} / {recall:.3%} / {calls:,.2f}" for ef, recall, calls in bracket)
        current = "未覆盖" if estimate is None else f"{estimate:,.1f}" + ("（估算）" if interpolated else "")
        reference = "未覆盖" if historic is None else f"{historic:,.1f}" + ("（估算）" if historic_interpolated else "")
        relative = "—" if estimate is None or oracle is None or method == "Oracle-Partition" else f"{estimate/oracle-1:+.1%}"
        records.append(f"| {DISPLAY[method][0]} | {nearby} | {current} | {reference} | {paper:,.1f} | {relative} |")
    text(700, 626, "Mean distance calls per query", 20, anchor="mt")
    text(48, 661, "* Interpolation between adjacent measured settings.", 17, fill="#4b6273")
    with (folder / "table3-hnswlib.png").open("xb") as out:
        image.save(out, format="PNG")
    return """![80% Recall@10 的距离计算对照](table3-hnswlib.png)

| 方法 | hnswlib 实测档位 / Recall@10 / 次数 | hnswlib 80% | Faiss 80% | 论文 80% | 相对本机 Oracle |
|---|---|---:|---:|---:|---:|
""" + "\n".join(records) + f"""

共 {len(points)} 个测点，每点 10,000 条查询，全部逐查询复算。每个测点的前 120 条查询均核对开启计数前后的 ID 与距离。与 Figure 7(a) 重合的档位另对照全部 10,000 条未开启计数的查询 ID。图中的插值只使用相邻实测档位。超出实测范围时保留最近点。

计数包括入口、上层和底层实际执行的查询到库向量平方 L2。计数模式的 QPS 仅用于复算记录，吞吐比较取 Figure 7(a)。
"""


def report(job, folder):
    counting = job == "table"
    points = audit(folder, counting)
    result = table(folder, points) if counting else figure(folder, points)
    evidence = "[测点](points.csv) · [逐查询返回 ID](neighbors.i64) · [后过滤实际宽度](effective-ef.csv)"
    if counting:
        evidence += " · [逐查询距离次数](distance-counts.u64)"
    return result.rstrip() + "\n\n" + evidence + "\n"
