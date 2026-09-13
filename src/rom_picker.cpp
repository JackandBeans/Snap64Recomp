/**
 * @file rom_picker.cpp
 * @brief See rom_picker.h.
 */
#include "rom_picker.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <SDL2/SDL.h>
#include <nfd.h>

#define XXH_INLINE_ALL
#include "xxHash/xxh3.h"

#include "paths.h"

namespace snap {
namespace {

constexpr const char* kRomName = "pokemonsnap.z64";
constexpr const char* kTitle = "Snap64 Recomp";

enum class Order { Big, Swapped2, Swapped4, NotRom };

// The first four bytes of an N64 ROM in each byte order (librecomp's rule).
Order detect(const std::vector<uint8_t>& d) {
    static const uint8_t big[4] = { 0x80, 0x37, 0x12, 0x40 };
    if (d.size() < 4) {
        return Order::NotRom;
    }
    auto is = [&](int a, int b, int c, int e) {
        return d[0] == big[a] && d[1] == big[b] && d[2] == big[c] && d[3] == big[e];
    };
    if (is(0, 1, 2, 3)) return Order::Big;
    if (is(3, 2, 1, 0)) return Order::Swapped4;
    if (is(1, 0, 3, 2)) return Order::Swapped2;
    return Order::NotRom;
}

void to_big_endian(std::vector<uint8_t>& d, Order o) {
    const size_t x = (o == Order::Swapped2) ? 1 : (o == Order::Swapped4) ? 3 : 0;
    if (x == 0) {
        return;
    }
    for (size_t i = 0; i + 3 < d.size(); i += 4) {
        uint8_t t[4] = { d[i], d[i + 1], d[i + 2], d[i + 3] };
        for (size_t k = 0; k < 4; k++) {
            d[i + (k ^ x)] = t[k];
        }
    }
}

const char* order_name(Order o) {
    switch (o) {
        case Order::Big:      return "big-endian";
        case Order::Swapped2: return "byte-swapped, .v64 order";
        case Order::Swapped4: return "little-endian, .n64 order";
        default:              return "not a ROM";
    }
}

std::string utf8(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return std::string(reinterpret_cast<const char*>(s.c_str()));
}

std::string hex64(uint64_t v) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%016llX", static_cast<unsigned long long>(v));
    return buf;
}

// A two-button question. True for the first button. With no display to show
// it on, the question is logged and the first button assumed, so a headless
// run never blocks.
bool ask(const std::string& text, const char* first, const char* second) {
    const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, first },
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, second },
    };
    const SDL_MessageBoxData box = { SDL_MESSAGEBOX_INFORMATION, nullptr, kTitle, text.c_str(),
                                     SDL_arraysize(buttons), buttons, nullptr };
    int choice = 1;
    if (SDL_ShowMessageBox(&box, &choice) != 0) {
        fprintf(stderr, "[SNAP] ROM: no dialog could be shown (%s); going on as if \"%s\"\n", SDL_GetError(), first);
        return true;
    }
    return choice == 1;
}

void tell(const std::string& text) {
    if (SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kTitle, text.c_str(), nullptr) != 0) {
        fprintf(stderr, "[SNAP] ROM: no dialog could be shown (%s)\n", SDL_GetError());
    }
}

struct Examined {
    bool ok = false;
    std::string problem;
    std::vector<uint8_t> image;   // big-endian
    Order order = Order::NotRom;
};

Examined examine(const std::filesystem::path& file, uint64_t expected) {
    Examined e;
    std::ifstream in(file, std::ios::binary);
    if (!in.good()) {
        e.problem = "That file could not be read.";
        return e;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size <= 0 || size > (64ll << 20)) {
        e.problem = "That file is not an N64 ROM: it is empty or far larger than any cartridge.";
        return e;
    }
    e.image.resize(static_cast<size_t>((size + 3) & ~3ll));
    in.read(reinterpret_cast<char*>(e.image.data()), size);
    if (!in.good() && !in.eof()) {
        e.problem = "That file could not be read.";
        return e;
    }
    e.order = detect(e.image);
    if (e.order == Order::NotRom) {
        e.problem = "That file is not an N64 ROM: its first bytes are not a ROM header's.";
        return e;
    }
    to_big_endian(e.image, e.order);
    const uint64_t hash = XXH3_64bits(e.image.data(), e.image.size());
    if (hash != expected) {
        e.problem = "That file is an N64 ROM, but not the US Pokemon Snap this port was made from.\n"
                    "Expected hash: " + hex64(expected) + "\nThis file's hash: " + hex64(hash) +
                    " (XXH3-64 of the image in big-endian order)";
        return e;
    }
    e.ok = true;
    return e;
}

