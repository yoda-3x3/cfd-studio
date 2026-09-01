cmake_minimum_required(VERSION 3.24)

project(VenturiCFD LANGUAGES CXX)

if(WIN32)
    # Needed so app/resources/app_icon.rc (the exe's own PE icon resource,
    # for Explorer/taskbar/shortcuts -- separate from Qt's app_icon.qrc,
    # which only sets the in-app QWindow icon) compiles via windres.
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Portable baseline, matching the existing solver3d/kernels.cpp build:
# never -march=native (illegal-instruction risk on a different machine).
add_compile_options(-march=x86-64-v2 -mtune=generic -O3)

include(cmake/Warnings.cmake)
include(cmake/StaticRuntime.cmake)
include(cmake/FindEmbreePrebuilt.cmake) # defines Embree::Embree + cfd_copy_embree_dlls(), used across subdirectories
include(cmake/DeployQt.cmake) # defines cfd_deploy_qt(), used by app/

option(CFD_BUILD_TESTS "Build the cfd_tests unit test target" ON)
option(CFD_BUILD_GUI "Build the Qt6 cfd_studio GUI target" OFF)

add_subdirectory(core)
add_subdirectory(mesh)
add_subdirectory(solvers)
add_subdirectory(io)
add_subdirectory(pipeline)
add_subdirectory(cli)

if(CFD_BUILD_GUI)
    add_subdirectory(app)
endif()

if(CFD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
