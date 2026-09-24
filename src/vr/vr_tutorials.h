#pragma once
#include <array>
#include <cstdint>
namespace snap::vr {
struct TutorialReplacement { uint32_t tableOffset; int page; const char* text; };
// USA overlay 114. Identify pages numerically, never by original dialogue bytes.
inline constexpr std::array<TutorialReplacement,5> tutorialReplacements={{
    {0xB4558,2,"Grip bait from the rear\nright dispenser. Swing your\nhand; release grip to throw."},
    {0xB4558,4,"For more distance, swing\nfaster and release upward.\nTry this next:"},
    {0xB4578,1,"Grip a Pester Ball from the\nfront right dispenser. Swing\nand release grip to throw."},
    {0xB4588,2,"Press the round flute icon\non the ZERO-ONE to play\nmusic for 10 seconds."},
    {0xB4588,3,"Press the ZERO-ONE button\nagain to change the tune\nand restart the 10 seconds."}
}};
}
