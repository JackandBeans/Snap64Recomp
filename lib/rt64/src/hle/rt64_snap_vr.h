//
// Pokemon Snap port: the headset.
//
// The port renders for a headset by replaying the game's own frame twice
// more, once per eye, with the ride camera's view and projection replaced by
// the eye's, and everything the game draws flat -- the film counter, the
// icons, the fade -- laid on a plane in front of the head. The game itself
// never learns about any of this: its camera follows the head through its
// own yaw and pitch (src/vr_game.cpp), its frame is what the photo will be,
// and that frame is what the viewfinder and the menus show.
//
// This header is the seam between the renderer and the port. The renderer
// asks the port, through the Interface below, for the headset's views and
// its images, and hands it the eye pictures to submit; the port owns the
// OpenXR session (src/vr_openxr.cpp). The renderer never includes an OpenXR
// header, so the flat build and the Linux build see none of it.
//
// Matrices here are in the layout the RSP loads them in, row vectors on
// the left (p' = p * M), the same layout hal_look_at_f and
// hal_perspective_fast_f produce in the game (src/sys/matrix.c) and the
// one RT64 decomposes the game's projection stack into.
#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "common/rt64_common.h"
#include "shared/rt64_rsp_viewport.h"
#include "plume_render_interface.h"

namespace RT64 {
    namespace SnapVR {
        // One eye's view for one presented image: the pose in the seated
        // space (metres, the headset's own axes: x right, y up, z backwards)
        // and the field of view (radians, signed the way OpenXR signs them:
        // left and down negative).
        struct EyeView {
            float pos[3] = { 0.0f, 0.0f, 0.0f };
            float quat[4] = { 0.0f, 0.0f, 0.0f, 1.0f };   // x, y, z, w
            float fovLeft = -0.785f;
            float fovRight = 0.785f;
            float fovUp = 0.785f;
            float fovDown = -0.785f;
        };

        struct Views {
            EyeView eye[2];
            int64_t displayTime = 0;    // the time the poses were predicted for
            bool valid = false;
        };

        // What the game says about its own camera, read by the port from the
        // game's memory each tick (src/vr_game.cpp).
        struct GameCamera {
            bool worldMode = false;     // the ride camera ran within the last ticks
            bool zoomed = false;        // the zoomed-in camera is the one running
            float eye[3] = { 0.0f, 0.0f, 0.0f };      // the ride camera's eye, as the game computed it
            float at[3] = { 0.0f, 0.0f, 1.0f };       // its look-at point
            float cartPos[3] = { 0.0f, 0.0f, 0.0f };  // gMovementState.pos
            float cartRot[3] = { 0.0f, 0.0f, 0.0f };  // gMovementState.rotation, applied x then y then z
            float unitsPerMetre = 80.0f;
            float hudDistanceMetres = 2.0f;
            float nearPlane = 10.0f;
            float farPlane = 25600.0f;
        };

        // The headset's timing as the present thread last learned it.
        struct Timing {
            int64_t predictedDisplayTime = 0;
            int64_t predictedDisplayPeriod = 0;
            uint32_t refreshRate = 0;
        };

        // The record of one presented sub-frame, written by the workload
        // thread before the sub-frame is declared available and read by the
        // present thread after: which views the eyes were drawn with, and
        // whether they were drawn at all.
        struct SubFrame {
            Views views;
            bool worldMode = false;
            bool zoomed = false;
            bool eyesRendered = false;
        };

