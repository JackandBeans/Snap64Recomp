/**
 * @file rt64_render_context.cpp
 * @brief RT64 RendererContext implementation for WaveRace64-Recomp.
 *
 * Wraps the RT64::Application into an ultramodern::renderer::RendererContext
 * subclass so that the N64ModernRuntime can drive display list submission,
 * screen updates, and shutdown through a uniform interface.
 */

#include "rt64_render_context.h"

#include <cstdio>
#include <cassert>
#include <algorithm>

#include "ultramodern/renderer_context.hpp"
#include "ultramodern/config.hpp"

#include "settings.h"

#if defined(_WIN32)
#include <unknwn.h>
#include <objidl.h>
#endif
#include "hle/rt64_application.h"

// ---------------------------------------------------------------------------
// Static dummy buffers required by RT64 (must persist for the lifetime of app)
// ---------------------------------------------------------------------------

// ROM header placeholder â€” RT64 reads the first 0x40 bytes for cartridge info.
// We don't need real header data for HLE rendering.
static uint8_t s_dummy_rom_header[0x40] = {};

// SP DMEM/IMEM â€” RT64's RSP HLE doesn't use these but the core struct must be
// non-null to avoid null-dereferences inside RT64's state setup paths.
static uint8_t s_DMEM[0x1000] = {};
static uint8_t s_IMEM[0x1000] = {};

// RDP / MI register storage â€” RT64 may read/write these during HLE rendering.
// Providing real zero-initialised storage prevents nullptr dereferences.
static unsigned int s_MI_INTR_REG     = 0;
static unsigned int s_DPC_START_REG   = 0;
static unsigned int s_DPC_END_REG     = 0;
static unsigned int s_DPC_CURRENT_REG = 0;
static unsigned int s_DPC_STATUS_REG  = 0;
static unsigned int s_DPC_CLOCK_REG   = 0;
static unsigned int s_DPC_BUFBUSY_REG = 0;
static unsigned int s_DPC_PIPEBUSY_REG= 0;
static unsigned int s_DPC_TMEM_REG    = 0;

// No-op interrupt check â€” the recompiler runtime handles interrupts itself.
static void dummy_check_interrupts() {}

// ---------------------------------------------------------------------------
// RT64 RendererContext subclass
// ---------------------------------------------------------------------------

