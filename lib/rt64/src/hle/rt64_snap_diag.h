/**
 * @file rt64_snap_diag.h
 * @brief Shared pieces of the Pokemon Snap port's diagnostics.
 *
 * Two things live here, both born from the code review of the transition-cut
 * work.
 *
 * The gate: the port accumulated a family of [SNAP-*] printf probes across
 * the renderer while the corner flash was being hunted. Left always-on they
 * cost formatting and flushes in hot paths and they are release-cleanup debt
 * scattered across a vendored fork. Every probe family now asks diagEnabled()
 * first, so shipping quiet builds is one environment variable, and stripping
 * the probes later is a search for one symbol.
 *
 * The dump helpers: both capture rigs (the game-side RDRAM dumper in
 * src/frame_dump.cpp and the present-queue capture) previously hand-rolled
 * their own BMP writer, directory creation, and file numbering. The writers
 * had diverging row-padding rules and both counted failed writes as
 * successes. One implementation now serves both, reports failure honestly,
 * and stamps every filename with a per-run token so a later run can never
 * silently overwrite an earlier run's evidence.
 */

#pragma once

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <direct.h>

namespace snapdiag {

// One switch for every [SNAP-*] diagnostic probe family.
inline bool diagEnabled() {
    static const bool enabled = (std::getenv("SNAP_DIAG") != nullptr);
    return enabled;
}

// Low-overhead measurement: the summary lines alone, no per-frame walks, no
// flushes, no captures. The heavy probes perturb frame pacing enough to
// change the very timing-sensitive behavior they are measuring, so numbers
// meant to describe how the game actually runs are gathered under this.
inline bool statsEnabled() {
    static const bool enabled = (std::getenv("SNAP_STATS") != nullptr);
    return enabled;
}

// One switch for both frame-capture rigs.
inline bool captureEnabled() {
    static const bool enabled = (std::getenv("SNAP_CAPTURE") != nullptr);
    return enabled;
}

// Counts frames the renderer held instead of showing, so the pacing meter can
// say whether a stutter is the hold system's own doing. A hold copies a whole
// render target and waits for the GPU to finish, which is real time spent on
// exactly the frames that are already the heaviest in the scene.
inline std::atomic<uint32_t> &holdCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

// What the renderer was asked for against what it managed. Interpolation is
// asked for one fully rendered frame per display refresh, so a high refresh
// display asks for a great many; when a frame cannot be finished in its slice
// of the tick it is dropped, and the motion in the frames that do arrive
// advances unevenly even though they arrive on a perfect schedule. Wall-clock
// pacing cannot see that -- these can.
inline std::atomic<uint32_t> &subFrameAskedCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

inline std::atomic<uint32_t> &subFrameDroppedCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

inline std::atomic<uint32_t> &workloadDroppedCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

// When the newest game state started being turned into pictures. Interpolation
// places display frames between two game frames, so what is on screen is
// always some distance behind the newest state the game has computed; this is
// how that distance gets measured instead of assumed.
inline std::atomic<int64_t> &newestStateNanos() {
    static std::atomic<int64_t> nanos{0};
    return nanos;
}

// Specialised shaders asked for and finished. Turning the camera reveals
// materials the renderer has never drawn before, and each one is compiled and
// turned into a graphics pipeline while the game runs. Pipeline creation goes
// through the driver, which can hold the same locks the drawing thread needs,
// so a burst of new materials is a plausible source of a long freeze -- these
// let a stall be attributed instead of guessed at.
inline std::atomic<uint32_t> &shaderAskedCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

inline std::atomic<uint32_t> &shaderReadyCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

// Game frames that were interpolated but then presented as a single image
// because the presented buffer could not be matched to what the renderer had
// drawn. Nine frames of smooth motion collapse into one, which on screen is a
// whole game frame where nothing moves.
inline std::atomic<uint32_t> &interpolationUnusedCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

// Ticks that produced a single image instead of a full set of interpolated
// ones, and interpolated frames whose weight did not advance forwards. The
// first is a game frame where the picture stands still; the second puts two
// different poses of the same motion on screen close enough together to be
// seen as two of everything.
inline std::atomic<uint32_t> &singleFrameTickCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

inline std::atomic<uint32_t> &weightWentBackwardsCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

// Which verdict asked for each held frame. A hold during ordinary play
// is a frame the console would have drawn, so knowing which mechanism
// asked for it is the difference between fixing the right one and
// tuning the wrong one.
inline std::atomic<uint32_t> &holdFromCameraCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

// Time the port's own animation hook spends walking model trees, summed over
// every object in a tick. It snapshots each tree before and after the game's
// own animation call, and a frame that creates Pokemon poses every one of them
// synchronously -- so the hook does its most work on exactly the frames that
// are already the slowest.
inline std::atomic<int64_t> &animHookNanos() {
    static std::atomic<int64_t> nanos{0};
    return nanos;
}

inline std::atomic<uint32_t> &animHookCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

// How much of a frame the renderer could actually pair with the frame before
// it. Anything unpaired is drawn at its current pose for every one of the
// display frames in that tick, so it steps once per game frame while
// everything around it glides -- which is most visible exactly when the camera
// moves and the whole screen is in motion.
inline std::atomic<uint32_t> &transformsSeenCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

// Every screen-space rectangle the game drew, named or not.
//
// The paired-versus-seen figure counts only rectangles that already carry a
// name, so it reads a hundred percent while most of the screen is untagged and
// still stepping at the game's rate. That statistic can confirm the mechanism
// works and can never reveal how much of the screen it reaches. This one can.
// Which code drew the rectangles nothing has named. Coverage says how much of
// the screen is reached; these say what is standing in the way of reaching the
// rest, which decides whether a native hook is enough or the function has to be
// replaced on the game side.
// Of the rectangles nothing has named, how many sat in exactly the same place
// as a rectangle on the previous drawn frame.
//
// A rectangle that does not move does not need interpolating: naming it would
// pair it with itself and blend two identical positions. So raw coverage
// overstates the problem, and the number that actually matters is how much of
// the unnamed content MOVES. Matching on exact coordinates cannot tell one
// still rectangle from another, but it does not have to -- if a rectangle with
// these exact corners was on screen last frame too, then whatever drew it did
// not move it.
// Rectangles the renderer actually moved, counted where it moves them. Pairing
// a rectangle and drawing it somewhere between two positions are different
// steps, and only the second one is what anybody sees.
// Which half of the condition to move a rectangle failed. Pairing marks the
// rectangle; the weight says where between the two frames this image sits. If
// rectangles arrive at the renderer still marked but always at weight one, the
// pairing is fine and the image being drawn is simply never between anything.
// Paired rectangles whose SIZE changed between the two drawn frames, and the
// largest such change.
//
// Position and size are lerped together, but they do not mean the same thing. A
// sprite that moves is a sprite moving. A sprite whose rectangle changes size
// has usually changed to a different picture -- animation is a texture swap in
// this game, and consecutive frames of an animation need not be the same
// dimensions. Blending between two sizes then stretches the NEW image smoothly
// across the interval, because the texture coordinates were computed for the
// authored rectangle and the quad is scaled by the viewport. That reads as a
// sprite breathing or popping rather than as a sprite moving.
// Raised by the player, at the moment they see the thing they are reporting.
//
// Every statistic here is averaged over six hundred presents, which is about
// two seconds -- long enough that a fault lasting a few frames is diluted into
// nothing by the frames either side of it. A run that has to be described
// afterwards from memory is exactly the position this project keeps getting
// stuck in. Pressing a key ends the interval on the spot, so the numbers that
// print next describe the moment and not the two seconds around it.
inline std::atomic<uint32_t> &markRequestCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

inline std::atomic<uint32_t> &markSerialCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

inline std::atomic<uint32_t> &rectSizeChangedCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

inline std::atomic<uint32_t> &rectBiggestSizeChangeCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

inline std::atomic<uint32_t> &rectDrawMarkedCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

inline std::atomic<uint32_t> &rectDrawWeightOneCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

// Rectangles uncovered rather than moved: drawn at the size the game asked for
// and clipped to the blended size, so the reveal runs at the display's rate
// without the picture being stretched into the gap.
inline std::atomic<uint32_t> &rectsRevealedCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

inline std::atomic<uint32_t> &rectsLerpedCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

inline std::atomic<uint32_t> &rectsUnnamedStillCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

inline std::atomic<uint32_t> &rectsUnnamedMovedCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

// The menu overlay carries its own private copy of the sprite library, and the
// port only tags the resident one. If the interface screens draw through the
// copy, every rectangle they produce is unnamed however well the resident path
// works.
inline std::atomic<uint32_t> &rectsFromWindowCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

// Background and depth fills. These never move, so they are counted in order to
// be subtracted rather than to be fixed.
inline std::atomic<uint32_t> &rectsFromCameraFillCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

// Transforms the game named with the reserved id zero, which RT64 reads as
// "ignore this one" and drops before any pairing is attempted. A matrix gets
// that id when its serial was never stamped, so this separates content the game
// does not tag at all from content it tags with nothing.
// Of the transforms that did not pair, how many actually MOVED.
//
// The same question that retired the two-dimensional coverage figure. A
// transform nothing paired is only a problem if the thing it draws is going
// somewhere; a background that sits still has the same matrix in both frames,
// and pairing it would blend two identical positions. Matching on the exact
// matrix cannot tell one still object from another, and does not need to -- if
// this exact transform was in the previous frame, whatever it draws did not
// move.
inline std::atomic<uint32_t> &transformsUnpairedStillCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

inline std::atomic<uint32_t> &transformsUnpairedMovedCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

inline std::atomic<uint32_t> &transformsIgnoredCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

inline std::atomic<uint32_t> &rectsFromEffectsCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

inline std::atomic<uint32_t> &rectsFromTextCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

inline std::atomic<uint32_t> &rectsFromPhotoCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

inline std::atomic<uint32_t> &rectsTotalCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

inline std::atomic<uint32_t> &rectsSeenCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

// How many of the game's 2D rectangles carried a name this frame and found the
// same element in the frame before. What does not pair is drawn where the game
// put it, which is correct but steps at the game's rate.
// How many named elements drew a DIFFERENT NUMBER of rectangles than they did
// on the previous drawn frame. The pairing key is (element, ordinal), and the
// ordinal is just the order the element emitted its rectangles in -- so if the
// count moves, rectangle three of an element is a different piece of it than it
// was last frame, and pairing them blends two unrelated pieces together. This
// counts how often that happens, because reading the sprite library's MIPS can
// establish that it is possible but not that it occurs.
inline std::atomic<uint32_t> &rectCountChangedCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

inline std::atomic<uint32_t> &rectElementsCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

// The largest distance, in whole pixels, any paired rectangle travelled between
// two drawn frames. The matcher refuses a pair past 160, so if real motion gets
// near that during play the backstop is silently switching interpolation off
// exactly when the camera moves.
inline std::atomic<uint32_t> &rectBiggestTravelCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

// Pairs the backstop actually refused.
inline std::atomic<uint32_t> &rectTravelRefusedCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

inline std::atomic<uint32_t> &rectsPairedCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

// Transforms the game actually named, as opposed to ones the renderer has to
// guess at by hashing the draw and looking for something nearby. The paired
// figure counts both, so a low pairing rate does not say WHY -- whether the
// game is not tagging that content at all, or is tagging it with something
// unstable. This separates the two.
inline std::atomic<uint32_t> &transformsTaggedCounter() {
    static std::atomic<uint32_t> counter = { 0 };
    return counter;
}

inline std::atomic<uint32_t> &transformsPairedCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

// How far through the world's motion each rendered image sits, in millionths
// of a game frame, published by the thread that draws it and read by the
// thread that shows it.
//
// Everything else here measures frame DELIVERY -- how often a picture arrived
// and how long it took. A player does not see delivery. They see whether the
// world's position climbs evenly against the clock, and a run where images
// arrive perfectly on schedule while the motion in them advances in lurches
// reads as flawless in every other counter in this file. That gap is why a
// port can measure healthy and feel wrong.
//
// Slot zero is the image drawn into the game's own target; slot k+1 is
// interpolated target k.
constexpr uint32_t SnapMotionSlots = 64;

inline std::atomic<int64_t> *motionSlots() {
    static std::atomic<int64_t> slots[SnapMotionSlots];
    return slots;
}

// The motion actually shown, sampled where it is shown.
inline std::atomic<int64_t> &motionShownMicroFrames() {
    static std::atomic<int64_t> value{0};
    return value;
}

inline std::atomic<uint32_t> &motionStillPresentsCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

inline std::atomic<uint32_t> &motionBackwardsCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

inline std::atomic<uint32_t> &motionBiggestStepMicro() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

// How many of the game's logic steps each drawn frame carried, and how often
// a frame carried more than usual -- which means the game skipped a draw
// because the renderer still had the graphics context.
inline std::atomic<uint32_t> &logicStepCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

inline std::atomic<uint32_t> &drawnFrameCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

inline std::atomic<uint32_t> &skippedDrawCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

// The game frame this is all happening on. Everything a report wants to
// correlate -- a slow frame, a Pokemon being created, a block boundary --
// happens on a different thread from the one that counts frames, and
// without a shared number the only way to line them up is by their order in
// a log, which interleaving destroys.
inline std::atomic<uint32_t> &gameFrameCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

// What each verdict ASKED for, before the never-consecutive rule and the
// other guards had their say. A verdict that fires often but holds rarely and
// one that never fires at all are different faults with the same symptom.
inline std::atomic<uint32_t> &stepDeclaredCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

inline std::atomic<uint32_t> &stepIsolatedCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

inline std::atomic<uint32_t> &cameraDeclaredCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

inline std::atomic<uint32_t> &holdFromStepCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

// Work the renderer does ON THE GAME THREAD, which the slow-frame report
// cannot see any other way: its renderer-wait figure covers only the wait
// for the render thread, so anything the game thread does inside the
// display-list walk reads as the game being slow. The framebuffer check is
// broken out because its upload path ends in a fence the game thread
// blocks on.
inline std::atomic<int64_t> &rdramCheckNanos() {
    static std::atomic<int64_t> nanos{0};
    return nanos;
}

inline std::atomic<uint32_t> &rdramUploadCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

// Distinguishes this run's files from every other run's. Derived from the
// launch time, so re-running an experiment adds files instead of replacing
// the evidence the previous run produced.
inline uint32_t runToken() {
    static const uint32_t token = uint32_t(time(nullptr) % 100000u);
    return token;
}

// Creates the dump directory once. A failed creation is remembered as a
// failure, not silently latched as success the way both rigs used to.
inline bool ensureDumpDir() {
    static int state = 0;
    if (state == 0) {
        const int result = _mkdir("snap_frame_dumps");
        state = ((result == 0) || (errno == EEXIST)) ? 1 : -1;
    }
    return state == 1;
}

// Writes a 24-bit BMP from a top-down, tightly packed BGR buffer. Returns
// whether the file was actually produced, so callers can count and report
// honestly.
inline bool writeBMP24(const char *path, uint32_t width, uint32_t height, const uint8_t *bgrTopDown) {
    if ((width == 0) || (height == 0) || (bgrTopDown == nullptr)) {
        return false;
    }

    FILE *f = fopen(path, "wb");
    if (f == nullptr) {
        return false;
    }

    const uint32_t rowBytes = ((width * 3) + 3) & ~3u;
    const uint32_t imageBytes = rowBytes * height;
    const uint32_t fileBytes = 54 + imageBytes;
    const uint8_t header[54] = {
        'B', 'M',
        uint8_t(fileBytes), uint8_t(fileBytes >> 8), uint8_t(fileBytes >> 16), uint8_t(fileBytes >> 24),
        0, 0, 0, 0,
        54, 0, 0, 0,
        40, 0, 0, 0,
        uint8_t(width), uint8_t(width >> 8), uint8_t(width >> 16), 0,
        uint8_t(height), uint8_t(height >> 8), uint8_t(height >> 16), 0,
        1, 0, 24, 0,
        0, 0, 0, 0,
        uint8_t(imageBytes), uint8_t(imageBytes >> 8), uint8_t(imageBytes >> 16), uint8_t(imageBytes >> 24),
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

    bool ok = (fwrite(header, 1, sizeof(header), f) == sizeof(header));

    // BMP rows run bottom-up; pad each packed row out to four bytes.
    const uint8_t padding[3] = {};
    const uint32_t padBytes = rowBytes - width * 3;
    for (int32_t y = int32_t(height) - 1; ok && (y >= 0); y--) {
        ok = (fwrite(bgrTopDown + uint64_t(y) * width * 3, 1, width * 3, f) == width * 3);
        if (ok && (padBytes > 0)) {
            ok = (fwrite(padding, 1, padBytes, f) == padBytes);
        }
    }

    fclose(f);
    return ok;
}

} // namespace snapdiag
