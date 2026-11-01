"""Replay single-label updates and compare every reported scope to a Python oracle.

The fixture exercises overlapping DAG scopes, removal from a multi-parent label,
shared ancestors, internal/root destinations and initially empty concepts. Timings
are deliberately not assertions.
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
PARENTS = {0: (), 1: (0,), 2: (0,), 3: (1, 2), 4: (1,), 5: (2,)}


@unittest.skipIf(COMPILER is None, 'no C++ compiler available')
class DagScopeProbeReplay(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.tmp.name) / 'probe'
        subprocess.run([COMPILER, '-O1', '-std=c++17', '-o', str(cls.binary), str(SOURCE)],
                       check=True, capture_output=True)
        cls.data = Path(cls.tmp.name) / 'data'
        cls.data.mkdir()
        # 3 has two parents; 5 is initially empty. Labels 1 and 2 are internal.
        (cls.data / 'edges.tsv').write_text(
            '1\t0\n2\t0\n3\t1\n3\t2\n4\t1\n5\t2\n'
        )
        (cls.data / 'objects.txt').write_text('3\n3\n1\n2\n4\n3\n1\n')
        (cls.data / 'queries.tsv').write_text('1\t0\n1\t1\n1\t2\n1\t3\n2\t1\t2\n')

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_probe(self, *extra, operations=None):
        with tempfile.TemporaryDirectory(dir=self.tmp.name) as run_dir:
            out = Path(run_dir) / 'out'
            command = [str(self.binary), '--data', str(self.data), '--out', str(out),
                       '--dump-scopes', *extra]
            if operations is not None:
                replay = Path(run_dir) / 'replay.tsv'
                replay.write_text(''.join(f'{obj}\t{label}\n' for obj, label in operations))
                command += ['--replay', str(replay)]
            subprocess.run(command, check=True, capture_output=True, text=True)
            return json.loads((out / 'probe.json').read_text())

    def assert_scopes(self, report, labels):
        expected = {concept: [] for concept in PARENTS}
        # Independent upward traversal of the fixed raw parent relation, without
        # using the probe's cached closures or reported counters/bitmaps.
        for obj, label in enumerate(labels):
            seen = set()
            pending = [label]
            while pending:
                concept = pending.pop()
                if concept in seen:
                    continue
                seen.add(concept)
                pending.extend(PARENTS[concept])
            for concept in seen:
                expected[concept].append(obj)
        state = report['replay_state']
        self.assertEqual(state['final_labels'], labels)
        for name in ('online', 'mat_all', 'mat_select'):
            with self.subTest(strategy=name):
                strategy = state['strategies'][name]
                self.assertEqual(strategy['labels'], labels)
                self.assertEqual([scope['concept'] for scope in strategy['scopes']],
                                 list(PARENTS))
                for scope in strategy['scopes']:
                    members = expected[scope['concept']]
                    self.assertEqual(scope['members'], members)
                    self.assertEqual(scope['cardinality'], len(members))
        # All objects remain under the shared root, counted once per object.
        self.assertEqual(state['strategies']['mat_all']['scopes'][0]['cardinality'], 7)

    def fixture_replay(self):
        initial = self.run_probe('--relabels', '0')['replay_state']['initial_labels']
        shared = [obj for obj, label in enumerate(initial) if label == 3]
        a, b, c = shared
        branch = initial.index(4)
        operations = [
            (a, 1),       # remove only a from shared posting, retaining b and c
            (b, 2),       # retain shared ancestor 2 while losing ancestor 1
            (c, 0),       # empty multi-parent scope; root retains all objects
            (branch, 2),  # empty a separate source scope
            (a, 0),       # move an internal label to the root
            (a, 3),       # repopulate the emptied materialised shared scope
            (b, 5),       # populate an initially empty, unmaterialised concept
            (b, 1),       # remove from it and switch ancestor branches
            (c, 5),
            (c, 3),       # remove from emptyable scope into both parent scopes
        ]
        return initial, operations

    def test_every_replay_prefix_matches_exact_membership(self):
        initial, operations = self.fixture_replay()
        labels = initial.copy()
        for length in range(len(operations) + 1):
            if length:
                obj, label = operations[length - 1]
                labels[obj] = label
            with self.subTest(prefix=length):
                report = self.run_probe('--budget-mb', '1', '--verify-batch', '1',
                                        operations=operations[:length])
                self.assert_scopes(report, labels)
                self.assertEqual(report['relabel_events'], length)
                self.assertEqual(report['verification']['checkpoints'], length + 1)
                self.assertEqual(report['verification']['mismatches'], 0)

    def test_zero_budget_and_partial_final_batch(self):
        initial, operations = self.fixture_replay()
        expected = initial.copy()
        for obj, label in operations:
            expected[obj] = label
        report = self.run_probe('--budget-mb', '0', '--verify-batch', '3',
                                operations=operations)
        self.assert_scopes(report, expected)
        self.assertEqual(report['verification']['checkpoints'], 5)
        self.assertEqual(report['materialised_bytes']['select'], 0)
        for scope in report['replay_state']['strategies']['mat_select']['scopes']:
            self.assertFalse(scope['materialised'])

    def test_same_label_replay_preserves_shared_membership(self):
        initial, _ = self.fixture_replay()
        obj = initial.index(3)
        report = self.run_probe('--verify-batch', '1',
                                operations=[(obj, 3), (obj, 3)])
        self.assert_scopes(report, initial)
        self.assertEqual(report['relabel_events'], 2)

    def test_invalid_replay_exits_nonzero_without_report(self):
        with tempfile.TemporaryDirectory(dir=self.tmp.name) as run_dir:
            replay = Path(run_dir) / 'bad.tsv'
            replay.write_text('0\t6\n')  # no concept 6 in this graph
            out = Path(run_dir) / 'out'
            completed = subprocess.run(
                [str(self.binary), '--data', str(self.data), '--out', str(out),
                 '--replay', str(replay)], capture_output=True, text=True)
            self.assertNotEqual(completed.returncode, 0)
            self.assertFalse((out / 'probe.json').exists())


if __name__ == '__main__':
    unittest.main()
