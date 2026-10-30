"""Check a complete benchmark CSV and print the default batch-8 comparison.

Uses only the standard library. Timing distributions are empirical, not FPS.
Example: python analyze_sampling_cost.py doc/plans/results/sampling-cost-run1.csv
"""
import csv
import math
import sys
from pathlib import Path


def require(value, message):
    if not value:
        raise ValueError(message)


def read(path):
    rows = list(csv.reader(Path(path).open(encoding="utf-8-sig")))
    headers = {row[0].removesuffix("_header"): row[1:] for row in rows if row and row[0].endswith("_header")}
    data = {kind: [dict(zip(header, row[1:], strict=True)) for row in rows if row and row[0] == kind]
            for kind, header in headers.items()}
    counts, stages = data["counts"], data["stage"]
    extended = any(row[:3] == ["meta", "dataset", "cpu"] for row in rows)
    trials, samples = (8, 4096) if extended else (120, 128)
    require(len(counts) == trials and len(stages) == trials * 10, "Incomplete timing matrix")
    shared = headers["counts"][:11]
    key = lambda row: tuple(row[field] for field in shared)
    indexed = {key(row): row for row in counts}
    require(len(indexed) == len(counts), "Duplicate trial identity")
    for row in counts:
        n = lambda field: int(row[field])
        require(n("samples") == samples and n("failed") == n("stale") == 0, "Sampling failed or samples missing")
        require(n("observed_slots_high") <= n("slots") and n("observed_queued_high") <= n("slots"), "Slot bound exceeded")
        require(0 <= n("wasted_sampled_joints") <= n("sampled_joints"), "Invalid wasted-work count")
        if row["mode"] != "direct":
            used = n("sampled_joints") - n("wasted_sampled_joints")
            require(used + n("fallback_motions") * n("joints") == n("samples") * n("motions") * n("joints"),
                    "Useful plus fallback work differs from the requested pose")
        if row["mode"] in ("barrier", "wait"):
            groups = (n("motions") + n("batch") - 1) // n("batch")
            require(n("accepted") + n("rejected") == groups * n("samples"), "Unaccounted admission")
            require(n("accepted") == sum(n(field) for field in ("ready_jobs", "cancelled_queued", "abandoned_running",
                                                               "discarded_ready", "failed", "stale")), "Unaccounted accepted job")
    for row in stages:
        values = [float(row[field]) for field in ("p50_us", "p95_us", "p99_us", "max_us")]
        require(all(math.isfinite(value) and value >= 0 for value in values) and values == sorted(values), "Invalid timing distribution")
        if row["stage"] == "total":
            require(int(row["n"]) == int(indexed[key(row)]["samples"]), "Missing frame distribution samples")
        if not int(row["n"]):
            require(values == [0, 0, 0, 0], "Empty timing series has nonzero values")
    return stages, indexed, key


def report(path):
    stages, counts, key = read(path)
    print(f"{path}: {len(counts)} complete trials, {sum(int(row['samples']) for row in counts.values())} measured frames; accounting invariants pass")
    print("| Motions x joints | Mode | Workers | Overlap | Median / p95 / p99 / max us | Ready jobs | Fallback motions | Wasted sampled joints |")
    print("| --- | --- | --- | --- | --- | --- | --- | --- |")
    for row in stages:
        if (row["stage"] != "total" or row["batch"] != "8" or row["keys"] != "32" or row["cold_ms"] != "0" or
                row["contention_threads"] != "0" or row["workers"] not in ("0", "2") or row["slots"] == "1"):
            continue
        count = counts[key(row)]
        timing = " / ".join(row[field] for field in ("p50_us", "p95_us", "p99_us", "max_us"))
        print(f'| {row["motions"]} x {row["joints"]} | {row["mode"]} | {row["workers"]} | '
              f'{"synthetic" if int(row["overlap_iterations"]) else "none"} | {timing} | '
              f'{count["ready_jobs"]} | {count["fallback_motions"]} | {count["wasted_sampled_joints"]} |')


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("Pass one or more complete timing CSV files")
    for argument in sys.argv[1:]:
        report(argument)
