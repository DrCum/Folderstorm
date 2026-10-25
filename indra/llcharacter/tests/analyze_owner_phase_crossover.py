"""Validate the predeclared intermediate-cohort publication comparison."""
import argparse
import json
from pathlib import Path
from analyze_owned_row_publication import analyze
from analyze_event_sampling_cost import normalized_hash


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('raw', 'plan', 'provenance', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    proof = json.loads(args.provenance.read_text(encoding='utf-8-sig'))
    assert normalized_hash(args.plan) == proof['matrix_plan_normalized_sha256']
    plan = json.loads(args.plan.read_text(encoding='utf-8-sig'))
    assert {case['avatars'] for case in plan['cases']} == {2, 4, 8, 16, 32}
    assert {case['mode'] for case in plan['cases']} == {'direct', 'event-later-retained', 'event-rows-retained'}
    assert sum(case['kind'] == 'timing' for case in plan['cases']) == 30
    result = analyze(args.raw, plan, proof, pattern='crossover14w-*.json', expected_count=45)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(f"Validated {result['accepted']}/{result['planned']} crossover cases; complete={result['complete']}")
