# fby_aotc - FemboyScript bytecode -> C++26 -> native plugin.

add_executable(fby_aotc Main.cpp)

target_link_libraries(fby_aotc PRIVATE FemboyEngine2D::Engine fby::compiler_flags)

target_include_directories(fby_aotc PRIVATE "${FBY_SOURCE_ROOT}")

set_target_properties(fby_aotc PROPERTIES
    OUTPUT_NAME fby_aotc
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tools")
