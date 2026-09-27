/**
 * @file mod_installer.cpp
 * @brief Installing mods from a file: dropped on the window, or left in mods/.
 *
 * A mod is a .nrm; a texture pack is a .rtz; and a zip may hold either, which
 * is how Thunderstore packages a mod (a manifest.json, an icon and a README
 * beside the mod itself). Every .nrm, .rtz and native library in a zip is
 * taken, wherever in the zip it sits; the rest is left. Before a mod is
 * installed its manifest is read: a mod for another game, or one that needs
 * a newer release of the port, is refused with the reason, since the
 * runtime would refuse it at every start with an error box.
 *
 * Mods and their native libraries go to mods/, texture packs to
 * texture_packs/ (rt64_render_context.cpp loads those at start). The runtime
 * opens mods when the game starts and holds them open, and it cannot rescan
 * the folder while the game runs (scan_mod_folder closes every opened mod,
 * code and all), so a mod installed during play loads at the next start. A
 * file already there is written beside it as <name>.new, and swapped in at
 * the next start before anything opens it.
 *
 * At start, before the runtime scans the folder (main.cpp): the staged
 * files are swapped in, and a zip left in mods/ -- the natural thing to do
 * with a download -- is unpacked the same way and renamed <name>.zip.installed
 * so it is not unpacked again. The other recompilations take a zip dropped on
 * the window (Zelda64Recomp's ui_mod_installer.cpp); a zip in the folder is
 * this port's own addition.
 */

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#include <cstdlib>

#include "librecomp/game.hpp"
#include "librecomp/mods.hpp"
#include "json/json.hpp"

#include "paths.h"
#include "settings.h"
#include "version.h"

#include <SDL.h>
#include <nfd.h>

