#include "widescreen.h"
#include "settings.h"
#include "recomp.h"
#include <atomic>
#include <bit>

extern "C" void __real_Pokemon_GetFlag100(uint8_t*, recomp_context*);
extern "C" void func_80364618_504A28(uint8_t*, recomp_context*);

namespace snap {
extern std::atomic<bool> g_app_level_resident;
static std::atomic<float> expansion{1.0f};
void update_visibility_viewport(int width, int height) {
    expansion.store(visibility_expansion(width, height), std::memory_order_relaxed);
}
}

extern "C" void Pokemon_GetFlag100(uint8_t* rdram, recomp_context* ctx) {
    const gpr object = ctx->r4;
    __real_Pokemon_GetFlag100(rdram, ctx);
    const float expansion = snap::expansion.load(std::memory_order_relaxed);
    if (expansion <= 1.0f || !snap::g_app_level_resident.load(std::memory_order_relaxed)) return;
    {
        std::lock_guard<std::mutex> lock(snap::settings_mutex());
        if (!snap::settings().widescreen) return;
    }

    // Query draw eligibility independently of the photo collector's 12 slots.
    // Do not change flag 0x100 or photo eligibility: those describe the native
    // photo view. Scripted GObj hiding still runs before this draw callback.
    const gpr pokemon = MEM_W(0x58, object);
    if (MEM_HU(0x08, pokemon) & 0x40) { ctx->r2 = 0; return; }
    if (std::bit_cast<float>(uint32_t(MEM_W(0x6C, pokemon))) > 10000.0f) {
        ctx->r2 = 1;
        return;
    }

    auto call = *ctx;
    call.f_odd = call.mips3_float_mode ? &call.f1.u32l : &call.f0.u32h;
    call.r4 = object;
    call.r5 = MEM_W(0x100, pokemon);
    call.r6 = MEM_W(0x104, pokemon);
    call.r7 = MEM_W(0x108, pokemon);
    // The retail callee stores arguments in the caller's home slots. Keep the
    // original query's context and stack effects, apart from its return value.
    const int32_t homes[4] = {MEM_W(0, call.r29), MEM_W(4, call.r29),
                             MEM_W(8, call.r29), MEM_W(12, call.r29)};
    func_80364618_504A28(rdram, &call);
    // Its 0x40-byte frame leaves guMtxXFMF's x/y/z outputs at sp-4/-8/-12.
    const float x = std::bit_cast<float>(uint32_t(MEM_W(-4, call.r29)));
    const float y = std::bit_cast<float>(uint32_t(MEM_W(-8, call.r29)));
    const float z = std::bit_cast<float>(uint32_t(MEM_W(-12, call.r29)));
    for (int i = 0; i < 4; ++i) MEM_W(i * 4, call.r29) = homes[i];
    ctx->r2 = call.r2 != 0 && snap::outside_wide_view(x, y, z, expansion);
}
