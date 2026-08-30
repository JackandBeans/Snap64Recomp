//
// RT64
//

#include "rt64_raster_shader_cache.h"

#include "common/rt64_thread.h"
#include "hle/rt64_snap_diag.h"

#define ENABLE_OPTIMIZED_SHADER_GENERATION

namespace RT64 {
    // RasterShaderCache::CompilationThread
    
    RasterShaderCache::CompilationThread::CompilationThread(RasterShaderCache *shaderCache) {
        assert(shaderCache != nullptr);

        this->shaderCache = shaderCache;

        thread = std::make_unique<std::thread>(&CompilationThread::loop, this);
        threadRunning = false;
    }

    RasterShaderCache::CompilationThread::~CompilationThread() {
        threadRunning = false;
        shaderCache->descQueueChanged.notify_all();
        thread->join();
        thread.reset(nullptr);
    }

    void RasterShaderCache::CompilationThread::loop() {
        Thread::setCurrentThreadName("RT64 Shader");

        // The shader compilation thread should have idle priority by default as the application can use the ubershader in the meantime.
        Thread::setCurrentThreadPriority(Thread::Priority::Idle);

        threadRunning = true;

        while (threadRunning) {
            ShaderDescription shaderDesc;
            bool fromPriorityQueue = false;
            
            // Check the top of the queue or wait if it's empty.
            {
                std::unique_lock<std::mutex> queueLock(shaderCache->descQueueMutex);
                shaderCache->descQueueActiveCount--;
                shaderCache->descQueueChanged.wait(queueLock, [this]() {
                    return !threadRunning || !shaderCache->descQueue.empty();
                });

                shaderCache->descQueueActiveCount++;
                if (!shaderCache->descQueue.empty()) {
                    shaderDesc = shaderCache->descQueue.front();
                    shaderCache->descQueue.pop();
                    fromPriorityQueue = true;
                }
            }
            
            // Compile the shader at the top of the queue.
            if (fromPriorityQueue) {
                assert((shaderCache->shaderUber != nullptr) && "Ubershader should've been created by the time a new shader is submitted to the cache.");
                const RenderPipelineLayout *uberPipelineLayout = shaderCache->shaderUber->pipelineLayout.get();
                const RenderMultisampling multisampling = shaderCache->multisampling;
                std::unique_ptr<RasterShader> newShader = std::make_unique<RasterShader>(shaderCache->device, shaderDesc, uberPipelineLayout, shaderCache->shaderFormat, multisampling, shaderCache->shaderCompiler.get(), &shaderCache->optimizerCacheSPIRV, shaderCache->blobCache.get());

                {
                    const std::unique_lock<std::mutex> lock(shaderCache->GPUShadersMutex);
                    shaderCache->GPUShaders[shaderDesc.hash()] = std::move(newShader);
                }

                snapdiag::shaderReadyCounter().fetch_add(1, std::memory_order_relaxed);

                // The moment the specialised pipeline replaces the ubershader.
                if (snapdiag::diagEnabled()) {
                    fprintf(stdout, "[SNAP-SHADER] ready  %016llX\n", (unsigned long long)shaderDesc.hash());
                    fflush(stdout);
                }
            }
        }
    }

    // RasterShaderCache

    RasterShaderCache::RasterShaderCache(uint32_t threadCount, uint32_t ubershaderThreadCount) {
        assert(threadCount > 0);

        this->threadCount = threadCount;
        this->ubershaderThreadCount = ubershaderThreadCount;

#ifdef ENABLE_OPTIMIZED_SHADER_GENERATION
#   ifdef _WIN32
        shaderCompiler = std::make_unique<ShaderCompiler>();
#   endif

        descQueueActiveCount = threadCount;

        for (uint32_t t = 0; t < threadCount; t++) {
            compilationThreads.push_back(std::make_unique<CompilationThread>(this));
        }
#endif
    }

    RasterShaderCache::~RasterShaderCache() {
        compilationThreads.clear();
    }

    void RasterShaderCache::setup(RenderDevice *device, RenderShaderFormat shaderFormat, const ShaderLibrary *shaderLibrary, const RenderMultisampling &multisampling) {
        assert(device != nullptr);

        this->device = device;
        this->shaderFormat = shaderFormat;
        this->multisampling = multisampling;

        shaderUber = std::make_unique<RasterShaderUber>(device, shaderFormat, multisampling, shaderLibrary, ubershaderThreadCount);
        usesHDR = shaderLibrary->usesHDR;

        // Initialize the re-spirv optimizer cache.
        if (shaderFormat == RenderShaderFormat::SPIRV) {
            optimizerCacheSPIRV.initialize();
        }
    }

    // Pokemon Snap port: keep compiled shader bytecode between launches. Only
    // the DXIL path is worth it -- the SPIR-V path specialises pre-baked modules
    // in process, which is already fast, and what it costs lives inside the
    // driver rather than in the compiler. Deliberately not called from setup(),
    // which runs again whenever antialiasing changes: the cache should survive
    // that, and it does because the sample count picks a different shader
    // library and the library is part of every key.
    void RasterShaderCache::openBlobCache(const std::filesystem::path &path) {
        if ((shaderFormat != RenderShaderFormat::DXIL) || path.empty()) {
            return;
        }

        blobCache = std::make_unique<ShaderBlobCache>();
        blobCache->open(path, device);
    }

