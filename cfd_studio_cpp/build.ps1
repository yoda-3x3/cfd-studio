<#
.SYNOPSIS
    One-command build + test for Venturi CFD's C++ rewrite (cfd_studio_cpp).

.DESCRIPTION
    Sets up the exact toolchain this project needs (the Qt-matched MinGW
    13.1.0 compiler -- NOT whatever g++ happens to already be on PATH, which
    may be a different, ABI-incompatible version) for THIS SCRIPT'S PROCESS
    ONLY. It does not modify your permanent system/user PATH -- run this
    script (or one like it) every time you build, rather than expecting a
    plain `cmake --build` to work from an arbitrary terminal.

.PARAMETER Clean
    Delete the build/ and vcpkg_installed/ directories first and reconfigure
    from scratch.

.PARAMETER SkipTests
    Configure and build only; don't run cfd_tests.exe afterward.

.PARAMETER BuildGui
    Also configure and build the Qt6 cfd_studio GUI target (CFD_BUILD_GUI=ON),
    adding Qt's CMake package path via CMAKE_PREFIX_PATH. Off by default so
    plain CLI/library work doesn't pay Qt's extra configure/build cost.
#>
param(
    [switch]$Clean,
    [switch]$SkipTests,
    [switch]$BuildGui
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

# --- Prerequisites this script expects to already be installed --------
$qtMingw = "C:\Users\reach\Dance\Qt\Tools\mingw1310_64\bin"
$qtRoot  = "C:\Users\reach\Dance\Qt\6.9.3\mingw_64"
$qtBin   = "$qtRoot\bin"
$cmakeBin = "C:\Program Files\CMake\bin"
$vcpkgExe = "C:\Users\reach\Dance\vcpkg\vcpkg.exe"
$vcpkgToolchain = "C:\Users\reach\Dance\vcpkg\scripts\buildsystems\vcpkg.cmake"
$embreeRoot = "C:\Users\reach\Dance\third_party"

foreach ($p in @($qtMingw, $cmakeBin, $vcpkgExe, $vcpkgToolchain, "$embreeRoot\bin\embree4.dll")) {
    if (-not (Test-Path $p)) {
        throw "Missing prerequisite: $p`nSee BUILD.md for how to install it."
    }
}

# Prepend the exact toolchain this project needs, ahead of anything else
# already on PATH (in particular, ahead of any other g++ that might exist).
$env:Path = "$qtMingw;$qtBin;$cmakeBin;$env:Path"

if ($Clean) {
    Write-Host "Removing build/ and vcpkg_installed/ ..."
    Remove-Item -Recurse -Force "$root\build" -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force "$root\vcpkg_installed" -ErrorAction SilentlyContinue
}

Write-Host "Configuring ..."
$guiFlags = @("-DCFD_BUILD_GUI=OFF")
if ($BuildGui) {
    $guiFlags = @("-DCFD_BUILD_GUI=ON", "-DCMAKE_PREFIX_PATH=$qtRoot")
}
& cmake -S $root -B "$root\build" -G "MinGW Makefiles" `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_TOOLCHAIN_FILE="$vcpkgToolchain" `
    -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic -DVCPKG_HOST_TRIPLET=x64-mingw-dynamic `
    -DEMBREE_PREBUILT_ROOT="$embreeRoot" `
    -DCFD_BUILD_TESTS=ON @guiFlags
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed (exit $LASTEXITCODE)" }

Write-Host "Building ..."
& cmake --build "$root\build" -j 4
if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)" }

if (-not $SkipTests) {
    Write-Host "Running tests ..."
    & "$root\build\tests\cfd_tests.exe"
    if ($LASTEXITCODE -ne 0) { throw "Tests failed (exit $LASTEXITCODE)" }
}

Write-Host "`nDone. Executables:"
Write-Host "  $root\build\cli\cfd_headless.exe"
Write-Host "  $root\build\tests\cfd_tests.exe"
if ($BuildGui) {
    Write-Host "  $root\build\app\cfd_studio.exe"
}
