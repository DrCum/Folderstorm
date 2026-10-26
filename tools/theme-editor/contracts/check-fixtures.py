"""Replay the exact same files through the C# and production C++ validators."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--dotnet", required=True)
parser.add_argument("--cli", required=True)
parser.add_argument("--native", required=True)
arguments = parser.parse_args()
root = Path(__file__).with_name("fixtures")
cases = json.loads((root / "cases.json").read_text(encoding="utf-8"))
results = []
for case in cases:
    entry = {"case": case["name"], "expected": case["expected"]}
    for name, command in [("managed", [arguments.dotnet, arguments.cli]), ("native", [arguments.native])]:
        process = subprocess.run(command + ["manifest", str(root / case["file"])], text=True, encoding="utf-8", capture_output=True, timeout=20)
        result = "valid" if process.returncode == 0 else process.stderr.strip()
        if name == "managed" and process.returncode:
            try:
                result = json.loads(result)["error"]
            except (ValueError, KeyError):
                pass
        entry[name] = result
    entry["passed"] = entry["managed"] == entry["native"] == entry["expected"]
    results.append(entry)
print(json.dumps(results, indent=2, ensure_ascii=False))
sys.exit(0 if all(result["passed"] for result in results) else 1)