namespace snap {

extern bool g_app_level_resident;                            // overlay_hook.cpp
extern bool g_focus_dot_visible;                             // focus_dot.cpp
extern bool g_world_rebased;                                 // matrix_tags.cpp
extern float g_world_rebase_delta[3];                        // matrix_tags.cpp

class RT64Context : public ultramodern::renderer::RendererContext {
public:
    RT64Context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode) {
        // Populate the RT64 core struct from the emulated hardware state.
        RT64::Application::Core core{};

        // The RDRAM pointer is the base of the emulated N64 memory.
        core.RDRAM = rdram;

        // Use the static dummy header â€” real ROM header bytes are not needed for HLE.
        core.HEADER = s_dummy_rom_header;

        // DMEM/IMEM must be non-null; RT64 may reference them during state init.
        core.DMEM = s_DMEM;
        core.IMEM = s_IMEM;

        // Wire up a no-op interrupt callback. The ultramodern runtime owns
        // interrupt delivery; RT64 does not need to trigger them directly.
        core.checkInterrupts = dummy_check_interrupts;

        // RDP / MI registers â€” provide real storage so RT64 can read/write safely.
        core.MI_INTR_REG      = &s_MI_INTR_REG;
        core.DPC_START_REG    = &s_DPC_START_REG;
        core.DPC_END_REG      = &s_DPC_END_REG;
        core.DPC_CURRENT_REG  = &s_DPC_CURRENT_REG;
        core.DPC_STATUS_REG   = &s_DPC_STATUS_REG;
        core.DPC_CLOCK_REG    = &s_DPC_CLOCK_REG;
        core.DPC_BUFBUSY_REG  = &s_DPC_BUFBUSY_REG;
        core.DPC_PIPEBUSY_REG = &s_DPC_PIPEBUSY_REG;
        core.DPC_TMEM_REG     = &s_DPC_TMEM_REG;

        // VI register pointers: RT64 reads these from RDRAM to determine framebuffer.
        // The ultramodern runtime sets up VI registers at known addresses.
        // We obtain the VI registers from the ultramodern runtime.
        auto* vi_regs = ultramodern::renderer::get_vi_regs();
        core.VI_STATUS_REG          = &vi_regs->VI_STATUS_REG;
        core.VI_ORIGIN_REG          = &vi_regs->VI_ORIGIN_REG;
        core.VI_WIDTH_REG           = &vi_regs->VI_WIDTH_REG;
        core.VI_INTR_REG            = &vi_regs->VI_INTR_REG;
        core.VI_V_CURRENT_LINE_REG  = &vi_regs->VI_V_CURRENT_LINE_REG;
        core.VI_TIMING_REG          = &vi_regs->VI_TIMING_REG;
        core.VI_V_SYNC_REG          = &vi_regs->VI_V_SYNC_REG;
        core.VI_H_SYNC_REG          = &vi_regs->VI_H_SYNC_REG;
        core.VI_LEAP_REG            = &vi_regs->VI_LEAP_REG;
        core.VI_H_START_REG         = &vi_regs->VI_H_START_REG;
        core.VI_V_START_REG         = &vi_regs->VI_V_START_REG;
        core.VI_V_BURST_REG         = &vi_regs->VI_V_BURST_REG;
        core.VI_X_SCALE_REG         = &vi_regs->VI_X_SCALE_REG;
        core.VI_Y_SCALE_REG         = &vi_regs->VI_Y_SCALE_REG;

        // Set up the SDL window for RT64.
#if defined(_WIN32)
        core.window = window_handle.window;
#elif defined(__APPLE__)
        core.window.window = window_handle.window;
        core.window.view   = window_handle.view;
#else
        // Linux / Android: WindowHandle IS SDL_Window*
        core.window = window_handle;
#endif

        // Configure the RT64 application.
        RT64::ApplicationConfiguration app_config{};
        app_config.appId = "pokemonsnap";
        // Disable config file I/O â€” we manage settings ourselves.
        app_config.useConfigurationFile = false;

        // Create the RT64 application.
        app_ = std::make_unique<RT64::Application>(core, app_config);

        // Enable developer/debug mode if requested.
        app_->userConfig.developerMode = developer_mode;

        // Attempt setup.
        auto result = app_->setup(0);

        switch (result) {
            case RT64::Application::SetupResult::Success:
                setup_result = ultramodern::renderer::SetupResult::Success;
                break;
            case RT64::Application::SetupResult::DynamicLibrariesNotFound:
                setup_result = ultramodern::renderer::SetupResult::DynamicLibrariesNotFound;
                break;
            case RT64::Application::SetupResult::InvalidGraphicsAPI:
                setup_result = ultramodern::renderer::SetupResult::InvalidGraphicsAPI;
                break;
            case RT64::Application::SetupResult::GraphicsAPINotFound:
                setup_result = ultramodern::renderer::SetupResult::GraphicsAPINotFound;
                break;
            case RT64::Application::SetupResult::GraphicsDeviceNotFound:
                setup_result = ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
                break;
        }

        if (result != RT64::Application::SetupResult::Success) {
            app_.reset();
            return;
        }

        // Map the API that RT64 actually chose.
        switch (app_->chosenGraphicsAPI) {
            case RT64::UserConfiguration::GraphicsAPI::D3D12:
                chosen_api = ultramodern::renderer::GraphicsApi::D3D12;
                break;
            case RT64::UserConfiguration::GraphicsAPI::Vulkan:
                chosen_api = ultramodern::renderer::GraphicsApi::Vulkan;
                break;
            case RT64::UserConfiguration::GraphicsAPI::Metal:
                chosen_api = ultramodern::renderer::GraphicsApi::Metal;
                break;
            default:
                chosen_api = ultramodern::renderer::GraphicsApi::Vulkan;
                break;
        }

        printf("[SNAP-RT64] Renderer context created (result=%d, api=%d)\n",
               static_cast<int>(result), static_cast<int>(chosen_api));

    }

