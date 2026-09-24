#pragma once
#include "vr_math.h"
#include "vr_throw.h"
#include <cstdint>
#include <deque>
#include <vector>
#include <string>

namespace snap::vr {
enum class Held : uint8_t { None, Camera, Apple, PesterBall };
struct Settings {
    bool leftHanded=false;
    float eyeHeight=1.2f, renderScale=1.0f, throwStrength=1.0f, unitsPerMeter=100.0f;
};
struct HandInput {
    Pose grip{}, aim{};
    bool tracked=false;
    float squeeze=0, trigger=0, stickX=0, stickY=0;
    bool primary=false, secondary=false, menu=false, thumbTouch=false, stickClick=false;
};
struct Tracking {
    uint64_t frame=0;
    double seconds=0;
    bool focused=false, headValid=false;
    Pose head{};
    std::array<Pose,2> eyes{};
    std::array<Fov,2> fovs{};
    std::array<HandInput,2> hands{};
};
struct SmokePuff { Vec3 position; uint64_t born=0; };
struct GameState {
    uint64_t epoch=0, frame=0;
    bool course=false, paused=false, apples=false, pesterBalls=false, itemReady=true;
    bool cinematic=false, fluteUnlocked=false;
    float fluteSeconds=0;
    std::vector<SmokePuff> smoke;
    Vec3 cartPosition{}, cartVelocity{}; // game units; velocity per second
    float cartYaw=0;
    int film=60;
    std::string message;
    bool messageContinue=false;
};
// Match the world's interpolation interval, including block-origin rebases.
inline GameState interpolateCart(const GameState& previous,GameState current,float weight,Vec3 originDelta={}) {
    if(previous.epoch!=current.epoch||!previous.course||!current.course||current.paused)return current;
    weight=std::clamp(weight,0.f,1.f);
    Vec3 from=previous.cartPosition+originDelta;
    current.cartPosition=from+(current.cartPosition-from)*weight;
    current.cartYaw=previous.cartYaw+std::remainder(current.cartYaw-previous.cartYaw,2*pi)*weight;
    return current;
}
struct Throw { Held item; Vec3 position, velocity; uint64_t epoch; };
struct InteractionFrame {
    uint64_t frame=0, epoch=0;
    Pose head{}, lens{}, lensInCart{};
    std::array<Pose,2> hands{};
    std::array<Held,2> held{};
    std::vector<Throw> throws;
    bool shutter=false, dash=false, flute=false, advance=false, pause=false, cameraHeld=false;
    float fovY=45;
};
class Interaction {
public:
    Settings settings;
    static constexpr Vec3 fluteButton{-0.24f,0.82f,-0.48f};
    static constexpr Vec3 cameraDock{0.28f,0.92f,-0.38f};
    static constexpr Vec3 appleBin{0.36f,0.74f,0.02f};
    static constexpr Vec3 pesterBin{0.36f,0.74f,-0.23f};
    void recenter(const Tracking& tracking);
    InteractionFrame update(const Tracking& tracking, const GameState& game);
    Pose toWorld(Pose trackingPose,const GameState& game) const;
    Pose cartPose(const GameState& game) const;
    Pose localPose(Pose trackingPose) const;
private:
    std::array<std::deque<ThrowSample>,2> history;
    std::array<Held,2> held{};
    std::array<bool,2> gripping{}, triggering{}, primary{}, menus{}, armed{}, fluteArmed{};
    Pose origin{};
    bool centered=false, wasFocused=false;
    uint64_t epoch=0;
    double lastTime=0, nextThrow=0;
    float fovY=45;
    Quat lastLens{};
    bool hadCamera=false;
};
}
