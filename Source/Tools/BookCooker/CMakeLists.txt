# FemboyEngine2D - book cooker tool.

add_executable(fby_bookcook Main.cpp)
target_link_libraries(fby_bookcook
    PRIVATE
        FemboyEngine2D::Engine
        fby::compiler_flags)
set_target_properties(fby_bookcook PROPERTIES
    OUTPUT_NAME fby_bookcook
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}/tools")
