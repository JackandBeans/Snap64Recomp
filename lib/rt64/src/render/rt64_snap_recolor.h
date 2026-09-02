//
// Pokemon Snap port: the opt-in Virtual Console Jynx recolour.
//
// What the cartridge does. Jynx's face and hands carry no texture. The game
// draws them with the RDP's primitive colour multiplied by the lit shade --
// gsDPSetCombineLERP(PRIMITIVE, 0, SHADE, 0, 0, 0, 0, PRIMITIVE, 0, 0, 0,
// COMBINED, 0, 0, 0, COMBINED) with gsSPTexture(..., G_OFF) -- and the
// primitive colours it sets for those runs of triangles are #050505 for the
// face (assets/cave/jynx/part0_draw_near.gfx.inc.c line 26 and its far and
// hd twins) and #505870 for the two hands (part14 and part17, near, far and
// hd). Neither colour occurs anywhere else in the decomp's display lists or
// sources as a literal (grep, 2026-09-02); the game's fourteen runtime
// gDPSetPrimColor sites take their colours from variables, which is why the
// rule below also demands Jynx's exact combiner with texture off before it
// touches anything. Jynx's ten textures (arm, torso, three eyes,
// three hair, lips, dress) contain no black texel, so nothing about this is
// a texture operation, and the texture cache is untouched by it.
//
// What the re-releases did. Nintendo's Wii Virtual Console (2007) showed the
// face purple; the Wii U release kept the hands as they were; the Switch
// Online release (2022) recoloured the hands too. The exact colour values
// those releases use are not publicly documented, so this recolour does not
// try to reproduce them. It takes the purple from Nintendo's official Jynx
// artwork instead, and it recolours the hands as well: the Switch Online
// look.
//
// What this does. When the setting is on, a draw call that is exactly one of
// those runs -- texture off, that combiner, that primitive colour, alpha
// 255 -- has its primitive colour replaced by the artwork purple on the copy
// of the call the renderer records. The shade the combiner multiplies it
// with is the game's own lighting, so every gradient the console gave the
// black face is kept; the RDP's own primitive colour is left as the
// cartridge set it, so later draws that inherit it are unaffected. Nothing
// is stored: it is arithmetic on the game's own draw call, every frame, and
// the setting takes effect on the next display list parsed -- there is no
// cache to invalidate.
//
// Why here and not in the texture path: see above. Why a header: RT64's
// CMakeLists names every source file, and the port keeps its edits to the
// fork small.
//

#pragma once

#include <cstdint>
#include <cstdio>

#include "hle/rt64_draw_call.h"
#include "hle/rt64_snap_diag.h"

namespace RT64 {
    namespace SnapJynxVC {
        struct RGB8 {
            uint8_t r;
            uint8_t g;
            uint8_t b;
        };

        // The cartridge's primitive colour for the face: gsDPSetPrimColor(0,
        // 0, 0x05, 0x05, 0x05, 0xFF) in the decomp's Jynx display lists.
        constexpr RGB8 CartridgeFace{ 5, 5, 5 };

        // The cartridge's primitive colour for the hands: gsDPSetPrimColor(0,
        // 0, 0x50, 0x58, 0x70, 0xFF). Not black but a dark slate; how dark it
        // reads under the Cave's lighting was not checked by the port.
        constexpr RGB8 CartridgeHands{ 80, 88, 112 };

        // The purple both become. This is the official-artwork purple, NOT the
        // Virtual Console texel value, which is not publicly available.
        // Source: Nintendo's official Jynx artwork as served by Bulbapedia,
        // https://archives.bulbagarden.net/media/upload/0/07/0124Jynx.png
        // (541x541), read in a browser on 2026-09-02 and sampled in place;
        // no copy of the image was made. Two 12x12 regions on the flat of
        // the left and right cheeks averaged (163,134,181) and (163,135,182)
        // with a per-channel standard deviation under 3; the value below is
        // the left cheek's, the darker of the two. The artwork's hands sit
        // in its cel shadow at (124,102,132); the recolour gives the hands
        // the face's purple rather than that shadow tone, because in the
        // game the shading comes from the lights, not from the colour.
        constexpr RGB8 ArtworkPurple{ 163, 134, 181 };

