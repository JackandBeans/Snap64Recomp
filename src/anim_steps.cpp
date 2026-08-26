/**
 * @file anim_steps.cpp
 * @brief Which poses the game meant to jump to rather than move to.
 *
 * Animation channels in this game come in two kinds. Most describe motion: a
 * value and a rate, sampled continuously, which is exactly what interpolation
 * was built to smooth. Some are steps -- sys/anim.c installs them as
 * ANIM_TYPE_STEP and reads them back as "invDuration <= time ? target :
 * initial", so they hold one value and then change to another with nothing in
 * between. The console drew the old value on one frame and the new one on the
 * next, because that is the only shape a step can have at thirty frames a
 * second.
 *
 * Blending a step draws the object at positions it was never in, and two of
 * this port's oldest artifacts are that and nothing else: the camera in Todd's
 * hand sliding out of his grip just before the intro's close-up cut, and a leaf
 * of the intro's scenery spending a frame part-way through a thousand-unit jump
 * -- which, since the movie sets its fog wall at 989 units, is drawn beyond the
 * fog in solid fog colour and reads as the grass vanishing rather than moving.
 *
 * Every defence the renderer already had asks how MUCH moved: a velocity
 * threshold, a vote across an object's matrices, a census of how many objects
 * changed at once. A step can be small -- the prop moves two and a half units
 * -- and still unmistakable, because it leaves a hand. Magnitude was never the
 * right question, and the game holds the right answer in its own data.
 *
 * This reads that answer and touches nothing. Each step channel is evaluated
 * before and after the game's own update, and an object is called stepped when
 * such a value changed AND the object's transform changed with it. The second
 * half costs eleven floats an object and pays for itself: it ignores steps the
 * game declined to apply -- a model flagged to skip its writes, a channel whose
 * new value never reached a matrix -- without needing to know why.
 *
 * Nothing here writes to game memory. An earlier attempt at the same fix
 * replaced the animation routine outright and broke every scene in the game;
 * the difference is that a wrong answer here can only produce a wrong
 * interpolation decision for one frame. The verdict reaches the renderer
 * through the display list, the way the camera's cut verdict does, so it lands
 * on the frame it was computed for with no state shared across threads.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <atomic>

#include <chrono>

#include "hle/rt64_snap_diag.h"
#include "recomp.h"

// The present-queue capture runs while this is positive (src/frame_dump.cpp).
extern "C" std::atomic<int32_t> snap_frame_dump_pending;

extern "C" {
#include "funcs.h"
}

namespace snap {

// Reserved values and layouts from the decompilation (sys/anim.h, sys/om.h).
namespace {

constexpr uint32_t AnimTypeStep = 1;         // ANIM_TYPE_STEP
constexpr uint32_t AnimParamModelMin = 1;    // the model transform channels
constexpr uint32_t AnimParamModelMax = 10;
constexpr float AnimationDisabled = -3.4028235e38f;   // FLOAT_NEG_MAX

// AObj: next 0x00, paramID 0x04, kind 0x05, invDuration 0x08, time 0x0C,
// initialValue 0x10, targetValue 0x14.
// DObj: next 0x08, firstChild 0x10, position 0x18, rotation 0x28, scale 0x3C,
// aobjList 0x6C, timeLeft 0x74.
// GObj: data.dobj 0x48 (0x3C is cmdList, which is not it).

constexpr uint32_t MaxSteppedObjects = 16;   // matches the renderer's capacity
constexpr uint32_t MaxStepSamples = 1024;
// The intro's scenery is a single object of 226 nodes, so a limit chosen for
// a character silently excluded the very tree carrying the artifact: every
// frame failed closed and the scene never received a verdict at all. Sized for
// the largest tree the game builds, with room over it.
constexpr uint32_t MaxTrackedDObjs = 384;
constexpr uint32_t TransformFloats = 11;     // position 3, rotation 5, scale 3

uint32_t g_stepped_objects[MaxSteppedObjects];
uint32_t g_stepped_object_count = 0;

struct StepSample {
    uint32_t dobj;
    uint32_t paramID;
    float value;
};

bool valid_ram_address(uint32_t address) {
    // KSEG0 only: the MEM_ macros subtract 0x80000000 without masking, so a
    // KSEG1 pointer that passes a masked range test is then read far outside
    // what the runtime committed. See the note in matrix_tags.cpp.
    return ((address >> 29) == 4u) && ((address & 0x1FFFFFFFu) < 0x00800000u);
}

float read_f32(uint8_t* rdram, uint32_t addr) {
    const uint32_t bits = static_cast<uint32_t>(MEM_W(0, (gpr)(int32_t)addr));
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

void read_transform(uint8_t* rdram, uint32_t dobj, float* out) {
    for (int i = 0; i < 3; i++) {
        out[i] = read_f32(rdram, dobj + 0x18 + i * 4);
    }
    for (int i = 0; i < 5; i++) {
        out[3 + i] = read_f32(rdram, dobj + 0x28 + i * 4);
    }
    for (int i = 0; i < 3; i++) {
        out[8 + i] = read_f32(rdram, dobj + 0x3C + i * 4);
    }
}

// Walks a model tree the way the game does and records what every step channel
// currently reads, plus each object's transform. Bounded at every step; a tree
// that outgrows the snapshot records nothing and returns false, which leaves
// the frame behaving exactly as it did before this file existed.
bool sample_steps(uint8_t* rdram, uint32_t rootDObj, StepSample* samples, uint32_t& sampleCount,
                  float* transforms, uint32_t& transformCount) {
    sampleCount = 0;
    transformCount = 0;

    uint32_t stack[MaxTrackedDObjs];
    uint32_t depth = 0;
    stack[depth++] = rootDObj;

    while (depth > 0) {
        const uint32_t dobj = stack[--depth];
        if (!valid_ram_address(dobj)) {
            continue;
        }

        if (transformCount >= MaxTrackedDObjs) {
            if (snapdiag::diagEnabled() || snapdiag::statsEnabled()) {
                static bool warned = false;
                if (!warned) {
                    warned = true;
                    printf("[SNAP-STEP] a model tree is larger than the snapshot; its steps go unnoticed\n");
                    fflush(stdout);
                }
            }
            return false;
        }
        read_transform(rdram, dobj, transforms + transformCount * TransformFloats);
        transformCount++;

        if (read_f32(rdram, dobj + 0x74) != AnimationDisabled) {
            uint32_t aobj = static_cast<uint32_t>(MEM_W(0x6C, (gpr)(int32_t)dobj));
            for (int guard = 0; valid_ram_address(aobj) && (guard < 32); guard++) {
                const uint32_t kind = static_cast<uint32_t>(MEM_B(0x05, (gpr)(int32_t)aobj)) & 0xFFu;
                const uint32_t paramID = static_cast<uint32_t>(MEM_B(0x04, (gpr)(int32_t)aobj)) & 0xFFu;
                if ((kind == AnimTypeStep) && (paramID >= AnimParamModelMin) && (paramID <= AnimParamModelMax)) {
                    if (sampleCount >= MaxStepSamples) {
                        return false;
                    }
                    const float invDuration = read_f32(rdram, aobj + 0x08);
                    const float time = read_f32(rdram, aobj + 0x0C);
                    const float initialValue = read_f32(rdram, aobj + 0x10);
                    const float targetValue = read_f32(rdram, aobj + 0x14);
                    samples[sampleCount].dobj = dobj;
                    samples[sampleCount].paramID = paramID;
                    samples[sampleCount].value = (invDuration <= time) ? targetValue : initialValue;
                    sampleCount++;
                }
                aobj = static_cast<uint32_t>(MEM_W(0x00, (gpr)(int32_t)aobj));
            }
        }

        const uint32_t child = static_cast<uint32_t>(MEM_W(0x10, (gpr)(int32_t)dobj));
        const uint32_t sibling = static_cast<uint32_t>(MEM_W(0x08, (gpr)(int32_t)dobj));
        if (valid_ram_address(child)) {
            if (depth >= MaxTrackedDObjs) {
                return false;
            }
            stack[depth++] = child;
        }
        if (valid_ram_address(sibling)) {
            if (depth >= MaxTrackedDObjs) {
                return false;
            }
            stack[depth++] = sibling;
        }
    }
    return true;
}

} // namespace

// Records an object whose animation stepped. Duplicates are dropped: one
// object stepping several channels at once is still one verdict.
void note_stepped_object(uint32_t gobj) {
    if ((gobj == 0) || (g_stepped_object_count >= MaxSteppedObjects)) {
        return;
    }
    for (uint32_t i = 0; i < g_stepped_object_count; i++) {
        if (g_stepped_objects[i] == gobj) {
            return;
        }
    }
    g_stepped_objects[g_stepped_object_count++] = gobj;
    // Photograph what the screen actually shows across an authored step. The
    // whole chain verifies in logs; only the pictures can say whether the
    // object still travels between its two poses.
    if (snapdiag::captureEnabled()) {
        snap_frame_dump_pending.store(14);
    }
    // Diagnostics only. Under the stats gate this printed and flushed once
    // per stepped object, up to sixteen times, on precisely the frames worth
    // measuring: a spawn poses every Pokemon it creates, so they all step at
    // once. A console flush on Windows is a synchronous write to the
    // terminal, and sixteen of them put milliseconds into the very number
    // the gate exists to report. The gate promises no flushes; this is the
    // site that broke it, and it made a measured 46 ms frame untrustworthy.
    if (snapdiag::diagEnabled()) {
        printf("[SNAP-STEP] object %08X stepped to its pose this frame\n", gobj);
        fflush(stdout);
    }
}

// Read by the camera hook in matrix_tags.cpp, which writes these into the
// frame's display list and then clears them.
uint32_t stepped_object_count() {
    return g_stepped_object_count;
}

uint32_t stepped_object(uint32_t index) {
    return (index < g_stepped_object_count) ? g_stepped_objects[index] : 0;
}

void clear_stepped_objects() {
    g_stepped_object_count = 0;
}

} // namespace snap

extern "C" void animUpdateModelTreeAnimation(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t gobj = static_cast<uint32_t>(ctx->r4);

    // Only the outermost call is measured. A model's animation callback can
    // re-enter the animation system, and a nested view of a tree is a view of a
    // tick already half applied.
    static int depth = 0;
    const bool outermost = (depth == 0);
    depth++;

    static snap::StepSample beforeSamples[snap::MaxStepSamples];
    static snap::StepSample afterSamples[snap::MaxStepSamples];
    static float beforeTransforms[snap::MaxTrackedDObjs * snap::TransformFloats];
    static float afterTransforms[snap::MaxTrackedDObjs * snap::TransformFloats];
    uint32_t beforeCount = 0, afterCount = 0;
    uint32_t beforeTransformCount = 0, afterTransformCount = 0;
    bool sampled = false;
    uint32_t rootDObj = 0;

    const bool timing = outermost && snapdiag::statsEnabled();
    const auto beforeStart = timing ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};

    if (outermost && snap::valid_ram_address(gobj)) {
        rootDObj = static_cast<uint32_t>(MEM_W(0x48, (gpr)(int32_t)gobj));   // GObj::data.dobj
        if (snap::valid_ram_address(rootDObj)) {
            sampled = snap::sample_steps(rdram, rootDObj, beforeSamples, beforeCount,
                                         beforeTransforms, beforeTransformCount);
        }
    }

    if (timing) {
        snapdiag::animHookNanos().fetch_add(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - beforeStart).count(),
            std::memory_order_relaxed);
        snapdiag::animHookCounter().fetch_add(1, std::memory_order_relaxed);
    }

    __real_animUpdateModelTreeAnimation(rdram, ctx);

    depth--;

    // The rest of this function is the port's too, so it is timed as well.
    struct AfterTimer {
        bool on;
        std::chrono::steady_clock::time_point start;
        ~AfterTimer() {
            if (on) {
                snapdiag::animHookNanos().fetch_add(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - start).count(),
                    std::memory_order_relaxed);
            }
        }
    } afterTimer{timing, timing ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}};

    if (!sampled || (snap::stepped_object_count() >= snap::MaxSteppedObjects)) {
        return;
    }

    if (!snap::sample_steps(rdram, rootDObj, afterSamples, afterCount,
                            afterTransforms, afterTransformCount)) {
        return;
    }

    // Matched by which object and which parameter, not by position. The two
    // walks are not guaranteed to produce the same list: parsing the next
    // animation command creates channels that did not exist a moment ago
    // (omDObjAddAObj), and it does so on exactly the ticks worth catching --
    // comparing the lists position by position gave up precisely when a key
    // fired. A channel present in only one snapshot is not evidence either way
    // and is passed over.
    bool stepped = false;
    for (uint32_t i = 0; (i < beforeCount) && !stepped; i++) {
        for (uint32_t j = 0; j < afterCount; j++) {
            if ((beforeSamples[i].dobj == afterSamples[j].dobj) &&
                (beforeSamples[i].paramID == afterSamples[j].paramID)) {
                if (beforeSamples[i].value != afterSamples[j].value) {
                    stepped = true;
                }
                break;
            }
        }
    }

    // And the pose has to have actually moved with it, which ignores steps the
    // game itself declined to apply without needing to know why it declined.
    if (stepped) {
        const uint32_t common = (beforeTransformCount < afterTransformCount) ?
            beforeTransformCount : afterTransformCount;
        bool transformMoved = false;
        for (uint32_t i = 0; (i < common * snap::TransformFloats) && !transformMoved; i++) {
            if (beforeTransforms[i] != afterTransforms[i]) {
                transformMoved = true;
            }
        }
        stepped = transformMoved;
    }

    if (stepped) {
        snap::note_stepped_object(gobj);
    }
}
