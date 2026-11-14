# fby_matc - MaterialGraph to cooked material compiler.

add_executable(fby_matc Main.cpp)

target_link_libraries(fby_matc
    PRIVATE
        FemboyEngine2D::Engine
        FemboyEngine2D::ShaderCompilerCore
        fby::compiler_flags)

target_include_directories(fby_matc PRIVATE "${FBY_SOURCE_ROOT}")

set_target_properties(fby_matc PROPERTIES
    OUTPUT_NAME fby_matc
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tools")

# SPIRV-Cross is an offline cook dependency: it is linked into fby_matc only,
# never into FemboyEngine2D. SetupScripts downloads it next to third_party.
set(FBY_SPIRV_CROSS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/spirv-cross"
    CACHE PATH "SPIRV-Cross source root (offline material cooker)")
if(EXISTS "${FBY_SPIRV_CROSS_ROOT}/CMakeLists.txt")
    set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_GLSL ON CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)
    add_subdirectory("${FBY_SPIRV_CROSS_ROOT}" "${CMAKE_BINARY_DIR}/spirv-cross")
    target_link_libraries(fby_matc PRIVATE spirv-cross-hlsl)
    target_include_directories(fby_matc PRIVATE "${FBY_SPIRV_CROSS_ROOT}")
    target_compile_definitions(fby_matc PRIVATE FBY_WITH_SPIRV_CROSS=1)
    message(STATUS "fby_matc: SPIRV-Cross HLSL enabled")
else()
    message(STATUS "fby_matc: SPIRV-Cross not found; DX HLSL cooking disabled")
endif()
