# fby_fempak - asset cooker (.fempak).

add_executable(fby_fempak
    Main.cpp
    AudioCook.cpp
    "${FBY_SOURCE_ROOT}/Engine/Assets/AudioClipFormat.cpp"
    "${FBY_SOURCE_ROOT}/Engine/Audio/OggOpus.cpp"
    "${FBY_SOURCE_ROOT}/Engine/Core/SecureRng.cpp")

target_link_libraries(fby_fempak
    PRIVATE
        FemboyEngine2D::AssetLib
        FemboyEngine2D::AssetCook
        fby::compiler_flags)

target_include_directories(fby_fempak PRIVATE
    "${FBY_ENGINE_SOURCE_ROOT}/third_party/stb"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${FBY_SOURCE_ROOT}")

if(FBY_OPUS_TARGET)
    target_link_libraries(fby_fempak PRIVATE ${FBY_OPUS_TARGET})
endif()
if(EXISTS "${FBY_ENGINE_SOURCE_ROOT}/third_party/opus-master/opus-main/include")
    target_include_directories(fby_fempak PRIVATE
        "${FBY_ENGINE_SOURCE_ROOT}/third_party/opus-master/opus-main/include")
endif()

set_target_properties(fby_fempak PROPERTIES
    OUTPUT_NAME fby_fempak
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tools")