        struct Interface {
            virtual ~Interface() { }
            // The session is running: render for it.
            virtual bool active() = 0;
            // The port's reading of the game's camera this tick.
            virtual bool gameCamera(GameCamera &out) = 0;
            // Both eyes' poses for an image to be shown at that time. Any thread.
            virtual bool locateViews(int64_t displayTime, Views &out) = 0;
            // The headset's timing as of the last frame wait. Any thread.
            virtual void latestTiming(Timing &out) = 0;
            // Present thread. frameWait blocks until the headset wants the
            // next image; a false return means no image is wanted now (the
            // session is not running), and the desktop path carries on alone.
            virtual bool frameWait(Timing &out) = 0;
            virtual bool frameBegin() = 0;
            // The eye's image for this frame, wrapped for the renderer. Its
            // size is eyeWidth by eyeHeight. Null when the swapchain could not
            // be acquired; the frame then ends without eyes.
            virtual plume::RenderTexture *acquireEyeImage(uint32_t eye) = 0;
            virtual void releaseEyeImage(uint32_t eye) = 0;
            // The flat picture's image, for the menu screen and the
            // viewfinder: a swapchain of exactly this size, made or remade.
            virtual plume::RenderTexture *acquireScreenImage(uint32_t width, uint32_t height) = 0;
            virtual void releaseScreenImage() = 0;
            // Submits the frame: the eyes as the world, and the flat picture
            // as a screen, world-locked ahead of the seat (the menus) or
            // head-locked as a window (the viewfinder while zoomed in).
            virtual void frameEnd(const Views &views, bool showEyes, bool showScreen, bool screenHeadLocked, float screenAspect) = 0;
            virtual uint32_t eyeWidth() = 0;
            virtual uint32_t eyeHeight() = 0;
            virtual bool traceEnabled() = 0;
        };

        inline std::atomic<Interface *> &instanceSlot() {
            static std::atomic<Interface *> slot{nullptr};
            return slot;
        }

        inline Interface *get() {
            return instanceSlot().load(std::memory_order_acquire);
        }

        // The virtual picture an eye is rendered as: 320 pixels wide like the
        // console's, as tall as the eye's aspect asks, so the renderer's
        // scale is the same on both axes.
        constexpr uint32_t VirtualWidth = 320;

        inline uint32_t virtualHeight(uint32_t eyeWidth, uint32_t eyeHeight) {
            if (eyeWidth == 0) {
                return 240;
            }
            const uint32_t h = uint32_t(std::lround(double(VirtualWidth) * double(eyeHeight) / double(eyeWidth)));
            return (h < 16) ? 16 : h;
        }

        // Rotates v by the unit quaternion q (x, y, z, w).
        inline void quatRotate(const float q[4], const float v[3], float out[3]) {
            const float qx = q[0], qy = q[1], qz = q[2], qw = q[3];
            const float tx = 2.0f * (qy * v[2] - qz * v[1]);
            const float ty = 2.0f * (qz * v[0] - qx * v[2]);
            const float tz = 2.0f * (qx * v[1] - qy * v[0]);
            out[0] = v[0] + qw * tx + (qy * tz - qz * ty);
            out[1] = v[1] + qw * ty + (qz * tx - qx * tz);
            out[2] = v[2] + qw * tz + (qx * ty - qy * tx);
        }

        // The look-at matrix exactly as hal_look_at_f builds it: right, up
        // and the backward-pointing look vector down the columns, the eye's
        // projections along the bottom row.
        inline hlslpp::float4x4 lookAt(const hlslpp::float3 &eye, const hlslpp::float3 &at, const hlslpp::float3 &upHint) {
            hlslpp::float3 look = eye - at;
            const float lookLen = float(hlslpp::length(look));
            if (lookLen > 1e-9f) {
                look = look / lookLen;
            }
            else {
                look = hlslpp::float3(0.0f, 0.0f, 1.0f);
            }

            hlslpp::float3 right = hlslpp::cross(upHint, look);
            const float rightLen = float(hlslpp::length(right));
            if (rightLen > 1e-9f) {
                right = right / rightLen;
            }
            else {
                right = hlslpp::float3(1.0f, 0.0f, 0.0f);
            }

            hlslpp::float3 up = hlslpp::normalize(hlslpp::cross(look, right));
            hlslpp::float4x4 m = hlslpp::float4x4::identity();
            m[0][0] = right.x; m[1][0] = right.y; m[2][0] = right.z; m[3][0] = -float(hlslpp::dot(eye, right));
            m[0][1] = up.x;    m[1][1] = up.y;    m[2][1] = up.z;    m[3][1] = -float(hlslpp::dot(eye, up));
            m[0][2] = look.x;  m[1][2] = look.y;  m[2][2] = look.z;  m[3][2] = -float(hlslpp::dot(eye, look));
            m[0][3] = 0.0f;    m[1][3] = 0.0f;    m[2][3] = 0.0f;    m[3][3] = 1.0f;
            return m;
        }

