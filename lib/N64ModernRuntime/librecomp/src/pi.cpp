#include <memory>
#include <fstream>
#include <atomic>
#include <chrono>
#include <array>
#include <cstring>
#include <string>
#include <mutex>
#include "recomp.h"
#include "librecomp/addresses.hpp"
#include "librecomp/game.hpp"
#include "librecomp/files.hpp"
#include <ultramodern/ultra64.h>
#include <ultramodern/ultramodern.hpp>

static std::vector<uint8_t> rom;

bool recomp::is_rom_loaded() {
    return !rom.empty();
}

void recomp::set_rom_contents(std::vector<uint8_t>&& new_rom) {
    rom = std::move(new_rom);
}

std::span<const uint8_t> recomp::get_rom() {
    return rom;
}

constexpr uint32_t k1_to_phys(uint32_t addr) {
    return addr & 0x1FFFFFFF;
}

constexpr uint32_t phys_to_k1(uint32_t addr) {
    return addr | 0xA0000000;
}

extern "C" void __osPiGetAccess_recomp(uint8_t* rdram, recomp_context* ctx) {
}

extern "C" void __osPiRelAccess_recomp(uint8_t* rdram, recomp_context* ctx) {
}

extern "C" void osCartRomInit_recomp(uint8_t* rdram, recomp_context* ctx) {
    OSPiHandle* handle = TO_PTR(OSPiHandle, recomp::cart_handle);
    handle->type = 0; // cart
    handle->baseAddress = phys_to_k1(recomp::rom_base);
    handle->domain = 0;

    ctx->r2 = (gpr)recomp::cart_handle;
}

extern "C" void osDriveRomInit_recomp(uint8_t * rdram, recomp_context * ctx) {
    OSPiHandle* handle = TO_PTR(OSPiHandle, recomp::drive_handle);
    handle->type = 1; // bulk
    handle->baseAddress = phys_to_k1(recomp::drive_base);
    handle->domain = 0;

    ctx->r2 = (gpr)recomp::drive_handle;
}

extern "C" void osCreatePiManager_recomp(uint8_t* rdram, recomp_context* ctx) {
    ;
}

// Pokemon Snap port: how long the game thread spent copying out of the ROM,
// read and cleared once a tick by the slow-frame report. On the console this
// was DMA the CPU did not wait on; here it is a memcpy the calling thread
// performs, so it lands inside whatever frame asked for it.
extern "C" {
    std::atomic<int64_t> snap_rom_read_nanos{0};
    std::atomic<uint64_t> snap_rom_read_bytes{0};
    std::atomic<uint32_t> snap_rom_read_count{0};
}

void recomp::do_rom_read(uint8_t* rdram, gpr ram_address, uint32_t physical_addr, size_t num_bytes) {
    const auto snapReadStart = std::chrono::steady_clock::now();

    // TODO handle misaligned DMA
    assert((physical_addr & 0x1) == 0 && "Only PI DMA from aligned ROM addresses is currently supported");
    assert((ram_address & 0x7) == 0 && "Only PI DMA to aligned RDRAM addresses is currently supported");
    uint8_t* rom_addr = rom.data() + physical_addr - recomp::rom_base;
    for (size_t i = 0; i < num_bytes; i++) {
        MEM_B(i, ram_address) = *rom_addr;
        rom_addr++;
    }

    snap_rom_read_nanos.fetch_add(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - snapReadStart).count(),
        std::memory_order_relaxed);
    snap_rom_read_bytes.fetch_add(num_bytes, std::memory_order_relaxed);
    snap_rom_read_count.fetch_add(1, std::memory_order_relaxed);
}

void recomp::do_rom_pio(uint8_t* rdram, gpr ram_address, uint32_t physical_addr) {
    assert((physical_addr & 0x3) == 0 && "PIO not 4-byte aligned in device, currently unsupported");
    assert((ram_address & 0x3) == 0 && "PIO not 4-byte aligned in RDRAM, currently unsupported");
    uint8_t* rom_addr = rom.data() + physical_addr - recomp::rom_base;
    MEM_B(0, ram_address) = *rom_addr++;
    MEM_B(1, ram_address) = *rom_addr++;
    MEM_B(2, ram_address) = *rom_addr++;
    MEM_B(3, ram_address) = *rom_addr++;
}

