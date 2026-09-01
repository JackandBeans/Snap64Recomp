#include <cstdio>
#include <string>
#include <system_error>
#include "files.hpp"

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <Windows.h>
#    define PATHFMT "%ls"
#else
#    include <cerrno>
#    include <fcntl.h>
#    include <unistd.h>
#    define PATHFMT "%s"
#endif

constexpr std::u8string_view backup_suffix = u8".bak";
constexpr std::u8string_view temp_suffix = u8".temp";

static std::error_code last_error() {
#ifdef _WIN32
    return std::error_code{static_cast<int>(GetLastError()), std::system_category()};
#else
    return std::error_code{errno, std::generic_category()};
#endif
}

// Reads exactly contents.size() bytes from path. Returns false, with why describing the problem,
// when the file is missing, is not that size, cannot be opened, or reads short.
static bool read_exact_file(const std::filesystem::path& path, std::span<char> contents, std::string& why) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        why = ec.message();
        return false;
    }
    if (size != contents.size()) {
        why = std::to_string(size) + " bytes, expected " + std::to_string(contents.size());
        return false;
    }
    std::ifstream file{path, std::ios_base::binary};
    if (!file.is_open()) {
        why = "could not be opened";
        return false;
    }
    if (!file.read(contents.data(), static_cast<std::streamsize>(contents.size()))) {
        why = "read failed after " + std::to_string(file.gcount()) + " bytes";
        return false;
    }
    return true;
}

// Creates path with contents and forces it to disk before closing, so the bytes are on the medium
// before anything is renamed over them. Sets ec on failure.
static bool write_file_to_disk(const std::filesystem::path& path, std::span<const char> contents, std::error_code& ec) {
    ec.clear();
#ifdef _WIN32
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        ec = last_error();
        return false;
    }
    // WriteFile counts in DWORDs; the loop keeps this correct for any size.
    size_t offset = 0;
    while (offset < contents.size()) {
        const size_t remaining = contents.size() - offset;
        const DWORD chunk = remaining < MAXDWORD ? static_cast<DWORD>(remaining) : MAXDWORD;
        DWORD written = 0;
        if (!WriteFile(file, contents.data() + offset, chunk, &written, nullptr)) {
            ec = last_error();
            break;
        }
        if (written == 0) {
            // No progress and no error would spin forever; report it as an I/O error instead.
            ec = std::make_error_code(std::errc::io_error);
            break;
        }
        offset += written;
    }
    if (!ec && !FlushFileBuffers(file)) {
        ec = last_error();
    }
    if (!CloseHandle(file) && !ec) {
        ec = last_error();
    }
#else
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0) {
        ec = last_error();
        return false;
    }
    size_t offset = 0;
    while (offset < contents.size()) {
        const ssize_t written = ::write(fd, contents.data() + offset, contents.size() - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            ec = last_error();
            break;
        }
        if (written == 0) {
            // No progress and no error would spin forever; report it as an I/O error instead.
            ec = std::make_error_code(std::errc::io_error);
            break;
        }
        offset += static_cast<size_t>(written);
    }
    if (!ec && ::fsync(fd) != 0) {
        ec = last_error();
    }
    if (::close(fd) != 0 && !ec) {
        ec = last_error();
    }
#endif
    return !ec;
}

// Renames from over to. On Windows std::filesystem::rename cannot ask for a write-through move, so
// MoveFileExW is called directly: REPLACE_EXISTING swaps the name atomically and WRITE_THROUGH
// makes the swap durable before returning. POSIX rename(2) is already atomic.
static void rename_file(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& ec) {
#ifdef _WIN32
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ec.clear();
    }
    else {
        ec = last_error();
    }
#else
    std::filesystem::rename(from, to, ec);
#endif
}

// Publishes filepath's temporary file: the current file becomes the backup and the temporary file
// becomes the current file, each by one atomic rename, so every name on disk always refers to a
// complete image.
static bool publish_temp_file(const std::filesystem::path& filepath) {
    std::filesystem::path backup_path{filepath};
    backup_path += backup_suffix;

    std::filesystem::path temp_path{filepath};
    temp_path += temp_suffix;

    std::error_code ec;
    rename_file(filepath, backup_path, ec);
    // No current file means this is the first publish, or the previous one stopped between its two
    // renames. Either way the backup is left as it is and the new file simply takes the name.
    if (ec && ec != std::errc::no_such_file_or_directory) {
        fprintf(stderr, "[files] Failed to move " PATHFMT " to " PATHFMT ": %s\n", filepath.c_str(), backup_path.c_str(), ec.message().c_str());
        return false;
    }
    rename_file(temp_path, filepath, ec);
    if (ec) {
        fprintf(stderr, "[files] Failed to move " PATHFMT " to " PATHFMT ": %s\n", temp_path.c_str(), filepath.c_str(), ec.message().c_str());
        return false;
    }
    return true;
}

std::ifstream recomp::open_input_backup_file(const std::filesystem::path& filepath, std::ios_base::openmode mode) {
    std::filesystem::path backup_path{filepath};
    backup_path += backup_suffix;
    return std::ifstream{backup_path, mode};
}

std::ifstream recomp::open_input_file_with_backup(const std::filesystem::path& filepath, std::ios_base::openmode mode) {
    std::ifstream ret{filepath, mode};

    // Check if the file failed to open and open the corresponding backup file instead if so.
    if (!ret.good()) {
        return open_input_backup_file(filepath, mode);
    }

    return ret;
}

bool recomp::read_file_with_backup(const std::filesystem::path& filepath, std::span<char> contents) {
    std::string why;
    if (read_exact_file(filepath, contents, why)) {
        return true;
    }

    std::filesystem::path backup_path{filepath};
    backup_path += backup_suffix;

    std::string backup_why;
    if (read_exact_file(backup_path, contents, backup_why)) {
        fprintf(stderr, "[files] " PATHFMT " unusable (%s); loaded backup " PATHFMT "\n", filepath.c_str(), why.c_str(), backup_path.c_str());
        return true;
    }

    fprintf(stderr, "[files] No usable copy of " PATHFMT " (%s; backup: %s)\n", filepath.c_str(), why.c_str(), backup_why.c_str());
    return false;
}

std::ofstream recomp::open_output_file_with_backup(const std::filesystem::path& filepath, std::ios_base::openmode mode) {
    std::filesystem::path temp_path{filepath};
    temp_path += temp_suffix;
    std::ofstream temp_file_out{ temp_path, mode };

    return temp_file_out;
}

bool recomp::finalize_output_file_with_backup(const std::filesystem::path& filepath) {
    return publish_temp_file(filepath);
}

bool recomp::write_file_with_backup(const std::filesystem::path& filepath, std::span<const char> contents) {
    std::filesystem::path temp_path{filepath};
    temp_path += temp_suffix;

    std::error_code ec;
    if (!write_file_to_disk(temp_path, contents, ec)) {
        fprintf(stderr, "[files] Failed to write " PATHFMT ": %s\n", temp_path.c_str(), ec.message().c_str());
        return false;
    }
    return publish_temp_file(filepath);
}
