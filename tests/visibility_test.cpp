#include "hle/rt64_framebuffer_pair.h"
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <bit>
#include <vector>
#include "settings.h"
#include "widescreen.h"
#include "recomp.h"

namespace snap {
std::atomic<bool> g_app_level_resident{true};
static Settings test_settings;
static std::mutex test_mutex;
Settings& settings() { return test_settings; }
std::mutex& settings_mutex() { return test_mutex; }
}

static float camera_x = 1200.0f, camera_y = 0.0f, camera_z = -1000.0f;
extern "C" void __real_Pokemon_GetFlag100(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = (MEM_HU(8, MEM_W(0x58, ctx->r4)) & 0x100) != 0;
    ctx->r16 = 0x12345678;
}
extern "C" void func_80364618_504A28(uint8_t* rdram, recomp_context* ctx) {
    MEM_W(-4, ctx->r29) = std::bit_cast<uint32_t>(camera_x);
    MEM_W(-8, ctx->r29) = std::bit_cast<uint32_t>(camera_y);
    MEM_W(-12, ctx->r29) = std::bit_cast<uint32_t>(camera_z);
    MEM_W(0, ctx->r29) = 0xDEADBEEF;
    MEM_W(12, ctx->r29) = 0xBADCAFE;
    ctx->f_odd[0] = 0x12345678;
    ctx->r16 = 0;
    ctx->r2 = 1;
}
extern "C" void Pokemon_GetFlag100(uint8_t*, recomp_context*);

static int checks = 0;
static void check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
    ++checks;
}
static RT64::FixedRect rect(int x0, int y0, int x1, int y1) {
    RT64::FixedRect r; r.ulx = x0; r.uly = y0; r.lrx = x1; r.lry = y1; return r;
}
static bool equal(const RT64::FixedRect& a, const RT64::FixedRect& b) {
    return a.ulx == b.ulx && a.uly == b.uly && a.lrx == b.lrx && a.lry == b.lry;
}
static void draw(RT64::FramebufferPair& pair, RT64::Projection::Type type, RT64::FixedRect scissor) {
    pair.changeProjection(pair.projectionCount + 1, type);
    RT64::GameCall call{};
    call.callDesc.scissorRect = scissor;
    call.callDesc.triangleCount = 1;
    pair.addGameCall(call);
}
int main() {
    using Type = RT64::Projection::Type;
    RT64::FramebufferPair pair;
    pair.reset();
    const auto full = rect(0, 0, 1280, 960);
    const auto inset = rect(120, 80, 1160, 880);
    check(pair.displayColorRect(true).isEmpty(), "empty pass stays empty");
    draw(pair, Type::Perspective, full);
    check(pair.drawColorRect.isEmpty(), "reproduce a submitted 3D pass with no native pixels");
    check(pair.displayColorRect(false).isEmpty(), "4:3 keeps original pass rejection");
    check(equal(pair.displayColorRect(true), full), "wide display keeps complete offscreen Pokemon pass");
    check(pair.drawColorRect.isEmpty() && pair.drawDepthRect.isEmpty(), "display coverage does not change native writeback bounds");
    pair.drawColorRect = rect(200, 100, 500, 700);
    check(equal(pair.displayColorRect(true), pair.drawColorRect), "existing native coverage unchanged");
    pair.reset(); draw(pair, Type::Perspective, inset);
    check(equal(pair.displayColorRect(true), inset), "viewfinder keeps its own scissor");
    for (Type type : {Type::Rectangle, Type::Orthographic, Type::Triangle}) {
        pair.reset(); draw(pair, type, full);
        check(pair.displayColorRect(true).isEmpty(), "2D/offscreen copies do not acquire 3D display coverage");
    }
    pair.reset(); pair.changeProjection(1, Type::Perspective);
    pair.projections[0].scissorRect = full;
    check(pair.displayColorRect(true).isEmpty(), "unused perspective projection remains empty");
    pair.reset(); draw(pair, Type::Perspective, rect(0, 0, 0, 0));
    check(pair.displayColorRect(true).isEmpty(), "zero-area scissor still hides pass");
    pair.reset(); draw(pair, Type::Perspective, inset); draw(pair, Type::Perspective, full);
    check(equal(pair.displayColorRect(true), full), "multiple perspective passes retain all display coverage");
    std::vector<uint8_t> memory(1024 * 1024);
    uint8_t* rdram = memory.data();
    recomp_context ctx{};
    ctx.r4 = (gpr)(int32_t)0x80010000;
    ctx.r29 = (gpr)(int32_t)0x80030000;
    const gpr pokemon = (gpr)(int32_t)0x80011000;
    MEM_W(0x58, ctx.r4) = pokemon;
    MEM_H(8, pokemon) = 0x100;
    MEM_W(0x6C, pokemon) = std::bit_cast<uint32_t>(1000.0f);
    MEM_W(0, ctx.r29) = 42; MEM_W(12, ctx.r29) = 99;
    snap::settings().widescreen = true;
    for (const auto dimensions : {std::pair{640, 480}, {1600, 900}, {2520, 1080}, {3840, 1080}}) {
        snap::update_visibility_viewport(dimensions.first, dimensions.second);
        Pokemon_GetFlag100(rdram, &ctx);
        check(ctx.r2 == (dimensions.first == 640), "4:3 rejection preserved; widened draw retained at 16:9, 21:9 and 32:9");
        check(MEM_HU(8, pokemon) == 0x100, "native photo visibility flag remains unchanged");
    }
    check(ctx.r16 == 0x12345678 && ctx.f0.u32h == 0, "guest query registers preserved");
    check(MEM_W(0, ctx.r29) == 42 && MEM_W(12, ctx.r29) == 99, "guest argument home slots preserved");
    camera_x = 2500; Pokemon_GetFlag100(rdram, &ctx);
    check(ctx.r2 == 0, "32:9 draws beyond narrower ultrawide gate");
    snap::update_visibility_viewport(2520, 1080); Pokemon_GetFlag100(rdram, &ctx);
    check(ctx.r2 == 1, "outside expanded horizontal view still rejected");
    camera_x = 1200; camera_y = 1000; Pokemon_GetFlag100(rdram, &ctx);
    check(ctx.r2 == 1, "vertical rejection preserved");
    camera_y = 0; camera_z = 1; Pokemon_GetFlag100(rdram, &ctx);
    check(ctx.r2 == 1, "behind-camera rejection preserved");
    camera_z = -10001; Pokemon_GetFlag100(rdram, &ctx);
    check(ctx.r2 == 1, "far camera-plane rejection preserved");
    camera_z = -1000; MEM_W(0x6C, pokemon) = std::bit_cast<uint32_t>(10001.0f);
    Pokemon_GetFlag100(rdram, &ctx); check(ctx.r2 == 1, "player-distance rejection preserved");
    MEM_H(8, pokemon) = 0x140; Pokemon_GetFlag100(rdram, &ctx);
    check(ctx.r2 == 0 && MEM_HU(8, pokemon) == 0x140, "retail flag-40 bypass without mutation");
    MEM_H(8, pokemon) = 0x100; MEM_W(0x6C, pokemon) = 0;
    snap::settings().widescreen = false; Pokemon_GetFlag100(rdram, &ctx);
    check(ctx.r2 == 1, "widescreen disabled restores original query");
    snap::settings().widescreen = true; snap::g_app_level_resident = false;
    Pokemon_GetFlag100(rdram, &ctx); check(ctx.r2 == 1, "non-course queries unchanged");
    std::printf("PASS: %d production display-bound and draw-eligibility checks\n", checks);
}