namespace snap {

namespace {

namespace fs = std::filesystem;

const char* const kGameId = "pokemonsnap";

// What one install did, for the log and the box after a drop.
struct Report {
    std::vector<std::string> installed;   // "Unlimited Film 1.0.0"
    std::vector<std::string> refused;     // "foo.zip: no mod inside"
};

std::mutex g_drop_mutex;
Report g_drop_report;
std::atomic<bool> g_installed_during_play{false};

// For the Mods page (settings.h): what was installed during play, what was
// found at start and will not load, and a generation the page watches.
std::mutex g_list_mutex;
std::vector<InstalledMod> g_session_installs;
std::vector<BrokenMod> g_broken_at_start;
std::atomic<uint32_t> g_install_generation{0};

std::string lower_extension(const fs::path& p) {
    std::string ext = p.extension().string();
    for (char& c : ext) {
        c = char(tolower(uint8_t(c)));
    }
    return ext;
}

std::string display(const fs::path& p) {
    const auto name = p.filename().u8string();
    return std::string(name.begin(), name.end());
}

bool is_native_library(const fs::path& p) {
    const std::string ext = lower_extension(p);
#if defined(_WIN32)
    return ext == ".dll";
#elif defined(__APPLE__)
    return ext == ".dylib";
#else
    const std::string name = display(p);
    return (ext == ".so") || (name.find(".so.") != std::string::npos);
#endif
}

FILE* open_read(const fs::path& p) {
#ifdef _WIN32
    return _wfopen(p.c_str(), L"rb");
#else
    return fopen(p.c_str(), "rb");
#endif
}

// A mod file's manifest, read the way the runtime reads it: its id, name,
// version, descriptions, authors and picture, or the reason it would not
// load here (the id and name are filled whenever the manifest could be
// read). A mod installed during play is not opened by the runtime until the
// next start, so its details page has these from here.
bool check_mod(const fs::path& file, InstalledMod& info, std::string& reason) {
    recomp::mods::ModOpenError error{};
    recomp::mods::ZipModFileHandle handle(file, error);
    if (error != recomp::mods::ModOpenError::Good) {
        reason = "not a mod file (it does not open as one)";
        return false;
    }
    bool exists = false;
    const std::vector<char> bytes = handle.read_file("mod.json", exists);
    if (!exists) {
        reason = "not a mod file (no mod.json inside)";
        return false;
    }
    recomp::mods::ModManifest manifest{};
    std::string param;
    if (recomp::mods::parse_manifest(manifest, bytes, param) != recomp::mods::ModOpenError::Good) {
        reason = "its mod.json could not be read" + (param.empty() ? std::string() : " (" + param + ")");
        return false;
    }
    info.id = manifest.mod_id;
    info.name = manifest.display_name.empty() ? manifest.mod_id : manifest.display_name;
    info.version = manifest.version.to_string();
    info.desc = manifest.short_description.empty() ? manifest.description : manifest.short_description;
    info.fullDesc = manifest.description;
    info.authors = manifest.authors;
    bool thumbExists = false;
    info.thumb = handle.read_file("thumb.dds", thumbExists);
    if (!thumbExists) {
        info.thumb = handle.read_file("thumb.png", thumbExists);
    }
    try {
        const nlohmann::json j = nlohmann::json::parse(bytes.begin(), bytes.end());
        const auto schema = j.find("config_schema");
        if ((schema != j.end()) && schema->is_object()) {
            const auto options = schema->find("options");
            if ((options != schema->end()) && options->is_array()) {
                info.optCount = int(options->size());
            }
        }
    } catch (const nlohmann::json::exception&) {
        info.optCount = 0;   // parse_manifest read it; this cannot fail where that did not
    }
    bool forUs = false;
    for (const std::string& id : manifest.mod_game_ids) {
        forUs = forUs || (id == kGameId);
    }
    if (!forUs) {
        std::string ids;
        for (const std::string& id : manifest.mod_game_ids) {
            ids += (ids.empty() ? "" : ", ") + id;
        }
        reason = "a mod for another game (" + (ids.empty() ? std::string("none named") : ids) + ")";
        return false;
    }
    const recomp::Version ours(SNAP_VERSION_MAJOR, SNAP_VERSION_MINOR, SNAP_VERSION_PATCH, SNAP_VERSION_SUFFIX);
    if (manifest.minimum_recomp_version > ours) {
        reason = "it needs Snap64 Recomp " + manifest.minimum_recomp_version.to_string() + " or newer";
        return false;
    }
    return true;
}

// A texture pack: a zip with RT64's database at its root.
bool check_pack(const fs::path& file, std::string& reason) {
    FILE* f = open_read(file);
    if (f == nullptr) {
        reason = "it could not be read";
        return false;
    }
    mz_zip_archive zip{};
    bool good = mz_zip_reader_init_cfile(&zip, f, 0, 0) != 0;
    if (good) {
        good = mz_zip_reader_locate_file(&zip, "rt64.json", nullptr, 0) >= 0;
        mz_zip_reader_end(&zip);
        if (!good) {
            reason = "not a texture pack (no rt64.json inside)";
        }
    } else {
        reason = "not a texture pack (it does not open as a zip)";
    }
    fclose(f);
    return good;
}

// Moves a written and checked file to its place: the name itself, or
// <name>.new beside it when the name is taken during play (the running game
// may hold it open).
bool place(const fs::path& part, const fs::path& target, bool duringPlay, std::string& reason) {
    std::error_code ec;
    fs::path to = target;
    if (duringPlay && fs::exists(target, ec)) {
        to += ".new";
    }
    fs::remove(to, ec);
    fs::rename(part, to, ec);
    if (ec) {
        fs::remove(part, ec);
        reason = "it could not be written to " + display(to.parent_path()) + " (" + ec.message() + ")";
        return false;
    }
    return true;
}

// One file, already written as <target>.part: checked by its kind, then
// placed. The label names it in the report.
void take(const fs::path& part, const fs::path& target, const std::string& shownAs, bool duringPlay, Report& report) {
    const std::string ext = lower_extension(target);
    std::string label, reason;
    InstalledMod info;
    bool good = true;
    if (ext == ".nrm") {
        good = check_mod(part, info, reason);
        label = info.name + " " + info.version;
    } else if (ext == ".rtz") {
        good = check_pack(part, reason);
        label = "texture pack " + display(target);
    } else {
        label = "library " + display(target);
    }
    std::error_code ec;
    if (!good) {
        fs::remove(part, ec);
        report.refused.push_back(shownAs + ": " + reason);
        return;
    }
    if (!place(part, target, duringPlay, reason)) {
        report.refused.push_back(shownAs + ": " + reason);
        return;
    }
    report.installed.push_back(label);
    if (duringPlay && (ext == ".nrm")) {
        std::lock_guard lock(g_list_mutex);
        bool replaced = false;
        for (InstalledMod& m : g_session_installs) {
            if (m.id == info.id) {
                m = info;
                replaced = true;
            }
        }
        if (!replaced) {
            g_session_installs.push_back(info);
        }
        g_install_generation++;
    }
}

fs::path destination(const fs::path& name) {
    return (lower_extension(name) == ".rtz") ? base_path("texture_packs") : recomp::mods::get_mods_directory();
}

size_t write_to_stream(void* opaque, mz_uint64 offset, const void* bytes, size_t count) {
    std::ofstream& out = *static_cast<std::ofstream*>(opaque);
    out.seekp(std::streamoff(offset), std::ios::beg);
    out.write(static_cast<const char*>(bytes), std::streamsize(count));
    return out.bad() ? 0 : count;
}

// Every mod, texture pack and native library in a zip.
void take_zip(const fs::path& file, bool duringPlay, Report& report) {
    const std::string shown = display(file);
    FILE* f = open_read(file);
    if (f == nullptr) {
        report.refused.push_back(shown + ": it could not be read");
        return;
    }
    mz_zip_archive zip{};
    if (mz_zip_reader_init_cfile(&zip, f, 0, 0) == 0) {
        fclose(f);
        report.refused.push_back(shown + ": it does not open as a zip");
        return;
    }
    size_t found = 0;
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; i++) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat) || stat.m_is_directory) {
            continue;
        }
        // Only the entry's own name: a package may keep its mod in a folder.
        const fs::path name = fs::u8path(stat.m_filename).filename();
        const std::string ext = lower_extension(name);
        if ((ext != ".nrm") && (ext != ".rtz") && !is_native_library(name)) {
            continue;
        }
        found++;
        const fs::path dir = destination(name);
        std::error_code ec;
        fs::create_directories(dir, ec);
        const fs::path target = dir / name;
        fs::path part = target;
        part += ".part";
        bool written = false;
        {
            std::ofstream out(part, std::ios::binary | std::ios::trunc);
            written = out.is_open() && (mz_zip_reader_extract_to_callback(&zip, i, write_to_stream, &out, 0) != 0);
            out.close();
            written = written && !out.fail();
        }
        if (!written) {
            fs::remove(part, ec);
            report.refused.push_back(shown + ": " + display(name) + " could not be unpacked");
            continue;
        }
        take(part, target, shown + ": " + display(name), duringPlay, report);
    }
    mz_zip_reader_end(&zip);
    fclose(f);
    if (found == 0) {
        report.refused.push_back(shown + ": no mod inside (no .nrm or .rtz)");
    }
}

