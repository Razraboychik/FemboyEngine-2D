# Toolchain configuration for FemboyEngine2D 0.4.0-alpha.1.

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Supported compilers are the newest validated releases of the 0.4 alpha line.
# The floor is intentionally the current release, not an older compatibility
# version: the engine uses the latest C++26 library surface.
set(FBY_MIN_CLANG "23.1.4")
set(FBY_MIN_GCC "16.2.0")
set(FBY_MIN_MSVC "19.44")
set(FBY_MIN_APPLECLANG "21.0")

set(FBY_IS_MSVC FALSE)
set(FBY_IS_CLANG_CL FALSE)
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(FBY_IS_MSVC TRUE)
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "${FBY_MIN_MSVC}")
        message(FATAL_ERROR
            "FemboyEngine2D requires MSVC ${FBY_MIN_MSVC} or newer; "
            "found MSVC ${CMAKE_CXX_COMPILER_VERSION}.")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "${FBY_MIN_APPLECLANG}")
        message(FATAL_ERROR
            "FemboyEngine2D requires AppleClang ${FBY_MIN_APPLECLANG} or newer; "
            "found AppleClang ${CMAKE_CXX_COMPILER_VERSION}.")
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "${FBY_MIN_CLANG}")
        message(FATAL_ERROR
            "FemboyEngine2D requires Clang ${FBY_MIN_CLANG} or newer; "
            "found Clang ${CMAKE_CXX_COMPILER_VERSION}.")
    endif()
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        set(FBY_IS_CLANG_CL TRUE)
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "${FBY_MIN_GCC}")
        message(FATAL_ERROR
            "FemboyEngine2D requires GCC ${FBY_MIN_GCC} or newer; "
            "found GCC ${CMAKE_CXX_COMPILER_VERSION}.")
    endif()
else()
    message(FATAL_ERROR
        "FemboyEngine2D supports MSVC ${FBY_MIN_MSVC}+, Clang ${FBY_MIN_CLANG}+, "
        "AppleClang ${FBY_MIN_APPLECLANG}+ and GCC ${FBY_MIN_GCC}+; "
        "the active compiler is '${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}'.")
endif()

if(FBY_IS_CLANG_CL OR FBY_IS_MSVC)
    # clang-cl and cl.exe spell the C++26 draft switch the Microsoft way.
    set(FBY_CXX_STANDARD_FLAG "/std:c++latest")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    # AppleClang 21 still spells the C++26 draft as c++2c; c++26 is accepted
    # from the upstream Clang toolchain used on Linux and by the setup scripts.
    set(FBY_CXX_STANDARD_FLAG "-std=c++2c")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(FBY_CXX_STANDARD_FLAG "-std=c++26")
else()
    message(FATAL_ERROR
        "FemboyEngine2D supports MSVC, Clang and GCC only; "
        "the active compiler is '${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}'.")
endif()

add_library(fby_compiler_flags INTERFACE)
add_library(fby::compiler_flags ALIAS fby_compiler_flags)

target_compile_options(fby_compiler_flags INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:${FBY_CXX_STANDARD_FLAG}>)

if(FBY_IS_MSVC)
    target_compile_options(fby_compiler_flags INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:/W4>
        $<$<COMPILE_LANGUAGE:CXX>:/permissive->
        $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>
        $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
        $<$<COMPILE_LANGUAGE:CXX>:/MP>)
