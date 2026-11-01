"""Consumer-visible DAG scope ANN regression; no latency/performance assertions."""
import csv
import json
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMPILER = shutil.which('g++') or shutil.which('clang++')
STRATEGIES = ('online', 'mat_all', 'mat_select', 'partition_top')


def write_fvecs(path, values):
    with path.open('wb') as handle:
        for value in values:
            handle.write(struct.pack('<if', 1, value))


def ids(row, key='result_ids'):
    return set(map(int, row[key].split(';'))) if row[key] else set()


@unittest.skipIf(COMPILER is None, 'no C++ compiler available')
class DagScopeAnnReplay(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.path = Path(cls.tmp.name)
        cls.binary = cls.path / 'ann'
        subprocess.run([
            COMPILER, '-O1', '-std=c++17', '-I' + str(ROOT / 'src'),
            '-I' + str(ROOT / 'deps' / 'hnswlib'),
            str(ROOT / 'src' / 'dag_scope_ann.cpp'), '-lpthread', '-o', str(cls.binary),
        ], check=True, capture_output=True)
        cls.data = cls.path / 'data'
        cls.data.mkdir()
        # Shared child 3; 7 has a shortcut from root AND a longer path through 6.
        # Thus depth(7)=1, yet it has ancestor 3 at cut depth 2: its direct label
        # must retain its own above-cut key, not get assigned to ancestor 3.
        (cls.data / 'edges.tsv').write_text(
            '1\t0\n2\t0\n3\t1\n3\t2\n4\t1\n5\t0\n'
            '6\t3\n7\t0\n7\t6\n8\t2\n')
        (cls.data / 'objects.txt').write_text('0\n1\n2\n3\n3\n4\n6\n7\n')
        (cls.data / 'queries.tsv').write_text(
            '1\t0\n1\t1\n1\t2\n1\t3\n2\t1\t2\n'
            '1\t5\n1\t7\n1\t4\n2\t0\t1\n1\t8\n')
        cls.vectors = cls.path / 'base.fvecs'
        cls.queries = cls.path / 'query.fvecs'
        write_fvecs(cls.vectors, range(8))
        write_fvecs(cls.queries, [0.125 + i * 0.25 for i in range(10)])
        cls.counter = 0

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def command(self, *extra):
        type(self).counter += 1
        out = self.path / ('out-' + str(self.counter))
        command = [str(self.binary), '--data', str(self.data), '--out', str(out),
                   '--vectors', str(self.vectors), '--queries', str(self.queries),
                   '--partition', 'top', '--partition-depth', '2',
                   '--max-queries', '10', '--k', '3', '--ef', '20', *extra]
        return command, out

    def run_ann(self, *extra):
        command, out = self.command(*extra)
        subprocess.run(command, check=True, capture_output=True, text=True)
        report = json.loads((out / 'ann.json').read_text())
        with (out / report['query_csv']).open() as handle:
            rows = list(csv.DictReader(handle))
        return report, rows

    def test_exact_multiparent_union_empty_and_above_cut(self):
        report, rows = self.run_ann('--k', '20', '--ef', '20', '--tau', '100', '--repeats', '3')
        expected = [8, 6, 5, 4, 7, 0, 1, 1, 8, 0]
        self.assertEqual(report['partition_depth'], 2)
        self.assertEqual(report['partition_slots'], 8)
        self.assertEqual(report['nonempty_partitions'], 6)
        self.assertEqual(report['allocated_partition_graphs'], 6)
        self.assertEqual(report['queries'], 10)
        self.assertEqual(report['pass_queries'], 30)
        self.assertEqual(len(rows), 120)
        for row in rows:
            scope = int(row['scope_row'])
            self.assertEqual(int(row['vector_row']), scope)
            self.assertEqual(int(row['eligible_count']), expected[scope])
            self.assertEqual(int(row['denominator']), expected[scope])
            self.assertEqual(len(ids(row)), expected[scope])
            self.assertEqual(ids(row), ids(row, 'truth_ids'))
            self.assertEqual(int(row['hits']), expected[scope])
            self.assertEqual(int(row['shortage']), 0)
            self.assertEqual(row['path'], 'exact' if expected[scope] else 'empty')
        by_scope = {int(row['scope_row']): row for row in rows
                    if row['strategy'] == 'partition_top' and row['pass'] == '0'}
        self.assertEqual(int(by_scope[0]['touched_partitions']), 6)
        self.assertEqual(ids(by_scope[4]), ids(by_scope[1]) | ids(by_scope[2]))
        self.assertGreater(len(ids(by_scope[1]) & ids(by_scope[2])), 0)
        self.assertEqual(ids(by_scope[8]), ids(by_scope[0]))
        for name in STRATEGIES:
            stats = report['strategies'][name]
            self.assertEqual(stats['empty_queries'], 6)
            self.assertEqual(stats['nonempty_queries'], 24)
            self.assertEqual(stats['nonempty_macro_recall'], 1)
            self.assertEqual(stats['pair_recall'], 1)
            self.assertEqual(stats['recall_denominator'], sum(expected) * 3)
        for summary in report['passes']:
            for stats in summary['strategies'].values():
                self.assertEqual(stats['empty_queries'], 2)
                self.assertEqual(stats['nonempty_queries'], 8)

    def test_graph_paths_and_four_arm_rotation(self):
        report, rows = self.run_ann('--tau', '1', '--repeats', '3')
        for row in rows:
            eligible = int(row['eligible_count'])
            self.assertEqual(row['path'], 'empty' if not eligible else 'exact' if eligible <= 1 else 'graph')
            self.assertEqual(int(row['denominator']), min(3, eligible))
            self.assertEqual(int(row['hits']), min(3, eligible))
            self.assertEqual(int(row['shortage']), 0)
            self.assertEqual(len(ids(row)), int(row['result_count']))
            scope = int(row['scope_row'])
            strategy = STRATEGIES.index(row['strategy'])
            self.assertEqual(strategy, (scope + int(row['pass']) + int(row['order'])) % 4)
        for name in STRATEGIES:
            self.assertEqual(report['strategies'][name]['graph_queries'], 18)
            self.assertEqual(report['strategies'][name]['exact_queries'], 6)
        # Partition is genuinely first in some measured queries, not always last.
        self.assertTrue(any(row['strategy'] == 'partition_top' and row['order'] == '0' for row in rows))

    def test_sampling_is_stable_across_cuts_and_limits(self):
        args = ('--sample-queries', '8', '--seed', '20261101', '--max-queries', '4', '--repeats', '3')
        first, first_rows = self.run_ann(*args)
        second, second_rows = self.run_ann(*args, '--partition-depth', '1', '--ef', '30')
        self.assertEqual(first['selected_scope_rows'], second['selected_scope_rows'])
        self.assertEqual(len(set(first['selected_scope_rows'])), 4)
        self.assertEqual(first['queries'], 4)
        self.assertEqual(first['pass_queries'], 12)
        keys = ('scope_row', 'vector_row', 'pass', 'order', 'strategy')
        self.assertEqual([[row[key] for key in keys] for row in first_rows],
                         [[row[key] for key in keys] for row in second_rows])
        full, _ = self.run_ann('--sample-queries', '8', '--seed', '20261101')
        self.assertEqual(first['selected_scope_rows'], full['selected_scope_rows'][:4])

    def test_empty_only_workload_has_no_recall_denominator(self):
        empty = self.path / 'empty-data'
        empty.mkdir(exist_ok=True)
        for name in ('edges.tsv', 'objects.txt'):
            (empty / name).write_text((self.data / name).read_text())
        (empty / 'queries.tsv').write_text('1\t5\n1\t8\n')
        report, rows = self.run_ann('--data', str(empty))
        self.assertEqual(len(rows), 8)
        for stats in report['strategies'].values():
            self.assertEqual(stats['empty_queries'], 2)
            self.assertEqual(stats['nonempty_queries'], 0)
            self.assertEqual(stats['recall_denominator'], 0)
            self.assertIsNone(stats['nonempty_macro_recall'])
            self.assertIsNone(stats['pair_recall'])

    def test_rejects_impossible_options_and_missing_vector_rows(self):
        for options in (('--k', '0'), ('--ef', '0'), ('--repeats', '0'),
                        ('--partition', 'other'), ('--k', '21', '--ef', '20')):
            with self.subTest(options=options):
                command, _ = self.command(*options)
                result = subprocess.run(command, capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
        short = self.path / 'short.fvecs'
        write_fvecs(short, [0.125])
        command, _ = self.command('--queries', str(short))
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('no corresponding vector query row', result.stderr)


if __name__ == '__main__':
    unittest.main()
