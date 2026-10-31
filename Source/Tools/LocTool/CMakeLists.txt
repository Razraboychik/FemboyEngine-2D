# fby_loc_tool - localization catalogue compiler.

add_executable(fby_loc_tool Main.cpp)

target_link_libraries(fby_loc_tool PRIVATE fby_assetlib)

target_include_directories(fby_loc_tool PRIVATE "${FBY_SOURCE_ROOT}")

set_target_properties(fby_loc_tool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tools")
