/**
 * @file steam_art.cpp
 * @brief See steam_art.h.
 */
#include "steam_art.h"

// A Linux desktop's and Windows's alone: Android defines __linux__ too and has no
// Steam, and macOS has no shortcut this has been tried with.
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
#define SNAP_STEAM_ART_HERE 1
#else
#define SNAP_STEAM_ART_HERE 0
#endif

#if SNAP_STEAM_ART_HERE

#include "paths.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace snap {

namespace {

namespace fs = std::filesystem;

// One of Steam's art slots for a shortcut: the picture's name in the steam_art
// folder, and what follows the app id in the grid folder's file name.
struct ArtSlot {
    const char* source;
    const char* suffix;
    const char* say;
};

constexpr ArtSlot kArtSlots[] = {
    {"poster", "p", "poster"},
    {"background", "_hero", "background"},
    {"logo", "_logo", "logo"},
    {"wide", "", "wide capsule"},
};

// The temporary name a picture has while it is copied, in the grid folder. Steam
// looks for its own names, so it never reads half a picture.
constexpr const char* kPartSuffix = ".snap64part";

// Slots whose picture the port has written, one "<account> <app id> <slot>" a line,
// in the port's data folder, so a picture the player deletes stays deleted. A slot
// that holds the player's own art is not listed: it is looked at again at each start.
constexpr const char* kDoneFile = "steam_art_done.txt";

// What every name of this game holds, in lower case: the executable's, the Flatpak's
// id (io.github.jackandbeans.Snap64Recomp, in a Flatpak shortcut's launch options),
// and the name the shortcut has in Steam unless the player changed it.
constexpr const char* kGameName = "snap64";

// What this start says, read once on the main thread: this file's thread must not
// read the environment while the rest of the port is running.
struct Launch {
    bool off = false;
    std::optional<std::string> gameId;
    std::optional<std::string> appId;
    std::optional<std::string> testRoot;
    std::optional<std::string> home;
};

std::optional<std::string> env_value(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
}

Launch read_launch() {
    Launch launch;
    const std::optional<std::string> off = env_value("SNAP_NO_STEAM_ART");
    launch.off = off.has_value() && !off->empty() && (*off != "0");
    launch.gameId = env_value("SteamGameId");
    launch.appId = env_value("SteamAppId");
    launch.testRoot = env_value("SNAP_STEAM_ROOT");
    launch.home = env_value("HOME");
    return launch;
}

std::string path_text(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

void say_line(const std::string& line) {
    printf("[SNAP] Steam art: %s\n", line.c_str());
    fflush(stdout);
}

std::string lower_text(std::string text) {
    for (char& c : text) {
        if ((c >= 'A') && (c <= 'Z')) {
            c = char(c - 'A' + 'a');
        }
    }
    return text;
}

// "1 picture", "4 pictures".
std::string counted(size_t n, const char* thing) {
    return std::to_string(n) + " " + thing + ((n == 1) ? "" : "s");
}

// The app id in a value Steam hands over, to say which shortcut it means. A shortcut's
// 64-bit id is the app id above and 0x02000000 below, the mark of a shortcut; any
// other number that fits is taken as the app id alone. False for a value that can be
// neither.
bool app_id_in(const std::string& value, uint32_t* appId) {
    if (value.empty() || !std::all_of(value.begin(), value.end(), [](char c) { return (c >= '0') && (c <= '9'); })) {
        return false;
    }
    errno = 0;
    const unsigned long long id = std::strtoull(value.c_str(), nullptr, 10);
    if (errno != 0) {
        return false;
    }
    const uint64_t high = id >> 32;
    const uint64_t low = id & 0xFFFFFFFFull;
    if (low == 0x02000000ull) {
        *appId = uint32_t(high);
    } else if (high == 0) {
        *appId = uint32_t(low);
    } else {
        return false;
    }
    return *appId != 0;
}

struct Shortcut {
    uint32_t appId = 0;
    std::string name;
    std::string exe;
    std::string options;
    std::string icon;
};

// shortcuts.vdf is Steam's binary form of nested key lists. An item is a type byte,
// its key ending in a zero, and its value: type 0 opens a list, 1 is text ending in a
// zero, 2 is four bytes low first, and a lone 8 closes a list. The file is one list
// of shortcuts, each a list; the keys have been spelled in more than one case over
// the years. A shortcut counts once its list has closed and it has an app id. A cut
// file or a type not known here ends the reading, and what came before stands.
std::vector<Shortcut> read_shortcuts(const std::string& vdf) {
    std::vector<Shortcut> found;
    Shortcut current;
    int depth = 0;
    size_t at = 0;
    const auto read_text = [&vdf, &at](std::string* out) {
        const size_t end = vdf.find('\0', at);
        if (end == std::string::npos) {
            return false;
        }
        *out = vdf.substr(at, end - at);
        at = end + 1;
        return true;
    };
    while (at < vdf.size()) {
        const char type = vdf[at++];
        if (type == 8) {
            if (depth == 0) {
                break;
            }
            if ((depth == 2) && (current.appId != 0)) {
                found.push_back(current);
            }
            if (depth == 2) {
                current = Shortcut{};
            }
            depth--;
            continue;
        }
        std::string key;
        if (((type != 0) && (type != 1) && (type != 2)) || !read_text(&key)) {
            break;
        }
        key = lower_text(key);
        if (type == 0) {
            depth++;
        } else if (type == 1) {
            std::string value;
            if (!read_text(&value)) {
                break;
            }
            if ((depth == 2) && (key == "appname")) {
                current.name = value;
            } else if ((depth == 2) && (key == "exe")) {
                current.exe = value;
            } else if ((depth == 2) && (key == "launchoptions")) {
                current.options = value;
            } else if ((depth == 2) && (key == "icon")) {
                current.icon = value;
            }
        } else {
            if ((vdf.size() - at) < 4) {
                break;
            }
            uint32_t value = 0;
            for (size_t b = 0; b < 4; b++) {
                value |= uint32_t(uint8_t(vdf[at + b])) << (8 * b);
            }
            at += 4;
            if ((depth == 2) && (key == "appid")) {
                current.appId = value;
            }
        }
    }
    return found;
}

// Whether a shortcut's own entry names this game, in its name in Steam, its target or
// its launch options. A run Steam started from another shortcut, a launcher, reports
// that shortcut's id, which is why Steam's word alone is not enough.
bool names_this_game(const Shortcut& shortcut) {
    return lower_text(shortcut.name + "\n" + shortcut.exe + "\n" + shortcut.options).find(kGameName) != std::string::npos;
}

// Where Steam is. Empty when it cannot be found. SNAP_STEAM_ROOT names it instead,
// for a test's made-up folder.
fs::path steam_root(const Launch& launch) {
    if (launch.testRoot.has_value()) {
        return fs::path(*launch.testRoot);
    }
#if defined(_WIN32)
    wchar_t value[1024];
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS) {
        return fs::path(value);
    }
    return {};
#else
    // A Flatpak's HOME is the real home folder. The second name is a link to the
    // first on a Deck, and the only name on some other installs.
    if (!launch.home.has_value() || launch.home->empty()) {
        return {};
    }
    for (const char* rel : {".local/share/Steam", ".steam/steam"}) {
        std::error_code ec;
        const fs::path p = fs::path(*launch.home) / rel;
        if (fs::is_directory(p / "userdata", ec)) {
            return p;
        }
    }
    return {};
#endif
}

struct SteamAccount {
    std::string id;
    fs::path grid;
    std::vector<Shortcut> shortcuts;
};

// Every account with a shortcut list, in a steady order. Two people's accounts on
// one machine each have their own list and their own art.
std::vector<SteamAccount> read_accounts(const fs::path& root) {
    std::vector<SteamAccount> accounts;
    std::error_code ec;
    for (fs::directory_iterator it(root / "userdata", ec), end; !ec && (it != end); it.increment(ec)) {
        std::error_code entryEc;
        const fs::path vdfPath = it->path() / "config" / "shortcuts.vdf";
        if (!it->is_directory(entryEc) || !fs::is_regular_file(vdfPath, entryEc)) {
            continue;
        }
        std::ifstream vdfFile(vdfPath, std::ios::binary);
        if (!vdfFile) {
            continue;
        }
        const std::string vdf((std::istreambuf_iterator<char>(vdfFile)), std::istreambuf_iterator<char>());
        accounts.push_back({path_text(it->path().filename()), it->path() / "config" / "grid", read_shortcuts(vdf)});
    }
    std::sort(accounts.begin(), accounts.end(), [](const SteamAccount& a, const SteamAccount& b) { return a.id < b.id; });
    return accounts;
}

// One shortcut in one account, to write the art of.
struct ArtTarget {
    const SteamAccount* account;
    uint32_t appId;
};

std::vector<ArtTarget> targets_naming_this_game(const std::vector<SteamAccount>& accounts) {
    std::vector<ArtTarget> targets;
    for (const SteamAccount& account : accounts) {
        std::set<uint32_t> ids;
        for (const Shortcut& shortcut : account.shortcuts) {
            if (names_this_game(shortcut)) {
                ids.insert(shortcut.appId);
            }
        }
        for (uint32_t id : ids) {
            targets.push_back({&account, id});
        }
    }
    return targets;
}

// Whether any account's list holds this app id.
bool listed_in(const std::vector<SteamAccount>& accounts, uint32_t appId) {
    return std::any_of(accounts.begin(), accounts.end(), [appId](const SteamAccount& account) {
        return std::any_of(account.shortcuts.begin(), account.shortcuts.end(),
                           [appId](const Shortcut& shortcut) { return shortcut.appId == appId; });
    });
}

// "SteamGameId 123", "SteamAppId unset".
std::string seen_value(const char* name, const std::optional<std::string>& value) {
    const std::string shown = !value.has_value() ? "unset" : (value->empty() ? "empty" : *value);
    return std::string(name) + " " + shown;
}

// The picture for a slot beside the executable, keeping its own format. Empty when
// this package has none.
fs::path shipped_picture(const fs::path& artFolder, const char* source) {
    for (const char* ext : {".png", ".jpg"}) {
        std::error_code ec;
        fs::path p = artFolder / (std::string(source) + ext);
        if (fs::is_regular_file(p, ec)) {
            return p;
        }
    }
    return {};
}

// Whether the slot has a file already, in whatever format. Steam's <id>.json beside
// the wide capsule's name is where it keeps the logo's position, not a picture.
bool slot_is_taken(const fs::path& grid, const std::string& stem) {
    std::error_code ec;
    for (fs::directory_iterator it(grid, ec), end; !ec && (it != end); it.increment(ec)) {
        const fs::path name = it->path().filename();
        if ((path_text(name.stem()) == stem) && (name.extension() != ".json") && (name.extension() != kPartSuffix)) {
            return true;
        }
    }
    return false;
}

// Puts a copy of the picture at dest without ever replacing a file there: a file
// that turns up at dest in the moment after the look above stays, and this reports
// false. `why` receives the reason.
bool place_new_file(const fs::path& picture, const fs::path& dest, std::string* why) {
    fs::path part = dest;
    part += kPartSuffix;
    std::error_code ec;
    // A run that died can leave one, read-only if the package's picture is.
    fs::remove(part, ec);
    fs::copy_file(picture, part, fs::copy_options::none, ec);
    if (ec) {
        *why = ec.message();
        fs::remove(part, ec);
        return false;
    }
#if defined(_WIN32)
    // No MOVEFILE_REPLACE_EXISTING: it fails when dest exists.
    const bool placed = MoveFileExW(part.c_str(), dest.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
    if (!placed) {
        *why = std::error_code(int(GetLastError()), std::system_category()).message();
        fs::remove(part, ec);
    }
    return placed;
#else
    // A hard link fails when dest exists, which rename would replace.
    bool placed = (link(part.c_str(), dest.c_str()) == 0);
    const int err = errno;
    if (placed) {
        fs::remove(part, ec);
        return true;
    }
    if ((err != EEXIST) && !fs::exists(dest, ec)) {
        // A filesystem with no hard links. Only here can a file appearing in this
        // instant be replaced.
        fs::rename(part, dest, ec);
        if (!ec) {
            return true;
        }
        *why = ec.message();
    } else {
        *why = std::strerror(err);
    }
    fs::remove(part, ec);
    return false;
#endif
}

std::set<std::string> done_slots(const fs::path& doneFile) {
    std::set<std::string> done;
    std::ifstream f(doneFile);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty()) {
            done.insert(line);
        }
    }
    return done;
}

} // namespace

