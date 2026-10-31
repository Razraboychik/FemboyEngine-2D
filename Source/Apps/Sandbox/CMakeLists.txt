# FemboyEngine2D - sandbox demo.

add_executable(fby_sandbox
    Main.cpp
    PieClient.cpp
    SandboxOptions.cpp)

if(FBY_WITH_ASAN OR FBY_WITH_UBSAN)
    fby_asan_apply(fby_sandbox)
endif()

target_link_libraries(fby_sandbox PRIVATE FemboyEngine2D::Engine)

set_target_properties(fby_sandbox PROPERTIES
    OUTPUT_NAME fby_sandbox
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}")

# Games start without a console window; the frame-time log still lands in
# Saved/Logs. -DFBY_CONSOLE_APPS=ON keeps the console for development.
fby_make_gui_app(fby_sandbox)
