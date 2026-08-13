find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)

qt_standard_project_setup()

qt_add_executable(cfd_studio
    src/main.cpp
    src/main_window.cpp
)

target_include_directories(cfd_studio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(cfd_studio PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    cfd_pipeline
)

cfd_apply_warnings(cfd_studio)
# Deliberately NOT cfd_apply_static_runtime(cfd_studio) -- see that
# function's own comment in cmake/StaticRuntime.cmake. Qt is a set of
# dynamic DLLs on this toolchain; cfd_studio ships alongside them
# (via windeployqt, Phase 7) instead of statically linking like every
# other target in this project.
cfd_copy_embree_dlls(cfd_studio)
