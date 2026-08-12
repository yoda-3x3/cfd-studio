add_library(cfd_core STATIC
    src/hashing.cpp
    src/connected_components.cpp
    src/binary_morphology.cpp
)

target_include_directories(cfd_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(cfd_core PRIVATE
    ${CMAKE_SOURCE_DIR}/vendor/xxhash
)

cfd_apply_warnings(cfd_core)
cfd_apply_static_runtime(cfd_core)
