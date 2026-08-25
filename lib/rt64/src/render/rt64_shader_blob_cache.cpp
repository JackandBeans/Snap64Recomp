//
// RT64
//

#include "rt64_shader_blob_cache.h"

#include <chrono>
#include <cstring>
#include <fstream>

#include "xxHash/xxh3.h"

#include "common/rt64_thread.h"
#include "hle/rt64_snap_diag.h"

namespace RT64 {
    static const uint32_t BlobCacheMagic = 0x43425352; // 'RSBC'

#pragma pack(push, 1)
    struct BlobCacheFileHeader {
        uint32_t magic;
        uint32_t formatVersion;
        uint32_t entryCount;
        uint32_t reserved;
        uint64_t deviceKey;
        uint64_t payloadBytes;
        uint64_t payloadHash;
    };

    struct BlobCacheEntryHeader {
        uint64_t key;
        uint64_t blobHash;
        uint32_t size;
        uint32_t reserved;
    };
#pragma pack(pop)

    static_assert(sizeof(BlobCacheFileHeader) == 40, "Cache header layout changed. Bump FormatVersion.");
    static_assert(sizeof(BlobCacheEntryHeader) == 24, "Cache entry layout changed. Bump FormatVersion.");

    // Bytecode compiled for one device is meaningless on another, and a driver
    // update can change what the runtime will accept, so both are part of the
    // identity of the file rather than something to discover by crashing.
    static uint64_t deviceKeyFromDevice(const RenderDevice *device) {
        if (device == nullptr) {
            return 0;
        }

        const RenderDeviceDescription &desc = device->getDescription();
        struct {
            uint64_t driverVersion;
            uint64_t nameHash;
            uint32_t vendor;
            uint32_t type;
        } identity = {};

        identity.driverVersion = desc.driverVersion;
        identity.nameHash = desc.name.empty() ? 0 : XXH3_64bits(desc.name.data(), desc.name.size());
        identity.vendor = uint32_t(desc.vendor);
        identity.type = uint32_t(desc.type);
        return XXH3_64bits(&identity, sizeof(identity));
    }

    // ShaderBlobCache

    ShaderBlobCache::~ShaderBlobCache() {
        close();
    }

    void ShaderBlobCache::open(const std::filesystem::path &path, const RenderDevice *device) {
        close();

        if (path.empty()) {
            return;
        }

        filePath = path;
        deviceKey = deviceKeyFromDevice(device);
        enabled = true;

        std::error_code ec;
        if (std::filesystem::exists(filePath, ec) && !readFile()) {
            std::filesystem::remove(filePath, ec);
        }

        if (snapdiag::diagEnabled() || snapdiag::statsEnabled()) {
            fprintf(stdout, "[SNAP-SHADER] blob cache opened with %u shaders kept from earlier runs (%llu bytes)\n",
                uint32_t(entries.size()), (unsigned long long)(totalBytes));
            fflush(stdout);
        }

        writeThreadRunning = true;
        writeThread = std::make_unique<std::thread>(&ShaderBlobCache::writeLoop, this);
    }

    void ShaderBlobCache::close() {
        if (writeThread != nullptr) {
            writeThreadRunning = false;
            writeCondition.notify_all();
            writeThread->join();
            writeThread.reset();
        }

        if (enabled) {
            if (refresh != nullptr) {
                uint64_t refreshKey = 0;
                std::vector<uint8_t> refreshed;
                if (refresh(refreshKey, refreshed) && !refreshed.empty()) {
                    replace(refreshKey, refreshed.data(), refreshed.size());
                }
            }

            // Bounded by MaxTotalBytes and only done at all if something new was
            // compiled, so shutdown does not wait on a large write for nothing.
            writeFile();

            if (snapdiag::diagEnabled() || snapdiag::statsEnabled()) {
                const std::unique_lock<std::mutex> lock(entriesMutex);
                fprintf(stdout, "[SNAP-SHADER] blob cache: %u served, %u compiled, %u stored, %llu bytes\n",
                    hits.load(), misses.load(), uint32_t(entries.size()), (unsigned long long)(totalBytes));
                fflush(stdout);
            }
        }

        {
            const std::unique_lock<std::mutex> lock(entriesMutex);
            entries.clear();
            totalBytes = 0;
            dirty = false;
        }

        enabled = false;
        filePath.clear();
    }