static void do_the_art(const Launch& launch) {
    // Begun at once and said last, whatever happens, so every start has this line.
    std::string line = seen_value("SteamGameId", launch.gameId) + ", " + seen_value("SteamAppId", launch.appId);
    if (launch.off) {
        say_line(line + "; switched off by SNAP_NO_STEAM_ART");
        return;
    }
    const fs::path root = steam_root(launch);
    if (root.empty()) {
        say_line(line + "; Steam's folder was not found; nothing written");
        return;
    }
    const std::vector<SteamAccount> accounts = read_accounts(root);

    const std::vector<ArtTarget> targets = targets_naming_this_game(accounts);
    if (targets.empty()) {
        // Steam may not have saved a shortcut added a moment ago.
        size_t shortcuts = 0;
        for (const SteamAccount& account : accounts) {
            shortcuts += account.shortcuts.size();
        }
        std::string why;
        uint32_t id = 0;
        for (const std::optional<std::string>* value : {&launch.gameId, &launch.appId}) {
            if (why.empty() && value->has_value() && app_id_in(**value, &id) && listed_in(accounts, id)) {
                why = ", and the shortcut Steam started it from, " + std::to_string(id) + ", names something else";
            }
        }
        say_line(line + "; no shortcut names this game (" + counted(accounts.size(), "account") + ", " + counted(shortcuts, "shortcut") +
                 ", under " + path_text(root) + ")" + why + "; nothing written, and the port looks again at its next start");
        return;
    }
    std::set<uint32_t> ids;
    for (const ArtTarget& target : targets) {
        ids.insert(target.appId);
    }
    line += (ids.size() == 1) ? "; shortcut " : "; shortcuts ";
    const char* const verb = (ids.size() == 1) ? " names this game" : " name this game";
    bool firstId = true;
    for (uint32_t one : ids) {
        line += (firstId ? "" : ", ") + std::to_string(one);
        firstId = false;
    }
    line += verb;

    // The icon Steam keeps for each: a file's path, or for a Flatpak's shortcut the
    // name its .desktop file gives, which Steam looks up in the icon theme
    // (io.github.jackandbeans.Snap64Recomp on JackandBeans's Deck, Nov 7 2026).
    for (const ArtTarget& target : targets) {
        for (const Shortcut& shortcut : target.account->shortcuts) {
            if (shortcut.appId != target.appId) {
                continue;
            }
            std::string icon = shortcut.icon;
            if ((icon.size() >= 2) && (icon.front() == '"') && (icon.back() == '"')) {
                icon = icon.substr(1, icon.size() - 2);
            }
            std::string said = "shortcut " + std::to_string(target.appId) + "'s icon in Steam: ";
            if (icon.empty()) {
                said += "none set";
            } else if ((icon.find('/') == std::string::npos) && (icon.find('\\') == std::string::npos)) {
                said += icon + ", a name Steam looks up in the icon theme";
            } else {
                std::error_code ec;
                const fs::path file(std::u8string(reinterpret_cast<const char8_t*>(icon.data()), icon.size()));
                said += icon + (fs::is_regular_file(file, ec) ? "" : ", not found from here");
            }
            say_line(said);
            break;
        }
    }

    const fs::path artFolder = exe_path("steam_art");
    const fs::path doneFile = base_path(kDoneFile);
    const std::set<std::string> done = done_slots(doneFile);
    size_t wrote = 0;
    size_t failed = 0;
    size_t leftAlone = 0;
    size_t stopped = 0;
    bool noteFailed = false;
    for (const ArtTarget& target : targets) {
        const SteamAccount& account = *target.account;
        bool gridFailed = false;
        for (const ArtSlot& slot : kArtSlots) {
            const std::string key = account.id + " " + std::to_string(target.appId) + " " + slot.source;
            if (done.count(key) != 0) {
                stopped++;
                continue;
            }
            const fs::path picture = shipped_picture(artFolder, slot.source);
            if (picture.empty()) {
                continue;
            }
            if (gridFailed) {
                failed++;
                continue;
            }
            std::error_code ec;
            fs::create_directories(account.grid, ec);
            if (ec) {
                say_line("could not make " + path_text(account.grid) + " (" + ec.message() + ")");
                gridFailed = true;
                failed++;
                continue;
            }
            const std::string stem = std::to_string(target.appId) + slot.suffix;
            if (slot_is_taken(account.grid, stem)) {
                leftAlone++;
                continue;
            }
            const fs::path dest = account.grid / (stem + path_text(picture.extension()));
            std::string why;
            if (!place_new_file(picture, dest, &why)) {
                say_line("could not write the " + std::string(slot.say) + " to " + path_text(dest) + " (" + why + ")");
                failed++;
                continue;
            }
            say_line("wrote the " + std::string(slot.say) + " to " + path_text(dest));
            wrote++;
            // Noted at once: this thread does not hold the port back from quitting.
            std::ofstream f(doneFile, std::ios::app);
            f << key << '\n';
            f.flush();
            if (!f && !noteFailed) {
                say_line("could not note what was done in " + path_text(doneFile));
                noteFailed = true;
            }
        }
    }

    std::vector<std::string> results;
    if (wrote > 0) {
        results.push_back("wrote " + counted(wrote, "picture") + ", which Steam shows after its next restart");
    }
    if (failed > 0) {
        results.push_back(counted(failed, "picture") + " could not be written, and the port tries again at its next start");
    }
    if (leftAlone > 0) {
        results.push_back(counted(leftAlone, "picture") + " left alone because the slot has a file");
    }
    if (stopped > 0) {
        results.push_back(counted(stopped, "picture") + " stopped by " + path_text(doneFile) + ", written at an earlier start");
    }
    if (results.empty()) {
        results.push_back("this package has no pictures; nothing written");
    }
    for (const std::string& result : results) {
        line += "; " + result;
    }
    say_line(line);
}

// A thrown error on this thread would end the game, and no picture is worth that.
static void do_the_art_guarded(const Launch& launch) {
    try {
        do_the_art(launch);
    } catch (const std::exception& e) {
        say_line(std::string("stopped by an error: ") + e.what());
    } catch (...) {
        say_line("stopped by an error");
    }
}

void steam_art_run() {
    do_the_art_guarded(read_launch());
}

void steam_art_start() {
    std::thread([launch = read_launch()] { do_the_art_guarded(launch); }).detach();
}

} // namespace snap

#else

namespace snap {
void steam_art_start() {}
void steam_art_run() {}
} // namespace snap

#endif
