# Venue Brief

| Field | Value | Source |
|---|---|---|
| Venue family | SIGMOD / PACMMOD / VLDB / ICDE | user selection |
| Language | Chinese (正文中文，保留英文术语) | user selection |
| min_citations | auto-infer → target ~40 unique references | inferred from local exemplars |
| Citation format | numeric `[1]`, `[2-4]` | local PACMMOD/VLDB exemplars (RWalks, PathSeer, SimJoin) |
| Structure | Abstract → Introduction → Related Work → Method → Experimental Setup → Results → Discussion → Conclusion | local PACMMOD/VLDB exemplars |
| Local style library | `papers/filtered-ann/` (ACORN, Filtered-DiskANN, LSSG, Curator, PathSeer, RWalks, ELI), `papers/vector-join/`, `papers/prefix-vector-search/` | local full-text library |
| Local reference library | `papers/papers.jsonl` (12 filtered-ann + vector-join + prefix-vector-search papers) | local |

## Style observations from local exemplars

- PACMMOD/SIGMOD systems papers (RWalks, PathSeer, Curator, iRangeGraph): strong experimental protocol (datasets, baselines, recall-latency curves, index size/build time), numeric citations, ~50–80 unique refs.
- VLDB (ELI): label-hybrid AKNN with formal problem definition, index-selection framework, evaluation on real and synthetic label sets.
- Database venues emphasize: exact problem definition, baselines with open code, recall/QPS curves, index space and build cost, single-thread or matched-thread fairness.

## min_citations inference

Local filtered-ann and vector-join exemplars carry 51–81 unique numeric references. A focused systems paper on a single index structure with strong baselines typically needs ~40 references: 8–10 foundational ANN, 8–10 filtered/hybrid ANN, 5–8 hierarchical/directory papers, 5–8 systems/baselines, 5–8 datasets/protocols. Target: ≥ 40 unique verified references.

## Known local PDF library

- `papers/filtered-ann/`: 12 papers (ACORN, Filtered-DiskANN, LSSG, Curator, PathSeer, RWalks, ELI, iRangeGraph, VADER, Grant, Tag-filtered, Graph Range Filters).
- `papers/prefix-vector-search/`: Directory-Aware/TrieHI.
- `papers/vector-join/`: SimJoin and related.
- These are full text and can be cited with verified metadata from `papers.jsonl` and `paper.report.md`.

## Venue requirements (inferred, must be confirmed at submission)

- PACMMOD: numeric citations, two-column acmart format; page limit typically 12–18 for full research.
- VLDB: numeric citations, vldb format; page limit typically 12–14.
- ICDE: numeric citations, IEEE conference format.
- These are inferred from local exemplars, not from an official current author guide; must be reconfirmed before submission.