    ShaderBlobCache::Blob ShaderBlobCache::lookup(uint64_t key) {
        if (!enabled || (key == 0)) {
            return nullptr;
        }

        const std::unique_lock<std::mutex> lock(entriesMutex);
        auto it = entries.find(key);
        const bool found = (it != entries.end());
        (found ? hits : misses).fetch_add(1, std::memory_order_relaxed);
        return found ? it->second : nullptr;
    }

    void ShaderBlobCache::store(uint64_t key, const void *data, uint64_t size) {
        if (!enabled || (key == 0) || (data == nullptr) || (size == 0) || (size > MaxEntryBytes)) {
            return;
        }

        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data);
        Blob blob = std::make_shared<const std::vector<uint8_t>>(bytes, bytes + size);
        {
            const std::unique_lock<std::mutex> lock(entriesMutex);
            if (entries.find(key) != entries.end()) {
                return;
            }

            // Once the budget is reached the cache stops growing and keeps what
            // it has. Nothing is ever evicted while the process runs, so a blob
            // a compilation thread is reading cannot be freed underneath it.
            if (((totalBytes + size) > MaxTotalBytes) || (entries.size() >= MaxEntryCount)) {
                return;
            }

            entries.emplace(key, std::move(blob));
            totalBytes += size;
            dirty = true;
        }

