# Static linking for every non-Qt target, matching build_native.py's existing
# rule for solver3d/kernels.cpp: zero runtime DLL surface, verified via
# `ntldd -R` on the build output. The Qt GUI target (cfd_studio) is the one
# deliberate exception -- see cfd_studio_cpp/CMakeLists.txt comment in app/.
function(cfd_apply_static_runtime target)
    if(MINGW)
        target_link_options(${target} PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endfunction()
