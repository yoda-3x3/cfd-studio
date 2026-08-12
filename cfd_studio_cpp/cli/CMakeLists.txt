add_executable(cfd_headless src/main_cli.cpp)

target_link_libraries(cfd_headless PRIVATE cfd_core cfd_mesh cfd_solvers cfd_io)

cfd_apply_warnings(cfd_headless)
cfd_apply_static_runtime(cfd_headless)
cfd_copy_embree_dlls(cfd_headless)
