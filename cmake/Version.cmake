# VR releases advance independently of project(VERSION), the desktop version.
# Bump these values for a VR release; do not change them when merging desktop updates.
set(SNAP_VR_VERSION "1.0")
set(SNAP_VR_VERSION_PRERELEASE "")

set(SNAP_VERSION_BASE "${PROJECT_VERSION}")
if(SNAP_ENABLE_VR)
    set(SNAP_VERSION_BASE "${SNAP_VR_VERSION}")
    set(SNAP_VERSION_PRERELEASE "${SNAP_VR_VERSION_PRERELEASE}")
endif()

# Windows resources and C++ macros need all three numeric components even
# when the displayed release is 1.0. Keep the display spelling in BASE.
string(REPLACE "." ";" _snap_version_parts "${SNAP_VERSION_BASE}")
list(GET _snap_version_parts 0 SNAP_VERSION_MAJOR)
list(GET _snap_version_parts 1 SNAP_VERSION_MINOR)
list(LENGTH _snap_version_parts _snap_version_count)
set(SNAP_VERSION_PATCH 0)
if(_snap_version_count GREATER 2)
    list(GET _snap_version_parts 2 SNAP_VERSION_PATCH)
endif()

if(SNAP_VERSION_PRERELEASE)
    set(SNAP_VERSION_SUFFIX "-${SNAP_VERSION_PRERELEASE}")
    set(SNAP_RC_FILEFLAGS "VS_FF_PRERELEASE")
else()
    set(SNAP_VERSION_SUFFIX "")
    set(SNAP_RC_FILEFLAGS "0x0L")
endif()
set(SNAP_VERSION_FULL "${SNAP_VERSION_BASE}${SNAP_VERSION_SUFFIX}")
string(REPLACE "-" " " SNAP_VERSION_CREDITS "${SNAP_VERSION_FULL}")