        // The perspective matrix in hal_perspective_fast_f's layout, with an
        // off-centre frustum: the headset's lenses are not centred on the
        // eyes, so the runtime hands each eye four half-angles rather than
        // one field of view. Scale is 1: the renderer decomposes the game's
        // projection stack to that convention too.
        inline hlslpp::float4x4 perspectiveAsymmetric(float fovLeft, float fovRight, float fovUp, float fovDown, float nearPlane, float farPlane) {
            const float tanL = std::tan(-fovLeft);
            const float tanR = std::tan(fovRight);
            const float tanU = std::tan(fovUp);
            const float tanD = std::tan(-fovDown);
            hlslpp::float4x4 m = hlslpp::float4x4(0.0f);
            m[0][0] = 2.0f / (tanL + tanR);
            m[1][1] = 2.0f / (tanU + tanD);
            m[2][0] = (tanR - tanL) / (tanR + tanL);
            m[2][1] = (tanU - tanD) / (tanU + tanD);
            m[2][2] = (nearPlane + farPlane) / (nearPlane - farPlane);
            m[2][3] = -1.0f;
            m[3][2] = (2.0f * nearPlane * farPlane) / (nearPlane - farPlane);
            m[3][3] = 0.0f;
            return m;
        }

        // The cart's rotation as the game applies it to its look vector:
        // Vec3fGetEulerRotation about x, then y, then z (src/sys/vector.c).
        inline hlslpp::float3 cartRotate(const hlslpp::float3 &v, const float rot[3]) {
            float x = v.x, y = v.y, z = v.z;
            {
                const float s = std::sin(rot[0]), c = std::cos(rot[0]);
                const float ny = y * c - z * s;
                const float nz = y * s + z * c;
                y = ny; z = nz;
            }
            {
                const float s = std::sin(rot[1]), c = std::cos(rot[1]);
                const float nx = x * c + z * s;
                const float nz = z * c - x * s;
                x = nx; z = nz;
            }
            {
                const float s = std::sin(rot[2]), c = std::cos(rot[2]);
                const float nx = x * c - y * s;
                const float ny = x * s + y * c;
                x = nx; y = ny;
            }
            return hlslpp::float3(x, y, z);
        }

        // The headset's axes to the cart's: the headset looks down -z with x
        // to the right, the cart looks down +z with x to the left. A half turn
        // about the vertical maps one onto the other.
        inline hlslpp::float3 headsetToCart(const float v[3]) {
            return hlslpp::float3(-v[0], v[1], -v[2]);
        }

        // One eye's frame in the game's world, and the placement of the flat
        // plane in front of it.
        struct EyeFrame {
            hlslpp::float3 position;
            hlslpp::float3 forward;
            hlslpp::float3 up;
            hlslpp::float4x4 view;
            hlslpp::float4x4 proj;
            hlslpp::float4x4 viewProj;
            // The game's picture, in its own clip space, lands on the plane
            // through x' = x * planeSx + planeTx, y' = y * planeSy + planeTy
            // (both in the eye's normalized device coordinates), at depth
            // planeZ.
            float planeSx = 1.0f;
            float planeSy = 1.0f;
            float planeTx = 0.0f;
            float planeTy = 0.0f;
            float planeZ = 0.0f;
        };

        // The game's own vertical field of view, zoomed out (player.c: 55
        // degrees): the plane in front of the head subtends it, so the flat
        // picture sits at the size the game drew it at.
        constexpr float GameFovYDegrees = 55.0f;

