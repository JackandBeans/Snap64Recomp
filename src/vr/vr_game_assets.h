#pragma once
#include <array>
#include <cstdint>

namespace snap::vr {
struct FluteIcon {
    static constexpr unsigned side=28;
    std::array<uint8_t,side*side*4> rgba{};
};

// USA app_level Icons_IconDefs[2]: the original RGBA32 HUD sprite.
// Decode the player's loaded overlay; no ROM pixels belong in the repository.
inline bool harvestFluteIcon(const uint8_t* rdram,FluteIcon& icon) {
    constexpr uint32_t sprite=0x803880E8;
    auto valid=[](uint32_t address,uint32_t count) {
        return address>=0x80000000u&&address<0x80800000u&&count<=0x80800000u-address;
    };
    auto byte=[&](uint32_t address){return rdram[(address-0x80000000u)^3u];};
    auto half=[&](uint32_t address){return uint32_t(byte(address))*256+byte(address+1);};
    auto word=[&](uint32_t address){return (half(address)<<16)|half(address+2);};
    if(!rdram||half(sprite+4)!=FluteIcon::side||half(sprite+6)!=FluteIcon::side||
       byte(sprite+0x30)!=0||byte(sprite+0x31)!=3||half(sprite+0x28)!=1)return false;
    const uint32_t bitmap=word(sprite+0x34);
    if(!valid(bitmap,16)||half(bitmap)!=FluteIcon::side||half(bitmap+2)!=FluteIcon::side||
       half(bitmap+4)!=0||half(bitmap+6)!=0||half(bitmap+12)!=FluteIcon::side)return false;
    const uint32_t pixels=word(bitmap+8);
    if(!valid(pixels,uint32_t(icon.rgba.size())))return false;
    const bool shuffled=(half(sprite+0x14)&0x200)!=0;
    for(unsigned y=0;y<FluteIcon::side;y++)for(unsigned x=0;x<FluteIcon::side;x++) {
        // RGBA32 LoadBlockS swaps 64-bit halves of each 128-bit odd-row group.
        const unsigned sourceX=shuffled&&(y&1)?x^2u:x;
        for(unsigned channel=0;channel<4;channel++)
            icon.rgba[(y*FluteIcon::side+x)*4+channel]=byte(pixels+(y*FluteIcon::side+sourceX)*4+channel);
    }
    return true;
}
}
