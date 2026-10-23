/**
 * @file android_thermal.h
 * @brief Android: the phone's thermal status and headroom, in the log
 * (android_thermal.cpp). Elsewhere a no-op.
 */

#ifndef SNAP_ANDROID_THERMAL_H
#define SNAP_ANDROID_THERMAL_H

namespace snap {

// Starts a thread that writes a [SNAP-THERMAL] line every minute and at each
// change of Android's thermal status.
void android_thermal_start();

} // namespace snap

#endif