void install(const fs::path& file, bool duringPlay, Report& report) {
    const std::string ext = lower_extension(file);
    const std::string shown = display(file);
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        report.refused.push_back(shown + ": it could not be found");
        return;
    }
    if (fs::is_directory(file, ec)) {
        report.refused.push_back(shown + ": a folder; drop the mod file or its zip");
        return;
    }
    if (ext == ".zip") {
        take_zip(file, duringPlay, report);
        return;
    }
    if ((ext != ".nrm") && (ext != ".rtz")) {
        report.refused.push_back(shown + (is_native_library(file)
            ? ": a mod's library alone; drop the mod's zip, which carries the mod with it"
            : ": not a mod (a mod is a .nrm, a texture pack a .rtz, or a .zip holding them)"));
        return;
    }
    const fs::path dir = destination(file);
    fs::create_directories(dir, ec);
    const fs::path target = dir / file.filename();
    if (fs::equivalent(file, target, ec)) {
        report.refused.push_back(shown + ": it is already in its folder");
        return;
    }
    fs::path part = target;
    part += ".part";
    fs::copy_file(file, part, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        fs::remove(part, ec);
        report.refused.push_back(shown + ": it could not be copied (" + ec.message() + ")");
        return;
    }
    take(part, target, shown, duringPlay, report);
}

void log_report(const char* how, const Report& report) {
    for (const std::string& s : report.installed) {
        printf("[SNAP-MODS] %s: installed %s\n", how, s.c_str());
    }
    for (const std::string& s : report.refused) {
        printf("[SNAP-MODS] %s: not installed: %s\n", how, s.c_str());
    }
    fflush(stdout);
}

// Staged files (<name>.new) over their names, in a folder nothing holds yet.
void swap_in_staged(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return;
    }
    std::vector<fs::path> staged, parts;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::string ext = lower_extension(entry.path());
        if (ext == ".new") {
            staged.push_back(entry.path());
        } else if (ext == ".part") {
            parts.push_back(entry.path());
        }
    }
    for (const fs::path& p : staged) {
        fs::path target = p;
        target.replace_extension();
        fs::remove(target, ec);
        fs::rename(p, target, ec);
        printf("[SNAP-MODS] start: %s %s\n", ec ? "could not replace" : "replaced", display(target).c_str());
    }
    // A write the last run did not finish.
    for (const fs::path& p : parts) {
        fs::remove(p, ec);
    }
}

} // namespace

