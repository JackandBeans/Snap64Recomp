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

// The crash record. A phone gives a player no way to read why an app closed,
// so the log has to say it. android_exit_report copies in what Android kept
// of the app's last ends (the activity asked for it before this side
// started: Snap64ExitInfo.java), with the system's own report of a native
// crash reduced to its signal and the crashing thread's frames.
// android_crash_install puts handlers on the fatal signals: each writes the
// signal, the thread, the Vulkan call that thread began last and the code
// addresses on its stack straight into the log, then hands the signal back
// to the system, whose own report is still made. Both are called once, after
// the log is bound.
void android_exit_report();
void android_crash_install();

} // namespace snap