struct {
    std::vector<char> save_buffer;
    std::thread saving_thread;
    std::filesystem::path save_file_path;
    moodycamel::LightweightSemaphore write_sempahore;
    // Used to tell the saving thread that a file swap is pending.
    moodycamel::LightweightSemaphore swap_file_pending_sempahore;
    // Used to tell the consumer thread that the saving thread is ready for a file swap.
    moodycamel::LightweightSemaphore swap_file_ready_sempahore;
    std::mutex save_buffer_mutex;
} save_context;

const std::u8string save_folder = u8"saves";

extern std::filesystem::path config_path;

std::filesystem::path ultramodern::get_save_file_path() {
    return save_context.save_file_path;
}

void set_save_file_path(const std::u8string& subfolder, const std::u8string& name) {
    std::filesystem::path save_folder_path = config_path / save_folder;
    if (!subfolder.empty()) {
        save_folder_path = save_folder_path / subfolder;
    }
    save_context.save_file_path = save_folder_path / (name + u8".bin");
}

// Cleared when a save exists on disk that could not be read. The buffer then holds zeros that are
// not the player's data, and writing them back would destroy a save that is very likely still
// intact, so no publish happens for the rest of the run.
static std::atomic_bool save_writable{true};

// Whether what currently sits at the save file's own name is known to be a whole record. Only such
// a file may be rotated into the backup; rotating a partial or unread one would push the last good
// copy out. Atomic because read_save_file sets it from a game thread when a mod swaps save files.
static std::atomic_bool current_save_file_complete{true};

// One dialog per run. The saving thread would otherwise raise one on every publish, and a folder
// that cannot be written to fails for a reason that does not change between attempts.
static bool reported_save_write_failure = false;

// Copies the buffer out under the lock so the game's writes only wait for a memcpy, not for the
// disk write and flush. rotate_backup says whether the file being replaced is known to be whole and
// may therefore become the new backup.
bool update_save_file(bool rotate_backup) {
    // A save that could not be read at boot is still on disk. Never write over it.
    if (!save_writable.load(std::memory_order_acquire)) {
        return false;
    }

    std::vector<char> save_contents;
    {
        std::lock_guard lock{ save_context.save_buffer_mutex };
        save_contents = save_context.save_buffer;
    }

    if (!recomp::write_file_with_backup(ultramodern::get_save_file_path(), save_contents, rotate_backup)) {
        if (!reported_save_write_failure) {
            reported_save_write_failure = true;
            ultramodern::error_handling::message_box("Failed to write to the save file. Check your file permissions and whether the save folder has been moved to Dropbox or similar, as this can cause issues.");
        }
        return false;
    }

    reported_save_write_failure = false;
    return true;
}

extern std::atomic_bool exited;

// Set by join_saving_thread to bring the saving thread down. This is deliberately not `exited`.
// The game's threads are pooled host threads that are never joined, so when `exited` is set they
// can still be part-way through writing a save; a saving thread that stopped there would drop the
// whole thing. The thread instead waits for the writes to stop, with a bound so a wedged game
// cannot hold the exit open.
static std::atomic_bool saving_thread_stop{false};

