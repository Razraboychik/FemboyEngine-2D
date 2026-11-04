# FemboyEngine2D - offline asset pipeline.

add_subdirectory(FempakCooker)
add_subdirectory(FontCompiler)
add_subdirectory(TemplateGen)
add_subdirectory(AotCompiler)
add_subdirectory(LocTool)
add_subdirectory(SceneTool)
add_subdirectory(TextCompiler)

# The material compiler links the uninstrumented glslang shader stack, so it is
# excluded from sanitizer builds instead of mixing its objects with ASan ones.
if(NOT (FBY_WITH_ASAN OR FBY_WITH_UBSAN))
    add_subdirectory(MaterialCompiler)
endif()

# The pipeline tools link the instrumented engine during a sanitizer build, so
# they are instrumented as well. The shader compiler stays out of the set: it
# links glslang, which is plain C++ and cannot be mixed with sanitized objects
# on the MSVC frontend.
if(FBY_WITH_ASAN OR FBY_WITH_UBSAN)
    foreach(FbyAsanTool fby_aotc fby_fempak fby_fontc fby_loc_tool fby_scene_tool fby_template_gen fby_textc)
        fby_asan_apply(${FbyAsanTool})
    endforeach()
endif()
