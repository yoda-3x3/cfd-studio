# Deploys the Qt6 runtime, platform plugins, and MinGW runtime DLLs next to
# a Qt GUI target so the built exe is standalone -- matching the same
# "self-contained on plain Windows system directories" bar the other
# targets already meet via cfd_apply_static_runtime() / cfd_copy_embree_dlls().
# cfd_studio is the one target that stays dynamically linked against Qt
# (see app/CMakeLists.txt's comment), so it needs this instead.
function(cfd_deploy_qt target)
    if(NOT WINDEPLOYQT_EXECUTABLE)
        message(WARNING "windeployqt not found; ${target} will not be self-contained")
        return()
    endif()
    # windeployqt --mingw locates the MinGW runtime DLLs (libstdc++-6.dll,
    # libgcc_s_seh-1.dll, libwinpthread-1.dll) via PATH, so point it at the
    # exact compiler CMake resolved rather than trusting ambient PATH --
    # same "pin the exact toolchain" approach build.ps1 already uses.
    get_filename_component(_mingw_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    # A literal ';' in a COMMAND argument is CMake's list separator, not a
    # PATH separator -- it silently splits this string into extra tokens.
    # $<SEMICOLON> survives to the actual command line intact.
    #
    # Note: this Qt 6.9.3 windeployqt has no --mingw flag (that's an older
    # Qt version's option, confirmed via --help) -- --compiler-runtime is
    # the current equivalent, forcing MinGW runtime DLL deployment even for
    # a Release build (default is Debug-only). It locates those DLLs via
    # PATH, hence still prepending the MinGW compiler's bin dir.
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${_mingw_bin_dir}$<SEMICOLON>$ENV{PATH}"
                ${WINDEPLOYQT_EXECUTABLE}
                --compiler-runtime
                --no-translations
                --no-system-d3d-compiler
                --no-system-dxc-compiler
                $<TARGET_FILE:${target}>
        COMMENT "windeployqt: deploying Qt runtime + plugins next to ${target}"
    )
endfunction()