    // Pokemon Snap port: replay every shader this machine has ever needed,
    // before the game needs any of them. The DXIL blob cache and the driver
    // pipeline library make each replayed compile near-free on a warm
    // machine; on a cold one the work happens during the boot logos on idle
    // threads instead of as a present stall the first time a Pokemon walks
    // on screen. The file is raw packed ShaderDescriptions behind a version
    // guard; a size mismatch discards it wholesale.
    void RasterShaderCache::openSeenList(const std::filesystem::path &path) {
        if (path.empty()) {
            return;
        }
        seenListPath = path;

        FILE *f = nullptr;
#if defined(_WIN32)
        _wfopen_s(&f, path.wstring().c_str(), L"rb");
#else
        f = fopen(path.string().c_str(), "rb");
#endif
        if (f == nullptr) {
            return;
        }
        uint32_t magic = 0;
        uint32_t descSize = 0;
        size_t replayed = 0;
        bool headerOk = (fread(&magic, 4, 1, f) == 1) && (magic == 0x314E5353u) &&
                        (fread(&descSize, 4, 1, f) == 1) && (descSize == uint32_t(sizeof(ShaderDescription)));
        if (headerOk) {
            ShaderDescription desc;
            replayingSeenList = true;
            while (fread(&desc, sizeof(desc), 1, f) == 1) {
                submit(desc);
                replayed++;
            }
            replayingSeenList = false;
        }
        fclose(f);
        if (!headerOk) {
            // A stale layout must not keep collecting appends behind a bad
            // header; recording restarts clean.
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
        if (replayed > 0) {
            fprintf(stdout, "[SNAP-SHADER] warming %zu shaders from the seen list\n", replayed);
            fflush(stdout);
        }
    }

    void RasterShaderCache::submit(const ShaderDescription &desc) {
        {
            std::unique_lock<std::mutex> queueLock(submissionMutex);

            // Verify if an entry with the same hash was already submitted before.
            const uint64_t shaderHash = desc.hash();
            bool &found = shaderHashes[shaderHash];
            if (found) {
                return;
            }

            found = true;
            snapdiag::shaderAskedCounter().fetch_add(1, std::memory_order_relaxed);

            // A genuinely new shader joins the seen list, so the next launch
            // warms it. Replayed submissions are already in the file.
            if (!replayingSeenList && !seenListPath.empty()) {
                const bool fresh = !std::filesystem::exists(seenListPath);
                FILE *f = nullptr;
#if defined(_WIN32)
                _wfopen_s(&f, seenListPath.wstring().c_str(), L"ab");
#else
                f = fopen(seenListPath.string().c_str(), "ab");
#endif
                if (f != nullptr) {
                    if (fresh) {
                        const uint32_t magic = 0x314E5353u;
                        const uint32_t descSize = uint32_t(sizeof(ShaderDescription));
                        fwrite(&magic, 4, 1, f);
                        fwrite(&descSize, 4, 1, f);
                    }
                    fwrite(&desc, sizeof(desc), 1, f);
                    fclose(f);
                }
            }

            // Pokemon Snap port, diagnostic: every draw whose specialised
            // pipeline is still compiling renders through the ubershader,
            // whose fixed state differs from the specialised one in depth
            // equality and blending. An object that is visible exactly once,
            // at first sight, and never again would look like this handoff.
            // The submission and the completion below bracket the window.
            if (snapdiag::diagEnabled()) {
                fprintf(stdout, "[SNAP-SHADER] submit %016llX om %08X %08X cc %08X %08X cyc %u zc %u zu %u zm %u\n",
                    (unsigned long long)shaderHash, desc.otherMode.H, desc.otherMode.L,
                    desc.colorCombiner.H, desc.colorCombiner.L,
                    desc.otherMode.cycleType() >> G_MDSFT_CYCLETYPE,
                    desc.otherMode.zCmp() ? 1u : 0u, desc.otherMode.zUpd() ? 1u : 0u,
                    desc.otherMode.zMode() >> 10);
                fflush(stdout);
            }
        }

        // Push a new shader compilation to the queue.
        {
            const std::unique_lock<std::mutex> queueLock(descQueueMutex);
            descQueue.push(desc);
        }

        descQueueChanged.notify_all();
    }
    
    void RasterShaderCache::waitForAll() {
        {
            std::unique_lock<std::mutex> queueLock(descQueueMutex);
            descQueue = std::queue<ShaderDescription>();
        }

        bool keepWaiting = false;
        do {
            std::unique_lock<std::mutex> queueLock(descQueueMutex);
            keepWaiting = (descQueueActiveCount > 0);
        } while (keepWaiting);
    }

    void RasterShaderCache::destroyAll() {
        {
            std::unique_lock<std::mutex> lock(GPUShadersMutex);
            GPUShaders.clear();
        }

        {
            std::unique_lock<std::mutex> queueLock(submissionMutex);
            shaderHashes.clear();
        }
    }

    RasterShader *RasterShaderCache::getGPUShader(const ShaderDescription &desc) {
        const uint64_t shaderHash = desc.hash();

        const std::unique_lock<std::mutex> lock(GPUShadersMutex);
        auto shaderIt = GPUShaders.find(shaderHash);
        if (shaderIt == GPUShaders.end()) {
            return nullptr;
        }

        return shaderIt->second.get();
    }

    RasterShaderUber *RasterShaderCache::getGPUShaderUber() const {
        return shaderUber.get();
    }

    uint32_t RasterShaderCache::shaderCount() {
        std::unique_lock<std::mutex> lock(GPUShadersMutex);
        return GPUShaders.size();
    }
};