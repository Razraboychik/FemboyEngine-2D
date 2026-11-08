# fby_atlas - deterministic offline sprite atlas packer.

add_executable(fby_atlas Main.cpp)

target_link_libraries(fby_atlas
    PRIVATE
        FemboyEngine2D::Engine
        fby::compiler_flags)

target_include_directories(fby_atlas PRIVATE
    "${FBY_ENGINE_SOURCE_ROOT}/third_party/stb"
    "${FBY_SOURCE_ROOT}")

set_target_properties(fby_atlas PROPERTIES
    OUTPUT_NAME fby_atlas
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tools")
