add_library(cfd_solvers STATIC
    src/kernels_scalar.cpp
)

target_include_directories(cfd_solvers PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(cfd_solvers PUBLIC cfd_core)

cfd_apply_warnings(cfd_solvers)
cfd_apply_static_runtime(cfd_solvers)