void mods_install_at_start() {
    const fs::path mods = recomp::mods::get_mods_directory();
    std::error_code ec;
    fs::create_directories(mods, ec);
    swap_in_staged(mods);
    swap_in_staged(base_path("texture_packs"));

    std::vector<fs::path> zips;
    for (const auto& entry : fs::directory_iterator(mods, ec)) {
        if (entry.is_regular_file(ec) && (lower_extension(entry.path()) == ".zip")) {
            zips.push_back(entry.path());
        }
    }
    for (const fs::path& zip : zips) {
        Report report;
        take_zip(zip, false, report);
        log_report("start", report);
        if (!report.installed.empty()) {
            fs::path done = zip;
            done += ".installed";
            fs::remove(done, ec);
            fs::rename(zip, done, ec);
            printf("[SNAP-MODS] start: %s %s\n", ec ? "could not rename" : "unpacked and renamed", display(done).c_str());
        }
    }

    // Every mod file the runtime is about to open, read the same way the
    // installer reads a new one, so the Mods page can say why one will not
    // load instead of leaving it out without a word.
    std::vector<BrokenMod> broken;
    for (const auto& entry : fs::directory_iterator(mods, ec)) {
        if (!entry.is_regular_file(ec) || (lower_extension(entry.path()) != ".nrm")) {
            continue;
        }
        InstalledMod info;
        std::string reason;
        if (!check_mod(entry.path(), info, reason)) {
            broken.push_back(BrokenMod{display(entry.path()), info.id, reason});
            printf("[SNAP-MODS] start: %s will not load: %s\n", display(entry.path()).c_str(), reason.c_str());
        }
    }
    {
        std::lock_guard lock(g_list_mutex);
        g_broken_at_start = std::move(broken);
    }
    fflush(stdout);
}

std::vector<InstalledMod> mods_installed_this_session() {
    std::lock_guard lock(g_list_mutex);
    return g_session_installs;
}

std::vector<BrokenMod> mods_broken_at_start() {
    std::lock_guard lock(g_list_mutex);
    return g_broken_at_start;
}

uint32_t mods_install_generation() {
    return g_install_generation.load();
}

// The session mark: snap64.running in the data folder from the start until a
// normal end. Found at a start, it means the last session was stopped from
// outside or crashed.
static fs::path running_mark() {
    return base_path("snap64.running");
}