    ~RT64Context() override {
        if (app_) {
            app_->end();
        }
    }

    bool valid() override {
        return setup_result == ultramodern::renderer::SetupResult::Success && app_ != nullptr;
    }

    bool update_config(const ultramodern::renderer::GraphicsConfig& old_config,
                       const ultramodern::renderer::GraphicsConfig& new_config) override {
        (void)old_config;
        if (!app_) return false;
        namespace ren = ultramodern::renderer;

        // Aspect: Expand = true widescreen (extended FOV), applied to both the
        // primary and extended-origin framebuffers.
        auto ar = (new_config.ar_option == ren::AspectRatio::Expand)
            ? RT64::UserConfiguration::AspectRatio::Expand
            : RT64::UserConfiguration::AspectRatio::Original;
        app_->userConfig.aspectRatio = ar;
        app_->userConfig.extAspectRatio = ar;

        switch (new_config.msaa_option) {
            case ren::Antialiasing::MSAA8X: app_->userConfig.antialiasing = RT64::UserConfiguration::Antialiasing::MSAA8X; break;
            case ren::Antialiasing::MSAA4X: app_->userConfig.antialiasing = RT64::UserConfiguration::Antialiasing::MSAA4X; break;
            case ren::Antialiasing::MSAA2X: app_->userConfig.antialiasing = RT64::UserConfiguration::Antialiasing::MSAA2X; break;
            default:                        app_->userConfig.antialiasing = RT64::UserConfiguration::Antialiasing::None;   break;
        }

        switch (new_config.rr_option) {
            case ren::RefreshRate::Display: app_->userConfig.refreshRate = RT64::UserConfiguration::RefreshRate::Display; break;
            case ren::RefreshRate::Manual:  app_->userConfig.refreshRate = RT64::UserConfiguration::RefreshRate::Manual;  break;
            default:                        app_->userConfig.refreshRate = RT64::UserConfiguration::RefreshRate::Original; break;
        }
        app_->userConfig.refreshRateTarget = new_config.rr_manual_value;
        app_->userConfig.downsampleMultiplier = std::max(1, new_config.ds_option);
        app_->userConfig.threePointFiltering = snap::settings().three_point_filtering;

        app_->userConfig.validate();
        app_->updateUserConfig(true);

        // Overscan crop: hide the dead margins the game leaves in its
        // framebuffer, the way the CRTs it was authored for did. See
        // settings.h for the measurements behind the defaults. Bounded so a
        // hand-edited config cannot zoom the picture into nonsense: half the
        // axis stays, which also keeps the scaled viewport far inside the
        // graphics API's coordinate limits.
        const bool cropOn = snap::settings().crop_enabled;
        const auto boundedCrop = [cropOn](int value, int limit) {
            return cropOn ? uint32_t(std::clamp(value, 0, limit)) : 0u;
        };
        app_->enhancementConfig.presentation.crop[0] = boundedCrop(snap::settings().crop_left, 80);
        app_->enhancementConfig.presentation.crop[1] = boundedCrop(snap::settings().crop_right, 80);
        app_->enhancementConfig.presentation.crop[2] = boundedCrop(snap::settings().crop_top, 60);
        app_->enhancementConfig.presentation.crop[3] = boundedCrop(snap::settings().crop_bottom, 60);
        app_->updateEnhancementConfig();

        // Applied unconditionally: the first push after startup compares
        // equal to the defaults, so a saved fullscreen setting would never
        // reach the window otherwise.
        app_->setFullScreen(new_config.wm_option == ren::WindowMode::Fullscreen);
        return true;
    }

