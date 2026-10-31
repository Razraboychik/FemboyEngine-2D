# FemboyEngine2D - lobby service application.

add_executable(fby_lobby Main.cpp)

if(FBY_WITH_ASAN OR FBY_WITH_UBSAN)
    fby_asan_apply(fby_lobby)
endif()
target_link_libraries(fby_lobby PRIVATE FemboyEngine2D::Engine)
set_target_properties(fby_lobby PROPERTIES
    OUTPUT_NAME fby_lobby
    RUNTIME_OUTPUT_DIRECTORY "${FBY_OUTPUT_DIR}")
