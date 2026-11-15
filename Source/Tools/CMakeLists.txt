# FemboyEngine2D - offline asset pipeline.

# Sanitizer gates build only the runtime and tests. The tools link plain C++
# shader libraries, and mixing those objects with the instrumented engine trips
# lld's /failifmismatch on Windows.
if(FBY_WITH_ASAN OR FBY_WITH_UBSAN)
    return()
endif()

add_subdirectory(AtlasCooker)
add_subdirectory(BookCooker)
add_subdirectory(VideoBench)
add_subdirectory(FempakCooker)
add_subdirectory(FontCompiler)
add_subdirectory(TemplateGen)
add_subdirectory(AotCompiler)
add_subdirectory(LocTool)
add_subdirectory(SceneTool)
add_subdirectory(TextCompiler)
add_subdirectory(MaterialCompiler)
add_subdirectory(VoiceSfxGen)
