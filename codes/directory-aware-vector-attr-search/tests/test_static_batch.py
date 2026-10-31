"""Static resident-index queries preserve results across policy/budget transitions."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_closure import fixture, BINARY, BASELINE, load_rows


def execute(*args, check=True):
    return subprocess.run(list(map(str, args)), capture_output=True, text=True, check=check)


def schedule(path, passes):
    path.write_text(str(len(passes)) + '\n' + ''.join(f'{ef} {policy} {json.dumps(str(out))}\n' for ef, policy, out in passes))


class StaticBatchReplay(unittest.TestCase):
    def test_resident_queries_match_independent_runs_without_stat_leakage(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); data, mixed = fixture(root)
            events = [s for s in mixed.read_text().splitlines()[1:] if s.startswith('Q ')]
            trace = root / 'static.trace'; trace.write_text(str(len(events)) + '\n' + '\n'.join(events) + '\n')
            oracle = root / 'oracle'
            execute(BINARY, '--data', data, '--trace', trace, '--out', oracle, '--oracle-only')
            tape = str(oracle) + '.queries.csv'
            for arm, policies in [('A', ['off'] * 3), ('P', ['cap', 'hybrid', 'cap', 'exact', 'cap'])]:
                passes = [(15 if i % 2 == 0 else 20, policy, root / f'{arm}-batch-{i}') for i, policy in enumerate(policies)]
                batch = root / f'{arm}.schedule'; schedule(batch, passes)
                common = [BASELINE, '--data', data, '--trace', trace, '--arm', arm, '--scan-threshold', 0,
                          '--pack-layout', 'dfs', '--pack-size', 1024, '--gt-in', tape]
                execute(*common, '--static-batch', batch, '--eligibility-policy', 'off' if arm == 'A' else 'cap')
                summaries = []
                for i, (ef, policy, out) in enumerate(passes):
                    independent = root / f'{arm}-single-{i}'
                    execute(*common, '--out', independent, '--ef', ef, '--eligibility-policy', policy)
                    actual = load_rows(Path(str(out) + '.queries.csv'))
                    expected = load_rows(Path(str(independent) + '.queries.csv'))
                    self.assertEqual(len(actual), len(events))
                    keys = ('event', 'version', 'query_index', 'result_ids', 'gt_ids', 'distance_calls',
                            'graph_calls', 'scan_distance_calls', 'scalar_scan_group_calls', 'effective_ef',
                            'eligibility_mask_mismatches', 'ann_hops')
                    self.assertEqual([[r[k] for k in keys] for r in actual], [[r[k] for k in keys] for r in expected])
                    summary = json.loads(Path(str(out) + '.summary.json').read_text()); summaries.append(summary)
                    self.assertEqual(summary['n_queries'], len(events))
                    self.assertEqual(summary['n_updates'], 0)
                    self.assertEqual(summary['distance_calls'], sum(int(r['distance_calls']) for r in actual))
                    self.assertEqual(summary['graph_calls_total'], sum(int(r['graph_calls']) for r in actual))
                    self.assertEqual(summary['static_batch_pass'], i)
                    self.assertEqual(summary['static_batch_passes'], len(passes))
                    self.assertEqual(summary['static_index_reused'], int(i > 0))
                    self.assertEqual(summary['gt_distance_calls'], 0)
                    self.assertEqual(load_rows(Path(str(out) + '.updates.csv')), [])
                self.assertEqual(sum(s['build_charged_this_pass'] for s in summaries), 1)
                self.assertEqual(len({s['build_distance_calls'] for s in summaries}), 1)
                if arm == 'P':
                    self.assertTrue(all(s['initial_packed_fingerprint'] == s['final_packed_fingerprint'] for s in summaries))

    def test_update_trace_and_duplicate_output_rejected_before_query_outputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); data, mixed = fixture(root)
            oracle = root / 'oracle'
            execute(BINARY, '--data', data, '--trace', mixed, '--out', oracle, '--oracle-only')
            out = root / 'blocked'; batch = root / 'schedule'
            schedule(batch, [(20, 'cap', out)])
            command = [BASELINE, '--data', data, '--trace', mixed, '--arm', 'P', '--pack-size', 1024,
                       '--eligibility-policy', 'cap', '--gt-in', str(oracle) + '.queries.csv', '--static-batch', batch]
            result = execute(*command, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('only Q', result.stderr)
            self.assertFalse(Path(str(out) + '.queries.csv').exists())
            schedule(batch, [(20, 'cap', out), (20, 'hybrid', out)])
            result = execute(*command, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('duplicate', result.stderr)
            self.assertFalse(Path(str(out) + '.queries.csv').exists())


if __name__ == '__main__':
    unittest.main()
