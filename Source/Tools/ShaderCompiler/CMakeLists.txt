# FemboyEngine2D - shader compiler.

add_library(fby_shaderc_core STATIC ShaderCompiler.cpp)

add_library(FemboyEngine2D::ShaderCompilerCore ALIAS fby_shaderc_core)

# Vendored glslang is C++17. The engine-wide C++26 mode is not applied to these
target_compile_features(fby_shaderc_core PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(fby_shaderc_core PUBLIC /EHsc)
else()
    target_compile_options(fby_shaderc_core PUBLIC -fexceptions)
endif()

target_link_libraries(fby_shaderc_core PUBLIC
    glslang
    SPIRV
    glslang-default-resource-limits)

# The Source include root exposes "Tools/ShaderCompiler/ShaderCompiler.hpp".
target_include_directories(fby_shaderc_core PUBLIC
    "${FBY_SOURCE_ROOT}"
    "${glslang_SOURCE_DIR}"
    "${glslang_BINARY_DIR}")

set_target_properties(fby_shaderc_core PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/lib")

add_executable(fby_shaderc Main.cpp)
target_compile_features(fby_shaderc PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(fby_shaderc PRIVATE /EHsc)
else()
    target_compile_options(fby_shaderc PRIVATE -fexceptions)
endif()
target_link_libraries(fby_shaderc PRIVATE fby_shaderc_core)

set_target_properties(fby_shaderc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tools")
