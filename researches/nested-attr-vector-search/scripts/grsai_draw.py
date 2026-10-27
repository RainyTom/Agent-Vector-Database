#!/usr/bin/env python3
"""Generate illustrative figures for the research report via grsai API.

Data plots stay with matplotlib (exact numbers); this script only produces
conceptual/architecture/scenario illustrations.

Usage:
  export GRSAI_API_KEY=sk-...
  python3 researches/nested-attr-vector-search/scripts/grsai_draw.py [--only name,...]

API (async submit + poll):
  POST {BASE}/generate  {"model", "prompt", "aspectRatio", "imageSize", "replyType":"async"}
  GET  {BASE}/result?id=TASK_ID
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

BASE = os.environ.get("GRSAI_BASE_URL", "https://grsai.dakka.com.cn/v1/api")
MODEL = os.environ.get("GRSAI_MODEL", "gpt-image-1.5")
OUT = Path(__file__).resolve().parents[1] / "assets"

# Prompt design notes: AI image models render text unreliably, so prompts ask
# for minimal, English-only labels and emphasize layout/geometry.
PROMPTS = {
    "concept-scope": (
        "Clean flat vector infographic, white background, 16:9. Left: a directory "
        "tree with 3 levels, folders labeled only with short English words "
        "('/HR/', '/HR/Policies/', '/Finance/', '/Archive/'). One folder is "
        "highlighted in amber and its whole subtree tinted. Right: a 2D scatter of "
        "small dots in a soft vector space, the dots belonging to the highlighted "
        "subtree tinted amber, three of them connected to a query point with dashed "
        "lines labeled 'top-3'. A thin arrow from the highlighted folder to the "
        "amber dot cluster labeled 'scope'. Modern minimal style, blue and amber "
        "palette, no extra text, no fake numbers."
    ),
    "arch-compare": (
        "Clean flat architecture comparison diagram, white background, 16:9, two "
        "horizontal panels. Top panel titled 'Global index + bitmap filter': one "
        "large layered cylinder representing a global ANN index, with a grid mask "
        "overlay showing filtered-out cells, arrow through the whole cylinder. "
        "Bottom panel titled 'Per-node partitioned indexes': a small folder tree, "
        "each folder owning its own tiny index tile, arrow entering only the tiles "
        "of the selected subtree. Minimal English labels only, blue vs purple "
        "palette, no fake numbers, modern vector style."
    ),
    "app-scenarios": (
        "Four-quadrant flat illustration, white background, 16:9, thin dividers. "
        "Quadrant 1: source code repository tree with a magnifier over one folder. "
        "Quadrant 2: enterprise document folders with policy and finance icons. "
        "Quadrant 3: AI agent memory bubbles organized in a hierarchy. Quadrant 4: "
        "academic paper classification tree with subject and year branches. Each "
        "quadrant minimal line icons, single accent color, tiny English labels "
        "only, no text paragraphs, no fake numbers."
    ),
    "opt-roadmap": (
        "Flat technical roadmap diagram, white background, 16:9, three lanes stacked "
        "vertically, each with a small icon and one short English label. Lane A "
        "'lane partition': a subtree split into parallel colored lanes each scanning "
        "its own slice, merged into one ranked list. Lane B 'hierarchical pruning': "
        "a tree where most subtrees are grayed out and one best-first path is "
        "highlighted with distance bounds. Lane C 'cross-partition edges': two small "
        "peer index clusters connected by a few sparse directed edges along a folder "
        "boundary. Minimal, clean, purple and gray palette, no fake numbers."
    ),
}


def _req(method: str, url: str, key: str, body: dict | None = None) -> dict:
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("Authorization", f"Bearer {key}")
    if body is not None:
        req.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.loads(r.read().decode())


def generate(name: str, prompt: str, key: str) -> Path:
    task = _req("POST", f"{BASE}/generate", key, {
        "model": MODEL,
        "prompt": prompt,
        "aspectRatio": "16:9",
        "imageSize": "2K",
        "replyType": "async",
    })
    tid = task.get("id")
    if not tid:
        raise SystemExit(f"[{name}] submit failed: {task}")
    print(f"[{name}] task {tid} submitted", flush=True)
    deadline = time.time() + 600
    while time.time() < deadline:
        time.sleep(5)
        res = _req("GET", f"{BASE}/result?" + urllib.parse.urlencode({"id": tid}), key)
        status = res.get("status")
        if status == "succeeded":
            url = res["results"][0]["url"]
            OUT.mkdir(parents=True, exist_ok=True)
            dst = OUT / f"grsai-{name}.png"
            urllib.request.urlretrieve(url, dst)
            print(f"[{name}] saved {dst}", flush=True)
            return dst
        if status in ("failed", "violation"):
            raise SystemExit(f"[{name}] {status}: {res}")
        print(f"[{name}] {status}", flush=True)
    raise SystemExit(f"[{name}] timeout")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="", help="comma-separated prompt names")
    args = ap.parse_args()
    key = os.environ.get("GRSAI_API_KEY", "")
    if not key:
        kf = Path(os.environ.get("GRSAI_KEY_FILE", Path.home() / ".config" / "grsai.key"))
        if kf.is_file():
            key = kf.read_text().strip()
    if not key:
        print("ERROR: set GRSAI_API_KEY (Bearer key from grsai.com)", file=sys.stderr)
        print("Available prompts:", ", ".join(PROMPTS), file=sys.stderr)
        return 2
    names = [n.strip() for n in args.only.split(",") if n.strip()] or list(PROMPTS)
    for n in names:
        if n not in PROMPTS:
            raise SystemExit(f"unknown prompt: {n}")
        generate(n, PROMPTS[n], key)
    return 0


if __name__ == "__main__":
    sys.exit(main())
