/**
 * @file mod_api.cpp
 * @brief The functions a mod imports from the port itself.
 *
 * A mod built with N64Recomp's mod tool imports its helpers by name: most of
 * them the runtime provides (allocation, the configuration API, the hook
 * return values), and the loader refuses a mod whose imports it cannot
 * find. recomp_printf is the port's to provide. The other recompilations
 * write it as game-side code that their patch toolchain exports; this
 * port's patches are the decompilation's own functions compiled with IDO,
 * which cannot export, so recomp_printf is a host function that formats
 * the guest's printf call itself, reading the arguments the way the MIPS
 * o32 convention lays them out for a variadic call: the first four words in
 * a0 to a3, the rest on the caller's stack, a double or a 64-bit integer in
 * an even-aligned pair of slots. The text goes to the port's log. The
 * collections a mod keeps between calls (recompdata.h) are mod_data_api.cpp.
 *
 * snap64_set_status (snap64.h) is the port's own: a mod gives its id and a
 * line, and the Mods page shows the line on the mod's details page after
 * the description, with any %T in it filled with the clock a mod reads
 * (mod_time_api.cpp) while the page is up. An empty line clears it.
 */
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#include "recomp.h"
#include "librecomp/overlays.hpp"

#include "mod_api.h"
#include "settings.h"

namespace {

// The variadic arguments of a guest call, after the format string.
struct GuestArgs {
    uint8_t* rdram;
    recomp_context* ctx;
    int slot = 1; // slot 0 is the format string

    uint32_t word() {
        uint32_t value;
        if (slot < 4) {
            value = static_cast<uint32_t>((&ctx->r4)[slot]);
        }
        else {
            value = static_cast<uint32_t>(MEM_W(slot * 4, ctx->r29));
        }
        slot++;
        return value;
    }

    uint64_t dword() {
        if (slot & 1) {
            slot++; // an eight-byte argument starts on an even slot
        }
        const uint64_t high = word();
        const uint64_t low = word();
        return (high << 32) | low; // big-endian: the first slot is the high word
    }

    double real() {
        const uint64_t bits = dword();
        double value;
        std::memcpy(&value, &bits, sizeof value);
        return value;
    }
};

std::string guest_string(uint8_t* rdram, uint32_t address) {
    std::string out;
    if (address == 0) {
        return "(null)";
    }
    for (uint32_t i = 0; i < 4096; i++) {
        const char c = static_cast<char>(MEM_B(i, static_cast<gpr>(static_cast<int32_t>(address))));
        if (c == '\0') {
            break;
        }
        out += c;
    }
    return out;
}

// Formats one conversion with the host's printf, from a specification copied
// out of the guest's format string (flags, width, precision, the conversion).
template <typename T>
void emit(std::string& out, const std::string& spec, T value) {
    char buffer[512];
    std::snprintf(buffer, sizeof buffer, spec.c_str(), value);
    out += buffer;
}

extern "C" void recomp_printf(uint8_t* rdram, recomp_context* ctx) {
    GuestArgs args{ rdram, ctx };
    const std::string format = guest_string(rdram, static_cast<uint32_t>(ctx->r4));
    std::string out;

    for (size_t i = 0; i < format.size(); i++) {
        const char c = format[i];
        if (c != '%') {
            out += c;
            continue;
        }
        // Collect the specification up to the conversion character.
        size_t j = i + 1;
        while (j < format.size() && std::strchr("-+ #0123456789.", format[j]) != nullptr) {
            j++;
        }
        bool is_long_long = false;
        bool is_long = false;
        while (j < format.size() && (format[j] == 'l' || format[j] == 'h' || format[j] == 'z')) {
            if (format[j] == 'l') {
                if (is_long) is_long_long = true;
                is_long = true;
            }
            j++;
        }
        if (j >= format.size()) {
            out += format.substr(i);
            break;
        }
        const char conv = format[j];
        std::string spec = "%" + format.substr(i + 1, j - (i + 1));
        // Strip the guest's length modifiers; the host's are added per type.
        spec.erase(std::remove_if(spec.begin(), spec.end(), [](char ch) { return ch == 'l' || ch == 'h' || ch == 'z'; }), spec.end());
        i = j;

        switch (conv) {
            case '%':
                out += '%';
                break;
            case 'd':
            case 'i':
                if (is_long_long) emit(out, spec + "lld", static_cast<long long>(args.dword()));
                else emit(out, spec + "d", static_cast<int32_t>(args.word()));
                break;
            case 'u':
            case 'x':
            case 'X':
            case 'o':
                if (is_long_long) emit(out, spec + "ll" + conv, static_cast<unsigned long long>(args.dword()));
                else emit(out, spec + conv, args.word());
                break;
            case 'c':
                emit(out, spec + "c", static_cast<int>(args.word()));
                break;
            case 's':
                emit(out, spec + "s", guest_string(rdram, args.word()).c_str());
                break;
            case 'p':
                emit(out, spec + "08X", args.word());
                break;
            case 'f':
            case 'F':
            case 'e':
            case 'E':
            case 'g':
            case 'G':
                emit(out, spec + conv, args.real());
                break;
            default:
                // Unknown conversion: reproduce it and take one word, as a
                // libc would have.
                out += spec + conv;
                args.word();
                break;
        }
    }

    std::fputs(out.c_str(), stdout);
    std::fflush(stdout);
    ctx->r2 = static_cast<int32_t>(out.size());
}

// The line each mod shows on its details page, by mod id. The game thread
// writes it; the page reads it as it composes (menu_assets.cpp).
std::mutex g_status_mutex;
std::unordered_map<std::string, std::string> g_status_lines;

// void snap64_set_status(const char* mod_id, const char* line)
void snap64_set_status(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t idAddr = static_cast<uint32_t>(ctx->r4);
    const uint32_t lineAddr = static_cast<uint32_t>(ctx->r5);
    if (idAddr == 0) {
        return;
    }
    const std::string id = guest_string(rdram, idAddr);
    std::string line = (lineAddr == 0) ? std::string() : guest_string(rdram, lineAddr);
    if (line.size() > 240) {
        line.resize(240);     // three lines of the page, more than any line needs
    }
    std::lock_guard<std::mutex> lock(g_status_mutex);
    if (line.empty()) {
        g_status_lines.erase(id);
    }
    else {
        g_status_lines[id] = line;
    }
}

// void snap64_notice(const char* mod_id, const char* line): a line over the
// course for five seconds (menu_assets.cpp menu_notice_set).
void snap64_notice(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t lineAddr = static_cast<uint32_t>(ctx->r5);
    std::string line = (lineAddr == 0) ? std::string() : guest_string(rdram, lineAddr);
    if (line.size() > 120) {
        line.resize(120);
    }
    snap::menu_notice_set(line);
}

} // namespace

namespace snap {

std::string mod_status_line(const std::string& id) {
    std::lock_guard<std::mutex> lock(g_status_mutex);
    const auto it = g_status_lines.find(id);
    return (it == g_status_lines.end()) ? std::string() : it->second;
}

void register_mod_exports() {
    recomp::overlays::register_base_export("recomp_printf", recomp_printf);
    recomp::overlays::register_base_export("snap64_set_status", snap64_set_status);
    recomp::overlays::register_base_export("snap64_notice", snap64_notice);
    register_data_api_exports();
    register_time_api_exports();
}

} // namespace snap