// The image written beside its final name, then renamed, so a half-written
// file is never mistaken for the ROM on the next start.
bool put(const std::vector<uint8_t>& image, const std::filesystem::path& dest, std::string& why) {
    const std::filesystem::path part = dest.string() + ".part";
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        if (!out.good()) {
            why = "the folder could not be written";
            return false;
        }
        out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
        if (!out.good()) {
            why = "the file could not be written in full";
            out.close();
            std::error_code ec;
            std::filesystem::remove(part, ec);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(part, dest, ec);
    if (ec) {
        why = ec.message();
        std::filesystem::remove(part, ec);
        return false;
    }
    return true;
}

} // namespace

bool ensure_rom(uint64_t expected_hash) {
    const std::filesystem::path dest = base_path(kRomName);
    std::error_code ec;
    if (std::filesystem::exists(dest, ec)) {
        return true;
    }
    const char* hook = std::getenv("SNAP_ROM_PICK");
    printf("[SNAP] ROM: none at %s; asking for the file%s\n", utf8(dest).c_str(), hook ? " (SNAP_ROM_PICK answers)" : "");
    fflush(stdout);

    if (!hook) {
        if (!ask("Snap64 Recomp needs your own dump of Pokemon Snap (US). No game data is included.\n\n"
                 "Choose the file (.z64, .v64 or .n64). It is copied into the port's folder as " + std::string(kRomName) +
                 " and you are not asked again.", "Choose the file", "Quit")) {
            printf("[SNAP] ROM: the player chose Quit; the game cannot start without the file\n");
            return false;
        }
        if (NFD_Init() != NFD_OKAY) {
            const char* err = NFD_GetError();
            printf("[SNAP] ROM: the file chooser could not open (%s)\n", err ? err : "no reason given");
            tell("The file chooser could not be opened. Put your dump beside the program yourself, named " +
                 std::string(kRomName) + ".");
            return false;
        }
    }

    bool done = false;
    while (!done) {
        std::filesystem::path chosen;
        if (hook) {
            if (strcmp(hook, "cancel") == 0) {
                printf("[SNAP] ROM: the chooser was cancelled; the game cannot start without the file\n");
                return false;
            }
            chosen = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(hook)));
        } else {
            nfdu8char_t* out = nullptr;
            const nfdu8filteritem_t filters[] = { { "N64 ROM", "z64,v64,n64" } };
            const nfdresult_t r = NFD_OpenDialogU8(&out, filters, 1, nullptr);
            if (r == NFD_CANCEL) {
                printf("[SNAP] ROM: the chooser was cancelled; the game cannot start without the file\n");
                tell("The game cannot run without the file. Start it again when you have it.");
                NFD_Quit();
                return false;
            }
            if (r != NFD_OKAY || out == nullptr) {
                const char* err = NFD_GetError();
                printf("[SNAP] ROM: the file chooser failed (%s)\n", err ? err : "no reason given");
                tell("The file chooser failed. Put your dump beside the program yourself, named " +
                     std::string(kRomName) + ".");
                NFD_Quit();
                return false;
            }
            chosen = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(out)));
            NFD_FreePathU8(out);
        }

        Examined e = examine(chosen, expected_hash);
        if (!e.ok) {
            printf("[SNAP] ROM: %s refused: %s\n", utf8(chosen).c_str(), e.problem.c_str());
            if (hook) {
                return false;
            }
            if (!ask(e.problem + "\n\nChoose another file?", "Choose again", "Quit")) {
                NFD_Quit();
                return false;
            }
            continue;
        }

        std::string why;
        if (!put(e.image, dest, why)) {
            printf("[SNAP] ROM: %s could not be copied to %s: %s\n", utf8(chosen).c_str(), utf8(dest).c_str(), why.c_str());
            if (!hook) {
                tell("The file could not be copied to " + utf8(dest) + ": " + why + ".\n\nPut it there yourself, named " +
                     std::string(kRomName) + ", or start with SNAP_DATA_DIR set to a folder that can be written.");
                NFD_Quit();
            }
            return false;
        }
        printf("[SNAP] ROM: %s (%s) copied to %s\n", utf8(chosen).c_str(), order_name(e.order), utf8(dest).c_str());
        done = true;
    }
    if (!hook) {
        NFD_Quit();
    }
    fflush(stdout);
    return true;
}

} // namespace snap