elseif(FBY_IS_CLANG_CL)
    target_compile_options(fby_compiler_flags INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:/W4>
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
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(fby_compiler_flags INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall>
        $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
        $<$<COMPILE_LANGUAGE:CXX>:-Wshadow>
        $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>)
else()
    message(FATAL_ERROR
        "FemboyEngine2D supports MSVC, Clang and GCC only; "
        "the active compiler is '${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}'.")
endif()

# Windows SDK headers define min/max macros unless NOMINMAX is set. This must
# apply to every Windows compiler, not only clang-cl.
if(WIN32)
    target_compile_definitions(fby_compiler_flags INTERFACE
        _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
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

# Optional interprocedural optimisation for release measurements.
option(FBY_ENABLE_LTO "Enable link-time optimisation" OFF)
if(FBY_ENABLE_LTO)
    target_compile_options(fby_compiler_flags INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-flto>)
    target_link_options(fby_compiler_flags INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-flto>)
endif()

# Profile-guided builds: generate with one run, then point the use option at the
# resulting profile. Clang uses LLVM's instr-generate/instr-use pair; GCC uses
# the profile-generate/profile-use pair.
option(FBY_PGO_GENERATE "Compile with profile instrumentation" OFF)
set(FBY_PGO_PROFILE "" CACHE STRING "Profile data for the optimized build")
if(FBY_PGO_GENERATE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(fby_compiler_flags INTERFACE
            $<$<COMPILE_LANGUAGE:CXX>:-fprofile-instr-generate>)
        target_link_options(fby_compiler_flags INTERFACE
            $<$<COMPILE_LANGUAGE:CXX>:-fprofile-instr-generate>)
    else()
        target_compile_options(fby_compiler_flags INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-fprofile-generate>)
        target_link_options(fby_compiler_flags INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-fprofile-generate>)
    endif()
endif()
if(FBY_PGO_PROFILE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(fby_compiler_flags INTERFACE
            $<$<COMPILE_LANGUAGE:CXX>:-fprofile-instr-use=${FBY_PGO_PROFILE}>)
        target_link_options(fby_compiler_flags INTERFACE
            $<$<COMPILE_LANGUAGE:CXX>:-fprofile-instr-use=${FBY_PGO_PROFILE}>)
    else()
        target_compile_options(fby_compiler_flags INTERFACE
            $<$<COMPILE_LANGUAGE:CXX>:-fprofile-use=${FBY_PGO_PROFILE}>)
        target_link_options(fby_compiler_flags INTERFACE
            $<$<COMPILE_LANGUAGE:CXX>:-fprofile-use=${FBY_PGO_PROFILE}>)
    endif()
endif()

# Debug builds enable the engine's assertions. Contract violations that a release
# Debug-only protection switches. Both default to ON in Debug and compile out
# completely when OFF or when the configuration is not Debug.
option(FBY_DEBUG_ACCESS_VALIDATION "Validate declared SystemAccess against actual ECS access" ON)
option(FBY_DEBUG_SYSTEM_TIMING "Record per-system SystemGraph timings" ON)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(fby_compiler_flags INTERFACE FBY_ENABLE_ASSERTS=1)
    if(FBY_DEBUG_ACCESS_VALIDATION)
        target_compile_definitions(fby_compiler_flags INTERFACE FBY_ENABLE_ACCESS_VALIDATION=1)
    endif()
    if(FBY_DEBUG_SYSTEM_TIMING)
        target_compile_definitions(fby_compiler_flags INTERFACE FBY_ENABLE_SYSTEM_TIMING=1)
    endif()
endif()

# Feature probes

function(fby_try_compile_probe name source out_var)
    set(source_file "${CMAKE_BINARY_DIR}/probe_${name}.cpp")
    file(WRITE "${source_file}" "${source}")

    if(FBY_IS_CLANG_CL OR FBY_IS_MSVC)
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" /nologo /std:c++latest /EHsc /c "${source_file}"
                    "/Fo${CMAKE_BINARY_DIR}/probe_${name}.obj"
            RESULT_VARIABLE result
            OUTPUT_QUIET
            ERROR_QUIET)
    else()
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" ${FBY_CXX_STANDARD_FLAG} -fsyntax-only "${source_file}"
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
            "FemboyEngine2D ${FBY_VERSION_STRING} requires a C++26 toolchain, but '${name}' does not compile.\n"
            "Probe source:\n${source}\n"
            "Use MSVC ${FBY_MIN_MSVC}+, Clang ${FBY_MIN_CLANG}+ or GCC ${FBY_MIN_GCC}+, "
            "configured from a shell where the compiler can find the platform headers "
            "and Windows SDK libraries.")
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
