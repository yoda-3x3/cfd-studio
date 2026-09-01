add_library(cfd_io STATIC
    src/openfoam_writer.cpp
    src/vtk_writer.cpp
    src/run_cache.cpp
    src/results_cache_writer.cpp
    src/results_cache_reader.cpp
)

target_include_directories(cfd_io PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(cfd_io PRIVATE
    ${CMAKE_SOURCE_DIR}/vendor/json
)

target_link_libraries(cfd_io PUBLIC cfd_core cfd_mesh)

cfd_apply_warnings(cfd_io)
cfd_apply_static_runtime(cfd_io)
