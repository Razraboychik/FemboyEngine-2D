# fby_voice_sfx_gen - deterministic generator for the built-in voice reaction cues.

add_executable(fby_voice_sfx_gen Main.cpp)

target_link_libraries(fby_voice_sfx_gen
    PRIVATE
        FemboyEngine2D::Engine
        fby::compiler_flags)

target_include_directories(fby_voice_sfx_gen PRIVATE "${FBY_SOURCE_ROOT}")

set_target_properties(fby_voice_sfx_gen PROPERTIES
    OUTPUT_NAME fby_voice_sfx_gen
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tools")
