# Shared warning flags for every in-repo target (not applied to vendored/third-party code).
function(cfd_apply_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    )
endfunction()