    void enable_instant_present() override {
        if (!app_) return;
        // Enable present-early mode for minimal latency (matches reference).
        app_->enhancementConfig.presentation.mode =
            RT64::EnhancementConfiguration::Presentation::Mode::PresentEarly;
        app_->updateEnhancementConfig();
    }

    void send_dl(const OSTask* task) override {
        if (!app_) return;

        // task->t.ucode / ucode_data / data_ptr are PTR(u64) = int32_t holding
        // N64 KSEG0 virtual addresses (e.g. 0x80XXXXXX).
        // RT64 expects physical byte offsets into the RDRAM buffer.
        // Masking with 0x3FFFFFF strips the KSEG0/KSEG1 high bits and gives
        // the physical address within the N64's 64MB address space.
        uint32_t ucode_phys      = static_cast<uint32_t>(task->t.ucode)      & 0x3FFFFFFu;
        uint32_t ucode_data_phys = static_cast<uint32_t>(task->t.ucode_data) & 0x3FFFFFFu;
        uint32_t dl_start_phys   = static_cast<uint32_t>(task->t.data_ptr)   & 0x3FFFFFFu;

        // Reset the RSP state machine before processing each new display list.
        // REQUIRED: removing this leaks matrix/vertex state across frames and
        // shreds terrain geometry (tested 2026-08-15). The texture flashing is
        // NOT caused by this reset.
        // Kept in sync every list so F6 takes effect immediately.
        app_->state->setRenderToRAM(snap::settings().render_to_ram ? 1 : 0);
        app_->workloadQueue->snapInterpolateCamera = snap::settings().interpolate_camera;
        app_->workloadQueue->ubershadersOnly = snap::settings().ubershaders_only;

        // The crop follows edits to the settings file like the toggles above
        // do, instead of waiting for an unrelated graphics-config change to
        // re-run update_config. Pushed only when it actually changed; the
        // push is a mutex and a struct copy.
        {
            const bool cropOn = snap::settings().crop_enabled;
            const auto boundedCrop = [cropOn](int value, int limit) {
                return cropOn ? uint32_t(std::clamp(value, 0, limit)) : 0u;
            };
            const uint32_t crop[4] = {
                boundedCrop(snap::settings().crop_left, 80),
                boundedCrop(snap::settings().crop_right, 80),
                boundedCrop(snap::settings().crop_top, 60),
                boundedCrop(snap::settings().crop_bottom, 60),
            };
            uint32_t (&applied)[4] = app_->enhancementConfig.presentation.crop;
            if ((crop[0] != applied[0]) || (crop[1] != applied[1]) || (crop[2] != applied[2]) || (crop[3] != applied[3])) {
                for (uint32_t i = 0; i < 4; i++) {
                    applied[i] = crop[i];
                }
                app_->updateEnhancementConfig();
            }
        }

        app_->state->rsp->reset();

        // Tell the HLE interpreter which GBI microcode variant to use.
        // This must be called before processDisplayLists() or hleGBI will be null.
        app_->interpreter->loadUCodeGBI(ucode_phys, ucode_data_phys, true);

        // The game's focus indicator (red dot in the camera crosshair) is
        // written by the CPU directly into the framebuffer in RDRAM after the
        // frame's render task completes (PokemonDetector_PostProcessImage).
        // On hardware the VI scans RDRAM so the dot shows; RT64 presents its
        // GPU render target instead, so the CPU's scribble is never visible.
        // Instead, ask RT64 to draw the dot as the frame's final draws right
        // before G_RDPFULLSYNC (see snapFocusDotRequest in rt64_state.h).
        // The game draws its focus indicator into the framebuffer in RDRAM,
        // which HLE presentation never shows. src/focus_dot.cpp watches the
        // game's own draw and reports it here, so the condition, its timing
        // and the state behind it all stay the game's (see
        // State::snapFocusDotRequest).
        //
        // Nothing else gates this. It used to be qualified with the in-level
        // overlay being resident, from an earlier design where this code read
        // .app_level bss directly and needed those addresses to mean
        // something. It does not any more, and the qualifier was never true
        // during play in any case: the flag is only set for the overlay whose
        // ROM start is 0x46270, and it is cleared again by any load covering
        // the level code region, which the 0x05F050 overlay does every time it
        // comes in. So the indicator was suppressed even on the frames the
        // game had drawn it. The observation is self-gating -- the hook only
        // runs when the game runs it, and it clears itself below.
        app_->state->snapFocusDotRequest = snap::g_focus_dot_visible;

        // Crossing into the next world block moves the origin everything is
        // expressed about, so this frame cannot be blended with the last one.
        // Written onto the workload this display list is about to fill, so the
        // rebase travels with its own frame. The queue's write slot was begun at
        // the end of the previous list, so nothing resets it between here and
        // the renderer reading it.
        if (snap::g_world_rebased) {
            snap::g_world_rebased = false;
            RT64::Workload &workload = app_->workloadQueue->workloads[app_->workloadQueue->writeCursor];
            workload.snapOriginRebased = true;
            workload.snapOriginDelta = hlslpp::float3(
                snap::g_world_rebase_delta[0],
                snap::g_world_rebase_delta[1],
                snap::g_world_rebase_delta[2]);
        }

        // Consumed, so the next frame has to be established by the game again.
        // Without this the indicator latches on: with render to RAM enabled the
        // frame RT64 drew, dot included, is copied back over the framebuffer in
        // RDRAM, so the pixel the hook samples would already be the dot colour
        // before the game had decided anything.
        snap::g_focus_dot_visible = false;

        // Process the display list. Pass 0 for dlEndAddress â€” RT64 will walk
        // the list until it encounters a G_ENDDL command.
        app_->processDisplayLists(
            app_->core.RDRAM,
            dl_start_phys,
            0,    // end address: 0 means walk until G_ENDDL
            true  // HLE mode
        );

        // Wait for this frame's workload to finish rendering before letting the
        // game continue. Snap runs at 60fps with only two framebuffers; without
        // this backpressure the game runs ahead of the async render queue and a
        // later workload can repaint the render target for a framebuffer while
        // the present thread is displaying it, which showed up as random black
        // frames and layer flicker in the intro forest and beach rock wall.
        app_->workloadQueue->waitForWorkloadId(app_->state->workloadId);


    }

