"""Generate a private controller translation unit with two explicit test seams.

The viewer always compiles the original file. Fail if either source boundary
changes rather than silently benchmarking a different path.
"""
import argparse
import hashlib
import json
from pathlib import Path


def generate(source: Path, output: Path):
    raw = source.read_bytes()
    text = raw.decode("utf-8", errors="surrogateescape")
    declaration = '#include "llmotioncontroller.h"'
    newline = "\r\n" if b"\r\n" in raw else "\n"
    update = "void LLMotionController::updateMotions(bool force_update)" + newline + "{"
    callback = "    return motion.onUpdate(time, mask);"
    for boundary in (declaration, update, callback):
        if text.count(boundary) != 1:
            raise ValueError(f"Expected exactly one controller boundary: {boundary!r}")
    text = text.replace(declaration, declaration + """
namespace FSEventSampling
{
using TimePredict = bool (LLMotionController::*)(LLMotion&, const LLMotionController::SamplingClock&, F32&) const;
using MaskPredict = bool (LLMotionController::*)(const LLMotion&, U8 (&)[2][LL_CHARACTER_MAX_ANIMATED_JOINTS]) const;
void prepare(LLMotionController&, bool, TimePredict, MaskPredict);
bool invoke(LLMotionController&, LLMotion&, F32, U8*);
}
""", 1)
    text = text.replace(update, update + """
    FSEventSampling::prepare(*this, force_update,
        &LLMotionController::predictSamplingTime, &LLMotionController::predictSamplingMask);
""", 1)
    text = text.replace(callback, "    return FSEventSampling::invoke(*this, motion, time, mask);", 1)
    # Preserve original legacy comment bytes, as well as all controller arithmetic.
    generated = text.encode("utf-8", errors="surrogateescape")
    output.write_bytes(generated)
    output.with_suffix(".provenance.json").write_text(json.dumps({
        "source": str(source.resolve()), "source_sha256": hashlib.sha256(raw).hexdigest(),
        "generated_sha256": hashlib.sha256(generated).hexdigest(),
        "test_seams": ["before updateMotions body", "existing invokeMotionUpdate callback"],
        "viewer_source_modified": False,
    }, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    generate(args.source, args.output)
