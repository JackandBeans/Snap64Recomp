cmake_minimum_required(VERSION 3.20)
project(Snap64Recomp VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# =============================================================================
# Version -- the one place it lives
# =============================================================================
# project(VERSION) takes numbers only, so the prerelease tag is its own
# variable. SNAP_VERSION_FULL is what every surface shows: the title bar,
# the log banner, the credits line, the exe's version resource and the
# package name (all through the generated version.h / snap64.rc, below).
# An empty SNAP_VERSION_PRERELEASE is a final release.
set(SNAP_PRODUCT_NAME "Snap64 Recomp")
set(SNAP_VERSION_PRERELEASE "rc1")
if (SNAP_VERSION_PRERELEASE)
    set(SNAP_VERSION_SUFFIX "-${SNAP_VERSION_PRERELEASE}")
    set(SNAP_RC_FILEFLAGS "VS_FF_PRERELEASE")
else()
    set(SNAP_VERSION_SUFFIX "")
    set(SNAP_RC_FILEFLAGS "0x0L")
endif()
set(SNAP_VERSION_FULL "${PROJECT_VERSION}${SNAP_VERSION_SUFFIX}")
# The title screen's credits face has no hyphen (src/version.h.in).
string(REPLACE "-" " " SNAP_VERSION_CREDITS "${SNAP_VERSION_FULL}")
if (MSVC)
    enable_language(RC)
endif()

# =============================================================================
# Options
# =============================================================================
option(BUILD_RT64 "Build RT64 rendering backend" ON)

message(STATUS "${SNAP_PRODUCT_NAME} ${SNAP_VERSION_FULL}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# =============================================================================
# Find dependencies
# =============================================================================
if (WIN32)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    add_subdirectory(lib/SDL EXCLUDE_FROM_ALL)
    set(SDL2_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/lib/SDL/include")
    set(SDL2_LIBRARIES SDL2)
else()
    find_package(SDL2 REQUIRED)
endif()

# =============================================================================
# RT64 -- static rendering backend
# =============================================================================
if (BUILD_RT64)
    set(RT64_STATIC ON CACHE BOOL "Build RT64 as a static library" FORCE)
    set(RT64_SDL_WINDOW_VULKAN ON CACHE BOOL "RT64 uses SDL Vulkan window" FORCE)
    add_subdirectory(lib/rt64 EXCLUDE_FROM_ALL)
    if (WIN32)
        # SDK 22621 lacks D3D12_HEAP_TYPE_GPU_UPLOAD / OPTIONS15 / OPTIONS16;
        # the standalone DirectX-Headers provide current definitions.
        set(DXHEADERS "${CMAKE_CURRENT_SOURCE_DIR}/lib/DirectX-Headers/include")
        foreach(_dxtarget plume rt64)
            if (TARGET ${_dxtarget})
                target_include_directories(${_dxtarget} BEFORE PRIVATE "${DXHEADERS}/directx" "${DXHEADERS}")
            endif()
        endforeach()
    endif()
endif()

# =============================================================================
# N64ModernRuntime (ultramodern + librecomp)
# =============================================================================
add_subdirectory(lib/N64ModernRuntime EXCLUDE_FROM_ALL)

# =============================================================================
# Recompiled functions -- static library from N64Recomp output
# =============================================================================
file(GLOB RECOMP_FUNC_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/RecompiledFuncs/funcs_*.c"
)

add_library(recomp_funcs STATIC
    ${RECOMP_FUNC_SOURCES}
    
)

target_include_directories(recomp_funcs PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/RecompiledFuncs"
)

# recomp_funcs needs access to recomp.h (for recomp_context, gpr, etc.)
target_include_directories(recomp_funcs PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include"
)

# Suppress warnings in generated recompiled code.
if (MSVC)
    # /O2, not /O1: this target is the whole recompiled game and is the
    # hottest code in the build, so favour speed over size.
    target_compile_options(recomp_funcs PRIVATE /w /bigobj /O2)
else()
target_compile_options(recomp_funcs PRIVATE
    -g
    -Wno-unused-variable
    -Wno-unused-parameter
    -Wno-sign-compare
    -Wno-unused-but-set-variable
    -Wno-implicit-function-declaration
)
endif()

# =============================================================================
# Audio microcode -- recompiled from the ROM at build time
# =============================================================================
# aspMain is Nintendo's RSP audio microcode. Its recompiled C++ is derived
# from the ROM and is therefore never committed: RSPRecomp (vendored under
# lib/N64ModernRuntime/N64Recomp and already a target of this build through
# librecomp's add_subdirectory, EXCLUDE_FROM_ALL) regenerates it into the
# build tree from the builder's own ROM -- the same ROM the executable runs
# against. The DEPENDS below is what makes the build compile RSPRecomp first.
set(SNAP_ROM "" CACHE FILEPATH
    "Pokemon Snap (USA) ROM, big-endian .z64, used to recompile the RSP audio microcode")
if (SNAP_ROM STREQUAL "")
    foreach(_snap_rom_candidate
            "${CMAKE_CURRENT_SOURCE_DIR}/pokemonsnap.z64"
            "${CMAKE_CURRENT_BINARY_DIR}/Release/pokemonsnap.z64"
            "${CMAKE_CURRENT_BINARY_DIR}/pokemonsnap.z64")
        if (EXISTS "${_snap_rom_candidate}")
            set(SNAP_ROM "${_snap_rom_candidate}" CACHE FILEPATH
                "Pokemon Snap (USA) ROM, big-endian .z64, used to recompile the RSP audio microcode" FORCE)
            break()
        endif()
    endforeach()
endif()
if (NOT EXISTS "${SNAP_ROM}")
    message(FATAL_ERROR
        "SNAP_ROM: no ROM found. The RSP audio microcode (aspMain) is recompiled from "
        "the game's ROM at build time and is not part of the repository. Pass "
        "-DSNAP_ROM=<path to pokemonsnap.z64> (a big-endian .z64 dump of Pokemon Snap (USA)), "
        "or place pokemonsnap.z64 in ${CMAKE_CURRENT_SOURCE_DIR}, "
        "${CMAKE_CURRENT_BINARY_DIR}/Release or ${CMAKE_CURRENT_BINARY_DIR}.")
endif()
# Two cheap checks on the file itself; the runtime's hash check (src/main.cpp)
# remains the authority on the game.
file(READ "${SNAP_ROM}" _snap_rom_magic LIMIT 4 HEX)
file(READ "${SNAP_ROM}" _snap_rom_name OFFSET 32 LIMIT 20 HEX)
string(TOLOWER "${_snap_rom_magic}" _snap_rom_magic)
string(TOLOWER "${_snap_rom_name}" _snap_rom_name)
if (NOT _snap_rom_magic STREQUAL "80371240")
    message(FATAL_ERROR
        "SNAP_ROM=${SNAP_ROM} does not start with the big-endian N64 header 80 37 12 40; "
        "a byte-swapped (.v64) or little-endian (.n64) dump cannot be recompiled. Convert it to .z64 first.")
endif()
# "POKEMON SNAP" padded with spaces to the header's twenty-byte name field.
if (NOT _snap_rom_name STREQUAL "504f4b454d4f4e20534e41502020202020202020")
    message(FATAL_ERROR
        "SNAP_ROM=${SNAP_ROM}: the ROM header's internal name is not 'POKEMON SNAP'; this is not the expected game.")
endif()
message(STATUS "RSP audio microcode from ROM: ${SNAP_ROM}")

set(SNAP_RSP_DIR "${CMAKE_CURRENT_BINARY_DIR}/rsp")
set(SNAP_ASPMAIN_CPP "${SNAP_RSP_DIR}/aspMain.cpp")
# Absolute, forward-slashed paths: RSPRecomp joins a relative path with the
# toml's own directory and uses an absolute one as is, and a TOML basic string
# takes forward slashes without escaping.
file(TO_CMAKE_PATH "${SNAP_ROM}" SNAP_ROM_CMAKE)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/rsp/aspMain.us.toml.in"
               "${SNAP_RSP_DIR}/aspMain.us.toml" @ONLY)
add_custom_command(
    OUTPUT "${SNAP_ASPMAIN_CPP}"
    COMMAND RSPRecomp "${SNAP_RSP_DIR}/aspMain.us.toml"
    DEPENDS RSPRecomp "${SNAP_RSP_DIR}/aspMain.us.toml" "${SNAP_ROM}"
    COMMENT "Recompiling the aspMain audio microcode from ${SNAP_ROM}"
    VERBATIM)

# =============================================================================
# Game executable
# =============================================================================
# Game-side patches, recompiled from patches/ by N64Recomp (see patches.toml).
# Compiled straight into the executable rather than into a library, so these
# definitions are already resolved by the time the linker reaches
# recomp_funcs and the game's own versions are never pulled in.
set(SNAP_PATCH_SOURCES)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/RecompiledPatches/patches.c")
    set(SNAP_PATCH_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/RecompiledPatches/patches.c")
    message(STATUS "Game patches: RecompiledPatches/patches.c")
    # Same treatment the recompiled game gets: it is generated MIPS-derived C.
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/RecompiledPatches/patches.c"
        # /Oi-: the game has functions named sinf and cosf, which MSVC would
        # otherwise replace with its math intrinsics and then reject for
        # taking the recompiler's two arguments.
        PROPERTIES COMPILE_OPTIONS "$<IF:$<BOOL:${MSVC}>,/w;/O2;/Oi-,-w>")
endif()

set(SNAP_SOURCES
    ${SNAP_PATCH_SOURCES}
    src/main.cpp
    src/paths.cpp
    src/rt64_render_context.cpp
    src/rsp_microcode.cpp
    src/audio.cpp
    src/input.cpp
    src/os_stubs.cpp
    src/overlay_hook.cpp
    src/matrix_tags.cpp
    src/rect_tags.cpp
    src/rect_census.cpp
    src/fx_tags.cpp
    src/menu_assets.cpp
    src/menu_harvest.cpp
    src/anim_steps.cpp
    src/focus_dot.cpp
    src/dl_budget.cpp
    src/frame_cost.cpp
    src/sfx_volume_probe.cpp
    src/score_probe.cpp
    src/intro_probe.cpp
    src/patch_shims.cpp
    src/settings.cpp
    src/photo_export.cpp
    "${SNAP_ASPMAIN_CPP}"
)

# librecomp's RSP vector-unit implementation uses SSSE3 intrinsics.
if (NOT MSVC)
    set_source_files_properties("${SNAP_ASPMAIN_CPP}" PROPERTIES COMPILE_OPTIONS "-msse4.1")
endif()

# The identity header and the Windows version resource are written from
# src/version.h.in and src/snap64.rc.in with the numbers above; the sources
# include "version.h" and find the generated one because src/version.h no
# longer exists.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/generated/version.h" @ONLY)
if (MSVC)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/snap64.rc.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/generated/snap64.rc" @ONLY)
    list(APPEND SNAP_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/generated/snap64.rc")
endif()

add_executable(Snap64Recomp ${SNAP_SOURCES})

target_include_directories(Snap64Recomp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    "${CMAKE_CURRENT_SOURCE_DIR}/RecompiledFuncs"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/N64ModernRuntime/ultramodern/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/N64ModernRuntime/thirdparty"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/N64ModernRuntime/thirdparty/concurrentqueue"
)

# RT64 include paths (for rt64_application.h, etc.)
if (BUILD_RT64)
    target_include_directories(Snap64Recomp PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/contrib"
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/contrib/hlslpp/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/contrib/plume"
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/contrib/imgui"
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/contrib/mupen64plus-core/src/api"
    )
    target_compile_definitions(Snap64Recomp PRIVATE HAS_RT64=1 HLSL_CPU FFX_GCC)
if (WIN32)
    target_compile_definitions(Snap64Recomp PRIVATE NOMINMAX)
    # Game threads inherit the image default; recompiled call chains need Linux-like depth.
    target_link_options(Snap64Recomp PRIVATE /STACK:8388608 /MAP)
endif()
endif()

# Link libraries. recomp_funcs and librecomp are mutually dependent
# (recomp_funcs references librecomp symbols; librecomp needs recomp_funcs'
# function tables), which GNU ld needs --start-group to resolve. MSVC's
# link.exe resolves cyclic static libraries on its own and reports the GNU
# flags as LNK4044 on every link, so they are only passed where they mean
# something.
target_link_libraries(Snap64Recomp PRIVATE
    recomp_funcs
    librecomp
    ultramodern
    SDL2::SDL2
)

if (MSVC)
    # A patch defines a function the recompiled game also defines. Object
    # files are linked before libraries and the first definition wins, so the
    # patch in RecompiledPatches takes precedence over recomp_funcs; the
    # linker still has to be told the duplicate is intentional. This replaces
    # the --allow-multiple-definition that serves the same purpose under GNU
    # ld, which also covers symbols such as guMtxF2L appearing in several
    # overlay sections.
    target_link_options(Snap64Recomp PRIVATE /FORCE:MULTIPLE /IGNORE:4006 /IGNORE:4088)
else()
    target_link_options(Snap64Recomp PRIVATE
        -Wl,--start-group
        -Wl,--end-group
        -Wl,--allow-multiple-definition
    )
endif()

if (BUILD_RT64)
    target_link_libraries(Snap64Recomp PRIVATE rt64)
endif()

# Platform-specific link flags
if (UNIX AND NOT APPLE)
    target_link_libraries(Snap64Recomp PRIVATE
        pthread
        dl
    )
endif()

# Compiler warnings for game code
if (NOT MSVC)
    target_compile_options(Snap64Recomp PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# =============================================================================
# Runtime files next to the executable
# =============================================================================
# A fresh build directory must run without hand-copying (the copies in
# build-win/Release were placed by hand). SDL2.dll is the SDL target's own
# output; the DXC pair is what rt64 loads at run time, taken from the same
# place lib/rt64/CMakeLists.txt copies it into rt64's own binary dir (an
# ignored tree -- see VENDORING.md); the PNG is the title screen's "Recomp"
# badge (menu_assets.cpp). copy_if_different takes several sources when the
# last argument is a directory.
if (WIN32)
    set(SNAP_DXC_BIN "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64")
    set(SNAP_RUNTIME_DLLS "$<TARGET_FILE:SDL2>")
    if (BUILD_RT64)
        list(APPEND SNAP_RUNTIME_DLLS "${SNAP_DXC_BIN}/dxcompiler.dll" "${SNAP_DXC_BIN}/dxil.dll")
    endif()
    add_custom_command(TARGET Snap64Recomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SNAP_RUNTIME_DLLS} "$<TARGET_FILE_DIR:Snap64Recomp>"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Snap64Recomp>/menu_text"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/menu_text/recomp_logo.png"
            "$<TARGET_FILE_DIR:Snap64Recomp>/menu_text/recomp_logo.png"
        COMMENT "Placing the runtime DLLs and menu_text/ next to Snap64Recomp.exe"
        VERBATIM)
endif()

# =============================================================================
# Installation and packaging: one flat, portable folder (README "Where things live")
# =============================================================================
# The package is the Windows one (cpack -C Release in the build directory
# writes Snap64Recomp-<version>-win64.zip); the licence list below names the
# trees the Windows build compiles.
install(TARGETS Snap64Recomp RUNTIME DESTINATION .)
if (WIN32)
    install(FILES ${SNAP_RUNTIME_DLLS} DESTINATION .)
    # The /MAP output. The [SNAP-AV] crash report (main.cpp) prints image RVAs
    # that are symbolised against this file; a user's crash log is unreadable
    # without it.
    install(FILES "$<TARGET_FILE_DIR:Snap64Recomp>/$<TARGET_FILE_BASE_NAME:Snap64Recomp>.map"
            DESTINATION . OPTIONAL)
endif()
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/menu_text/recomp_logo.png" DESTINATION menu_text)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    DESTINATION .)

# Third-party licence texts, copied from the vendored trees at install time so
# they can never drift from the code that was built. name|path pairs; the
# name becomes licenses/<name>.txt. NOTICE.md lists the same components.
set(SNAP_THIRD_PARTY_LICENSES
    "SDL2|lib/SDL/LICENSE.txt"
    "RT64|lib/rt64/LICENSE"
    "N64ModernRuntime|lib/N64ModernRuntime/COPYING"
    "N64Recomp|lib/N64ModernRuntime/N64Recomp/LICENSE"
    "DirectX-Headers|lib/DirectX-Headers/LICENSE"
    "plume|lib/rt64/src/contrib/plume/LICENSE"
    "re-spirv|lib/rt64/src/contrib/re-spirv/LICENSE"
    "zstd|lib/rt64/src/contrib/zstd/LICENSE"
    "nativefiledialog-extended|lib/rt64/src/contrib/nativefiledialog-extended/LICENSE"
    "imgui|lib/rt64/src/contrib/imgui/LICENSE.txt"
    "im3d|lib/rt64/src/contrib/im3d/LICENSE"
    "implot|lib/rt64/src/contrib/implot/LICENSE"
    "hlslpp|lib/rt64/src/contrib/hlslpp/LICENSE"
    "ddspp|lib/rt64/src/contrib/ddspp/LICENSE"
    "stb|lib/rt64/src/contrib/stb/LICENSE"
    "xxHash|lib/rt64/src/contrib/xxHash/LICENSE"
    "mupen64plus-core|lib/rt64/src/contrib/mupen64plus-core/LICENSES"
    "miniz|lib/N64ModernRuntime/thirdparty/miniz/LICENSE"
    "o1heap|lib/N64ModernRuntime/thirdparty/o1heap/LICENSE"
    "sljit|lib/N64ModernRuntime/N64Recomp/lib/sljit/LICENSE"
    "fmt|lib/N64ModernRuntime/N64Recomp/lib/fmt/LICENSE"
    "rabbitizer|lib/N64ModernRuntime/N64Recomp/lib/rabbitizer/LICENSE"
    "tomlplusplus|lib/N64ModernRuntime/N64Recomp/lib/tomlplusplus/LICENSE"
)
foreach(_entry IN LISTS SNAP_THIRD_PARTY_LICENSES)
    string(REPLACE "|" ";" _pair "${_entry}")
    list(GET _pair 0 _name)
    list(GET _pair 1 _file)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${_file}" DESTINATION licenses RENAME "${_name}.txt")
endforeach()
# concurrentqueue ships its licence only as the header's leading comment
# (everything before "#pragma once"); cut that out at configure time.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/lib/N64ModernRuntime/thirdparty/concurrentqueue/concurrentqueue.h" _cq_head LIMIT 8192)
string(FIND "${_cq_head}" "#pragma once" _cq_end)
if (_cq_end LESS 1)
    message(FATAL_ERROR "concurrentqueue.h: no '#pragma once' in the first 8 KB; its licence comment could not be cut out")
endif()
string(SUBSTRING "${_cq_head}" 0 ${_cq_end} _cq_license)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/licenses/concurrentqueue.txt" "${_cq_license}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/licenses/concurrentqueue.txt" DESTINATION licenses)
# Tracked in the repository because the vendored copies carry no licence file:
# nlohmann/json has only an SPDX header, and the DXC binaries ship without
# their licence texts (BUILDING.md, step 13). DirectXShaderCompiler.txt is the
# LLVM Release License of the compiler and headers; DirectXShaderCompiler-dxil.txt
# is Microsoft's LICENSE-MS.txt of release v1.7.2308, the terms dxil.dll is
# shipped under (NOTICE.md, VENDORING.md "dxc"). A package without them is not
# produced.
foreach(_lic IN ITEMS DirectXShaderCompiler.txt DirectXShaderCompiler-dxil.txt)
    if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/licenses/${_lic}")
        message(WARNING "licenses/${_lic} is missing; cpack stops until it is added (BUILDING.md, step 13)")
    endif()
endforeach()
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/nlohmann-json.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/DirectXShaderCompiler.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/DirectXShaderCompiler-dxil.txt"
    DESTINATION licenses)

# cpack -C Release (from the build directory) -> Snap64Recomp-<version>-win64.zip
# with a top-level folder of the same name, and a .sha256 beside it.
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "Snap64Recomp")
set(CPACK_PACKAGE_VERSION "${SNAP_VERSION_FULL}")
set(CPACK_PACKAGE_FILE_NAME "Snap64Recomp-${SNAP_VERSION_FULL}-win64")
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_SOURCE_GENERATOR "")
include(CPack)

# =============================================================================
# Summary
# =============================================================================
message(STATUS "RT64 backend: ${BUILD_RT64}")
message(STATUS "Recompiled function sources: ${RECOMP_FUNC_SOURCES}")








