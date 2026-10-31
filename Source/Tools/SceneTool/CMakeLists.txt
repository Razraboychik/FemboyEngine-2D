# fby_scene_tool - scene/prefab compiler and round-trip verifier.

add_executable(fby_scene_tool Main.cpp)

target_link_libraries(fby_scene_tool
    PRIVATE
        FemboyEngine2D::Engine
        fby::compiler_flags)

target_include_directories(fby_scene_tool PRIVATE "${FBY_SOURCE_ROOT}")

set_target_properties(fby_scene_tool PROPERTIES
    OUTPUT_NAME fby_scene_tool
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tools")
