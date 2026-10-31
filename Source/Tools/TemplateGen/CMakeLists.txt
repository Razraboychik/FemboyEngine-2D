# fby_template_gen - isolated game project generator.

add_executable(fby_template_gen Main.cpp)

target_link_libraries(fby_template_gen PRIVATE fby::compiler_flags)

set_target_properties(fby_template_gen PROPERTIES
    OUTPUT_NAME fby_template_gen
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tools")
