"""Validate the prewritten private matrix and report complete-path costs.

Run after all accepted cases, or during collection to identify the exact missing
cells. No timing threshold, viewer deadline or FPS claim is derived here.
"""
import argparse
import csv
import hashlib
import io
import json
from datetime import datetime
from pathlib import Path


def cell(value):
    return (value['mode'], value['avatars'], value.get('gap', value.get('gapUs')),
            value['round'], value['load'], value.get('allocations', value.get('kind') == 'alloc'))


def percentile(values, percentage):
    values = sorted(values)
    return values[(len(values) * percentage + 99) // 100 - 1]


def distribution(values):
    return {'p50': percentile(values, 50), 'p95': percentile(values, 95), 'max': max(values)}


def normalized_hash(path):
    return hashlib.sha256(path.read_bytes().replace(b'\r\n', b'\n')).hexdigest()


def analyze(raw, plan, provenance):
    expected = {cell(value) for value in plan['cases']}
    assert len(expected) == len(plan['cases']) == 84
    accepted, excluded, identities, windows = {}, [], {}, []
    for metadata_path in sorted(raw.glob('event14s-*.json')):
        metadata = json.loads(metadata_path.read_text(encoding='utf-8-sig'))
        key = cell(metadata)
        assert key in expected, f'Unplanned observation: {metadata_path.name}'
        identities[metadata_path.name] = normalized_hash(metadata_path)
        if metadata['status'] != 'completed':
            excluded.append({'file': metadata_path.name, 'status': metadata['status']})
            continue
        assert key not in accepted, f'Duplicate accepted cell: {key}'
        assert metadata['exeSha256'].lower() == provenance['programs'][metadata['kind']]['sha256'].lower(), 'Unmatched executable identity'
        assert metadata['exit'] == 0 and metadata['before']['idle'] and metadata['after']['idle']
        assert not metadata['before']['activeBuild'] and not metadata['after']['activeBuild']
        assert not metadata['before']['buildOrProbeProcesses'] and not metadata['after']['buildOrProbeProcesses']
        assert metadata['before']['workerStatus'] == metadata['after']['workerStatus'] == 'idle'
        assert metadata['before']['phase'] == metadata['after']['phase'] == 'IDLE_MONITORING'
        assert metadata['frames'] == plan['frames_per_case'] == 64
        start, finish = (datetime.fromisoformat(metadata[side]['utc'].replace('Z', '+00:00')) for side in ('before', 'after'))
        assert start < finish
        windows.append((start, finish, metadata_path.name))
        csv_path = metadata_path.with_suffix('.csv')
        identities[csv_path.name] = normalized_hash(csv_path)
        rows = list(csv.DictReader(io.StringIO(csv_path.read_text(encoding='utf-8-sig'))))
        assert len(rows) == (1 if metadata['kind'] == 'alloc' else 64)
        for row in rows:
            assert row['mode'] == metadata['mode'] and row['load'] == metadata['load']
            assert int(row['avatars']) == metadata['avatars'] and int(row['gap_us']) == metadata['gapUs']
        summary = {'cell': key, 'file': metadata_path.name, 'exe_sha256': metadata['exeSha256']}
        if metadata['kind'] == 'alloc':
            row = rows[0]
            assert int(row['frames']) == 64
            summary['allocations'] = {name: int(row[name]) for name in row if name not in ('mode', 'load')}
            value = summary['allocations']
            assert value['new_calls'] >= value['first_frame_calls'] and value['requested_bytes'] >= value['first_frame_bytes']
            assert value['later_calls'] == value['new_calls'] - value['first_frame_calls']
            assert value['later_bytes'] == value['requested_bytes'] - value['first_frame_bytes']
            assert value['peak_bytes'] >= value['live_bytes']
        else:
            numeric = [{name: int(value) for name, value in row.items() if name not in ('mode', 'load')} for row in rows]
            assert [row['frame'] for row in numeric] == list(range(64))
            for row in numeric:
                assert row['callbacks'] == row['applied'] + row['fallback'] and row['callbacks'] > 0
                assert row['total_ns'] >= row['gap_ns']
                assert row['applied'] <= row['predicted'] <= 32
                assert row['sampled_joints'] in (0, row['predicted'] * 32)
                if metadata['mode'] == 'direct':
                    assert not any(row[name] for name in ('predicted', 'masked', 'excluded', 'rejected', 'accepted', 'applied', 'stale', 'sampled_joints'))
                else:
                    # This fixed ordinary fixture has one unmasked motion per character.
                    assert row['predicted'] == metadata['avatars'] and row['masked'] == metadata['avatars'] * 3 and row['excluded'] == 0
                    assert row['accepted'] + row['rejected'] == (0 if metadata['mode'] == 'owned' else 1)
                if metadata['load'] != 'normal':
                    assert row['applied'] == row['sampled_joints'] == row['stale'] == 0
                if metadata['mode'] == 'owned':
                    assert row['applied'] == row['callbacks'] == row['predicted']
                assert row['stale'] == 0
            for name in ('pool_cycles_window', 'owner_cycles_window'):
                assert len({row[name] for row in numeric}) == 1
            owner_window, worker_window = numeric[0]['owner_cycles_window'], numeric[0]['pool_cycles_window']
            assert owner_window >= sum(row['owner_cycles'] for row in numeric)
            assert worker_window >= sum(row['worker_cycles'] for row in numeric) if metadata['mode'] != 'owned' else worker_window == 0
            summary['costs'] = {name: distribution([row[name] for row in numeric]) for name in (
                'total_ns', 'owner_cycles', 'capture_ns', 'submit_ns', 'consume_ns', 'apply_ns', 'fallback_ns', 'cleanup_ns')}
            summary['owner_without_synthetic_gap_ns'] = distribution([row['total_ns'] - row['gap_ns'] for row in numeric])
            summary['totals'] = {name: sum(row[name] for row in numeric) for name in (
                'predicted', 'masked', 'rejected', 'accepted', 'callbacks', 'applied', 'fallback', 'stale', 'sampled_joints')}
            summary['owner_cycles_window'] = owner_window
            summary['worker_cycles_window'] = worker_window
            summary['combined_cycles_window'] = owner_window + worker_window
            summary['applied_fraction'] = summary['totals']['applied'] / summary['totals']['callbacks']
            sampled = [row for row in numeric if row['sampled_joints']]
            summary['sampled_jobs'] = len(sampled)
            summary['delivery_ns'] = distribution([row['delivery_ns'] for row in sampled]) if sampled else None
            summary['kernel_ns'] = distribution([row['kernel_ns'] for row in sampled]) if sampled else None
        accepted[key] = summary
    windows.sort()
    for previous, current in zip(windows, windows[1:]):
        assert previous[1] <= current[0], f'Accepted observations overlap: {previous[2]}, {current[2]}'
    return {
        'implementation_head': provenance['implementation_head'],
        'complete': len(accepted) == len(expected), 'accepted': len(accepted), 'planned': len(expected),
        'missing': sorted(expected - accepted.keys()), 'excluded_or_unrun': excluded,
        'cases': list(accepted.values()), 'normalized_lf_sha256': identities,
        'nonoverlap_verified': True,
        'limits': [
            'Synthetic cohorts/gaps; no live viewer, real deadline, power or FPS acceptance.',
            'Owner/worker windows include loop bookkeeping and final drain; per-job cycles are attribution only.',
            'Allocation split is first owner boundary versus later work/drain, not a drained warm-up comparison.',
            'Before/after host idle guards do not control unrelated desktop load, power or timer policy.',
        ],
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--raw', type=Path, required=True)
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--provenance', type=Path, required=True)
    args = parser.parse_args()
    proof = json.loads(args.provenance.read_text(encoding='utf-8-sig'))
    assert normalized_hash(args.plan) == proof['matrix_plan_normalized_sha256']
    result = analyze(args.raw, json.loads(args.plan.read_text(encoding='utf-8-sig')), proof)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(f"Validated {result['accepted']}/{result['planned']} planned cases; complete={result['complete']}")