        inline void computeEyeFrames(const GameCamera &cam, const Views &views, EyeFrame out[2]) {
            const float units = (cam.unitsPerMetre > 1.0f) ? cam.unitsPerMetre : 1.0f;
            const hlslpp::float3 cartPos(cam.cartPos[0], cam.cartPos[1], cam.cartPos[2]);
            // The ride camera's eye is the cart's point plus a hundred units
            // straight up (updateCameraZoomedOut, updateCameraZoomedIn); the
            // shake and the vibration the game adds are left out, since a
            // shaken head is a sick head.
            const hlslpp::float3 base = cartPos + hlslpp::float3(0.0f, 100.0f, 0.0f);
            float headPos[3] = {
                (views.eye[0].pos[0] + views.eye[1].pos[0]) * 0.5f,
                (views.eye[0].pos[1] + views.eye[1].pos[1]) * 0.5f,
                (views.eye[0].pos[2] + views.eye[1].pos[2]) * 0.5f
            };
            const float halfFov = GameFovYDegrees * 0.5f * 3.14159265f / 180.0f;
            for (uint32_t e = 0; e < 2; e++) {
                const EyeView &ev = views.eye[e];
                static const float fwdAxis[3] = { 0.0f, 0.0f, -1.0f };
                static const float upAxis[3] = { 0.0f, 1.0f, 0.0f };
                static const float rightAxis[3] = { 1.0f, 0.0f, 0.0f };
                float f[3], u[3], r[3];
                quatRotate(ev.quat, fwdAxis, f);
                quatRotate(ev.quat, upAxis, u);
                quatRotate(ev.quat, rightAxis, r);
                const float scaledPos[3] = { ev.pos[0] * units, ev.pos[1] * units, ev.pos[2] * units };
                EyeFrame &frame = out[e];
                frame.position = base + cartRotate(headsetToCart(scaledPos), cam.cartRot);
                frame.forward = hlslpp::normalize(cartRotate(headsetToCart(f), cam.cartRot));
                frame.up = hlslpp::normalize(cartRotate(headsetToCart(u), cam.cartRot));
                frame.view = lookAt(frame.position, frame.position + frame.forward, frame.up);
                frame.proj = perspectiveAsymmetric(ev.fovLeft, ev.fovRight, ev.fovUp, ev.fovDown, cam.nearPlane, cam.farPlane);
                frame.viewProj = hlslpp::mul(frame.view, frame.proj);

                // The plane: centred on the head, hudDistance ahead of it,
                // 4:3 and as tall as the game's field of view at that
                // distance. Seen from this eye it is shifted by the eye's
                // offset from the head, which is what makes it stereo.
                const float D = cam.hudDistanceMetres * units;
                const float halfHeight = D * std::tan(halfFov);
                const float halfWidth = halfHeight * (4.0f / 3.0f);
                const float off[3] = { headPos[0] - ev.pos[0], headPos[1] - ev.pos[1], headPos[2] - ev.pos[2] };
                const float ox = (off[0] * r[0] + off[1] * r[1] + off[2] * r[2]) * units;
                const float oy = (off[0] * u[0] + off[1] * u[1] + off[2] * u[2]) * units;
                const float P00 = float(frame.proj[0][0]);
                const float P11 = float(frame.proj[1][1]);
                const float P20 = float(frame.proj[2][0]);
                const float P21 = float(frame.proj[2][1]);
                const float P22 = float(frame.proj[2][2]);
                const float P32 = float(frame.proj[3][2]);
                frame.planeSx = halfWidth * P00 / D;
                frame.planeTx = ox * P00 / D - P20;
                frame.planeSy = halfHeight * P11 / D;
                frame.planeTy = oy * P11 / D - P21;
                frame.planeZ = (-D * P22 + P32) / D;
            }
        }

        // The clip-space matrix that carries a flat projection's picture onto
        // the plane: the projection's own viewport turns clip space into the
        // console's pixels, the pixels turn into the picture's normalized
        // coordinates, and those land on the plane. Affine in (x, y, w), so a
        // perspective projection that is not the ride's (the fade's camera)
        // maps the same way.
        inline hlslpp::float4x4 planeMatrix(const EyeFrame &frame, const interop::RSPViewport &viewport, float frameWidth, float frameHeight) {
            const float halfW = frameWidth * 0.5f;
            const float halfH = frameHeight * 0.5f;
            const float vsx = float(viewport.scale.x), vtx = float(viewport.translate.x);
            const float vsy = float(viewport.scale.y), vty = float(viewport.translate.y);
            // x_frame = x * vsx / halfW + w * (vtx / halfW - 1)
            // y_frame = y * vsy / halfH + w * (1 - vty / halfH)
            const float ax = vsx / halfW;
            const float bx = vtx / halfW - 1.0f;
            const float ay = vsy / halfH;
            const float by = 1.0f - vty / halfH;
            hlslpp::float4x4 m = hlslpp::float4x4(0.0f);
            m[0][0] = frame.planeSx * ax;
            m[3][0] = frame.planeSx * bx + frame.planeTx;
            m[1][1] = frame.planeSy * ay;
            m[3][1] = frame.planeSy * by + frame.planeTy;
            m[2][2] = 1e-4f;
            m[3][2] = frame.planeZ;
            m[3][3] = 1.0f;
            return m;
        }

