/**
 * @file os_stubs.cpp
 * @brief Stub implementations for N64 OS functions not provided by librecomp.
 *
 * These functions are referenced by recompiled code but don't have
 * implementations in the N64ModernRuntime librecomp library. We provide
 * no-op stubs so the executable links. Real implementations can be added
 * as they become necessary for correct behavior.
 */

#include <cstdint>
#include <cstdio>

#include "recomp.h"

// Recompiled callers read the return value out of v0 (ctx->r2), so every stub
// must write it. Left unwritten, v0 holds whatever the previously executed
// recompiled function left there and the game branches on register residue.
static constexpr int32_t StubOk = 0;
// PFS_ERR_NOPACK: no Controller Pak. Snap saves to EEPROM and never needs one,
// and reporting "no pak" keeps callers away from the out-parameters these
// stubs do not write.
static constexpr int32_t StubNoPak = 2;

// ---------------------------------------------------------------------------
// RSP audio microcode stub
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Controller Pak / PFS stubs  (recomp calling convention)
// ---------------------------------------------------------------------------

extern "C" void __osPfsSelectBank_recomp(uint8_t* rdram, recomp_context* ctx) {
    // Stub: Controller Pak bank select.
    (void)rdram;
    ctx->r2 = StubNoPak;
}

extern "C" void __osContRamWrite_recomp(uint8_t* rdram, recomp_context* ctx) {
    // Stub: Controller RAM write.
    (void)rdram;
    ctx->r2 = StubNoPak;
}

extern "C" void __osContRamRead_recomp(uint8_t* rdram, recomp_context* ctx) {
    // Stub: Controller RAM read.
    (void)rdram;
    ctx->r2 = StubNoPak;
}

extern "C" void osPfsIsPlug_recomp(uint8_t* rdram, recomp_context* ctx) {
    // Stub: Check if Controller Pak is plugged in.
    (void)rdram;
    ctx->r2 = StubNoPak;
}

extern "C" void osPfsInit_recomp(uint8_t* rdram, recomp_context* ctx) {
    // Stub: Initialize Controller Pak filesystem.
    (void)rdram;
    ctx->r2 = StubNoPak;
}

// ---------------------------------------------------------------------------
// Debug / diagnostic stubs
// ---------------------------------------------------------------------------

extern "C" void send_packet_recomp(uint8_t* rdram, recomp_context* ctx) {
    // Stub: Debug packet send (used by osWritebackDCache / rmon).
    (void)rdram;
    ctx->r2 = StubOk;
}

extern "C" void __osGetCause_recomp(uint8_t* rdram, recomp_context* ctx) {
    // Stub: Get MIPS cause register — no exception pending.
    (void)rdram;
    ctx->r2 = StubOk;
}

// Pokemon Snap: sets the CP0 WatchLo debug register at boot; no-op on PC.
extern "C" void __osSetWatchLo_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram; (void)ctx;
}

// libultra kernel internals referenced by symbol-less statics found in the
// prologue scan (interrupt/VI plumbing). ultramodern implements this layer
// natively, so these recompiled paths never execute meaningfully.
// v0 is written here for the same reason it is written everywhere else in this
// file: left alone it holds whatever the previously executed recompiled
// function put there. __osViGetCurrentContext returns an OSViContext*, so a
// caller that dereferences the residue faults on an address that has nothing
// to do with the game.
extern "C" void __osTimerInterrupt_recomp(uint8_t*, recomp_context* ctx) { ctx->r2 = 0; }
extern "C" void __osViSwapContext_recomp(uint8_t*, recomp_context* ctx) { ctx->r2 = 0; }
extern "C" void __osViGetCurrentContext_recomp(uint8_t*, recomp_context* ctx) { ctx->r2 = 0; }
extern "C" void __osContAddressCrc_recomp(uint8_t*, recomp_context* ctx) { ctx->r2 = 0; }
