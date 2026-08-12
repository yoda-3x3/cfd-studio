# Locates the official prebuilt Embree Windows release (NOT vcpkg-managed):
# vcpkg's `embree` port pulls in `tbb`, which fails to compile on this
# project's MinGW toolchain (a genuine upstream TBB/MinGW incompatibility in
# TBB's ITT-notify profiling code -- a `const char*` passed where TBB's
# Windows build expects `wchar_t*`). The official prebuilt binary release
# ships its own working tbb12.dll/tbbmalloc.dll, sidestepping that build
# entirely -- confirmed linkable and runnable from this MinGW toolchain via
# a standalone spike (rtcNewDevice/rtcNewScene round-trip) before adopting
# this as the real integration path.
#
# Expects the extracted release at EMBREE_PREBUILT_ROOT (default:
# C:/Users/reach/Dance/third_party, i.e. bin/embree4.dll, lib/embree4.lib,
# include/embree4/). Download from:
# https://github.com/RenderKit/embree/releases (the *.windows.zip asset).

set(EMBREE_PREBUILT_ROOT "C:/Users/reach/Dance/third_party" CACHE PATH
    "Root of the extracted prebuilt Embree Windows release")

find_path(EMBREE_INCLUDE_DIR embree4/rtcore.h PATHS "${EMBREE_PREBUILT_ROOT}/include")
find_library(EMBREE_LIBRARY NAMES embree4 PATHS "${EMBREE_PREBUILT_ROOT}/lib")
find_file(EMBREE_DLL NAMES embree4.dll PATHS "${EMBREE_PREBUILT_ROOT}/bin")
find_file(EMBREE_TBB_DLL NAMES tbb12.dll PATHS "${EMBREE_PREBUILT_ROOT}/bin")
find_file(EMBREE_TBBMALLOC_DLL NAMES tbbmalloc.dll PATHS "${EMBREE_PREBUILT_ROOT}/bin")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(EmbreePrebuilt
    REQUIRED_VARS EMBREE_INCLUDE_DIR EMBREE_LIBRARY EMBREE_DLL EMBREE_TBB_DLL EMBREE_TBBMALLOC_DLL
)

if(EmbreePrebuilt_FOUND AND NOT TARGET Embree::Embree)
    add_library(Embree::Embree SHARED IMPORTED)
    set_target_properties(Embree::Embree PROPERTIES
        IMPORTED_LOCATION "${EMBREE_DLL}"
        IMPORTED_IMPLIB "${EMBREE_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${EMBREE_INCLUDE_DIR}"
    )
endif()

# Runtime DLLs a consuming executable target must copy next to itself --
# see cfd_copy_embree_dlls() below.
set(EMBREE_RUNTIME_DLLS "${EMBREE_DLL}" "${EMBREE_TBB_DLL}" "${EMBREE_TBBMALLOC_DLL}")

function(cfd_copy_embree_dlls target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${EMBREE_RUNTIME_DLLS} $<TARGET_FILE_DIR:${target}>
    )
endfunction()
