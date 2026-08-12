find_package(Qhull CONFIG REQUIRED)
# Embree::Embree is already defined by the top-level CMakeLists.txt's
# include(cmake/FindEmbreePrebuilt.cmake) -- not repeated here.

add_library(cfd_mesh STATIC
    src/mesh.cpp
    src/stl_reader.cpp
    src/obj_reader.cpp
    src/ply_reader.cpp
    src/off_reader.cpp
    src/mesh_bvh.cpp
    src/convex_hull2d.cpp
    src/cap_boundaries.cpp
)

target_include_directories(cfd_mesh PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(cfd_mesh PUBLIC cfd_core)
target_link_libraries(cfd_mesh PRIVATE Qhull::qhullcpp Qhull::qhull_r Embree::Embree)

cfd_apply_warnings(cfd_mesh)
cfd_apply_static_runtime(cfd_mesh)
