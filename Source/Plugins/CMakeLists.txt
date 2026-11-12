# FemboyEngine2D - plugin ABI fixtures.

function(fby_add_test_plugin target output_name definition)
    add_library(${target} MODULE "${CMAKE_CURRENT_SOURCE_DIR}/HelloMod/HelloMod.cpp")
    target_link_libraries(${target}
        PRIVATE
            fby::compiler_flags)
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


function(fby_add_abi_plugin target output_name source_file)
    add_library(${target} MODULE "${CMAKE_CURRENT_SOURCE_DIR}/${source_file}")
    target_link_libraries(${target} PRIVATE fby::compiler_flags)
    target_include_directories(${target} PRIVATE "${FBY_SOURCE_ROOT}")
    set_target_properties(${target} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${output_name}"
        RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/plugins"
        LIBRARY_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/plugins")
endfunction()

fby_add_abi_plugin(fby_rainbow_palette fby_rainbow_palette RainbowPalette/RainbowPalette.cpp)
fby_add_abi_plugin(fby_themed_palettes fby_themed_palettes ThemedPalettes/ThemedPalettes.cpp)
fby_add_abi_plugin(fby_nvg fby_nvg Nvg/Nvg.cpp)
fby_add_abi_plugin(fby_thermal fby_thermal Thermal/Thermal.cpp)

fby_add_abi_plugin(fby_alcohol fby_alcohol Alcohol/Alcohol.cpp)

fby_add_abi_plugin(fby_voice_pack fby_voice_pack VoicePack/VoicePack.cpp)
