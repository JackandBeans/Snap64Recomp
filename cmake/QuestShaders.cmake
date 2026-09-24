# Compile the accessory shaders on the host; no runtime compiler on Quest.
set(SNAP_QUEST_SHADER_OUTPUTS)
foreach(_shader props presentation copy)
    foreach(_stage vs ps)
        set(_out "${CMAKE_CURRENT_BINARY_DIR}/quest-shaders/${_shader}.${_stage}.spv")
        set(_options)
        if(_stage STREQUAL "vs")
            list(APPEND _options -fvk-invert-y)
        endif()
        add_custom_command(OUTPUT "${_out}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/quest-shaders"
            COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxc.exe"
                -spirv -fspv-target-env=vulkan1.1 -fvk-use-dx-layout ${_options}
                -T ${_stage}_6_0 -E ${_stage}
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/quest/${_shader}.hlsl" -Fo "${_out}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/quest/${_shader}.hlsl" VERBATIM)
        list(APPEND SNAP_QUEST_SHADER_OUTPUTS "${_out}")
    endforeach()
endforeach()
add_custom_target(quest_shaders DEPENDS ${SNAP_QUEST_SHADER_OUTPUTS})
add_dependencies(Snap64Recomp quest_shaders)
