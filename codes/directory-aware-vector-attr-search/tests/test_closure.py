"""Actual CLI regressions: oracle interoperability, dynamic state and phase gates.

Build both C++ executables first. Small synthetic vectors are correctness inputs,
never performance evidence. No Python ANN or timed Python/C++ comparison.
"""
import csv
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'build/closure_research'
BASELINE = ROOT / 'build/dynamic_triad'
DRIVER = ROOT / 'scripts/run_closure.py'


def fixture(root):
    data = root / 'data'
    data.mkdir()
    n, dim, nq = 80, 4, 6
    parent = [-1, 0, 0, 1, 1, 2, 2, 0]
    (data / 'meta.txt').write_text(f'{n} {dim} {nq}\n')
    (data / 'base.f32').write_bytes(b''.join(struct.pack('<4f', i, i % 7, i % 11, i % 3) for i in range(n)))
    (data / 'test.f32').write_bytes(b''.join(struct.pack('<4f', i * 11 + .25, 2, 3, 1) for i in range(nq)))
    (data / 'tree.txt').write_text('8\n' + ''.join(f'{i} {p}\n' for i, p in enumerate(parent)))
    (data / 'bindings.txt').write_text(''.join(f'{i % 8}\n' for i in range(n)))
    (data / 'attrs.txt').write_text(''.join(f'{i % 3} {i - 40} {i % 8} {int(i < 76 and i != 21)}\n' for i in range(n)))
    events = []
    def queries():
        for qidx in range(nq):
            events.append(f'Q {qidx} 1 0 -1 -1 -1 0 0 0')
            events.append(f'Q {qidx} 1 1 -1 -20 30 1 1 0')
            events.append(f'Q {qidx} 0 2 -1 -1 -1 2 0 0')  # Explicit empty tag intersection.
    queries()
    for event in ('V 0 -2 -1', 'T 1 18446744073709551615', 'M 0 4', 'B 3 2',
                  'R 1 6', 'R 1 0', 'D 1', 'I 1 3 -2 -1 3', 'I 79 7 1 8 7'):
        events.append(event)
        queries()
    trace = root / 'mixed.trace'
    trace.write_text(str(len(events)) + '\n' + '\n'.join(events) + '\n')
    return data, trace


def load_rows(path):
    with path.open(newline='') as handle:
        return list(csv.DictReader(handle))