        writeCondition.notify_all();
    }

    void ShaderBlobCache::replace(uint64_t key, const void *data, uint64_t size) {
        if (!enabled || (key == 0) || (data == nullptr) || (size == 0) || (size > MaxEntryBytes)) {
            return;
        }

        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data);
        Blob blob = std::make_shared<const std::vector<uint8_t>>(bytes, bytes + size);
        const std::unique_lock<std::mutex> lock(entriesMutex);
        auto it = entries.find(key);
        if (it != entries.end()) {
            if (it->second->size() == size) {
                return;
            }

            totalBytes -= it->second->size();
            entries.erase(it);
        }

        if ((totalBytes + size) > MaxTotalBytes) {
            return;
        }

        entries.emplace(key, std::move(blob));
        totalBytes += size;
        dirty = true;
    }

    void ShaderBlobCache::writeLoop() {
        Thread::setCurrentThreadName("RT64 Shader Blob Cache");
        Thread::setCurrentThreadPriority(Thread::Priority::Idle);

        while (writeThreadRunning) {
            {
                std::unique_lock<std::mutex> lock(writeMutex);
                writeCondition.wait_for(lock, std::chrono::seconds(WriteDelaySeconds), [this]() {
                    return !writeThreadRunning.load();
                });
            }

            if (!writeThreadRunning) {
                break;
            }

            // Asked on this thread rather than a game one, so producing the
            // blob never lands inside a frame.
            if (refresh != nullptr) {
                uint64_t refreshKey = 0;
                std::vector<uint8_t> refreshed;
                if (refresh(refreshKey, refreshed) && !refreshed.empty()) {
                    replace(refreshKey, refreshed.data(), refreshed.size());
                }
            }

            writeFile();
        }
    }

    bool ShaderBlobCache::writeFile() {
        std::vector<std::pair<uint64_t, Blob>> snapshot;
        {
            const std::unique_lock<std::mutex> lock(entriesMutex);
            if (!dirty) {
                return true;
            }

            snapshot.reserve(entries.size());
            for (const auto &entry : entries) {
                snapshot.emplace_back(entry.first, entry.second);
            }

            dirty = false;
        }

        // The lock is gone before any file I/O happens. A compile that finishes
        // during the write lands in the next one; it cannot disturb this one,
        // because everything in the snapshot is immutable.
        std::error_code ec;
        if (!filePath.parent_path().empty()) {
            std::filesystem::create_directories(filePath.parent_path(), ec);
        }

        std::vector<uint8_t> payload;
        uint32_t writtenEntries = 0;
        for (const auto &entry : snapshot) {
            const uint64_t blobSize = entry.second->size();
            if ((blobSize == 0) || (blobSize > MaxEntryBytes) || (writtenEntries >= MaxEntryCount)) {
                continue;
            }

            if ((payload.size() + sizeof(BlobCacheEntryHeader) + blobSize) > MaxTotalBytes) {
                break;
            }

            BlobCacheEntryHeader entryHeader = {};
            entryHeader.key = entry.first;
            entryHeader.size = uint32_t(blobSize);
            entryHeader.blobHash = XXH3_64bits(entry.second->data(), size_t(blobSize));

            const uint8_t *headerBytes = reinterpret_cast<const uint8_t *>(&entryHeader);
            payload.insert(payload.end(), headerBytes, headerBytes + sizeof(entryHeader));
            payload.insert(payload.end(), entry.second->begin(), entry.second->end());
            writtenEntries++;
        }

        BlobCacheFileHeader fileHeader = {};
        fileHeader.magic = BlobCacheMagic;
        fileHeader.formatVersion = FormatVersion;
        fileHeader.entryCount = writtenEntries;
        fileHeader.deviceKey = deviceKey;
        fileHeader.payloadBytes = payload.size();
        fileHeader.payloadHash = payload.empty() ? 0 : XXH3_64bits(payload.data(), payload.size());

        // Written beside the real file and renamed over it, so a crash or a lost
        // power cord during the write cannot leave a half-file for the next
        // launch to find.
        std::filesystem::path tempPath = filePath;
        tempPath += ".tmp";
        {
            std::ofstream stream(tempPath, std::ios::binary | std::ios::trunc);
            if (!stream.is_open()) {
                return false;
            }

            stream.write(reinterpret_cast<const char *>(&fileHeader), sizeof(fileHeader));
            if (!payload.empty()) {
                stream.write(reinterpret_cast<const char *>(payload.data()), std::streamsize(payload.size()));
            }

            stream.flush();
            if (!stream.good()) {
                stream.close();
                std::filesystem::remove(tempPath, ec);
                return false;
            }
        }

        std::filesystem::rename(tempPath, filePath, ec);
        if (ec) {
            std::filesystem::remove(tempPath, ec);
            return false;
        }

        return true;
    }

    bool ShaderBlobCache::readFile() {
        std::error_code ec;
        const uintmax_t fileSize = std::filesystem::file_size(filePath, ec);
        if (ec || (fileSize < sizeof(BlobCacheFileHeader)) || (fileSize > (MaxTotalBytes + sizeof(BlobCacheFileHeader)))) {
            return false;
        }

        std::ifstream stream(filePath, std::ios::binary);
        if (!stream.is_open()) {
            return false;
        }

        BlobCacheFileHeader fileHeader = {};
        stream.read(reinterpret_cast<char *>(&fileHeader), sizeof(fileHeader));
        if (!stream.good()) {
            return false;
        }

        // Any mismatch means a file this process must not use. Refusing one is
        // always safe: it costs a warm-up and nothing else.
        if (fileHeader.magic != BlobCacheMagic) return false;
        if (fileHeader.formatVersion != FormatVersion) return false;
        if (fileHeader.deviceKey != deviceKey) return false;
        if (fileHeader.entryCount > MaxEntryCount) return false;
        if (fileHeader.payloadBytes > MaxTotalBytes) return false;
        if (fileHeader.payloadBytes != (uint64_t(fileSize) - sizeof(BlobCacheFileHeader))) return false;

        std::vector<uint8_t> payload(size_t(fileHeader.payloadBytes));
        if (fileHeader.payloadBytes > 0) {
            stream.read(reinterpret_cast<char *>(payload.data()), std::streamsize(fileHeader.payloadBytes));
            if (!stream.good()) {
                return false;
            }

            if (XXH3_64bits(payload.data(), payload.size()) != fileHeader.payloadHash) {
                return false;
            }
        }

        // Parsed into a local map first, so any bounds failure below leaves the
        // live cache untouched and empty rather than half-filled.
        std::unordered_map<uint64_t, Blob> parsed;
        uint64_t parsedBytes = 0;
        size_t cursor = 0;
        for (uint32_t i = 0; i < fileHeader.entryCount; i++) {
            if ((cursor + sizeof(BlobCacheEntryHeader)) > payload.size()) return false;

            BlobCacheEntryHeader entryHeader = {};
            memcpy(&entryHeader, payload.data() + cursor, sizeof(entryHeader));
            cursor += sizeof(entryHeader);

            if ((entryHeader.key == 0) || (entryHeader.size == 0) || (entryHeader.size > MaxEntryBytes)) return false;
            if ((cursor + entryHeader.size) > payload.size()) return false;

            const uint8_t *blobStart = payload.data() + cursor;
            if (XXH3_64bits(blobStart, entryHeader.size) != entryHeader.blobHash) return false;

            parsed[entryHeader.key] = std::make_shared<const std::vector<uint8_t>>(blobStart, blobStart + entryHeader.size);
            parsedBytes += entryHeader.size;
            cursor += entryHeader.size;
        }

        const std::unique_lock<std::mutex> lock(entriesMutex);
        entries = std::move(parsed);
        totalBytes = parsedBytes;
        dirty = false;
        return true;
    }
};
