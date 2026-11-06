"""Check the packaged trial boundary; native tests exercise the production path."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--base-ref", help="Also compare every preceding packaged control with this local Git ref")
args = parser.parse_args()
root = Path(__file__).resolve().parents[3]
checks = []

def require(value, label):
    if not value:
        raise AssertionError(label)
    checks.append(label)

def code(path):
    # ASCII identifier scan also covers legacy source comments with non-UTF8 bytes.
    text = (root / path).read_text(encoding="utf-8", errors="surrogateescape")
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)

def entries(raw):
    tree = ET.fromstring(raw)
    children = list(tree.find("map"))
    require(len(children) % 2 == 0, "Packaged root has complete key/value pairs")
    require(all(children[n].tag == "key" for n in range(0, len(children), 2)), "Packaged root alternates keys and values")
    return tree, {children[n].text: children[n + 1] for n in range(0, len(children), 2)}

settings_path = "indra/newview/app_settings/settings.xml"
tree, settings = entries((root / settings_path).read_bytes())
key = "FSGeneralPoolEventWait"
require(key in settings and len(tree.findall(f".//key[.='{key}']")) == 1, "Trial setting occurs once at the root")
fields = list(settings[key])
fields = {fields[n].text: fields[n + 1] for n in range(0, len(fields), 2)}
require(fields["Type"].text == "Boolean" and fields["Value"].text == "0", "Packaged trial defaults FALSE")
require(fields["Persist"].text == "0", "Trial control is nonpersistent")

app = code("indra/newview/llappviewer.cpp")
start = app.index("void LLAppViewer::initGeneralThread()")
body = app[start:app.index("bool LLAppViewer::initThreads()", start)]
require(app.count(f'getBOOL("{key}")') == 1 and f'getBOOL("{key}")' in body, "Only General startup reads the trial setting")
require(re.search(r'new LL::ThreadPool\("General",\s*3,\s*1024\s*\*\s*1024,\s*true,\s*'
                  r'request_event_wait\s*\?\s*WaitPolicy::EventWaitSingleWorker\s*:\s*WaitPolicy::PlatformDefault\)', body),
        "Only the explicit General request preserves fallback width, capacity and shutdown")
require("GeneralWaitPolicy requested=" in body and '" effective="' in body and '" threads="' in body,
        "Startup diagnostic exposes requested/effective policy and actual width")
require(body.index("mGeneralThreadPool->start()") < body.index("GeneralWaitPolicy requested="),
        "Reported width is observed after start")

header = code("indra/llcommon/threadpool.h")
implementation = code("indra/llcommon/threadpool.cpp")
require(header.count("WaitPolicy wait_policy = WaitPolicy::PlatformDefault") == 2, "Both constructor APIs retain default platform policy")
require("const WaitPolicy mWaitPolicy" in header and header.index("size_t mThreadCount") < header.index("const WaitPolicy mWaitPolicy"),
        "Effective policy is immutable and follows resolved width storage")
require(re.search(r'#if LL_WINDOWS\s+wait_policy == WaitPolicy::EventWaitSingleWorker\s*&&\s*mThreadCount == 1\s*\?\s*'
                  r'WaitPolicy::EventWaitSingleWorker\s*:\s*WaitPolicy::PlatformDefault\s*#else\s*'
                  r'WaitPolicy::PlatformDefault\s*#endif', implementation), "Only resolved single-worker Windows requests are eligible")
run_start = implementation.index("void LL::ThreadPoolBase::run(const std::string& name)")
run = implementation[run_start:implementation.index("void LL::ThreadPoolBase::run()", run_start)]
require(re.search(r'#if LL_WINDOWS\s+if\s*\(mWaitPolicy == WaitPolicy::EventWaitSingleWorker\)\s*'
                  r'boost::fibers::use_scheduling_algorithm<boost::fibers::algo::round_robin>\(\);\s*else\s*'
                  r'boost::fibers::use_scheduling_algorithm<sleepy_robin>\(\);\s*#endif', run),
        "Production installation retains polling except eligible startup opt-in")

uses = set()
for path in (root / "indra").rglob("*"):
    if path.suffix not in (".h", ".cpp") or "tests" in path.parts:
        continue
    relative = path.relative_to(root).as_posix()
    if "EventWaitSingleWorker" in code(relative):
        uses.add(relative)
require(uses == {"indra/llcommon/threadpool.h", "indra/llcommon/threadpool.cpp", "indra/newview/llappviewer.cpp"},
        "No other production caller supplies the event-wait policy")
fixture = code("indra/llcommon/tests/threadpool_wait_policy_test.cpp")
require("use_scheduling_algorithm" not in fixture and not re.search(r'void\s+run\s*\(', fixture),
        "New native fixture does not override scheduler installation")
if args.base_ref:
    _, old = entries(subprocess.check_output(["git", "show", f"{args.base_ref}:{settings_path}"], cwd=root))
    new = {name: ET.tostring(value) for name, value in settings.items() if name != key}
    require(new == {name: ET.tostring(value) for name, value in old.items()}, "Every preceding packaged control is unchanged")
print(json.dumps({"checks": len(checks), "status": "passed", "details": checks,
                  "scope": "Source/packaging boundary only; native and full viewer acceptance remain separate"}, indent=2))
