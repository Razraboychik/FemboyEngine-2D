# FemboyEngine2D - plugin ABI fixtures.

function(fby_add_test_plugin target output_name definition)
    add_library(${target} MODULE "${CMAKE_CURRENT_SOURCE_DIR}/HelloMod/HelloMod.cpp")
    target_link_libraries(${target}
        PRIVATE
            fby::compiler_flags
            fby::entt)
    target_include_directories(${target} PRIVATE
        "${FBY_SOURCE_ROOT}")
    target_compile_definitions(${target} PRIVATE ${definition})
    set_target_properties(${target} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${output_name}"
        RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/plugins"
        LIBRARY_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/plugins")
endfunction()

fby_add_test_plugin(fby_hello_mod fby_hello_mod FBY_PLUGIN_GOOD_LAYOUT)
fby_add_test_plugin(fby_hello_mod_bad fby_hello_mod_bad FBY_PLUGIN_BAD_LAYOUT)
