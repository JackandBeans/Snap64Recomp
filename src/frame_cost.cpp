/**
 * @file frame_cost.cpp
 * @brief Where a game frame's time actually goes, measured on the thread that
 *        spends it.
 *
 * The slow-frame report is written from send_dl, and ultramodern calls send_dl
 * on a host thread of its own rather than on any of the game's threads. That is
 * fine for the two figures it owns -- the wait for the renderer, and RT64's walk
 * of the display list -- and wrong for everything else it printed. The two
 * counters it read for the game's own thread scheduling are thread_local, so on
 * a thread that never participates in that scheduling they were zero by
 * construction rather than by measurement, and reporting them as zero refuted a
 * hypothesis that had never been tested. The residual it called "the game
 * itself" was not an attribution at all; it was a subtraction from a black box.
 *
 * The game's frame has exactly two halves and its own main loop names them.
 * gtlMain waits out its update interval, calls the update callback, and every
 * few frames calls the draw callback; omSetupScene installs gtlUpdate and
 * gtlDraw as those two. Every course here asks for one update per tick and one
 * draw every second tick, so a display-list-to-display-list frame is two updates
 * and one draw. That makes these the highest pair of functions that can split a
 * frame at all, and they run on the game's own thread.
 *
 * Which is what makes the parked figure possible. This game gives every object
 * its own process, and a process in this port is a real operating system thread:
 * starting one hands the CPU away and waits for it to come back. The game's
 * thread spends that time inside the update doing nothing, and ultramodern was
 * already counting it -- into a thread_local nobody read. Taking those counters
 * here separates the recompiled game running from the port handing the CPU
 * around, which is the whole question behind a spawn frame that costs sixty
 * milliseconds.
 *
 * Nothing here touches game memory or game control flow. Each wrapper reads a
 * clock, calls the real function, reads the clock again, and adds to an atomic.
 * No MEM_* access and no use of ctx beyond passing it straight through, so a
 * function that returns its value in ctx->r2 delivers it untouched.
 *
 * The timers are RAII rather than a matched pair of statements. Ultramodern
 * throws thread_terminated out of its scheduling calls, and that unwinds
 * straight through a recompiled frame when a scene tears down. A stop that can
 * be skipped would desynchronise the counter permanently, and this file is
 * useless the moment its numbers cannot be trusted.
 */

#include <atomic>
#include <chrono>
#include <cstdint>

#include "hle/rt64_snap_diag.h"
#include "recomp.h"

extern "C" {
#include "funcs.h"
}

// Ultramodern's per-thread handoff counters. They are thread_local AND they
// reset on read, so they mean something only when taken on the thread that did
// the waiting, and only one probe per thread may take them.
extern "C" int64_t  snap_switch_take_nanos();
extern "C" uint32_t snap_switch_take_count();
extern "C" int64_t  snap_recv_block_take_nanos();
extern "C" uint32_t snap_recv_block_take_count();

// Per-tick buckets. Written by the game's threads, read and cleared by the
// report on the graphics thread, so all of them are atomic.
extern "C" {
    std::atomic<int64_t>  snap_game_update_nanos{0};
    std::atomic<uint32_t> snap_game_update_count{0};
    std::atomic<int64_t>  snap_game_parked_nanos{0};
    std::atomic<uint32_t> snap_game_parked_count{0};
    std::atomic<int64_t>  snap_game_draw_nanos{0};
    std::atomic<uint32_t> snap_game_draw_count{0};
    std::atomic<int64_t>  snap_block_change_nanos{0};
    std::atomic<int64_t>  snap_spawn_nanos{0};
    std::atomic<uint32_t> snap_spawn_count{0};
}

namespace {

// Adds an interval to a bucket when the scope ends, however it ends.
struct CostScope {
    const bool on;
    std::atomic<int64_t> &bucket;
    std::chrono::steady_clock::time_point start;

    CostScope(bool on_, std::atomic<int64_t> &bucket_) :
        on(on_), bucket(bucket_)
    {
        if (on) {
            start = std::chrono::steady_clock::now();
        }
    }

    ~CostScope() {
        if (on) {
            bucket.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start).count(), std::memory_order_relaxed);
        }
    }

    CostScope(const CostScope &) = delete;
    CostScope &operator=(const CostScope &) = delete;
};

} // namespace

// The logic half of a frame: input, then every object's update and every
// object's process. Runs twice per drawn frame at this game's intervals.
extern "C" void gtlUpdate(uint8_t* rdram, recomp_context* ctx) {
    const bool on = snapdiag::statsEnabled();
    if (!on) {
        __real_gtlUpdate(rdram, ctx);
        return;
    }

    // Cleared rather than accumulated: whatever this thread parked for before
    // the update began belongs to the previous frame, and taking it here
    // without clearing would charge it to this one.
    snap_switch_take_nanos();
    snap_switch_take_count();
    snap_recv_block_take_nanos();
    snap_recv_block_take_count();

    {
        CostScope scope(true, snap_game_update_nanos);
        __real_gtlUpdate(rdram, ctx);
    }

    // How much of that update was this thread waiting rather than working. A
    // process in this port is a host thread, so starting one is a handoff and a
    // wait, and the game starts one per object it creates.
    const int64_t parkedNanos = snap_switch_take_nanos() + snap_recv_block_take_nanos();
    const uint32_t parkedCount = snap_switch_take_count() + snap_recv_block_take_count();
    snap_game_parked_nanos.fetch_add(parkedNanos, std::memory_order_relaxed);
    snap_game_parked_count.fetch_add(parkedCount, std::memory_order_relaxed);
    snap_game_update_count.fetch_add(1, std::memory_order_relaxed);
}

// The other half: reset the heap, build every object's display list, hand it to
// the RCP. The call COUNT matters as much as the time -- the game only draws
// every second tick, and it skips the draw outright if it cannot take the
// graphics context, which doubles a frame's length with no extra work done.
extern "C" void gtlDraw(uint8_t* rdram, recomp_context* ctx) {
    const bool on = snapdiag::statsEnabled();
    if (!on) {
        __real_gtlDraw(rdram, ctx);
        return;
    }

    {
        CostScope scope(true, snap_game_draw_nanos);
        __real_gtlDraw(rdram, ctx);
    }

    snap_game_draw_count.fetch_add(1, std::memory_order_relaxed);
}

// One Pokemon coming into existence: its object, its model tree, its matrices,
// the ground under it, three processes, and its first pose. Every species on
// every course funnels through here, and a course block boundary runs it once
// per Pokemon in the block that is being created.
extern "C" void Pokemon_SpawnOnGround(uint8_t* rdram, recomp_context* ctx) {
    const bool on = snapdiag::statsEnabled();
    if (!on) {
        __real_Pokemon_SpawnOnGround(rdram, ctx);
        return;
    }

    {
        CostScope scope(true, snap_spawn_nanos);
        __real_Pokemon_SpawnOnGround(rdram, ctx);
    }

    snap_spawn_count.fetch_add(1, std::memory_order_relaxed);
}
