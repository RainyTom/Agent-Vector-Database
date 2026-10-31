"""Real C++ packed replay against independent canonical GT; no speed assertions."""
import csv
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from test_closure import fixture, BINARY, BASELINE, load_rows


def execute(*args):
    subprocess.run(list(map(str, args)), check=True, capture_output=True, text=True)


def replay(data, trace, prefix, layout, policy='cap', ef=256, block=1024, repack=1):
    oracle = Path(str(prefix) + '-oracle')
    execute(BINARY, '--data', data, '--trace', trace, '--out', oracle, '--oracle-only')
    execute(BASELINE, '--data', data, '--trace', trace, '--out', prefix,
            '--arm', 'P', '--pack-layout', layout, '--pack-size', block,
            '--pack-final-repack', repack, '--eligibility-policy', policy,
            '--ef', ef, '--gt-in', str(oracle) + '.queries.csv')
    return (load_rows(Path(str(prefix) + '.queries.csv')),
            load_rows(Path(str(prefix) + '.updates.csv')),
            json.loads(Path(str(prefix) + '.summary.json').read_text()))


def dataset(root, n, live):
    data = root / 'data'; data.mkdir()
    (data / 'meta.txt').write_text(f'{n} 2 1\n')
    (data / 'base.f32').write_bytes(b''.join(struct.pack('<2f', i, i % 7) for i in range(n)))
    (data / 'test.f32').write_bytes(struct.pack('<2f', n - .25, 0))
    (data / 'tree.txt').write_text('3\n0 -1\n1 0\n2 0\n')
    (data / 'bindings.txt').write_text('1\n' * n)
    (data / 'attrs.txt').write_text(''.join(f'1 {i} 3 {int(i < live)}\n' for i in range(n)))
    return data


def trace_file(root, events):
    path = root / 'events.trace'
    path.write_text(str(len(events)) + '\n' + '\n'.join(events) + '\n')
    return path


class PackedReplay(unittest.TestCase):
    def check_answers(self, rows):
        for row in rows:
            self.assertEqual(row['result_ids'], row['gt_ids'])
            self.assertEqual(int(row['gt_distance_calls']), 0)
            self.assertEqual(int(row['invalid_count']), 0)
            self.assertEqual(int(row['eligibility_mask_mismatches']), 0)

    def test_whole_large_owner_and_small_group_coalescing(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); data = dataset(root, 3000, 3000)
            (data / 'tree.txt').write_text('19\n0 -1\n' + ''.join(f'{i} 0\n' for i in range(1, 19)))
            owners = [0] * 1200 + [i for i in range(1, 19) for _ in range(100)]
            (data / 'bindings.txt').write_text(''.join(f'{i}\n' for i in owners))
            events = ['Q 0 1 0 -1 -1 -1 0 0 0']
            events += [f'Q 0 0 {i} -1 -1 -1 0 0 0' for i in range(19)]
            trace = trace_file(root, events)
            rows, _, summary = replay(data, trace, root / 'whole', 'dfs', ef=4096)
            self.check_answers(rows)
            # 1200-object root stays one graph despite B=1024; 18 small groups
            # greedily coalesce into 1000/800, with no direct owner split.
            self.assertEqual(int(rows[0]['graph_calls']), 3)
            self.assertEqual([int(r['graph_calls']) for r in rows[1:]], [1] * 19)
            self.assertEqual(summary['initial_hnsw_node_count'], 3)

    def test_empty_block_reuse_and_full_repack_reclaim_slots(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); data = dataset(root, 3075, 1)
            query = 'Q 0 1 0 -1 -1 -1 0 0 0'
            events = [f'I {i} 1 1 {i} 3' for i in range(1, 2052)] + [query]
            # The first prospective split puts IDs1024..2048 in its right block.
            events += [f'D {i}' for i in range(1024, 2049)] + [query]
            events += [f'I {i} 1 1 {i} 3' for i in range(2052, 3075)] + [query]
            rows, _, summary = replay(data, trace_file(root, events), root / 'reuse', 'dfs', ef=4096)
            self.check_answers(rows)
            self.assertEqual([int(r['eligible_card']) for r in rows], [2052, 1027, 2050])
            self.assertEqual([int(r['physical_block_count']) for r in rows], [2, 2, 2])
            self.assertEqual(summary['physical_block_count'], 1)
            self.assertEqual(summary['active_packed_graph_count'], 1)

    def test_all_events_and_metadata_only_placement(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); data, trace = fixture(root)
            for layout in ('dfs', 'shuffle', 'node'):
                rows, updates, summary = replay(data, trace, root / layout, layout)
                self.check_answers(rows)
                for update in updates:
                    if update['op'] in 'VTMBR':
                        self.assertEqual(int(update['graph_distance_calls']), 0)
                        self.assertEqual(int(update['ann_insertions']), 0)
                        self.assertEqual(int(update['ann_deletions']), 0)
                self.assertTrue(summary['complete'])
                self.assertGreater(summary['final_repack_us'], 0)
                self.assertGreater(summary['final_repack_ann_insertions'], 0)
                self.assertGreaterEqual(summary['total_workload_us'], summary['final_repack_us'])

    def test_empty_first_insert_and_same_label_resurrection(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); data = dataset(root, 4, 0)
            query = 'Q 0 1 0 -1 -1 -1 0 0 0'
            trace = trace_file(root, [query, 'I 3 2 1 3 3', query, 'D 3', query,
                                      'I 3 1 1 3 3', query, 'M 3 0', query, 'R 2 1', query])
            for layout in ('dfs', 'shuffle', 'node'):
                rows, _, summary = replay(data, trace, root / layout, layout)
                self.check_answers(rows)
                self.assertEqual([int(r['eligible_card']) for r in rows], [0, 1, 0, 1, 1, 1])
                for row in rows:
                    self.assertEqual(int(row['graph_calls']), int(row['eligible_card']))

    def test_split_compact_and_repack_do_not_lose_new_labels(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); data = dataset(root, 2052, 1)
            query = 'Q 0 1 0 -1 -1 -1 0 0 0'
            events = [query] + [f'I {i} 1 1 {i} 3' for i in range(1, 2052)] + [query]
            # Metadata move before split/repack must not redirect physical deletion.
            events += ['M 2051 2', 'R 2 1', 'Q 0 1 2 -1 -1 -1 0 0 0']
            events += [f'D {i}' for i in range(1025)]
            events += [query, 'I 0 2 1 0 3', query]
            trace = trace_file(root, events)
            rows, updates, summary = replay(data, trace, root / 'split', 'dfs', ef=4096)
            self.check_answers(rows)
            self.assertGreater(sum(int(u['packed_splits']) for u in updates), 0)
            self.assertGreater(sum(int(u['compaction_count']) for u in updates if u['op'] == 'D'), 0)
            self.assertEqual(int(rows[2]['eligible_card']), 1)
            self.assertEqual(rows[2]['result_ids'], '2051')
            self.assertEqual(int(rows[-1]['eligible_card']), 1028)
            self.assertGreater(summary['final_repack_distance_calls'], 0)


if __name__ == '__main__':
    unittest.main()
