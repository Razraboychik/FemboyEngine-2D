# fby_textc - long-text asset cooker (.fbytext).

add_executable(fby_textc Main.cpp)

target_include_directories(fby_textc PRIVATE "${FBY_SOURCE_ROOT}")

target_link_libraries(fby_textc PRIVATE fby::compiler_flags)

set_target_properties(fby_textc PROPERTIES
    OUTPUT_NAME fby_textc
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tools")