void saving_thread_func(RDRAM_ARG1) {
    using clock = std::chrono::steady_clock;

    // The game does not hand its save over in one piece. Pokemon Snap rewrites the image a 16 KB
    // sector at a time -- one osFlashSectorErase and then 128 osFlashWriteArray pages per sector,
    // across the eight sectors its 0x1F2A4-byte record spans (decomp func_800C09C0_5D860 and
    // func_800C08DC_5D77C) -- which is over a thousand separate writes into the buffer for a full
    // save. The record carries a checksum over the whole image, so a copy taken part-way through
    // that run is not merely stale: it fails the game's own check and the save is thrown away. A
    // publish therefore only ever happens once the writes have stopped for this long.
    constexpr auto quiet_period = std::chrono::milliseconds(250);
    // How long the exit waits for a save that is still in flight when the player quits.
    constexpr auto shutdown_grace = std::chrono::seconds(3);
    // A game that never pauses would never get a save to disk at all. Reaching this only produces a
    // warning: nothing partial is published, because a stale save the game accepts is worth more
    // than a fresh one it rejects.
    constexpr auto stuck_warning_after = std::chrono::seconds(10);
    // The longest the drain below may run before the checks after it get a turn.
    constexpr auto drain_slice = std::chrono::milliseconds(250);
    constexpr int64_t poll_microseconds = 10000;

    bool pending = false;
    bool stopping = false;
    bool warned_stuck = false;
    clock::time_point first_write{};
    clock::time_point last_write{};
    clock::time_point stop_deadline{};
    clock::duration widest_gap{};

    while (true) {
        if (!stopping && saving_thread_stop.load(std::memory_order_acquire)) {
            stopping = true;
            stop_deadline = clock::now() + shutdown_grace;
        }

        // Take every write the game has queued. There is deliberately no cap on the *count*: cutting
        // a burst short at a fixed number of writes is exactly what puts a half-written record on
        // disk. Only wall-clock time bounds this, and only so the shutdown check keeps running.
        const auto drain_start = clock::now();
        while (save_context.write_sempahore.wait(poll_microseconds)) {
            const auto now = clock::now();
            if (!pending) {
                first_write = now;
                widest_gap = clock::duration::zero();
                warned_stuck = false;
            }
            else if (now - last_write > widest_gap) {
                widest_gap = now - last_write;
            }
            pending = true;
            last_write = now;
            // Hand control back to the checks below at least this often. Nothing is published by
            // leaving here -- the quiet test still has to pass -- but a game that never stops
            // writing would otherwise pin this thread and hold the exit open forever.
            if (now - drain_start >= drain_slice) {
                break;
            }
        }

        const auto now = clock::now();

        if (pending) {
            if (now - last_write >= quiet_period) {
                // The gap this burst went quiet on is the one thing that decides whether the image
                // is whole. If the game's own pauses ever get close to the window, say so: the next
                // run would publish in the middle of a record and nothing else would report it.
                if (widest_gap >= quiet_period / 2) {
                    fprintf(stderr, "[save] Warning: the game paused for %lld ms in the middle of a save burst, "
                                    "close to the %lld ms window used to decide the record is finished.\n",
                            (long long)std::chrono::duration_cast<std::chrono::milliseconds>(widest_gap).count(),
                            (long long)std::chrono::duration_cast<std::chrono::milliseconds>(quiet_period).count());
                }
                if (update_save_file(current_save_file_complete.load(std::memory_order_acquire))) {
                    // What was just written was written at a rest point, so it is whole and may
                    // become the backup at the next publish.
                    current_save_file_complete.store(true, std::memory_order_release);
                }
                pending = false;
            }
            else if (!warned_stuck && now - first_write >= stuck_warning_after) {
                fprintf(stderr, "[save] The game has been writing to the save without a pause for %lld s; "
                                "the last complete save on disk is being kept until it stops.\n",
                        (long long)std::chrono::duration_cast<std::chrono::seconds>(now - first_write).count());
                warned_stuck = true;
            }
        }

        if (save_context.swap_file_pending_sempahore.tryWait()) {
            save_context.swap_file_ready_sempahore.signal();
        }

        if (stopping) {
            if (!pending) {
                break;
            }
            if (now >= stop_deadline) {
                fprintf(stderr, "[save] Quit while the game was still writing its save; that save was not finished "
                                "and has not been written, so the last complete one on disk is untouched.\n");
                break;
            }
        }
    }
}

