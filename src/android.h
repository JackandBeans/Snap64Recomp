/**
 * @file android.h
 * @brief What the port asks Android's own side for, through SDL's activity
 * (android/app/src/main/java/io/github/jackandbeans/snap64recomp/
 * Snap64Activity.java): the system's document picker, which the ROM chooser
 * and the Mods page's Install row use where no file dialog library serves,
 * and a pipe to the system log for snap64.log's lines. Compiled on Android
 * only (CMakeLists.txt).
 */
#pragma once

#include <string>

namespace snap {

// The system's document picker, shown by the activity; blocks the calling
// thread (the game's, never the activity's own) until the player chose or
// canceled. The path of a copy of the chosen document in the app's cache,
// which the caller removes when done, or an empty string.
std::string android_pick_document();

// A pipe whose lines a thread of its own writes to the system log (logcat,
// tag Snap64): the write end, for the log echo in main.cpp, or -1.
int android_logcat_pipe();

} // namespace snap
