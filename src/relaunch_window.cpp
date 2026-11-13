/**
 * @file relaunch_window.cpp
 * @brief See relaunch_window.h.
 */
#include "relaunch_window.h"

#if defined(__linux__) && !defined(__ANDROID__)

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace snap {

namespace {

// The descriptor and the moment of the relaunch, "<fd>,<ms>". The monotonic clock
// runs on across execv.
constexpr const char* kKeptConnection = "SNAP_KEPT_X_CONNECTION";
constexpr long long kLimitMs = 10000;

std::atomic<int> s_connection{-1};   // this image's own
int s_kept = -1;                     // the last image's, until it is closed
long long s_relaunchAtMs = 0;

long long now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

} // namespace

void relaunch_window_note_connection(int fd) {
    s_connection.store(fd);
}

bool relaunch_window_keep() {
    const int fd = s_connection.load();
    if (fd < 0) {
        return false;
    }
    const int flags = fcntl(fd, F_GETFD);
    if ((flags < 0) || (fcntl(fd, F_SETFD, flags & ~FD_CLOEXEC) != 0)) {
        return false;
    }
    setenv(kKeptConnection, (std::to_string(fd) + "," + std::to_string(now_ms())).c_str(), 1);
    return true;
}

void relaunch_window_keep_undo() {
    const int fd = s_connection.load();
    if (fd >= 0) {
        const int flags = fcntl(fd, F_GETFD);
        if (flags >= 0) {
            fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
        }
    }
    unsetenv(kKeptConnection);
}

void relaunch_window_take() {
    const char* value = std::getenv(kKeptConnection);
    if (value == nullptr) {
        return;
    }
    int fd = -1;
    long long at = 0;
    const bool parsed = (sscanf(value, "%d,%lld", &fd, &at) == 2);
    unsetenv(kKeptConnection);
    // Only ever a socket this process holds, and never a standard stream: a word in
    // the environment must not close something else.
    struct stat st{};
    if (!parsed || (fd <= STDERR_FILENO) || (fstat(fd, &st) != 0) || !S_ISSOCK(st.st_mode)) {
        printf("[SNAP] gamescope: the relaunch left no connection to take\n");
        fflush(stdout);
        return;
    }
    const int flags = fcntl(fd, F_GETFD);
    if (flags >= 0) {
        fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }
    s_kept = fd;
    s_relaunchAtMs = at;
    printf("[SNAP] gamescope: the window from before the relaunch stays up until this one draws\n");
    fflush(stdout);
}

void relaunch_window_tick(bool gameRunning) {
    if (s_kept < 0) {
        return;
    }
    const long long ms = now_ms() - s_relaunchAtMs;
    if (!gameRunning && (ms < kLimitMs)) {
        return;
    }
    close(s_kept);
    s_kept = -1;
    printf("[SNAP] gamescope: the window from before the relaunch is closed, %lld ms after it (%s)\n",
           ms, gameRunning ? "the game is running" : "the limit; the game is not running yet");
    fflush(stdout);
}

} // namespace snap

#endif
