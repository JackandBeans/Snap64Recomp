# Writes the header that names the commit a build is made from. CMakeLists.txt runs
# this at every build, so the name cannot fall behind the tree; the header changes,
# and main.cpp compiles again, only when the name does.
#
# SNAP_BUILD_ID in the environment names it instead, for a copy of the tree with no
# .git, such as the Linux build's mirror. With neither, the name is "unknown".
#
# cmake -DTREE=<source folder> -DOUT=<header> -P build_id.cmake
set(id "$ENV{SNAP_BUILD_ID}")
if ((NOT id) AND EXISTS "${TREE}/.git")
    find_program(SNAP_GIT git)
    if (SNAP_GIT)
        execute_process(COMMAND "${SNAP_GIT}" -C "${TREE}" rev-parse --short=12 HEAD
                        RESULT_VARIABLE failed OUTPUT_VARIABLE id
                        ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
        if (failed)
            set(id "")
        else()
            # A tree with changes of its own is not that commit.
            execute_process(COMMAND "${SNAP_GIT}" -C "${TREE}" diff --quiet HEAD
                            RESULT_VARIABLE changed OUTPUT_QUIET ERROR_QUIET)
            if (changed)
                set(id "${id}-modified")
            endif()
        endif()
    endif()
endif()
if (NOT id)
    set(id "unknown")
endif()
set(text "#define SNAP_BUILD_ID \"${id}\"\n")
set(old "")
if (EXISTS "${OUT}")
    file(READ "${OUT}" old)
endif()
if (NOT old STREQUAL text)
    file(WRITE "${OUT}" "${text}")
endif()
