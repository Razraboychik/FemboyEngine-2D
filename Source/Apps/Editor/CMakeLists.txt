# FemboyEngine2D - editor application.

add_executable(fby_editor_app Main.cpp)

if((FBY_WITH_ASAN OR FBY_WITH_UBSAN) AND NOT WIN32)
    fby_asan_apply(fby_editor_app)
endif()

target_link_libraries(fby_editor_app
    PRIVATE
        FemboyEngine2D::Editor
        fby::compiler_flags)

set_target_properties(fby_editor_app PROPERTIES
    OUTPUT_NAME fby_editor
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}")

# No console window on launch: the editor is a GUI program and writes its log to
# Saved/Logs/editor. Configure with -DFBY_CONSOLE_APPS=ON to keep the console.
fby_make_gui_app(fby_editor_app)
fby_set_app_stack(fby_editor_app)