void save_write_ptr(const void* in, uint32_t offset, uint32_t count) {
    assert(offset + count <= save_context.save_buffer.size());

    {
        std::lock_guard lock { save_context.save_buffer_mutex };
        memcpy(&save_context.save_buffer[offset], in, count);
    }
    
    save_context.write_sempahore.signal();
}

void save_write(RDRAM_ARG PTR(void) rdram_address, uint32_t offset, uint32_t count) {
    assert(offset + count <= save_context.save_buffer.size());

    {
        std::lock_guard lock { save_context.save_buffer_mutex };
        for (gpr i = 0; i < count; i++) {
            save_context.save_buffer[offset + i] = MEM_B(i, rdram_address);
        }
    }

    save_context.write_sempahore.signal();
}

void save_read(RDRAM_ARG PTR(void) rdram_address, uint32_t offset, uint32_t count) {
    assert(offset + count <= save_context.save_buffer.size());

    std::lock_guard lock { save_context.save_buffer_mutex };
    for (gpr i = 0; i < count; i++) {
        MEM_B(i, rdram_address) = save_context.save_buffer[offset + i];
    }
}

void save_clear(uint32_t start, uint32_t size, char value) {
    assert(start + size < save_context.save_buffer.size());

    {
        std::lock_guard lock { save_context.save_buffer_mutex };
        std::fill_n(save_context.save_buffer.begin() + start, size, value);
    }

    save_context.write_sempahore.signal();
}

size_t get_save_size(recomp::SaveType save_type) {
    switch (save_type) {
        case recomp::SaveType::AllowAll:
        case recomp::SaveType::Flashram:
            return 0x20000;
        case recomp::SaveType::Sram:
            return 0x8000;
        case recomp::SaveType::Eep16k:
            return 0x800;
        case recomp::SaveType::Eep4k:
            return 0x200;
        case recomp::SaveType::None:
            return 0;
    }
    return 0;
}

void read_save_file() {
    std::filesystem::path save_file_path = ultramodern::get_save_file_path();

    // Ensure the save file directory exists.
    std::error_code ec;
    std::filesystem::create_directories(save_file_path.parent_path(), ec);

    // Read the save file, or its backup when the save file is missing or not a complete image.
    bool used_backup = false;
    if (recomp::read_file_with_backup(save_file_path, save_context.save_buffer, &used_backup)) {
        // When the backup had to stand in, what is at the save's own name is something this build
        // could not use. The first publish must replace it in place rather than rotate it over the
        // backup that just supplied the player's data.
        current_save_file_complete.store(!used_backup, std::memory_order_release);
        save_writable.store(true, std::memory_order_release);
        return;
    }

    // Nothing readable. Two very different situations arrive here and they must not be treated the
    // same way, because one of them still has the player's save sitting on the disk.
    std::filesystem::path backup_path{save_file_path};
    backup_path += u8".bak";
    ec.clear();
    const bool primary_exists = std::filesystem::exists(save_file_path, ec) && !ec;
    ec.clear();
    const bool backup_exists = std::filesystem::exists(backup_path, ec) && !ec;

    std::fill(save_context.save_buffer.begin(), save_context.save_buffer.end(), 0);

    if (primary_exists || backup_exists) {
        // A save is there and could not be read: a lock held by antivirus or a cloud sync client, a
        // permissions problem, a file something else truncated. These zeros did not come from the
        // player, so nothing may be written back over what is still on disk. Saving is disarmed for
        // this run and the player is told now, before they play, rather than after an hour of
        // progress has been committed over the save they still have.
        current_save_file_complete.store(false, std::memory_order_release);
        save_writable.store(false, std::memory_order_release);

        std::u8string path_u8 = save_file_path.u8string();
        std::string message =
            "Your save file is there but could not be read, so this session will not write to it.\n\n"
            + std::string(reinterpret_cast<const char*>(path_u8.c_str())) + "\n\n"
            "Nothing on disk has been changed. Close anything that may be holding the file open "
            "(antivirus, OneDrive or Dropbox sync, a backup tool), or move the save folder out of a "
            "synced directory, then start the game again.\n\n"
            "If you play on, the game will act as though there is no save and none of this session's "
            "progress will be kept.";
        ultramodern::error_handling::message_box(message.c_str());
        return;
    }

    // No save file and no backup: a first run. An all-zero buffer is the right starting state and
    // there is nothing on disk to lose by writing it.
    current_save_file_complete.store(true, std::memory_order_release);
    save_writable.store(true, std::memory_order_release);
}

