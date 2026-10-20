# Section Blueprint: index.md (research summary)

Target: `researches/nested-attr-vector-search/07-laminar-edge-sharing/index.md`.

Purpose: a clear, academic-style description of the current research state (problem, method, measured results, limitations), with navigation to the detailed experiment logs and baselines.

## Sections (reader-facing)

1. **Problem** — Exact/Subtree directory semantics over hierarchical labels; problem definition (D1–D9); why existing solutions pay expansion/difference/materialization/predicate costs.
2. **Method — Laminar Edge Sharing (LES)** — minimal-activation bucketing, two-segment buckets (B_exact / B_cross), preorder interval slicing, per-scope upper levels, correctness lemmas, space/degree bounds.
3. **Measured Results** — main tables: B4 per-scope materialization vs B0/B1/B2/Filtered-HNSW/ACORN vs LES prototype; exact vs subtree workload; space and degree analysis.
4. **Limitations** — v1 shares only level-0; exact is node-set (needs postings, not edges); H1 unverified; dual-semantic evaluation incomplete; single-thread static.
5. **Navigation** — links to daily logs, experiment records, benchmark protocol, baseline adaptation study, reproduction records.

## Evidence hooks (verified local artifacts)

- `2026-10-20-exps/report.md` (main experiment report)
- `2026-10-20-exps/data/primary-progression.csv`, `ccs-results.csv`, `ccs-les.csv`, `ccs-baselines.csv`, `les-analysis.txt`, `ccs-stats.json`
- `2026-10-20-exps/experiments/{faiss-build, pathseer-filtered-ann, les-prototype, exact-gap, directory-aware-baseline, directory-aware-reproduction}.md`
- `baseline-adaptation.md`, `benchmark-protocol.md`
- `production/nested-attr-vector-search/src/` code paths

## Style constraints

- Chinese body, keep English technical terms.
- Numeric citations `[1]` if references are added later; for now cite local artifacts by file path, not formal references.
- No invented numbers; use only measured values from the listed CSV/log artifacts.
- No SOTA claims without the matching baseline table.
