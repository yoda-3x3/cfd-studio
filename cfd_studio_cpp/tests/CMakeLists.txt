add_executable(cfd_tests
    ${CMAKE_SOURCE_DIR}/vendor/catch2/catch_amalgamated.cpp
    core/test_connected_components.cpp
    core/test_hashing.cpp
    solvers/test_kernels_scalar.cpp
)

target_include_directories(cfd_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/vendor/catch2
)

target_link_libraries(cfd_tests PRIVATE cfd_core cfd_mesh cfd_solvers cfd_io)

cfd_apply_static_runtime(cfd_tests)

add_test(NAME cfd_tests COMMAND cfd_tests)
