# Toolchain configuration for FemboyEngine 2.0.

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(FBY_IS_CLANG_CL FALSE)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(FBY_IS_CLANG_CL TRUE)
endif()

if(FBY_IS_CLANG_CL)
# clang-cl speaks the clang driver, but the standard switch is spelled the
    set(FBY_CXX_STANDARD_FLAG "/std:c++latest")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(FBY_CXX_STANDARD_FLAG "-std=c++26")
else()
    set(FBY_CXX_STANDARD_FLAG "/std:c++latest")
endif()

add_library(fby_compiler_flags INTERFACE)
add_library(fby::compiler_flags ALIAS fby_compiler_flags)

target_compile_options(fby_compiler_flags INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:${FBY_CXX_STANDARD_FLAG}>
    $<$<COMPILE_LANGUAGE:CXX>:$<IF:$<BOOL:${FBY_IS_CLANG_CL}>,/W4,-Wall>>
    $<$<COMPILE_LANGUAGE:CXX>:$<IF:$<BOOL:${FBY_IS_CLANG_CL}>,-Wno-unused-parameter,-Wextra>>
    $<$<COMPILE_LANGUAGE:CXX>:-Wshadow>)

if(FBY_IS_CLANG_CL)
    target_compile_options(fby_compiler_flags INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-microsoft-enum-value>
# clang-cl enables the C++98 compatibility group through its MSVC
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-c++98-compat>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-c++98-compat-pedantic>
# The engine is intentionally pointer-heavy (mmap spans, GPU buffers);
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-unsafe-buffer-usage>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-unsafe-buffer-usage-in-libc-call>
# Layout/style diagnostics that do not describe a defect in this code.
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-padded>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-ctad-maybe-unsupported>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-nrvo>)
    target_compile_definitions(fby_compiler_flags INTERFACE
        _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(fby_compiler_flags INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>)
else()
    target_compile_options(fby_compiler_flags INTERFACE /utf-8 /MP)
endif()

# Optimisation is per configuration and belongs to engine targets only.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(fby_compiler_flags INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:-O3>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-math-errno>)
elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    target_compile_options(fby_compiler_flags INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:-O2>
        $<$<COMPILE_LANGUAGE:CXX>:-g>)
endif()

# Debug builds enable the engine's assertions. Contract violations that a release
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(fby_compiler_flags INTERFACE FBY_ENABLE_ASSERTS=1)
endif()

# Feature probes

function(fby_try_compile_probe name source out_var)
    set(source_file "${CMAKE_BINARY_DIR}/probe_${name}.cpp")
    file(WRITE "${source_file}" "${source}")

    if(FBY_IS_CLANG_CL)
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" /nologo /std:c++latest /EHsc /c "${source_file}"
                    "/Fo${CMAKE_BINARY_DIR}/probe_${name}.obj"
            RESULT_VARIABLE result
            OUTPUT_QUIET
            ERROR_QUIET)
    else()
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" -std=c++26 -fsyntax-only "${source_file}"
            RESULT_VARIABLE result
            OUTPUT_QUIET
            ERROR_QUIET)
    endif()

    if(result EQUAL 0)
        set(${out_var} TRUE PARENT_SCOPE)
    else()
        set(${out_var} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(fby_probe_feature name source)
    fby_try_compile_probe(${name} "${source}" ok)
    if(NOT ok)
        message(FATAL_ERROR
            "FemboyEngine2D 0.1.0-alpha.1 requires a C++26 toolchain, but '${name}' does not compile.\n"
            "Probe source:\n${source}\n"
            "Use Clang 19+ or GCC 15+, configured from a shell where the compiler can find "
            "the platform headers and Windows SDK libraries.")
    endif()
    set(${name} TRUE CACHE INTERNAL "")
endfunction()

function(fby_probe_optional name source)
    fby_try_compile_probe(${name} "${source}" ok)
    if(ok)
        message(STATUS "probe ${name}: available")
        set(${name} TRUE CACHE INTERNAL "")
    else()
        message(STATUS "probe ${name}: not available (engine fallback is used)")
        set(${name} FALSE CACHE INTERNAL "")
    endif()
endfunction()

fby_probe_feature(FBY_HAS_MDSPAN [[
# include <mdspan>
    int main() {
        int data[6]{};
        std::mdspan<int, std::extents<std::size_t, 2, 3>> grid(data);
        return static_cast<int>(grid.extent(0) + grid.extent(1)) - 5;
    }
]])

fby_probe_feature(FBY_HAS_FORMAT [[
# include <format>
    int main() { return static_cast<int>(std::format("{}", 42).size()) - 2; }
]])

fby_probe_feature(FBY_HAS_JTHREAD [[
# include <thread>
    int main() {
        std::jthread worker([](std::stop_token token) { (void)token; });
        return 0;
    }
]])

fby_probe_feature(FBY_HAS_EXPECTED [[
# include <expected>
    int main() { std::expected<int, int> value = 7; return value.value() - 7; }
]])

fby_probe_optional(FBY_HAS_STD_FLAT_MAP [[
# include <flat_map>
    int main() { std::flat_map<int, int> map; map.insert({1, 2}); return 0; }
]])

fby_probe_optional(FBY_HAS_STD_SIMD [[
# include <simd>
    int main() {
        using V = std::simd<float, 4>;
        V value(1.0f);
        return static_cast<int>(value[0]) - 1;
    }
]])

fby_probe_optional(FBY_HAS_STATIC_REFLECTION [[
# include <meta>
    int main() { return 0; }
]])
