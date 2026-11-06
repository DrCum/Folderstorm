"""Validate context-only benchmark output and report paired per-call costs."""
import csv
import statistics
import sys
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def report(path):
    rows = list(csv.reader(Path(path).open(encoding="utf-8-sig")))
    require(len(rows) == 38 and rows[1] == ["stage", "sample", "iterations", "elapsed_ns", "checksum"],
            "Incomplete or unexpected context timing matrix")
    meta = dict(zip(rows[0][1::2], rows[0][2::2], strict=True))
    require(meta["samples"] == "9" and meta["separate_guard_object"] == "1", "Wrong timing contract")
    iterations = int(meta["iterations"])
    require(iterations > 0, "Invalid iteration count")
    stages = ("baseline_owner", "capture_owner", "baseline_worker", "guard_worker")
    data = {}
    for stage, sample, count, elapsed, checksum in rows[2:]:
        key = stage, int(sample)
        require(stage in stages and 0 <= key[1] < 9 and key not in data, "Duplicate/unknown timing identity")
        require(int(count) == int(checksum) == iterations and int(elapsed) > 0, "Missing work or invalid duration")
        data[key] = int(elapsed) / iterations
    require(set(data) == {(stage, i) for stage in stages for i in range(9)}, "Missing context samples")
    print(f"{path}: 36 complete timing samples, {iterations} observed calls each")
    for stage, baseline in (("capture_owner", "baseline_owner"), ("guard_worker", "baseline_worker")):
        observed = statistics.median(data[stage, i] for i in range(9))
        paired = statistics.median(data[stage, i] - data[baseline, i] for i in range(9))
        print(f"{stage}: median {observed:.3f} ns/call; paired baseline-subtracted {paired:.3f} ns/call")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Pass a context-cost CSV file")
    report(sys.argv[1])
