"""Generate tests for actual curve sampling methods and viewer math."""
from pathlib import Path
import argparse
import re

ROOT = Path(__file__).resolve().parents[1]
KINDS = ("Scale", "Rotation", "Position")

HARNESS = r"""
#include "linden_common.h"
#include "llmath.h"
#include "llquaternion.h"
#include "v3math.h"
#include "fsanimationkeys.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <random>
#include <type_traits>
#include <vector>

namespace CurveFixture
{
unsigned comparisons = 0;
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
#undef llassert
#define llassert(condition) require(bool(condition), "Production curve assertion failed")
enum InterpolationType { IT_STEP, IT_LINEAR, IT_SPLINE };
struct ScaleKey { F32 mTime = 0.f; LLVector3 mScale; };
struct RotationKey { F32 mTime = 0.f; LLQuaternion mRotation; };
struct PositionKey { F32 mTime = 0.f; LLVector3 mPosition; };
template<class Key> struct Traits;
template<> struct Traits<ScaleKey>
{
    using Value = LLVector3;
    static const Value& read(const ScaleKey& key) { return key.mScale; }
    static Value zero() { return LLVector3(0,0,0); }
    static Value blend(F32 u, const ScaleKey& a, const ScaleKey& b) { return lerp(a.mScale, b.mScale, u); }
};
template<> struct Traits<PositionKey>
{
    using Value = LLVector3;
    static const Value& read(const PositionKey& key) { return key.mPosition; }
    static Value zero() { return LLVector3(0,0,0); }
    static Value blend(F32 u, const PositionKey& a, const PositionKey& b) { return lerp(a.mPosition, b.mPosition, u); }
};
template<> struct Traits<RotationKey>
{
    using Value = LLQuaternion;
    static const Value& read(const RotationKey& key) { return key.mRotation; }
    static Value zero() { return LLQuaternion::DEFAULT; }
    static Value blend(F32 u, const RotationKey& a, const RotationKey& b) { return nlerp(u, a.mRotation, b.mRotation); }
};
template<class Key> struct CurveData
{
    using key_type = Key;
    using value_type = typename Traits<Key>::Value;
    using key_map_t = FSAnimationKeys::KeyMap<F32, Key>;
    InterpolationType mInterpolationType = IT_LINEAR;
    S32 mNumKeys = 0;
    key_map_t mKeys;
    Key mLoopInKey, mLoopOutKey;
};
class LLKeyframeMotion
{
public:
    static constexpr auto IT_STEP = CurveFixture::IT_STEP;
    static constexpr auto IT_LINEAR = CurveFixture::IT_LINEAR;
    static constexpr auto IT_SPLINE = CurveFixture::IT_SPLINE;
    using ScaleKey = CurveFixture::ScaleKey;
    using RotationKey = CurveFixture::RotationKey;
    using PositionKey = CurveFixture::PositionKey;
    class ScaleCurve : public CurveData<ScaleKey>
    {
    public:
        @SCALE_GET_DECL@;
        @SCALE_INTERP_DECL@;
    };
    class RotationCurve : public CurveData<RotationKey>
    {
    public:
        @ROTATION_GET_DECL@;
        @ROTATION_INTERP_DECL@;
    };
    class PositionCurve : public CurveData<PositionKey>
    {
    public:
        @POSITION_GET_DECL@;
        @POSITION_INTERP_DECL@;
    };
};
@METHODS@

// Frozen legacy lookup/interpolation behavior, independent of production interp.
template<class Curve> typename Curve::value_type legacy(Curve& curve, F32 time, F32 duration)
{
    using T = Traits<typename Curve::key_type>;
    if (curve.mKeys.empty()) return T::zero();
    auto right = curve.mKeys.lower_bound(time);
    if (right == curve.mKeys.end()) { --right; return T::read(right->second); }
    if (right == curve.mKeys.begin() || right->first == time) return T::read(right->second);
    auto left = right; --left;
    F32 indexBefore = left->first, indexAfter = right->first;
    auto& before = left->second;
    auto& after = right->second;
    if (right == curve.mKeys.end()) { after = curve.mLoopInKey; indexAfter = duration; }
    const F32 u = (time - indexBefore) / (indexAfter - indexBefore);
    return curve.mInterpolationType == IT_STEP ? T::read(before) : T::blend(u, before, after);
}
const F32* data(const LLVector3& value) { return value.mV; }
const F32* data(const LLQuaternion& value) { return value.mQ; }
constexpr std::size_t lanes(const LLVector3&) { return 3; }
constexpr std::size_t lanes(const LLQuaternion&) { return 4; }
template<class Value> bool exact(const Value& a, const Value& b)
{
    return std::memcmp(data(a), data(b), lanes(a) * sizeof(F32)) == 0;
}
void append(std::vector<unsigned char>& bytes, const void* input, std::size_t size)
{
    const auto* first = static_cast<const unsigned char*>(input);
    bytes.insert(bytes.end(), first, first + size);
}
template<class Key> void appendKey(std::vector<unsigned char>& bytes, const Key& key)
{
    append(bytes, &key.mTime, sizeof(F32));
    const auto& value = Traits<Key>::read(key);
    append(bytes, data(value), lanes(value) * sizeof(F32));
}
template<class Curve> std::vector<unsigned char> state(const Curve& curve)
{
    std::vector<unsigned char> bytes;
    append(bytes, &curve.mNumKeys, sizeof(curve.mNumKeys));
    append(bytes, &curve.mInterpolationType, sizeof(curve.mInterpolationType));
    appendKey(bytes, curve.mLoopInKey); appendKey(bytes, curve.mLoopOutKey);
    for (const auto& item : curve.mKeys)
    {
        append(bytes, &item.first, sizeof(F32)); appendKey(bytes, item.second);
    }
    return bytes;
}
template<class Curve> void check(Curve& curve, const std::vector<F32>& times)
{
    const auto before = state(curve);
    auto reference = curve;
    for (F32 duration : {0.f, 0.75f, 3.f, 100.f})
        for (F32 time : times)
        {
            const auto expected = legacy(reference, time, duration);
            const auto actual = curve.getValue(time, duration);
            require(exact(expected, actual), "Sampling changed legacy result bytes");
            ++comparisons;
        }
    require(state(curve) == before && state(reference) == before, "Sampling modified keys or metadata");
}
template<class Curve> void setKeys(Curve& curve, const std::vector<typename Curve::key_type>& keys)
{
    curve.mKeys.clear(); curve.mNumKeys = static_cast<S32>(keys.size());
    for (const auto& key : keys) curve.mKeys[key.mTime] = key;
    curve.mLoopInKey = keys.empty() ? typename Curve::key_type{} : keys.front();
    curve.mLoopOutKey = keys.empty() ? typename Curve::key_type{} : keys.back();
    curve.mLoopInKey.mTime = 0.375f; curve.mLoopOutKey.mTime = 1.625f;
}
ScaleKey scale(F32 time, const LLVector3& value) { ScaleKey key; key.mTime=time; key.mScale=value; return key; }
PositionKey position(F32 time, const LLVector3& value) { PositionKey key; key.mTime=time; key.mPosition=value; return key; }
RotationKey rotation(F32 time, const LLQuaternion& value) { RotationKey key; key.mTime=time; key.mRotation=value; return key; }
int run()
{
    using SC = LLKeyframeMotion::ScaleCurve;
    using PC = LLKeyframeMotion::PositionCurve;
    using RC = LLKeyframeMotion::RotationCurve;
#ifdef FS_EXPECT_CONST_CURVES
    static_assert(std::is_invocable_v<decltype(&SC::getValue), const SC&, F32, F32>, "Scale sampler must be const");
    static_assert(std::is_invocable_v<decltype(&PC::getValue), const PC&, F32, F32>, "Position sampler must be const");
    static_assert(std::is_invocable_v<decltype(&RC::getValue), const RC&, F32, F32>, "Rotation sampler must be const");
    static_assert(std::is_invocable_v<decltype(&SC::interp), const SC&, F32, const ScaleKey&, const ScaleKey&>);
    static_assert(std::is_invocable_v<decltype(&PC::interp), const PC&, F32, const PositionKey&, const PositionKey&>);
    static_assert(std::is_invocable_v<decltype(&RC::interp), const RC&, F32, const RotationKey&, const RotationKey&>);
#endif
    const std::vector<F32> times = {-10.f,-0.f,0.f,0.125f,0.25f,0.5f,0.75f,1.f,1.5f,2.f,2.5f,3.f,10.f,1.e6f};
    SC sc; PC pc; RC rc;
    require(exact(sc.getValue(0,1), LLVector3(0,0,0)) && exact(pc.getValue(0,1), LLVector3(0,0,0)) &&
            exact(rc.getValue(0,1), LLQuaternion::DEFAULT), "Empty curve defaults changed");
    for (auto mode : {IT_STEP,IT_LINEAR,IT_SPLINE})
    {
        sc.mInterpolationType=pc.mInterpolationType=rc.mInterpolationType=mode;
        setKeys(sc, {}); setKeys(pc, {}); setKeys(rc, {});
        check(sc,times); check(pc,times); check(rc,times);
        const LLVector3 a(1,2,3), b(5,6,7);
        setKeys(sc,{scale(0,a),scale(2,b)}); setKeys(pc,{position(0,a),position(2,b)});
        setKeys(rc,{rotation(0,LLQuaternion::DEFAULT),rotation(2,LLQuaternion::DEFAULT)});
        require(exact(sc.getValue(-10,3),a) && exact(sc.getValue(2,3),b) && exact(sc.getValue(10,3),b), "Scale boundary changed");
        require(exact(pc.getValue(-10,3),a) && exact(pc.getValue(2,3),b) && exact(pc.getValue(10,3),b), "Position boundary changed");
        const auto midpoint = mode == IT_STEP ? a : LLVector3(3,4,5);
        require(exact(sc.getValue(1,3),midpoint) && exact(pc.getValue(1,3),midpoint), "Known interpolation changed");
        require(exact(rc.getValue(1,3),LLQuaternion::DEFAULT), "Known identity rotation changed");
        check(sc,times); check(pc,times); check(rc,times);
        setKeys(sc,{scale(1,a),scale(1,b)}); setKeys(pc,{position(1,a),position(1,b)});
        setKeys(rc,{rotation(1,LLQuaternion::DEFAULT)});
        require(sc.mKeys.size()==1 && exact(sc.getValue(1,3),b), "Duplicate timestamp behavior changed");
        check(sc,times); check(pc,times); check(rc,times);
    }
    std::mt19937 random(0x46534355);
    for (unsigned iteration=0; iteration<240; ++iteration)
    {
        std::vector<ScaleKey> scales; std::vector<PositionKey> positions; std::vector<RotationKey> rotations;
        const unsigned count=iteration%5==0 ? 1 : 2+random()%64;
        for(unsigned i=0;i<count;++i)
        {
            const F32 time=static_cast<F32>(i)/2.f+0.25f;
            auto value=[&random] {return static_cast<F32>(static_cast<int>(random()%2049)-1024)/128.f;};
            scales.push_back(scale(time,LLVector3(value(),value(),value())));
            positions.push_back(position(time,LLVector3(value(),value(),value())));
            LLQuaternion q(value(),LLVector3::x_axis); q=q*LLQuaternion(value(),LLVector3::z_axis);
            if(i%2) for(auto& lane:q.mQ) lane=-lane;
            rotations.push_back(rotation(time,q));
        }
        setKeys(sc,scales); setKeys(pc,positions); setKeys(rc,rotations);
        sc.mInterpolationType=pc.mInterpolationType=rc.mInterpolationType=static_cast<InterpolationType>(iteration%3);
        std::vector<F32> queries=times;
        const auto pivot=scales[count/2].mTime;
        queries.push_back(std::nextafter(pivot,-1000.f)); queries.push_back(pivot); queries.push_back(std::nextafter(pivot,1000.f));
        check(sc,queries); check(pc,queries); check(rc,queries);
        // Edited loop metadata must be observed without changing stored key data.
        sc.mLoopInKey.mScale=LLVector3(-2,3,-4); pc.mLoopOutKey.mPosition=LLVector3(4,-3,2);
        rc.mLoopInKey.mRotation=LLQuaternion(0.75f,LLVector3::y_axis);
        check(sc,{pivot,1.f}); check(pc,{pivot,1.f}); check(rc,{pivot,1.f});
    }
    std::cout<<comparisons<<" actual-math curve sampling comparisons passed; key/metadata state unchanged\n";
    return 0;
}
}
int main() { return CurveFixture::run(); }
"""


