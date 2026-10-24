"""Summarize retained private observations; this imposes no timing pass threshold."""
import csv
import hashlib
import itertools
import json
import math
from pathlib import Path

root = Path(__file__).resolve().parent
progress = json.loads((root / "progress.json").read_text(encoding="utf-8-sig"))
for name, expected in progress["retained_raw_sha256_normalized_lf"].items():
    actual = hashlib.sha256((root / name).read_bytes().replace(b"\r\n", b"\n")).hexdigest()
    if actual != expected:
        raise ValueError(f"Retained input changed: {name}")

rows, seen, other = [], set(), []
expected_checksum = "10808560563965462229"
for metadata in sorted(root.glob("worker-wait14p2-r*.json")):
    case = json.loads(metadata.read_text(encoding="utf-8-sig"))
    if case["status"] != "completed":
        other.append({"case": case["case"], "status": case["status"]})
        continue
    key = tuple(case[k] for k in ("round", "width", "timer", "wait", "mode"))
    if key in seen:
        raise ValueError(f"Duplicate accepted comparison cell: {key}")
    seen.add(key)
    if case["exit"] != 0 or not case["before"]["idle"] or not case["after"]["idle"]:
        raise ValueError(f"Invalid accepted idle observation: {metadata}")
    with metadata.with_suffix(".csv").open(encoding="utf-8-sig", newline="") as stream:
        samples = list(csv.DictReader(stream))
    wakes = [r for r in samples if r["phase"] == "wake"]
    active, = [r for r in samples if r["phase"] == "active"]
    idle, = [r for r in samples if r["phase"] == "idle"]
    if len(wakes) != 64 or active["units"] != "4096" or active["checksum"] != expected_checksum:
        raise ValueError(f"Incomplete wake or active-work data: {metadata}")
    if any(r["status"] != "ok" for r in (active, idle)):
        raise ValueError(f"Resource cap/failure needs separate assessment: {metadata}")
    called = sorted(int(r["wall_ns"]) / 1000 for r in wakes if r["status"] == "ok")
    def percentile(fraction):
        return called[max(0, math.ceil(fraction * len(called)) - 1)] if called else None
    rows.append({
        "round": case["round"], "width": case["width"], "mode": case["mode"],
        "timer": idle["timer"], "wait": idle["wait"],
        "wake_p50_us": percentile(.50), "wake_p95_us": percentile(.95),
        "wake_admitted": sum(int(r["admitted"]) for r in wakes),
        "wake_rejected": sum(int(r["rejected"]) for r in wakes),
        "active_ms": int(active["wall_ns"]) / 1e6,
        "active_worker_cpu_100ns": int(active["cpu_100ns"]),
        "active_worker_cycles": int(active["thread_cycles"]),
        "idle_wall_seconds": int(idle["wall_ns"]) / 1e9,
        "idle_worker_cpu_100ns": int(idle["cpu_100ns"]),
        "idle_pool_percent_of_one_core": int(idle["cpu_100ns"]) * 10000 / int(idle["wall_ns"]),
        "idle_worker_cycles": int(idle["thread_cycles"]),
        "finite_wait_pulses": int(idle["units"]), "case": case["case"],
    })

expected = set(itertools.product((1, 2), (1, 3, 8), ("default", "request"), ("empty", "timed"), ("current", "stock")))
missing = [dict(zip(("round", "width", "timer", "wait", "mode"), key)) for key in sorted(expected - seen)]
summary = {
    "status": "complete" if not missing else "partial; remaining cases must complete before adoption assessment",
    "accepted_cases": len(rows), "expected_cases": len(expected), "remaining": missing,
    "nonaccepted_records": other, "wake_quantiles": "nearest rank among admitted samples; microseconds",
    "cpu_scope": "sum of worker kernel/user ticks; percentage is of one core, not the whole machine",
    "phase_scope": "wake and active phases precede finite-wait coroutines; timed/empty describes the subsequent idle control",
    "limits": progress["limits"], "cases": rows,
}
(root / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
with (root / "summary.csv").open("w", encoding="utf-8", newline="") as stream:
    writer = csv.DictWriter(stream, fieldnames=list(rows[0]) if rows else [])
    writer.writeheader()
    writer.writerows(rows)
print(f"Verified {len(rows)}/{len(expected)} cases; {len(missing)} remaining; {len(other)} nonaccepted records retained")
