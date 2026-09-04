# Copies the linker map for the release archive with the build machine's
# paths replaced, so no absolute path from that machine ships:
#
#   cmake -DINPUT=<map> -DOUTPUT=<map> -DSOURCE_DIR=<repo> -DBINARY_DIR=<build> -P tools/filter_map.cmake
#
# Both the native (backslash) and the forward-slash forms of each prefix are
# replaced; symbols and addresses are untouched, so the crash report
# ([SNAP-AV] lines in snap64.log) can still be read against the copy.
if (NOT INPUT OR NOT OUTPUT OR NOT SOURCE_DIR OR NOT BINARY_DIR)
    message(FATAL_ERROR "filter_map.cmake needs INPUT, OUTPUT, SOURCE_DIR and BINARY_DIR")
endif()
if (NOT EXISTS "${INPUT}")
    message(STATUS "no linker map at ${INPUT}; nothing to filter")
    return()
endif()
file(READ "${INPUT}" MAP_TEXT)
foreach(_pair "${BINARY_DIR}|<build>" "${SOURCE_DIR}|<repo>")
    string(REPLACE "|" ";" _kv "${_pair}")
    list(GET _kv 0 _dir)
    list(GET _kv 1 _name)
    file(TO_NATIVE_PATH "${_dir}" _native)
    string(REPLACE "${_native}" "${_name}" MAP_TEXT "${MAP_TEXT}")
    string(REPLACE "${_dir}" "${_name}" MAP_TEXT "${MAP_TEXT}")
    string(TOLOWER "${_native}" _native_lower)
    string(REPLACE "${_native_lower}" "${_name}" MAP_TEXT "${MAP_TEXT}")
endforeach()
file(WRITE "${OUTPUT}" "${MAP_TEXT}")