def function(text: str, signature: str) -> str:
    match = re.search(r"^" + re.escape(signature), text, re.M)
    if match is None:
        raise ValueError(f"Missing production method: {signature}")
    start = match.start()
    brace = text.index("{", start)
    depth = 0
    for match in re.finditer(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|[{}]', text[brace:], re.S):
        token = match.group()
        if token == "{":
            depth += 1
        elif token == "}":
            depth -= 1
            if depth == 0:
                return text[start:brace + match.end()]
    raise ValueError("Unterminated production method")


def generate(source: Path, reference_storage: bool = False) -> str:
    text = source.read_text(encoding="utf-8")
    result = HARNESS
    if reference_storage:
        # The container experiment controls storage independently of the viewer build option.
        result = result.replace('#include "fsanimationkeys.h"\n', '')
        result = result.replace('FSAnimationKeys::KeyMap<F32, Key>', 'std::map<F32, Key>')
    methods = []
    for kind in KINDS:
        value = "LLQuaternion" if kind == "Rotation" else "LLVector3"
        for name, tag in (("getValue", "GET"), ("interp", "INTERP")):
            prefix = f"{value} LLKeyframeMotion::{kind}Curve::{name}("
            method = function(text, prefix)
            if reference_storage:
                method = re.sub(r'    // <FS:Perf> Direct one-key sampling only in the experimental build\.\n'
                                r'#if FS_CONTIGUOUS_ANIMATION_KEYS\n.*?#endif\n    // </FS:Perf>\n', '', method, flags=re.S)
                if "FS_CONTIGUOUS_ANIMATION_KEYS" in method:
                    raise ValueError("Reference experiment could not isolate the original sampling path")
            declaration = re.sub(r"//[^\n]*", "", method[:method.index("{")]).strip()
            declaration = declaration.replace(f"LLKeyframeMotion::{kind}Curve::", "", 1)
            result = result.replace(f"@{kind.upper()}_{tag}_DECL@", declaration)
            methods.append(method)
    return result.replace("@METHODS@", "\n\n".join(methods))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "llkeyframemotion.cpp")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(generate(args.source), encoding="utf-8")
