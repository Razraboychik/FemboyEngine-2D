# FemboyEngine2D - storefront provider shared libraries.

function(fby_add_platform_provider target source)
    add_library(${target} SHARED "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${FBY_SOURCE_ROOT}")
    target_link_libraries(${target} PRIVATE fby::compiler_flags)
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME ${target}
        RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}")
    if(UNIX AND NOT APPLE)
        target_link_libraries(${target} PRIVATE dl)
    endif()
endfunction()

fby_add_platform_provider(fby_platform_steam SteamProvider.cpp)
fby_add_platform_provider(fby_platform_egs EgsProvider.cpp)
fby_add_platform_provider(fby_platform_gog GogProvider.cpp)

if(FBY_EOS_SDK_INCLUDE_DIR)
    target_include_directories(fby_platform_egs PRIVATE "${FBY_EOS_SDK_INCLUDE_DIR}")
    target_compile_definitions(fby_platform_egs PRIVATE FBY_HAS_EOS_SDK=1)
endif()
