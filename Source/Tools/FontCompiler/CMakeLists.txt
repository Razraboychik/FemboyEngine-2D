# fby_fontc - Pink UI font cooker (.fbyfont).

add_executable(fby_fontc Main.cpp)

target_include_directories(fby_fontc PRIVATE
    "${FBY_ENGINE_SOURCE_ROOT}/third_party/stb"
    "${FBY_SOURCE_ROOT}")

target_link_libraries(fby_fontc PRIVATE fby::compiler_flags)

set_target_properties(fby_fontc PROPERTIES
    OUTPUT_NAME fby_fontc
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tools")
