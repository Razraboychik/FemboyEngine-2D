# fby_matc - MaterialGraph to cooked material compiler.

add_executable(fby_matc Main.cpp)

target_link_libraries(fby_matc
    PRIVATE
        FemboyEngine2D::Engine
        FemboyEngine2D::ShaderCompilerCore
        fby::compiler_flags)

target_include_directories(fby_matc PRIVATE "${FBY_SOURCE_ROOT}")

set_target_properties(fby_matc PROPERTIES
    OUTPUT_NAME fby_matc
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tools")