void ultramodern::init_saving(RDRAM_ARG1) {
    set_save_file_path(u8"", recomp::current_game_id());

    save_context.save_buffer.resize(get_save_size(recomp::get_save_type()));

    read_save_file();

    save_context.saving_thread = std::thread{saving_thread_func, PASS_RDRAM};
}

void ultramodern::change_save_file(const std::u8string& subfolder, const std::u8string& name) {
    // Tell the saving thread that a file swap is pending.
    save_context.swap_file_pending_sempahore.signal();
    // Wait until the saving thread indicates it's ready to swap files.
    save_context.swap_file_ready_sempahore.wait();
    // Perform the save file swap.
    set_save_file_path(subfolder, name);
    read_save_file();
}

void ultramodern::join_saving_thread() {
    if (save_context.saving_thread.joinable()) {
        // The thread stops on this rather than on `exited` so that a save the game is still writing
        // when the player quits gets finished and written out. It waits for the writes to go quiet,
        // then publishes once, then returns; shutdown_grace bounds how long that can take.
        saving_thread_stop.store(true, std::memory_order_release);
        save_context.saving_thread.join();
    }
    // Nothing is left to answer a swap request, so release anyone already waiting on one instead of
    // leaving them blocked through the rest of the exit.
    save_context.swap_file_ready_sempahore.signal();
}

void do_dma(RDRAM_ARG PTR(OSMesgQueue) mq, gpr rdram_address, uint32_t physical_addr, uint32_t size, uint32_t direction) {
    // TODO asynchronous transfer
    // TODO implement unaligned DMA correctly
    if (direction == 0) {
        if (physical_addr >= recomp::rom_base) {
            // read cart rom
            recomp::do_rom_read(rdram, rdram_address, physical_addr, size);

            // Send a message to the mq to indicate that the transfer completed
            ultramodern::enqueue_external_message_src(mq, 0, false, ultramodern::EventMessageSource::Pi);
        } else if (physical_addr >= recomp::sram_base) {
            if (!recomp::sram_allowed()) {
                ultramodern::error_handling::message_box("Attempted to use SRAM saving with other save type");
                ULTRAMODERN_QUICK_EXIT();
            }
            // read sram
            save_read(rdram, rdram_address, physical_addr - recomp::sram_base, size);

            // Send a message to the mq to indicate that the transfer completed
            ultramodern::enqueue_external_message_src(mq, 0, false, ultramodern::EventMessageSource::Pi);
        } else {
            fprintf(stderr, "[WARN] PI DMA read from unknown region, phys address 0x%08X\n", physical_addr);
        }
    } else {
        if (physical_addr >= recomp::rom_base) {
            // write cart rom
            throw std::runtime_error("ROM DMA write unimplemented");
        } else if (physical_addr >= recomp::sram_base) {
            if (!recomp::sram_allowed()) {
                ultramodern::error_handling::message_box("Attempted to use SRAM saving with other save type");
                ULTRAMODERN_QUICK_EXIT();
            }
            // write sram
            save_write(rdram, rdram_address, physical_addr - recomp::sram_base, size);

            // Send a message to the mq to indicate that the transfer completed
            ultramodern::enqueue_external_message_src(mq, 0, false, ultramodern::EventMessageSource::Pi);
        } else {
            fprintf(stderr, "[WARN] PI DMA write to unknown region, phys address 0x%08X\n", physical_addr);
        }
    }
}

