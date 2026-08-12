# Qhull/Embree wiring lands in Phase 1/3 once mesh readers + MeshBVH are
# actually implemented -- kept dependency-free for now so core+solvers+tests
# can build and be verified independently of the vcpkg spike's outcome.
add_library(cfd_mesh STATIC
    src/placeholder.cpp
)

target_include_directories(cfd_mesh PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(cfd_mesh PUBLIC cfd_core)

cfd_apply_warnings(cfd_mesh)
cfd_apply_static_runtime(cfd_mesh)
