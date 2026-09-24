#pragma once
#include "vr_interaction.h"
#include "vr_menu.h"
#include <atomic>
#include <mutex>
#include <chrono>
namespace RT64 {struct WorkloadQueue;struct GameFrame;struct RenderTarget;}
namespace snap::vr {
extern std::atomic<bool> requested;
extern bool preview;
extern std::atomic<unsigned> displayRate;
struct SharedState {
    std::mutex mutex;
    GameState game;
    std::deque<GameState> gameHistory;
    InteractionFrame interaction;
    Tracking tracking;
    NameCell namePointer{},nameClick{};
    MenuPoint menuPointer{},menuClick{};
    uint64_t menuClickSerial=0;
    std::deque<Throw> releases;
    bool fluteRequest=false;
    uint16_t buttons=0, pulses=0;
    uint16_t pulseHeld=0;
    std::chrono::steady_clock::time_point pulseUntil{};
    float stickX=0,stickY=0;
    uint64_t focusFrame=0;
    std::array<uint64_t,2> detectorFrames{};
    bool focusVisible=false, recenter=false;
    bool viewTurned=false;
};
SharedState& shared();
bool input(uint16_t* buttons,float* x,float* y);
void sceneChanged(bool course);
void render(RT64::WorkloadQueue&,RT64::GameFrame&,const RT64::GameFrame&,float,RT64::RenderTarget*);
void shutdown();
}
