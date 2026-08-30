//
// RT64
//

#pragma once

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

#include "rt64_raster_shader.h"

namespace RT64 {
    struct RasterShaderCache {
        struct CompilationThread {
            RasterShaderCache *shaderCache;
            std::unique_ptr<std::thread> thread;
            std::atomic<bool> threadRunning;

            CompilationThread(RasterShaderCache *shaderCache);
            ~CompilationThread();
            void loop();
        };

        RenderDevice *device;
        std::unique_ptr<RasterShaderUber> shaderUber;
        OptimizerCacheSPIRV optimizerCacheSPIRV;
        std::mutex submissionMutex;
        std::queue<ShaderDescription> descQueue;
        std::mutex descQueueMutex;
        int32_t descQueueActiveCount = 0;
        std::condition_variable descQueueChanged;
        std::unordered_map<uint64_t, bool> shaderHashes;
        std::unordered_map<uint64_t, std::unique_ptr<RasterShader>> GPUShaders;
        std::mutex GPUShadersMutex;
        std::list<std::unique_ptr<CompilationThread>> compilationThreads;
        uint32_t threadCount;
        uint32_t ubershaderThreadCount;
        RenderShaderFormat shaderFormat;
        std::unique_ptr<ShaderCompiler> shaderCompiler;
        RenderMultisampling multisampling;
        bool usesHDR = false;

        // Survives setup(), which updateMultisampling() calls a second time.
        // Multisampling picks a different pixel shader library, and the library
        // is part of every key, so both configurations coexist in one file.
        std::unique_ptr<ShaderBlobCache> blobCache;

        // The warm list: every ShaderDescription this machine has ever seen,
        // replayed through submit() at startup so specialised pipelines exist
        // before gameplay ever asks -- first sight of a material mid-course
        // was a measured 30-70ms present stall (ubershader-cost frames plus
        // pipeline creation contending with the driver's submission path).
        std::filesystem::path seenListPath;
        bool replayingSeenList = false;

        RasterShaderCache(uint32_t threadCount, uint32_t ubershaderThreadCount);
        ~RasterShaderCache();
        void setup(RenderDevice *device, RenderShaderFormat shaderFormat, const ShaderLibrary *shaderLibrary, const RenderMultisampling &multisampling);
        void openBlobCache(const std::filesystem::path &path);
        void openSeenList(const std::filesystem::path &path);
        void submit(const ShaderDescription &desc);
        void waitForAll();
        void destroyAll();
        RasterShader *getGPUShader(const ShaderDescription &desc);
        RasterShaderUber *getGPUShaderUber() const;
        uint32_t shaderCount();
    };
};