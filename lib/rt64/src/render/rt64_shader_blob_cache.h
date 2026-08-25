//
// RT64
//
// Pokemon Snap port: an on-disk store for compiled shader bytecode.
//
// Turning the camera reveals materials the renderer has never drawn before, and
// each one is compiled from HLSL and linked against the shader libraries while
// the game runs. The renderer draws through the ubershader until the specialised
// one is ready, so nothing waits on it directly, but the compile is heavyweight
// work on several threads and the driver serialises parts of pipeline creation
// behind locks the drawing thread wants -- which is the micro-stutter a first
// playthrough shows whenever the view opens onto something new.
//
// The compiler's output is deterministic: the same shader source produces the
// same bytecode every time. So it only has to be produced once per machine
// rather than once per launch.
//
// This is purely a speed cache and cannot change what is drawn. The bytes it
// returns are the exact bytes the compiler handed the device when they were
// stored, and every way it can fail -- absent, wrong GPU, wrong driver, wrong
// build, truncated, corrupt, or not a shader container at all -- lands on the
// same code that runs today.
//

#pragma once

#include <atomic>
#include <functional>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common/rt64_plume.h"

namespace RT64 {
    struct ShaderBlobCache {
        // Stored blobs are immutable, so the writer thread takes a snapshot of
        // shared pointers instead of copying megabytes while holding the lock.
        typedef std::shared_ptr<const std::vector<uint8_t>> Blob;

        // Covers the file layout only. What the cached bytes MEAN is covered by
        // the key itself, which is derived from the shader source that produced
        // them -- so editing a shader cannot serve stale bytecode, and no one
        // has to remember to bump a number.
        static const uint32_t FormatVersion = 1;

        // The driver's pipeline store is one opaque blob rather than a set of
        // keyed entries, so it gets a well-known key of its own in the same
        // container -- same header checks, same checksums, same atomic replace.
        static const uint64_t DriverBlobKey = 1;

        static const uint64_t MaxEntryBytes = 4ull * 1024 * 1024;
        static const uint64_t MaxTotalBytes = 64ull * 1024 * 1024;
        static const uint32_t MaxEntryCount = 65536;

        // A compile that lands during a write goes into the next one. Waiting a
        // few seconds keeps a burst of new materials to one file write instead
        // of one per shader.
        static const uint32_t WriteDelaySeconds = 5;

        std::filesystem::path filePath;
        uint64_t deviceKey = 0;
        bool enabled = false;

        // Guards entries and dirty. Never held across file I/O, and never held
        // across a call into the graphics device.
        std::mutex entriesMutex;
        std::unordered_map<uint64_t, Blob> entries;
        uint64_t totalBytes = 0;
        bool dirty = false;

        // Asked, just before each write, whether one entry has changed. The
        // driver's pipeline store is not built up here entry by entry the way
        // shader bytecode is -- it is one opaque blob that only the driver can
        // produce, and only on demand. Without this it could be saved only at
        // shutdown, which made a performance feature depend on the player
        // quitting a particular way and lost everything on a crash.
        // Returns true and fills the vector when there is something new.
        std::function<bool(uint64_t &, std::vector<uint8_t> &)> refresh;

        // Reported once when the cache closes, so a run can say plainly whether
        // it served anything rather than leaving it to be inferred from feel.
        std::atomic<uint32_t> hits = { 0 };
        std::atomic<uint32_t> misses = { 0 };

        std::mutex writeMutex;
        std::condition_variable writeCondition;
        std::unique_ptr<std::thread> writeThread;
        std::atomic<bool> writeThreadRunning = { false };

        ShaderBlobCache() = default;
        ~ShaderBlobCache();

        // Opening reads whatever is usable and starts the writer. A file this
        // machine cannot use is deleted rather than re-read and re-rejected on
        // every future launch.
        void open(const std::filesystem::path &path, const RenderDevice *device);
        void close();

        Blob lookup(uint64_t key);
        void store(uint64_t key, const void *data, uint64_t size);
        // Unlike store(), replaces an entry that is already present. The driver
        // blob is one entry rewritten as it grows, not a new one each time.
        void replace(uint64_t key, const void *data, uint64_t size);

        void writeLoop();
        bool writeFile();
        bool readFile();
    };
};