        // RDP::setPrimColor stores each byte as byte / 255.0f; this recovers
        // the byte exactly for every value it can have produced.
        inline uint8_t toByte(float v) {
            const float scaled = v * 255.0f + 0.5f;
            if (scaled <= 0.0f) {
                return 0;
            }
            if (scaled >= 255.0f) {
                return 255;
            }
            return static_cast<uint8_t>(scaled);
        }

        // The combiner Jynx's untextured parts use, both cycles, colour and
        // alpha, checked through the same decode the shader generator uses.
        inline bool isJynxCombiner(const interop::ColorCombiner &cc) {
            using CC = interop::ColorCombiner;
            const bool cycle0Color =
                (cc.decodeColorInput(0, false) == CC::C_PRIMITIVE) &&
                (cc.decodeColorInput(1, false) == CC::C_ZERO) &&
                (cc.decodeColorInput(2, false) == CC::C_SHADE) &&
                (cc.decodeColorInput(3, false) == CC::C_ZERO);
            const bool cycle0Alpha =
                (cc.decodeAlphaInput(0, false) == CC::A_ZERO) &&
                (cc.decodeAlphaInput(1, false) == CC::A_ZERO) &&
                (cc.decodeAlphaInput(2, false) == CC::A_ZERO) &&
                (cc.decodeAlphaInput(3, false) == CC::A_PRIMITIVE);
            const bool cycle1Color =
                (cc.decodeColorInput(0, true) == CC::C_ZERO) &&
                (cc.decodeColorInput(1, true) == CC::C_ZERO) &&
                (cc.decodeColorInput(2, true) == CC::C_ZERO) &&
                (cc.decodeColorInput(3, true) == CC::C_COMBINED);
            const bool cycle1Alpha =
                (cc.decodeAlphaInput(0, true) == CC::A_ZERO) &&
                (cc.decodeAlphaInput(1, true) == CC::A_ZERO) &&
                (cc.decodeAlphaInput(2, true) == CC::A_ZERO) &&
                (cc.decodeAlphaInput(3, true) == CC::A_COMBINED);
            return cycle0Color && cycle0Alpha && cycle1Color && cycle1Alpha;
        }

        // Which cartridge colour, if any, this draw call carries in the
        // configuration Jynx draws it with. Returns nullptr for every other
        // draw in the game.
        inline const RGB8 *cartridgeColor(const DrawCall &call) {
            if (call.textureOn != 0) {
                return nullptr;
            }
            if (!isJynxCombiner(call.colorCombiner)) {
                return nullptr;
            }
            const interop::float4 &prim = call.rdpParams.primColor;
            if (toByte(prim.w) != 255) {
                return nullptr;
            }
            const uint8_t r = toByte(prim.x);
            const uint8_t g = toByte(prim.y);
            const uint8_t b = toByte(prim.z);
            if ((r == CartridgeFace.r) && (g == CartridgeFace.g) && (b == CartridgeFace.b)) {
                return &CartridgeFace;
            }
            if ((r == CartridgeHands.r) && (g == CartridgeHands.g) && (b == CartridgeHands.b)) {
                return &CartridgeHands;
            }
            return nullptr;
        }

        // Replaces the primitive colour on a matching draw call and says
        // whether it did. Meant for the recorded copy of the call, not the
        // renderer's live one. Under SNAP_STATS the first face and the first
        // hands recolour each print one line, so a Cave run can confirm the
        // rule fired without a screenshot.
        inline bool apply(DrawCall &call) {
            const RGB8 *from = cartridgeColor(call);
            if (from == nullptr) {
                return false;
            }
            interop::float4 &prim = call.rdpParams.primColor;
            prim.x = ArtworkPurple.r / 255.0f;
            prim.y = ArtworkPurple.g / 255.0f;
            prim.z = ArtworkPurple.b / 255.0f;
            if (snapdiag::statsEnabled()) {
                static bool faceReported = false;
                static bool handsReported = false;
                bool &reported = (from == &CartridgeFace) ? faceReported : handsReported;
                if (!reported) {
                    reported = true;
                    std::printf("[SNAP-JYNX] %s prim #%02X%02X%02X -> #%02X%02X%02X (%u triangles)\n",
                        (from == &CartridgeFace) ? "face" : "hands",
                        from->r, from->g, from->b,
                        ArtworkPurple.r, ArtworkPurple.g, ArtworkPurple.b,
                        call.triangleCount);
                    std::fflush(stdout);
                }
            }
            return true;
        }
    };
};
