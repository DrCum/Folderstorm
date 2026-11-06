"""Validate the fixed private binding-retention comparison and exact cleanup."""
import argparse
import csv
import io
import json
from datetime import datetime
from pathlib import Path
from analyze_event_sampling_cost import distribution, normalized_hash


def key(value):
    return tuple(value[name] for name in ('kind', 'round', 'mode', 'avatars', 'frames'))


def analyze(raw, plan, proof):
    expected = {key(case) for case in plan['cases']}
    assert len(expected) == 18
    accepted, excluded, hashes, windows = {}, [], {}, []
    for path in sorted(raw.glob('capture14t-*.json')):
        metadata = json.loads(path.read_text(encoding='utf-8-sig'))
        cell = key(metadata)
        assert cell in expected and metadata['gapUs'] == 0 and metadata['load'] == 'normal'
        hashes[path.name] = normalized_hash(path)
        if metadata['status'] != 'completed':
            excluded.append({'file': path.name, 'status': metadata['status']})
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
        csv_path = path.with_suffix('.csv')
        hashes[csv_path.name] = normalized_hash(csv_path)
        rows = list(csv.DictReader(io.StringIO(csv_path.read_text(encoding='utf-8-sig'))))
        assert len(rows) == (1 if metadata['kind'] == 'alloc' else metadata['frames'])
        numeric = []
        for row in rows:
            assert row['mode'] == metadata['mode'] and row['load'] == 'normal'
            value = {name: int(data) for name, data in row.items() if name not in ('mode', 'load')}
            assert value['avatars'] == metadata['avatars'] and value['gap_us'] == 0
            held = metadata['avatars'] * 512 if metadata['mode'] == 'owned-retained' else 0
            motions = metadata['avatars'] if held else 0
            assert value['retained_binding_bytes'] == held and value['retained_binding_motions'] == motions
            assert value['binding_bytes_after_cleanup'] == 0
            numeric.append(value)
        result = {'cell': cell, 'file': path.name, 'exe_sha256': metadata['exeSha256']}
        if metadata['kind'] == 'alloc':
            value = numeric[0]
            assert value['frames'] == metadata['frames'] == 64
            assert value['later_calls'] == value['new_calls'] - value['first_frame_calls']
            assert value['later_bytes'] == value['requested_bytes'] - value['first_frame_bytes']
            assert value['peak_bytes'] >= value['live_bytes']
            result['allocation'] = value
        else:
            assert metadata['frames'] == 1024
            assert [value['frame'] for value in numeric] == list(range(1024))
            for value in numeric:
                assert value['callbacks'] == metadata['avatars'] and value['callbacks'] == value['applied'] + value['fallback']
                assert not any(value[name] for name in ('stale', 'accepted', 'rejected', 'excluded', 'pool_cycles_window'))
                if metadata['mode'] == 'direct':
                    assert value['predicted'] == value['masked'] == value['applied'] == value['sampled_joints'] == 0
                else:
                    assert value['predicted'] == value['applied'] == metadata['avatars']
                    assert value['masked'] == metadata['avatars'] * 3 and value['sampled_joints'] == metadata['avatars'] * 32
            for name in ('owner_cycles_window', 'retained_binding_bytes', 'retained_binding_motions', 'binding_bytes_after_cleanup', 'final_binding_cleanup_ns'):
                assert len({value[name] for value in numeric}) == 1
            assert numeric[0]['owner_cycles_window'] >= sum(value['owner_cycles'] for value in numeric)
            result['costs'] = {name: distribution([value[name] for value in numeric]) for name in (
                'total_ns', 'owner_cycles', 'capture_ns', 'submit_ns', 'consume_ns', 'apply_ns', 'fallback_ns', 'cleanup_ns')}
            result['owner_cycles_window'] = numeric[0]['owner_cycles_window']
            result['retained_binding_bytes'] = numeric[0]['retained_binding_bytes']
            result['final_binding_cleanup_ns'] = numeric[0]['final_binding_cleanup_ns']
            result['callbacks'] = sum(value['callbacks'] for value in numeric)
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
    for name in ('raw', 'plan', 'provenance', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    proof = json.loads(args.provenance.read_text(encoding='utf-8-sig'))
    assert normalized_hash(args.plan) == proof['matrix_plan_normalized_sha256']
    result = analyze(args.raw, json.loads(args.plan.read_text(encoding='utf-8-sig')), proof)
    args.output.write_bytes((json.dumps(result, indent=2) + '\n').encode())
    print(f"Validated {result['accepted']}/{result['planned']} fixed capture cases; complete={result['complete']}")