class ClosureReplay(unittest.TestCase):
    def run_ok(self, command):
        result = subprocess.run(list(map(str, command)), capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def test_actual_core_semantics(self):
        self.run_ok([BINARY, '--self-check'])

    def test_oracle_is_usable_by_both_replays_and_rejects_stale_gt(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            data, trace = fixture(root)
            oracle = root / 'oracle'
            self.run_ok([BINARY, '--data', data, '--trace', trace, '--out', oracle, '--oracle-only'])
            tape = Path(str(oracle) + '.queries.csv')
            gt = load_rows(tape)
            # Independent exact first query: all coordinates and squared distances
            # are small/exactly representable; ties explicitly use external IDs.
            alive = [i for i in range(76) if i != 21]
            expected = sorted(alive, key=lambda i: ((i - .25)**2 + (i % 7 - 2)**2 +
                              (i % 11 - 3)**2 + (i % 3 - 1)**2, i))[:10]
            self.assertEqual(gt[0]['gt_ids'], ';'.join(map(str, expected)))
            for arm, binary in [('A', BASELINE), ('U', BINARY), ('G', BINARY),
                                ('R', BINARY), ('H', BINARY), ('H0', BINARY)]:
                prefix = root / arm
                self.run_ok([binary, '--data', data, '--trace', trace, '--out', prefix,
                             '--arm', arm, '--ef', 128, '--gt-in', tape])
                actual = load_rows(Path(str(prefix) + '.queries.csv'))
                self.assertEqual(len(actual), len(gt))
                for row, truth in zip(actual, gt):
                    self.assertEqual(row['result_ids'], truth['gt_ids'])
                    self.assertEqual(int(row['gt_distance_calls']), 0)
                    self.assertEqual(int(row['invalid_count']), 0)
                    self.assertEqual(int(row['duplicate_count']), 0)
                updates = load_rows(Path(str(prefix) + '.updates.csv'))
                self.assertEqual([r['op'] for r in updates], list('VTMBRRDII'))
                if arm != 'A':
                    for row in updates:
                        if row['op'] in 'VTMBR':
                            self.assertEqual(int(row['geometry_calls']), 0)
            audit = root / 'audit'
            self.run_ok([BINARY, '--data', data, '--trace', trace, '--out', audit,
                         '--audit-only', '--audit-count', 4])
            checked = load_rows(Path(str(audit) + '.audit.csv'))
            self.assertTrue(any(int(r['H_vs_R_diff']) for r in checked))
            self.assertTrue(any(int(r['heredity_applicable']) for r in checked))
            self.assertFalse(any(int(r['heredity_applicable']) and int(r['heredity_mismatch']) for r in checked))
            # Same counts, stale/dead ID: reject instead of silently accepting a tape.
            bad_tape = root / 'stale.csv'
            with tape.open(newline='') as source:
                reader = csv.DictReader(source)
                fieldnames, bad_rows = reader.fieldnames, list(reader)
            bad_rows[0]['gt_ids'] = '79;' + ';'.join(bad_rows[0]['gt_ids'].split(';')[1:])
            with bad_tape.open('w', newline='') as target:
                writer = csv.DictWriter(target, fieldnames=fieldnames)
                writer.writeheader(); writer.writerows(bad_rows)
            failed = root / 'failed'
            process = subprocess.run(list(map(str, [BINARY, '--data', data, '--trace', trace,
                '--out', failed, '--arm', 'H', '--ef', 128, '--gt-in', bad_tape])), capture_output=True)
            self.assertNotEqual(process.returncode, 0)
            self.assertIs(json.loads(Path(str(failed) + '.summary.json').read_text())['complete'], False)

    def test_frozen_pipeline_and_smoke_gate_cannot_authorize_real_dynamic(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            data, trace = fixture(root)
            common = [sys.executable, DRIVER, '--dataset', f'toy={data}', '--trace', f'toy={trace}',
                      '--max-queries', 180, '--efs', 128, '--repeats', 1]
            static = root / 'static'
            self.run_ok(common + ['--out', static, '--smoke'])
            selected = json.loads((static / 'toy/result.json').read_text())
            self.assertFalse(selected['dynamic_admitted'])
            cal = {r['qidx'] for r in load_rows(static / 'toy/calibration-oracle.queries.csv')}
            confirm = {r['qidx'] for r in load_rows(static / 'toy/confirmation-oracle.queries.csv')}
            self.assertFalse(cal & confirm)
            dynamic = root / 'dynamic-smoke'
            self.run_ok(common + ['--out', dynamic, '--stage', 'dynamic', '--static-results', static, '--smoke'])
            result = json.loads((dynamic / 'toy/result.json').read_text())
            self.assertEqual(result['event_counts'], {'Q': 180, 'V': 1, 'T': 1, 'M': 1, 'B': 1, 'R': 2, 'D': 1, 'I': 2})
            for run in result['runs']:
                self.assertEqual(run['metrics']['quality']['macro'], 1)
                self.assertEqual(run['metrics']['quality']['pair'], 1)
            probe = result['maintenance_probe']
            self.assertFalse(probe['metadata']['topology_unavailable'])
            for run in probe['runs']:
                self.assertEqual(run['metrics']['quality']['macro'], 1)
                self.assertEqual(run['metrics']['quality']['pair'], 1)
                if run['arm'] == 'H':
                    self.assertGreaterEqual(run['summary']['compaction_count'], 1)
                    self.assertGreater(run['summary']['update_geometry_calls'], 0)
                    updates = load_rows(Path(run['prefix'] + '.updates.csv'))
                    self.assertTrue({'R', 'B', 'M', 'D', 'I'}.issubset({r['op'] for r in updates}))
                    for row in updates:
                        if row['op'] in 'RBM':
                            self.assertEqual(int(row['geometry_calls']), 0)
            forbidden = root / 'forbidden'
            process = subprocess.run(list(map(str, common + ['--out', forbidden, '--stage', 'dynamic',
                                     '--static-results', static])), capture_output=True)
            self.assertNotEqual(process.returncode, 0)
            self.assertFalse(json.loads((forbidden / 'manifest.json').read_text())['complete'])


if __name__ == '__main__':
    unittest.main()
