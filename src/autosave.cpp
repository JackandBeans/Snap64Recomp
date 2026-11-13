/**
 * @file autosave.cpp
 * @brief Options > Game > Autosave: the game's own save, made as the lab
 *        opens with anything the last save does not hold (settings.h
 *        autosave).
 *
 * The cartridge saves only when asked. One routine writes the save
 * (func_800BF244_5C0E4, the decompilation's more_funcs/5BF20.c): it stamps
 * the time, recomputes the record's checksum and writes the whole record
 * to the flash. Three places call it: the lab's Save ("Shall I save your
 * progress?"), the save before the credits and the Gallery's print tray. A
 * ride's results reach the record in Oak's check and in the lab's events
 * (func_800E5298_8AAAB8), which the lab's main process runs once each time
 * the lab opens, after its panel is up: from the title, after Oak's check,
 * and back from the Album, the Report or the photo check.
 *
 * So the port keeps a copy of the record as the flash holds it: after the
 * boot read (func_800C05D4_5D474) and after every save that succeeds. With
 * Autosave on, the lab's events are followed by a comparison, and a record
 * that differs is saved by the game's own routine, in the lab's own
 * process, as the Save button would: no text, no pause, no music fade.
 * Off, the copies are still made (they only read memory), so turning the
 * row on mid-session compares against the real last save, and nothing the
 * game can see changes: the log only says when the lab opened with
 * anything unsaved.
 *
 * Two records are left to the player. One the game's copy check has marked
 * (PFID_ILLEGAL_COPY): the lab's Save runs the game's own handler for it
 * first, and the autosave does not save it. And a new game started over a
 * save: the title's new game resets the record in memory
 * (func_800C042C_5D2CC) while the flash keeps the old save until the player
 * saves, as on the cartridge. The autosave waits for that first save by
 * hand rather than write the new game over the old one.
 */

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "recomp.h"
#include "settings.h"

extern "C" {
#include "funcs.h"
}

namespace {
// D_800C21B0_5F050: the pointer to the save record (the decompilation's
// UnkBigBoy), aligned to 16 bytes by the boot read.
constexpr uint32_t RecordPtrAddr = 0x800C21B0u;
constexpr uint32_t RecordSize = 0x1F2A4u;   // a multiple of 4: a raw copy of RDRAM compares consistently
constexpr uint32_t RdramEnd = 0x80800000u;
constexpr int32_t PfidIllegalCopy = 21;     // include/enums.h PFID_ILLEGAL_COPY

std::vector<uint8_t> g_saved;      // the record as the flash holds it
bool g_have_saved = false;
bool g_in_boot_read = false;
bool g_boot_found_none = false;    // the boot read reset the record: the flash held no save
bool g_flash_holds_save = false;
bool g_new_game_waits = false;     // a new game replaced the record; the flash keeps the last save
bool g_autosaving = false;

// The record's RDRAM bytes, or nullptr before the boot read has set the
// pointer.
const uint8_t* record(uint8_t* rdram) {
    const uint32_t base = uint32_t(MEM_W(0, (gpr)(int32_t)RecordPtrAddr));
    if ((base < 0x80000000u) || (base > RdramEnd - RecordSize) || ((base & 3u) != 0)) {
        return nullptr;
    }
    return rdram + (base - 0x80000000u);
}

void keep_copy(uint8_t* rdram) {
    const uint8_t* rec = record(rdram);
    if (rec == nullptr) {
        return;
    }
    g_saved.assign(rec, rec + RecordSize);
    g_have_saved = true;
}
}  // namespace

// The boot read: the flash into the record, which the game checks and
// resets when it is not a valid save.
extern "C" void func_800C05D4_5D474(uint8_t* rdram, recomp_context* ctx) {
    g_in_boot_read = true;
    g_boot_found_none = false;
    __real_func_800C05D4_5D474(rdram, ctx);
    g_in_boot_read = false;
    g_flash_holds_save = !g_boot_found_none;
    g_new_game_waits = false;
    keep_copy(rdram);
    // A check's way to the save with no ride (tools/android_check.py): the
    // copy starts one bit off, so the first lab open finds a change. The eval
    // tape falls out of step on an emulator before it reaches Oak's check.
    if (g_have_saved && (std::getenv("SNAP_AUTOSAVE_TEST") != nullptr)) {
        g_saved[RecordSize - 1] ^= 1;
    }
}

// The record's reset: in the boot read when the flash held no save, and
// the title's new game.
extern "C" void func_800C042C_5D2CC(uint8_t* rdram, recomp_context* ctx) {
    __real_func_800C042C_5D2CC(rdram, ctx);
    if (g_in_boot_read) {
        g_boot_found_none = true;
    } else if (g_flash_holds_save) {
        g_new_game_waits = true;
    }
}

// The game's one save routine; 0 is success.
extern "C" void func_800BF244_5C0E4(uint8_t* rdram, recomp_context* ctx) {
    __real_func_800BF244_5C0E4(rdram, ctx);
    if (int32_t(ctx->r2) == 0) {
        keep_copy(rdram);
        g_flash_holds_save = true;
        if (!g_autosaving) {
            g_new_game_waits = false;
        }
    }
}

// The lab's events, once each time the lab opens.
extern "C" void func_800E5298_8AAAB8(uint8_t* rdram, recomp_context* ctx) {
    const recomp_context entry = *ctx;
    __real_func_800E5298_8AAAB8(rdram, ctx);

    if (!g_have_saved) {
        return;
    }
    const uint8_t* rec = record(rdram);
    if (rec == nullptr) {
        return;
    }
    uint32_t differ = 0;
    uint32_t first = 0;
    for (uint32_t i = 0; i < RecordSize; i++) {
        if (rec[i] != g_saved[i]) {
            if (differ == 0) {
                first = i;
            }
            differ++;
        }
    }
    if (differ == 0) {
        return;
    }
    if (!snap::settings().autosave) {
        std::printf("[SNAP-SAVE] autosave is off: the lab opened with changes the last save does not hold "
                    "(%u bytes, the first at +0x%05X)\n", differ, first);
        std::fflush(stdout);
        return;
    }

    recomp_context check = entry;
    check.r4 = PfidIllegalCopy;
    checkPlayerFlag(rdram, &check);
    if (int32_t(check.r2) != 0) {
        std::printf("[SNAP-SAVE] autosave: the lab opened with changes, not saved: the game's copy check has marked the record\n");
        std::fflush(stdout);
        return;
    }
    if (g_new_game_waits) {
        std::printf("[SNAP-SAVE] autosave: the lab opened with changes, not saved: this new game waits for its first save by hand, "
                    "and the file still holds the last one\n");
        std::fflush(stdout);
        return;
    }

    const auto start = std::chrono::steady_clock::now();
    recomp_context save = entry;
    g_autosaving = true;
    func_800BF244_5C0E4(rdram, &save);
    g_autosaving = false;
    const long long ms = (long long) std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    if (int32_t(save.r2) == 0) {
        std::printf("[SNAP-SAVE] autosave: the lab opened with changes (%u bytes, the first at +0x%05X), saved in %lld ms\n",
                    differ, first, ms);
    } else {
        std::printf("[SNAP-SAVE] autosave: the lab opened with changes; the save failed (%d) after %lld ms\n",
                    int(int32_t(save.r2)), ms);
    }
    std::fflush(stdout);
}
