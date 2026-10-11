#!/usr/bin/env python3
"""Search and check the local vector join bibliography using only the stdlib."""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "papers/vector-join"
REQUIRED = {
    "id", "aliases", "title", "authors", "year", "venue", "version_status",
    "versions", "links", "tags", "relevance", "problem", "join_type", "metrics",
    "data_assumptions", "method", "guarantees", "experiments", "limitations",
    "related_work", "project_insights", "evidence", "verification", "local_dir",
}


def read_jsonl(name):
    rows = []
    for number, line in enumerate((CATALOG / name).read_text().splitlines(), 1):
        if not line.strip():
            raise ValueError(f"{name}:{number}: blank line")
        try:
            row = json.loads(line)
        except json.JSONDecodeError as error:
            raise ValueError(f"{name}:{number}: {error}") from error
        if not isinstance(row, dict):
            raise ValueError(f"{name}:{number}: expected object")
        rows.append(row)
    return rows


def validate(papers):
    seen = {}
    canonical_ids = set()
    for row in papers:
        missing = REQUIRED - row.keys()
        if missing:
            raise ValueError(f"{row.get('id')}: missing {sorted(missing)}")
        if row["id"] in canonical_ids:
            raise ValueError(f"duplicate paper: {row['id']}")
        canonical_ids.add(row["id"])
        for alias in [row["id"], *row["aliases"]]:
            if alias in seen and seen[alias] != row["id"]:
                raise ValueError(f"duplicate identifier: {alias}")
            seen[alias] = row["id"]
        if row["relevance"]["level"] not in {"A", "B", "C"}:
            raise ValueError(f"{row['id']}: invalid relevance")
        if not row["evidence"] or not row["authors"] or not row["versions"]:
            raise ValueError(f"{row['id']}: empty evidence/authors/versions")
        if not (ROOT / row["local_dir"] / "paper.md").is_file():
            raise ValueError(f"{row['id']}: missing local full text")
        for evidence in row["evidence"]:
            if "local_path" in evidence and not (ROOT / evidence["local_path"]).is_file():
                raise ValueError(f"{row['id']}: missing evidence file")
    ids = {row["id"] for row in papers}
    relations = read_jsonl("relations.jsonl")
    keys = set()
    for edge in relations:
        if not {"source", "target", "type", "basis", "evidence"} <= edge.keys():
            raise ValueError("relation missing required field")
        if edge["source"] not in ids or edge["target"] not in ids:
            raise ValueError(f"relation endpoint not indexed: {edge}")
        key = (edge["source"], edge["target"], edge["type"])
        if key in keys or not edge["evidence"]:
            raise ValueError(f"duplicate or unsupported relation: {key}")
        keys.add(key)
    print(f"OK: {len(papers)} papers, {len(relations)} relations; identifiers and local evidence paths checked.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["search", "validate"])
    parser.add_argument("query", nargs="?", default="")
    parser.add_argument("--level", choices=["A", "B", "C"])
    parser.add_argument("--json", action="store_true", help="Print complete JSONL records")
    args = parser.parse_args()
    try:
        papers = read_jsonl("papers.jsonl")
        if args.command == "validate":
            validate(papers)
            return
        matches = 0
        for row in papers:
            if args.level and row["relevance"]["level"] != args.level:
                continue
            if args.query.casefold() not in json.dumps(row, ensure_ascii=False).casefold():
                continue
            matches += 1
            if args.json:
                print(json.dumps(row, ensure_ascii=False))
            else:
                print(f"{row['id']}\t{row['year']}\t{row['relevance']['level']}\t{row['title']}")
        if not matches:
            print("No matches.", file=sys.stderr)
    except (ValueError, OSError) as error:
        parser.exit(1, f"Error: {error}\n")


if __name__ == "__main__":
    main()
