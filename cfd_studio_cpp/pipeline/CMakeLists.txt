add_library(cfd_pipeline STATIC
    src/run_2d.cpp
    src/run_3d.cpp
)

target_include_directories(cfd_pipeline PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(cfd_pipeline PUBLIC cfd_core cfd_mesh cfd_solvers cfd_io)

cfd_apply_warnings(cfd_pipeline)
cfd_apply_static_runtime(cfd_pipeline)
