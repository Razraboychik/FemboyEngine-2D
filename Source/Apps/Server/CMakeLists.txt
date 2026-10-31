# FemboyEngine2D - dedicated server application.

add_executable(fby_server Main.cpp)

if(FBY_WITH_ASAN OR FBY_WITH_UBSAN)
    fby_asan_apply(fby_server)
endif()
target_link_libraries(fby_server PRIVATE FemboyEngine2D::Engine)
set_target_properties(fby_server PROPERTIES
    OUTPUT_NAME fby_server
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}")
