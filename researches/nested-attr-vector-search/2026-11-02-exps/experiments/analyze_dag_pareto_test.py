#!/usr/bin/env python3
import unittest
from analyze_dag_pareto import select_at_reference


def point(n, recall, qps, k=10, ef=32):
    return {'k': k, 'ef_requested': ef, 'ef_effective': max(k, ef),
            'n': n, 'recall_mean': recall, 'qps': qps}


class FixedCaseComparison(unittest.TestCase):
    def test_fastest_qualifying_not_highest_recall_or_fastest_invalid(self):
        reference = point(0, .95, 100)
        chosen = select_at_reference([point(1, .94, 500), point(2, .95, 200), point(3, 1., 120)], reference)
        self.assertEqual(chosen['point']['n'], 2)
        self.assertEqual(chosen['qps_ratio_to_reference'], 2)
        self.assertEqual(chosen['recall_overshoot'], 0)

    def test_unmet_recall_is_missing_not_zero_qps(self):
        chosen = select_at_reference([point(1, .90, 500), point(2, .94, 300)], point(0, .95, 100))
        self.assertEqual(chosen['status'], 'recall_target_unmet')
        self.assertIsNone(chosen['point'])
        self.assertIsNone(chosen['qps_ratio_to_reference'])
        self.assertEqual(chosen['max_observed_recall'], .94)

    def test_different_ef_or_k_cannot_supply_a_faster_point(self):
        for other in [point(1, 1., 500, ef=64), point(1, 1., 500, k=50)]:
            with self.assertRaises(ValueError):
                select_at_reference([other], point(0, .95, 100))

    def test_overshoot_is_exposed_not_interpolated(self):
        chosen = select_at_reference([point(1, .90, 500), point(2, .97, 200)], point(0, .95, 100))
        self.assertEqual(chosen['point']['recall_mean'], .97)
        self.assertAlmostEqual(chosen['recall_overshoot'], .02)
        self.assertEqual(chosen['point']['qps'], 200)


if __name__ == '__main__':
    unittest.main()
