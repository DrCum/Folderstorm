"""Validate the fixed per-motion publication matrix and complete CPU/useful-work costs."""
import argparse
import csv
import io
import json
from datetime import datetime
from pathlib import Path
from analyze_event_sampling_cost import distribution, normalized_hash, percentile


def key(value):
    return tuple(value[name] for name in ('kind', 'round', 'mode', 'avatars', 'frames'))


def stats(values):
    return dict(distribution(values), p99=percentile(values, 99))


def analyze(raw, plan, proof):
    expected = {key(case) for case in plan['cases']}
    assert len(expected) == 24
    accepted, excluded, hashes, windows = {}, [], {}, []
    for path in sorted(raw.glob('ownedrows14v-*.json')):
        metadata = json.loads(path.read_text(encoding='utf-8-sig'))
        cell = key(metadata)
        assert cell in expected and metadata['gapUs'] == 0 and metadata['load'] == 'normal'
        hashes[path.name] = normalized_hash(path)
        if metadata['status'] != 'completed':
            excluded.append(dict(file=path.name, status=metadata['status']))
            continue
        assert cell not in accepted and metadata['exit'] == 0
        assert metadata['exeSha256'].lower() == proof['programs'][metadata['kind']]['sha256'].lower()
        for side in ('before', 'after'):
            state = metadata[side]
            assert state['idle'] and not state['activeBuild'] and not state['workerCurrent'] and not state['buildOrProbeProcesses']
            assert state['workerStatus'] == 'idle' and state['phase'] == 'IDLE_MONITORING'
        begin, end = (datetime.fromisoformat(metadata[side]['utc'].replace('Z', '+00:00')) for side in ('before', 'after'))
        assert begin < end
        windows.append((begin, end, path.name))
        csv_path = path.with_suffix('.csv'); hashes[csv_path.name] = normalized_hash(csv_path)
        rows = list(csv.DictReader(io.StringIO(csv_path.read_text(encoding='utf-8-sig'))))
        assert len(rows) == (1 if metadata['kind'] == 'alloc' else metadata['frames'])
        numeric = []
        mode, avatars = metadata['mode'], metadata['avatars']
        per_row = mode in ('event-rows-retained', 'default-rows-retained')
        for row in rows:
            assert row['mode'] == mode and row['load'] == 'normal'
            value = {name: int(data) for name, data in row.items() if name not in ('mode', 'load')}
            assert value['avatars'] == avatars and value['gap_us'] == 0
            assert value['retained_binding_bytes'] == (0 if mode == 'direct' else avatars * 512)
            assert value['retained_binding_motions'] == (0 if mode == 'direct' else avatars)
            assert value['binding_bytes_after_cleanup'] == 0
            resident = value['row_resident_bytes']
            bounds = proof['row_limits']
            if per_row:
                assert bounds['fixed_group_bytes'] <= resident <= bounds['fixed_group_bytes'] + 2 * avatars * 32 * (8 + bounds['sampled_joint_bytes'])
            else:
                assert resident == 0
            numeric.append(value)
        result = dict(cell=cell, file=path.name, exe_sha256=metadata['exeSha256'])
        if metadata['kind'] == 'alloc':
            value = numeric[0]; assert value['frames'] == metadata['frames'] == 64
            assert value['later_calls'] == value['new_calls'] - value['first_frame_calls']
            assert value['later_bytes'] == value['requested_bytes'] - value['first_frame_bytes']
            assert value['peak_bytes'] >= value['live_bytes']
            result['allocation'] = value
        else:
            assert metadata['frames'] == 1024
            assert [value['frame'] for value in numeric] == list(range(1024))
            for value in numeric:
                assert value['callbacks'] == avatars == value['applied'] + value['fallback']
                assert value['stale'] == value['failed_batches'] == value['excluded'] == 0
                assert value['sampled_joints'] % 32 == 0 and 0 <= value['sampled_joints'] <= avatars * 32
                if per_row:
                    assert value['row_sampled_count'] * 32 == value['sampled_joints'] and value['row_failed_count'] == 0
                else:
                    assert value['sampled_joints'] in (0, avatars * 32)
                    assert value['row_sampled_count'] == value['row_failed_count'] == 0
                assert value['sampled_unused_joints'] == value['sampled_joints'] - value['applied'] * 32 >= 0
                if mode == 'direct':
                    assert not any(value[name] for name in ('predicted', 'masked', 'accepted', 'rejected', 'applied',
                        'readiness_checks', 'pending_misses', 'first_ready_callback', 'sampled_joints', 'pool_cycles_window'))
                else:
                    assert value['predicted'] == avatars and value['masked'] == avatars * 3
                    assert value['accepted'] + value['rejected'] == 1
                    ready = value['first_ready_callback']
                    if value['rejected']:
                        assert ready == value['readiness_checks'] == value['pending_misses'] == value['applied'] == 0
                    elif per_row:
                        assert value['readiness_checks'] == avatars and value['pending_misses'] == avatars - value['applied']
                        if ready:
                            assert 1 <= ready <= avatars and value['applied'] > 0
                        else:
                            assert value['applied'] == 0
                    elif ready:
                        assert 1 <= ready <= avatars
                        assert value['readiness_checks'] == ready and value['pending_misses'] == ready - 1
                        assert value['applied'] == avatars - ready + 1 and value['sampled_joints'] == avatars * 32
                        if mode == 'event-first-retained': assert ready == 1
                    else:
                        count = 1 if mode == 'event-first-retained' else avatars
                        assert value['readiness_checks'] == value['pending_misses'] == count and value['applied'] == 0
            for name in ('pool_cycles_window', 'owner_cycles_window', 'retained_binding_bytes', 'final_binding_cleanup_ns', 'row_resident_bytes'):
                assert len({value[name] for value in numeric}) == 1
            owner_window, worker_window = numeric[0]['owner_cycles_window'], numeric[0]['pool_cycles_window']
            assert owner_window >= sum(value['owner_cycles'] for value in numeric)
            assert worker_window >= sum(value['worker_cycles'] for value in numeric)
            result['costs'] = {name: stats([value[name] for value in numeric]) for name in (
                'total_ns', 'owner_cycles', 'capture_ns', 'submit_ns', 'consume_ns', 'apply_ns', 'fallback_ns', 'cleanup_ns')}
            result['row_resident_bytes'] = numeric[0]['row_resident_bytes']
            result['owner_cycles_window'] = owner_window
            result['worker_cycles_window'] = worker_window
            result['combined_cycles_window'] = owner_window + worker_window
            result['totals'] = {name: sum(value[name] for value in numeric) for name in (
                'accepted', 'rejected', 'callbacks', 'applied', 'fallback', 'readiness_checks', 'pending_misses',
                'sampled_joints', 'sampled_unused_joints')}
            result['applied_fraction'] = result['totals']['applied'] / result['totals']['callbacks']
            result['used_sampled_fraction'] = (result['totals']['applied'] * 32 / result['totals']['sampled_joints']) if result['totals']['sampled_joints'] else None
            ready = [value['first_ready_callback'] for value in numeric if value['first_ready_callback']]
            result['first_ready_callback'] = stats(ready) if ready else None
            sampled = [value for value in numeric if value['sampled_joints']]
            result['sampled_jobs'] = len(sampled)
            result['delivery_ns'] = stats([value['delivery_ns'] for value in sampled]) if sampled else None
            result['kernel_ns'] = stats([value['kernel_ns'] for value in sampled]) if sampled else None
        accepted[cell] = result
    windows.sort()
    for old, new in zip(windows, windows[1:]):
        assert old[1] <= new[0], f'Accepted observations overlap: {old[2]}, {new[2]}'
    return dict(complete=len(accepted) == len(expected), accepted=len(accepted), planned=len(expected),
                implementation_head=proof['implementation_head'], missing=sorted(expected - accepted.keys()),
                excluded_or_unrun=excluded, cases=list(accepted.values()), normalized_lf_sha256=hashes,
                nonoverlap_verified=True, limits=proof['limits'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('raw', 'plan', 'provenance', 'output'): parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    proof = json.loads(args.provenance.read_text(encoding='utf-8-sig'))
    assert normalized_hash(args.plan) == proof['matrix_plan_normalized_sha256']
    result = analyze(args.raw, json.loads(args.plan.read_text(encoding='utf-8-sig')), proof)
    args.output.write_bytes((json.dumps(result, indent=2) + '\n').encode())
    print(f"Validated {result['accepted']}/{result['planned']} fixed publication cases; complete={result['complete']}")
