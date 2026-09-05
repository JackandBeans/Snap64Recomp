#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
clang++ -std=c++20 -O2 -I "$ROOT/src" -I "$ROOT/lib/N64ModernRuntime/thirdparty" \
    -I "${SNAP_BUILD_DIR:-$ROOT/build-macos}/lib/SDL/include/SDL2" -I "${SNAP_BUILD_DIR:-$ROOT/build-macos}/lib/SDL/include" -I "${SNAP_BUILD_DIR:-$ROOT/build-macos}/lib/SDL/include-config-release/SDL2" \
    "$ROOT/macos/tests/controller_tests.cpp" "$ROOT/src/controller_support.cpp" \
    "${SNAP_BUILD_DIR:-$ROOT/build-macos}/lib/SDL/libSDL2-2.0.0.dylib" -Wl,-rpath,"${SNAP_BUILD_DIR:-$ROOT/build-macos}/lib/SDL" -o "${SNAP_BUILD_DIR:-$ROOT/build-macos}/controller-tests"
"${SNAP_BUILD_DIR:-$ROOT/build-macos}/controller-tests"