    void update_screen() override {
        if (app_) {
            app_->updateScreen();
        }
    }

    void shutdown() override {
        if (app_) {
            app_->end();
            app_.reset();
        }
    }

    uint32_t get_display_framerate() const override {
        if (app_ && app_->presentQueue) {
            return app_->presentQueue->ext.sharedResources->swapChainRate;
        }
        // Pokemon Snap targets 30fps (NTSC) â€” use as fallback.
        return 30;
    }

    float get_resolution_scale() const override {
        // TODO: Return configurable resolution scale from RT64.
        return 1.0f;
    }

private:
    std::unique_ptr<RT64::Application> app_;
};

// ---------------------------------------------------------------------------
// Factory function
// ---------------------------------------------------------------------------

std::unique_ptr<ultramodern::renderer::RendererContext> create_render_context(
    uint8_t* rdram,
    ultramodern::renderer::WindowHandle window_handle,
    bool developer_mode
) {
    auto ctx = std::make_unique<RT64Context>(rdram, window_handle, developer_mode);
    if (!ctx->valid()) {
        fprintf(stderr, "[SNAP-RT64] Failed to create render context (result=%d)\n",
                static_cast<int>(ctx->get_setup_result()));
        return nullptr;
    }
    return ctx;
}

} // namespace snap