extern "C" void osPiStartDma_recomp(RDRAM_ARG recomp_context* ctx) {
    uint32_t mb = ctx->r4;
    uint32_t pri = ctx->r5;
    uint32_t direction = ctx->r6;
    uint32_t devAddr = ctx->r7 | recomp::rom_base;
    gpr dramAddr = MEM_W(0x10, ctx->r29);
    uint32_t size = MEM_W(0x14, ctx->r29);
    PTR(OSMesgQueue) mq = MEM_W(0x18, ctx->r29);
    uint32_t physical_addr = k1_to_phys(devAddr);

    debug_printf("[pi] DMA from 0x%08X into 0x%08X of size 0x%08X\n", devAddr, dramAddr, size);

    do_dma(PASS_RDRAM mq, dramAddr, physical_addr, size, direction);

    ctx->r2 = 0;
}

extern "C" void osEPiStartDma_recomp(RDRAM_ARG recomp_context* ctx) {
    OSPiHandle* handle = TO_PTR(OSPiHandle, ctx->r4);
    OSIoMesg* mb = TO_PTR(OSIoMesg, ctx->r5);
    uint32_t direction = ctx->r6;
    uint32_t devAddr = handle->baseAddress | mb->devAddr;
    gpr dramAddr = mb->dramAddr;
    uint32_t size = mb->size;
    PTR(OSMesgQueue) mq = mb->hdr.retQueue;
    uint32_t physical_addr = k1_to_phys(devAddr);

    debug_printf("[pi] DMA from 0x%08X into 0x%08X of size 0x%08X\n", devAddr, dramAddr, size);

    do_dma(PASS_RDRAM mq, dramAddr, physical_addr, size, direction);

    ctx->r2 = 0;
}

extern "C" void osEPiReadIo_recomp(RDRAM_ARG recomp_context * ctx) {
    OSPiHandle* handle = TO_PTR(OSPiHandle, ctx->r4);
    uint32_t devAddr = handle->baseAddress | ctx->r5;
    gpr dramAddr = ctx->r6;
    uint32_t physical_addr = k1_to_phys(devAddr);

    if (physical_addr > recomp::rom_base) {
        // cart rom
        recomp::do_rom_pio(PASS_RDRAM dramAddr, physical_addr);
    } else {
        // sram
        assert(false && "SRAM ReadIo unimplemented");
    }

    ctx->r2 = 0;
}

extern "C" void osPiGetStatus_recomp(RDRAM_ARG recomp_context * ctx) {
    ctx->r2 = 0;
}

extern "C" void osPiRawStartDma_recomp(RDRAM_ARG recomp_context * ctx) {
    ultramodern::error_handling::message_box(
        "Stub `osPiRawStartDma_recomp` function called!\n"
        "Most games do not call this function directly, which means the libultra function\n"
        "that uses this function was not properly named.\n"
        "\n"
        "If you triggered this message, please make sure you have properly identified\n"
        "every libultra function on your recompiled game. If you are sure every libultra\n"
        "function has been identified and you still get this problem then open an issue on\n"
        "the N64ModernRuntime Github repository mentioning the game you are trying to\n"
        "recompile and steps to reproduce the issue.\n"
        "\n"
        "The application will close now, bye and good luck!"
    );
    ULTRAMODERN_QUICK_EXIT();
}

extern "C" void osEPiRawStartDma_recomp(RDRAM_ARG recomp_context * ctx) {
    ultramodern::error_handling::message_box(
        "Stub `osEPiRawStartDma_recomp` function called!\n"
        "Most games do not call this function directly, which means the libultra function\n"
        "that uses this function was not properly named.\n"
        "\n"
        "If you triggered this message, please make sure you have properly identified\n"
        "every libultra function on your recompiled game. If you are sure every libultra\n"
        "function has been identified and you still get this problem then open an issue on\n"
        "the N64ModernRuntime Github repository mentioning the game you are trying to\n"
        "recompile and steps to reproduce the issue.\n"
        "\n"
        "The application will close now, bye and good luck!"
    );
    ULTRAMODERN_QUICK_EXIT();
}