void mods_safe_start() {
    // SNAP_SAFE_START_ANSWER=keep or off answers the question without a box,
    // so a run can exercise it. Otherwise a replay or a scripted drop is a
    // test run, stopped from outside by design: it neither asks nor leaves
    // the mark.
    const char* answer = getenv("SNAP_SAFE_START_ANSWER");
    if ((answer == nullptr) && ((getenv("SNAP_REPLAY") != nullptr) || (getenv("SNAP_DROP_TEST") != nullptr))) {
        return;
    }
    std::error_code ec;
    if (fs::exists(running_mark(), ec)) {
        const fs::path config = base_path("mods.json");
        nlohmann::json doc;
        size_t on = 0;
        {
            std::ifstream in(config);
            if (in.is_open()) {
                doc = nlohmann::json::parse(in, nullptr, false);
            }
        }
        if (doc.is_object() && doc.contains("enabled_mods") && doc["enabled_mods"].is_array()) {
            on = doc["enabled_mods"].size();
        }
        printf("[SNAP-MODS] start: the last session did not end normally; %zu mod%s on\n", on, (on == 1) ? "" : "s");
        if (on > 0) {
            const std::string text = std::string(SNAP_PORT_NAME) + " did not close normally last time, and " +
                std::to_string(on) + ((on == 1) ? " mod is" : " mods are") + " on. If a mod stopped the game, "
                "it can stop it again.\n\nStart with the mods off? Options > Mods turns them back on.";
            const SDL_MessageBoxButtonData buttons[] = {
                { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Keep mods on" },
                { 0, 1, "Start with mods off" },
            };
            const SDL_MessageBoxData box = {
                SDL_MESSAGEBOX_WARNING, nullptr, SNAP_PORT_NAME, text.c_str(),
                SDL_arraysize(buttons), buttons, nullptr,
            };
            int choice = 0;
            if (answer != nullptr) {
                choice = (std::string(answer) == "off") ? 1 : 0;
                printf("[SNAP-MODS] start: SNAP_SAFE_START_ANSWER=%s answers the question\n", answer);
            } else if (SDL_ShowMessageBox(&box, &choice) != 0) {
                printf("[SNAP-MODS] start: the question could not be shown (%s)\n", SDL_GetError());
                choice = 0;
            }
            if (choice == 1) {
                doc["enabled_mods"] = nlohmann::json::array();
                std::ofstream out(config, std::ios::trunc);
                out << doc.dump(4);
                out.close();
                printf("[SNAP-MODS] start: %s\n", out.fail() ? "could not write mods.json; mods stay on"
                                                             : "every mod turned off in mods.json, at the player's word");
            } else {
                printf("[SNAP-MODS] start: mods kept on\n");
            }
        }
    }
    std::ofstream mark(running_mark(), std::ios::trunc);
    mark << "Snap64 Recomp is running, or did not close normally.\n";
    fflush(stdout);
}

void session_mark_clean() {
    std::error_code ec;
    fs::remove(running_mark(), ec);
}

// The Install row's request, from the game's thread to the window's.
static std::atomic<bool> g_pick_requested{false};

void mods_request_pick() {
    g_pick_requested.store(true, std::memory_order_relaxed);
}

// The file picker for the Install row, several files at once, a mod, a
// texture pack or a zip holding them. SNAP_PICK_TEST=<file>[;<file>...]
// answers it with those files, so a replay can install without a hand.
bool mods_pick_and_install() {
    if (!g_pick_requested.exchange(false, std::memory_order_relaxed)) {
        return false;
    }
    std::vector<std::string> files;
    if (const char* test = getenv("SNAP_PICK_TEST")) {
        std::string all(test);
        size_t from = 0;
        while (from < all.size()) {
            const size_t semi = all.find(';', from);
            const std::string one = all.substr(from, (semi == std::string::npos) ? std::string::npos : semi - from);
            if (!one.empty()) {
                files.push_back(one);
            }
            if (semi == std::string::npos) {
                break;
            }
            from = semi + 1;
        }
    } else {
        if (NFD_Init() != NFD_OKAY) {
            const char* err = NFD_GetError();
            printf("[SNAP-MODS] the file picker could not start: %s\n", err ? err : "no reason given");
            fflush(stdout);
            return false;
        }
        const nfdu8filteritem_t filters[] = { { "Mods, texture packs and their zips", "nrm,rtz,zip" } };
        const nfdpathset_t* set = nullptr;
        const nfdresult_t r = NFD_OpenDialogMultipleU8(&set, filters, 1, nullptr);
        if ((r == NFD_OKAY) && (set != nullptr)) {
            nfdpathsetsize_t count = 0;
            NFD_PathSet_GetCount(set, &count);
            for (nfdpathsetsize_t i = 0; i < count; i++) {
                nfdu8char_t* path = nullptr;
                if ((NFD_PathSet_GetPathU8(set, i, &path) == NFD_OKAY) && (path != nullptr)) {
                    files.push_back(path);
                    NFD_PathSet_FreePathU8(path);
                }
            }
            NFD_PathSet_Free(set);
        } else if (r == NFD_ERROR) {
            const char* err = NFD_GetError();
            printf("[SNAP-MODS] the file picker failed: %s\n", err ? err : "no reason given");
        }
        NFD_Quit();
    }
    printf("[SNAP-MODS] the Mods page's Install row: %zu file%s chosen\n", files.size(), (files.size() == 1) ? "" : "s");
    fflush(stdout);
    for (const std::string& f : files) {
        mods_drop_file(f.c_str());
    }
    return !files.empty();
}

void mods_drop_file(const char* path) {
    if (path == nullptr) {
        return;
    }
    Report report;
    install(fs::u8path(path), true, report);
    log_report("dropped", report);
    if (!report.installed.empty()) {
        g_installed_during_play = true;
    }
    std::lock_guard lock(g_drop_mutex);
    g_drop_report.installed.insert(g_drop_report.installed.end(), report.installed.begin(), report.installed.end());
    g_drop_report.refused.insert(g_drop_report.refused.end(), report.refused.begin(), report.refused.end());
}

bool mods_drop_summary(std::string& text, bool& anyInstalled) {
    Report report;
    {
        std::lock_guard lock(g_drop_mutex);
        report = std::move(g_drop_report);
        g_drop_report = Report{};
    }
    if (report.installed.empty() && report.refused.empty()) {
        return false;
    }
    text.clear();
    if (!report.installed.empty()) {
        text += "Installed:\n";
        for (const std::string& s : report.installed) {
            text += "    " + s + "\n";
        }
        text += "\nThey load when the game starts: restart it from Options > Mods, or close the game and open it again.\n";
    }
    if (!report.refused.empty()) {
        text += report.installed.empty() ? "Not installed:\n" : "\nNot installed:\n";
        for (const std::string& s : report.refused) {
            text += "    " + s + "\n";
        }
    }
    anyInstalled = !report.installed.empty();
    return true;
}

bool mods_installed_during_play() {
    return g_installed_during_play.load();
}

} // namespace snap
