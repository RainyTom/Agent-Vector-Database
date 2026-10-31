"""Consumer-visible eligibility and routing regressions against canonical C++ GT.

Actual binaries must be built first. Synthetic inputs prove semantics, not speed.
"""
import csv
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from test_closure import fixture, BINARY, BASELINE, load_rows


def run(*args):
    return subprocess.run(list(map(str, args)), check=True, capture_output=True, text=True)


class EligibilityReplay(unittest.TestCase):
    def test_exact_masks_survive_all_events_and_predicate_edges(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            data, trace = fixture(root)
            original = trace.read_text().splitlines()[1:]
            # Signed extremes, disabled-vs-literal -1, empty tag masks, AND/OR,
            # direct internal-node ownership and words crossing ID63/64.
            edges = [
                'Q 0 1 0 -1 -9223372036854775808 9223372036854775807 0 0 0',
                'Q 1 0 0 -1 -1 0 0 0 0',
                'Q 2 1 1 -2 -1 -1 1 0 1',
                'Q 3 1 0 -1 -1 -1 2 0 0',
                'Q 4 1 0 -1 -1 -1 3 0 0',
                'Q 5 1 0 -1 -1 -1 4 0 0',
                'Q 0 1 0 -2 -9223372036854775808 -1 3 18446744073709551615 1',
            ]
            events = ['V 64 -2 -9223372036854775808',
                      'V 65 -2 9223372036854775807', *edges]
            for e in original:
                events.append(e)
                if not e.startswith('Q '):
                    events.extend(edges)
            trace.write_text(str(len(events)) + '\n' + '\n'.join(events) + '\n')
            oracle = root / 'oracle'
            run(BINARY, '--data', data, '--trace', trace, '--out', oracle, '--oracle-only')
            tape = Path(str(oracle) + '.queries.csv')
            for binary, arm in ((BASELINE, 'A'), (BINARY, 'U'), (BINARY, 'H')):
                prefix = root / arm
                run(binary, '--data', data, '--trace', trace, '--out', prefix,
                    '--arm', arm, '--ef', 15, '--gt-in', tape,
                    '--eligibility-policy', 'hybrid', '--eligible-scan-threshold', 512)
                for actual, truth in zip(load_rows(Path(str(prefix) + '.queries.csv')), load_rows(tape)):
                    self.assertEqual(actual['result_ids'], truth['gt_ids'])
                    self.assertEqual(int(actual['distance_calls']), int(actual['eligible_card']))
                    self.assertEqual(int(actual['gt_distance_calls']), 0)
                updates = load_rows(Path(str(prefix) + '.updates.csv'))
                self.assertTrue(set('VTMBRDI').issubset({r['op'] for r in updates}))

    def test_empty_small_and_graph_routing_and_local_ef(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            data = root / 'data'
            data.mkdir()
            n = 600
            (data / 'meta.txt').write_text(f'{n} 2 1\n')
            (data / 'base.f32').write_bytes(b''.join(struct.pack('<2f', i, 0) for i in range(n)))
            (data / 'test.f32').write_bytes(struct.pack('<2f', 4.25, 0))
            (data / 'tree.txt').write_text('2\n0 -1\n1 0\n')
            (data / 'bindings.txt').write_text('0\n' * n)
            categories = [1] * 5 + [2] * 12 + [3] * 40 + [4] * 543
            (data / 'attrs.txt').write_text(''.join(f'{c} 0 0 1\n' for c in categories))
            events = [f'Q 0 1 0 {c} -1 -1 0 0 0' for c in (99, 1, 2, 3, 4, -1)]
            trace = root / 'queries.trace'
            trace.write_text('6\n' + '\n'.join(events) + '\n')
            oracle = root / 'oracle'
            run(BINARY, '--data', data, '--trace', trace, '--out', oracle, '--oracle-only')
            tape = Path(str(oracle) + '.queries.csv')
            for binary, arm in ((BASELINE, 'A'), (BINARY, 'U'), (BINARY, 'H')):
                for policy in ('cap', 'hybrid', 'exact'):
                    prefix = root / f'{arm}-{policy}'
                    run(binary, '--data', data, '--trace', trace, '--out', prefix,
                        '--arm', arm, '--ef', 20, '--gt-in', tape,
                        '--eligibility-policy', policy, '--eligible-scan-threshold', 512)
                    rows = load_rows(Path(str(prefix) + '.queries.csv'))
                    self.assertEqual([int(r['eligible_card']) for r in rows], [0, 5, 12, 40, 543, 600])
                    self.assertEqual(int(rows[0]['distance_calls']), 0)
                    self.assertEqual(rows[1]['result_ids'], rows[1]['gt_ids'])
                    self.assertEqual(int(rows[1]['effective_ef']), 5)
                    for r in rows:
                        e = int(r['eligible_card'])
                        self.assertLessEqual(int(r['effective_ef']), e)
                        self.assertEqual(int(r['invalid_count']), 0)
                        if policy == 'exact' or (policy == 'hybrid' and e <= 512):
                            self.assertEqual(r['result_ids'], r['gt_ids'])
                            self.assertEqual(int(r['distance_calls']), e)
                    if policy == 'hybrid':
                        self.assertEqual([r['execution_path'] for r in rows],
                                         ['empty', 'exact', 'exact', 'exact', 'graph', 'graph'])
                        if arm == 'A':
                            self.assertEqual([int(r['graph_calls']) for r in rows], [0, 0, 0, 0, 1, 1])


if __name__ == '__main__':
    unittest.main()