        // The eye's viewport: the whole virtual picture, the depth range the
        // game's own viewport had.
        inline interop::RSPViewport eyeViewport(const interop::RSPViewport &original, uint32_t virtualWidth, uint32_t virtualHeight) {
            interop::RSPViewport vp = original;
            vp.scale.x = float(virtualWidth) * 0.5f;
            vp.scale.y = float(virtualHeight) * 0.5f;
            vp.translate.x = float(virtualWidth) * 0.5f;
            vp.translate.y = float(virtualHeight) * 0.5f;
            return vp;
        }

        // The eye point a view matrix was built from: the rotation's columns
        // normalized, the bottom row's projections undone.
        inline hlslpp::float3 viewEye(const hlslpp::float4x4 &view) {
            hlslpp::float3 right = hlslpp::float3(float(view[0][0]), float(view[1][0]), float(view[2][0]));
            hlslpp::float3 up = hlslpp::float3(float(view[0][1]), float(view[1][1]), float(view[2][1]));
            hlslpp::float3 look = hlslpp::float3(float(view[0][2]), float(view[1][2]), float(view[2][2]));
            const float rl = float(hlslpp::length(right)), ul = float(hlslpp::length(up)), ll = float(hlslpp::length(look));
            if ((rl < 1e-6f) || (ul < 1e-6f) || (ll < 1e-6f)) {
                return hlslpp::float3(0.0f, 0.0f, 0.0f);
            }
            const hlslpp::float3 t = hlslpp::float3(float(view[3][0]) / rl, float(view[3][1]) / ul, float(view[3][2]) / ll);
            right = right / rl; up = up / ul; look = look / ll;
            return -(right * t.x + up * t.y + look * t.z);
        }

        // Whether a perspective projection is the ride camera's: its view's
        // eye point sits where the game put its camera and it looks the way
        // the game looked. Compared in the game's units, generously; the
        // other perspective cameras of a course (the fade's, the icons') sit
        // hundreds of units elsewhere.
        inline bool isRideView(const hlslpp::float4x4 &view, const GameCamera &cam) {
            // The rotation part, columns normalized in case the game's
            // projection scale was folded into the look column.
            hlslpp::float3 right = hlslpp::float3(float(view[0][0]), float(view[1][0]), float(view[2][0]));
            hlslpp::float3 up = hlslpp::float3(float(view[0][1]), float(view[1][1]), float(view[2][1]));
            hlslpp::float3 look = hlslpp::float3(float(view[0][2]), float(view[1][2]), float(view[2][2]));
            const float rl = float(hlslpp::length(right)), ul = float(hlslpp::length(up)), ll = float(hlslpp::length(look));
            if ((rl < 1e-6f) || (ul < 1e-6f) || (ll < 1e-6f)) {
                return false;
            }
            const hlslpp::float3 t = hlslpp::float3(float(view[3][0]) / rl, float(view[3][1]) / ul, float(view[3][2]) / ll);
            right = right / rl; up = up / ul; look = look / ll;
            // eye = -(t * R^T): t holds -(eye . axis) per axis.
            const hlslpp::float3 eye = -(right * t.x + up * t.y + look * t.z);
            const hlslpp::float3 gameEye(cam.eye[0], cam.eye[1], cam.eye[2]);
            const hlslpp::float3 gameAt(cam.at[0], cam.at[1], cam.at[2]);
            if (float(hlslpp::length(eye - gameEye)) > 8.0f) {
                return false;
            }
            hlslpp::float3 gameFwd = gameAt - gameEye;
            const float fl = float(hlslpp::length(gameFwd));
            if (fl < 1e-6f) {
                return true;
            }
            gameFwd = gameFwd / fl;
            return float(hlslpp::dot(-look, gameFwd)) > 0.99f;
        }
    };
};
