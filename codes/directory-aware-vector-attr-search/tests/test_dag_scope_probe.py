"""Regression test for the DAG scope-resolution probe.

Builds the probe with the system compiler, runs it on a tiny DAG fixture and
checks that the three strategies agree, that ancestor-propagated maintenance is
used, and that the selective materialisation rule respects its budget.
"""
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src' / 'dag_scope_probe.cpp'
COMPILER = shutil.which('g++') or shutil.which('clang++')


@unittest.skipIf(COMPILER is None, 'no C++ compiler available')
class DagScopeProbeReplay(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.tmp.name) / 'probe'
        subprocess.run([COMPILER, '-O1', '-std=c++17', '-o', str(cls.binary), str(SOURCE)],
                       check=True, capture_output=True)
        data = Path(cls.tmp.name) / 'data'
        data.mkdir()
        # 0 root, 1 and 2 children of the root, 3 a shared child of both (multi-parent).
        (data / 'edges.tsv').write_text(
            '1\t0\n2\t0\n3\t1\n3\t2\n4\t1\n'
        )
        (data / 'objects.txt').write_text('3\n3\n1\n2\n4\n3\n1\n')
        (data / 'queries.tsv').write_text('1\t0\n1\t1\n1\t2\n1\t3\n2\t1\t2\n')
        cls.data = data

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_probe(self, *extra):
        out = Path(self.tmp.name) / 'out'
        completed = subprocess.run(
            [str(self.binary), '--data', str(self.data), '--out', str(out), *extra],
            check=True, capture_output=True, text=True)
        return json.loads((out / 'probe.json').read_text()), completed.stdout

    def test_strategies_agree_and_updates_propagate(self):
        report, stdout = self.run_probe('--budget-mb', '1', '--relabels', '3')
        self.assertEqual(report['objects'], 7)
        self.assertEqual(report['concepts'], 5)
        self.assertEqual(report['queries'], 5)
        for name in ('online', 'mat_all', 'mat_select'):
            self.assertIn(name, report['query_us'])
        # A multi-parent concept gives concept 1 and 2 a shared scope member.
        self.assertGreaterEqual(report['scope_size']['max'], 4)
        self.assertIn('every strategy matched the reference bitmap', report['equivalence'])
        self.assertGreater(report['update_us']['rebuild_select']['mean'],
                           report['update_us']['mat_select']['mean'])

    def test_budget_bounds_selective_materialisation(self):
        report, _ = self.run_probe('--budget-mb', '0', '--relabels', '1')
        self.assertEqual(report['materialised_concepts']['select'], 0)
        self.assertEqual(report['materialised_bytes']['select'], 0)
        # Without materialised bitmaps the selective path must still resolve scopes.
        self.assertIn('mat_select', report['query_us'])


if __name__ == '__main__':
    unittest.main()
