#!/bin/bash
# The Android app, from a prepared port root: the vendored trees fetched and
# the inputs the ROM produces in place (BUILDING.md, steps 3 to 10). Three
# builds and a package: a native build of the two host tools a cross build
# cannot run (the audio microcode's C++ from RSPRecomp, and RT64's
# file_to_c), the arm64-v8a and x86_64 libraries with the NDK, and Gradle's
# release APK with its .sha256 sidecar, signed when the release key's
# variables are set (BUILDING.md, step 16). .github/workflows/build.yml runs
# it; so can a person, from the port root, under Linux or WSL:
#
#     ANDROID_HOME, ANDROID_NDK_HOME, JAVA_HOME (a JDK 17)    required
#     SNAP_VERSION_PRERELEASE                                 a label (rc1), or empty
#     SNAP64_ANDROID_KEYSTORE, _KEYSTORE_PASSWORD, _KEY_ALIAS  the release key
#     SNAP_LAUNCHER                                           a compiler launcher (ccache)
#     JOBS                                                    parallel jobs (all cores)
#
#     bash tools/android/build.sh
set -eu
: "${ANDROID_HOME:?ANDROID_HOME names the Android SDK}"
: "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME names the NDK}"
: "${JAVA_HOME:?JAVA_HOME names a JDK 17}"
pre="${SNAP_VERSION_PRERELEASE:-}"
jobs="${JOBS:-$(nproc)}"
launch=()
if [ -n "${SNAP_LAUNCHER:-}" ]; then
    launch=("-DCMAKE_C_COMPILER_LAUNCHER=$SNAP_LAUNCHER" "-DCMAKE_CXX_COMPILER_LAUNCHER=$SNAP_LAUNCHER")
fi
root=$(pwd)
host="$root/build-android-host"

echo "=== the host tools"
cmake -S . -B "$host" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DSNAP_VERSION_PRERELEASE=$pre" "${launch[@]}"
cmake --build "$host" --target file_to_c -j "$jobs"
ninja -C "$host" rsp/aspMain.cpp

for abi in arm64-v8a x86_64; do
    dir="build-android-${abi%-v8a}"
    echo "=== $abi"
    cmake -S . -B "$dir" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
        "-DANDROID_ABI=$abi" -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release \
        "-DSNAP_VERSION_PRERELEASE=$pre" "-DSNAP_ASPMAIN_PREBUILT=$host/rsp/aspMain.cpp" \
        "-DRT64_FILE_TO_C_HOST=$host/lib/rt64/src/tools/file_to_c/file_to_c" "${launch[@]}"
    cmake --build "$dir" --target Snap64Recomp -j "$jobs"
done

echo "=== the APK"
cd android
./gradlew --no-daemon assembleRelease "-PsnapPrerelease=$pre"
ls -l app/build/outputs/apk/release/
