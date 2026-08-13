@echo off
cd /d "%~dp0build\cli"
echo CFD Studio (C++) -- cfd_headless CLI
echo.
echo Example commands:
echo   cfd_headless 2d --scenario cavity --output-dir C:\path\to\output
echo   cfd_headless 3d --mesh C:\path\to\mesh.stl --domain-mode external --preset balanced --output-dir C:\path\to\output
echo   cfd_headless gen-mesh --shape tube --out C:\path\to\pipe.stl --length 4 --radius 0.5 --wall-thickness 0.1 --capped
echo   cfd_headless (with no arguments) prints full usage
echo.
cmd /k
