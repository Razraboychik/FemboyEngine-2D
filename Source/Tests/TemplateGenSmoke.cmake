# fby_template_gen smoke test.
#
# Invoked by ctest with -DTOOL=... -DOUTPUT_DIR=... -DENGINE_ROOT=...
# Verifies that the generator writes a complete, isolated game module.

file(REMOVE_RECURSE "${OUTPUT_DIR}")
execute_process(
    COMMAND "${TOOL}" --name TemplateSmoke --output "${OUTPUT_DIR}" --engine-root "${ENGINE_ROOT}"
    RESULT_VARIABLE Generated
    OUTPUT_VARIABLE GeneratorOutput
    ERROR_VARIABLE GeneratorError)

if(NOT Generated EQUAL 0)
    message(FATAL_ERROR "fby_template_gen failed (${Generated}): ${GeneratorError}")
endif()

set(ScopedProject "${OUTPUT_DIR}/TemplateSmoke")
foreach(Required IN ITEMS CMakeLists.txt GameMain.cpp project.femproj README.md .gitignore)
    if(NOT EXISTS "${ScopedProject}/${Required}")
        message(FATAL_ERROR "fby_template_gen did not write ${Required}")
    endif()
endforeach()
if(NOT EXISTS "${ScopedProject}/assets/scenes/main.toml")
    message(FATAL_ERROR "fby_template_gen did not create assets/scenes/main.toml")
endif()

file(READ "${ScopedProject}/CMakeLists.txt" ScopedCmake)
if(NOT ScopedCmake MATCHES "FemboyEngine2D::Engine")
    message(FATAL_ERROR "generated CMakeLists.txt does not link FemboyEngine2D::Engine")
endif()

file(READ "${ScopedProject}/GameMain.cpp" ScopedGameMain)
if(NOT ScopedGameMain MATCHES "Engine/Core/Definitions.hpp")
    message(FATAL_ERROR "generated GameMain.cpp does not include Definitions.hpp")
endif()
if(NOT ScopedGameMain MATCHES "SystemGraph")
    message(FATAL_ERROR "generated GameMain.cpp is missing the ECS loop")
endif()

message(STATUS "fby_template_gen smoke: ${ScopedProject}")
