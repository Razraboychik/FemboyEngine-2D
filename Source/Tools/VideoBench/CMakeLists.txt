# FemboyEngine2D - AV1 decode benchmark tool.

add_executable(fby_video_bench Main.cpp)
target_link_libraries(fby_video_bench
    PRIVATE
        FemboyEngine2D::Engine
        fby::compiler_flags)
set_target_properties(fby_video_bench PROPERTIES
    OUTPUT_NAME fby_video_bench
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tools")
if(WIN32 AND EXISTS "${FBY_DAV1D_ROOT}/bin/dav1d.dll")
    add_custom_command(TARGET fby_video_bench POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${FBY_DAV1D_ROOT}/bin/dav1d.dll"
                "${FBY_OUTPUT_DIR}/tools/dav1d.dll")
endif()
