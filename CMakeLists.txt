cmake_minimum_required(VERSION 3.20)
project(PokemonSnapRecomp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# =============================================================================
# Options
# =============================================================================
option(BUILD_RT64 "Build RT64 rendering backend" ON)

message(STATUS "PokemonSnapRecomp v${PROJECT_VERSION}")
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
# RT64 â€” static rendering backend
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
# Recompiled functions â€” static library from N64Recomp output
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
    "${SNAP_ASPMAIN_CPP}"
)

# librecomp's RSP vector-unit implementation uses SSSE3 intrinsics.
if (NOT MSVC)
    set_source_files_properties("${SNAP_ASPMAIN_CPP}" PROPERTIES COMPILE_OPTIONS "-msse4.1")
endif()

add_executable(PokemonSnapRecomp ${SNAP_SOURCES})

target_include_directories(PokemonSnapRecomp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/RecompiledFuncs"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/N64ModernRuntime/ultramodern/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/N64ModernRuntime/thirdparty"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/N64ModernRuntime/thirdparty/concurrentqueue"
)

# RT64 include paths (for rt64_application.h, etc.)
if (BUILD_RT64)
    target_include_directories(PokemonSnapRecomp PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/contrib"
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/contrib/hlslpp/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/contrib/plume"
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/contrib/imgui"
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/contrib/mupen64plus-core/src/api"
    )
    target_compile_definitions(PokemonSnapRecomp PRIVATE HAS_RT64=1 HLSL_CPU FFX_GCC)
if (WIN32)
    target_compile_definitions(PokemonSnapRecomp PRIVATE NOMINMAX)
    # Game threads inherit the image default; recompiled call chains need Linux-like depth.
    target_link_options(PokemonSnapRecomp PRIVATE /STACK:8388608 /MAP)
endif()
endif()

# Link libraries. recomp_funcs and librecomp are mutually dependent
# (recomp_funcs references librecomp symbols; librecomp needs recomp_funcs'
# function tables), which GNU ld needs --start-group to resolve. MSVC's
# link.exe resolves cyclic static libraries on its own and reports the GNU
# flags as LNK4044 on every link, so they are only passed where they mean
# something.
target_link_libraries(PokemonSnapRecomp PRIVATE
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
    target_link_options(PokemonSnapRecomp PRIVATE /FORCE:MULTIPLE /IGNORE:4006 /IGNORE:4088)
else()
    target_link_options(PokemonSnapRecomp PRIVATE
        -Wl,--start-group
        -Wl,--end-group
        -Wl,--allow-multiple-definition
    )
endif()

if (BUILD_RT64)
    target_link_libraries(PokemonSnapRecomp PRIVATE rt64)
endif()

# Platform-specific link flags
if (UNIX AND NOT APPLE)
    target_link_libraries(PokemonSnapRecomp PRIVATE
        pthread
        dl
    )
endif()

# Compiler warnings for game code
if (NOT MSVC)
    target_compile_options(PokemonSnapRecomp PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# =============================================================================
# Installation
# =============================================================================
install(TARGETS PokemonSnapRecomp DESTINATION bin)

# =============================================================================
# Summary
# =============================================================================
message(STATUS "RT64 backend: ${BUILD_RT64}")
message(STATUS "Recompiled function sources: ${RECOMP_FUNC_SOURCES}")








