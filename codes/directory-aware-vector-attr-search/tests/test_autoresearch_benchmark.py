"""Quality gates must reject invalid consumer-visible retrieval results."""
import copy
import importlib.util
from pathlib import Path
import unittest

import numpy as np

SPEC = importlib.util.spec_from_file_location(
    "benchmark", Path(__file__).resolve().parents[1] / "scripts/autoresearch_benchmark.py")
BENCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BENCH)


class RetrievalGateTest(unittest.TestCase):
    def setUp(self):
        self.scopes = (np.array([1, 1, 2]), np.array([0, 1, 2, 3]),
                       np.array([4, 2, 3, 4]), np.array([[0, 1, 0], [1, 0, 1], [2, 0, 3]]))
        self.reference = [
            {"query_index": 0, "recursive": True, "anchor": 0, "scope_card": 3, "gt_ids": [0, 1, 2]},
            {"query_index": 1, "recursive": False, "anchor": 1, "scope_card": 2, "gt_ids": [0, 1]},
            {"query_index": 2, "recursive": False, "anchor": 3, "scope_card": 0, "gt_ids": []},
        ]
        self.rows = []
        for ref in self.reference:
            ids = ref["gt_ids"]
            self.rows.append({"query_index": str(ref["query_index"]), "dataset": "wiki", "design": "NODE",
                              "query_type": "recursive" if ref["recursive"] else "non-recursive",
                              "scope_card": str(ref["scope_card"]), "result_ids": " ".join(map(str, ids + [-1] * (10 - len(ids)))),
                              "gt_count": str(len(ids)), "hit_count": str(len(ids)), "valid_return_count": str(len(ids)),
                              "e2e_ms": "0.005", "dir_us": "1", "search_us": "4"})

    def test_complete_small_and_empty_scopes(self):
        result = BENCH.validate_rows(self.rows, self.reference, self.scopes)
        self.assertEqual(result["recursive_pair_recall"], 1)
        self.assertEqual(result["nonrecursive_macro_recall"], 1)

    def test_rejects_duplicate_results(self):
        self.rows[0]["result_ids"] = "0 0 2 " + " ".join(["-1"] * 7)
        with self.assertRaisesRegex(ValueError, "duplicate returned"):
            BENCH.validate_rows(self.rows, self.reference, self.scopes)

    def test_rejects_out_of_scope_result(self):
        self.rows[1]["result_ids"] = "0 2 " + " ".join(["-1"] * 8)
        with self.assertRaisesRegex(ValueError, "out-of-scope"):
            BENCH.validate_rows(self.rows, self.reference, self.scopes)

    def test_rejects_optimistic_reported_hits(self):
        self.rows[0]["result_ids"] = "0 1 " + " ".join(["-1"] * 8)
        with self.assertRaisesRegex(ValueError, "independent GT"):
            BENCH.validate_rows(self.rows, self.reference, self.scopes)

    def test_rejects_duplicate_query_rows(self):
        self.rows[2] = copy.deepcopy(self.rows[1])
        with self.assertRaisesRegex(ValueError, "duplicate/missing"):
            BENCH.validate_rows(self.rows, self.reference, self.scopes)


if __name__ == "__main__":
    unittest.main()
