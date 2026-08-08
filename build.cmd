@echo off
setlocal EnableDelayedExpansion
rem ===========================================================================
rem  Build everything: sim, viewer, Arena (the editor), the MCP server, envcheck.
rem
rem  Usage:
rem    build.cmd                  Release, viewer on
rem    build.cmd debug            Debug build
rem    build.cmd fresh            wipe build\ and reconfigure first
rem    build.cmd noviewer         skip the GLFW viewer (sim + tools only)
rem    build.cmd target Arena     build one target only
rem    build.cmd help
rem
rem  Options combine: build.cmd fresh debug noviewer
rem
rem  Overridable by environment:
rem    VCPKG_ROOT       vcpkg checkout (default: the sibling MASS one)
rem    CMAKE_GENERATOR  Visual Studio generator (default: Visual Studio 18 2026)
rem ===========================================================================

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "BUILD=%ROOT%\build"

set "CONFIG=Release"
set "VIEWER=ON"
set "FRESH="
set "TARGET="

rem Parsed with labels rather than parenthesised blocks: `exit /b` inside nested
rem blocks does not reliably propagate its code out of the script.
:parse
if "%~1"=="" goto parsed
set "ARG=%~1"
if /i "%ARG%"=="help"     goto usage
if /i "%ARG%"=="/?"       goto usage
if /i "%ARG%"=="-h"       goto usage
if /i "%ARG%"=="--help"   goto usage
if /i "%ARG%"=="debug"    goto opt_debug
if /i "%ARG%"=="release"  goto opt_release
if /i "%ARG%"=="fresh"    goto opt_fresh
if /i "%ARG%"=="noviewer" goto opt_noviewer
if /i "%ARG%"=="target"   goto opt_target
echo [error] unknown option: %ARG%
echo         run "build.cmd help" for usage
exit /b 2

:opt_debug
set "CONFIG=Debug"
shift
goto parse
:opt_release
set "CONFIG=Release"
shift
goto parse
:opt_fresh
set "FRESH=1"
shift
goto parse
:opt_noviewer
set "VIEWER=OFF"
shift
goto parse
:opt_target
if "%~2"=="" goto err_target
set "TARGET=%~2"
shift
shift
goto parse
:err_target
echo [error] "target" needs a target name, e.g. build.cmd target Arena
exit /b 2

:parsed

rem ---- prerequisites --------------------------------------------------------
where cmake >nul 2>&1
if errorlevel 1 (
    echo [error] cmake is not on PATH.
    echo         Install CMake, or open a "Developer Command Prompt for VS".
    exit /b 3
)

if not defined VCPKG_ROOT set "VCPKG_ROOT=D:\Tootega\Source\MASS\Deps\vcpkg"
set "TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
if not exist "%TOOLCHAIN%" (
    echo [error] vcpkg toolchain not found:
    echo           %TOOLCHAIN%
    echo         This build reuses the vcpkg provisioned for the sibling MASS
    echo         project ^(dartsim, glfw3, glad, imgui, imguizmo, assimp,
    echo         tinyxml2, nlohmann-json, boost-asio^). Point VCPKG_ROOT at a
    echo         checkout that has them:
    echo           set VCPKG_ROOT=C:\path\to\vcpkg ^&^& build.cmd
    exit /b 4
)

if not defined CMAKE_GENERATOR set "CMAKE_GENERATOR=Visual Studio 18 2026"

rem ---- configure ------------------------------------------------------------
if defined FRESH if exist "%BUILD%" (
    echo [fresh] removing %BUILD%
    rmdir /s /q "%BUILD%"
)

if not exist "%BUILD%\CMakeCache.txt" (
    echo [configure] %CMAKE_GENERATOR% ^| viewer=%VIEWER%
    cmake -S "%ROOT%" -B "%BUILD%" -G "%CMAKE_GENERATOR%" -A x64 ^
        -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN%" ^
        -DVCPKG_TARGET_TRIPLET=x64-windows ^
        -DGAITNET_BUILD_VIEWER=%VIEWER%
    if errorlevel 1 (
        echo [error] configure failed
        exit /b 5
    )
) else (
    echo [configure] reusing %BUILD% ^(use "fresh" to reconfigure^)
)

rem ---- build ----------------------------------------------------------------
if defined TARGET (
    echo [build] %CONFIG% ^| target %TARGET%
    cmake --build "%BUILD%" --config %CONFIG% --target %TARGET%
) else (
    echo [build] %CONFIG% ^| all targets
    cmake --build "%BUILD%" --config %CONFIG%
)
if errorlevel 1 (
    echo [error] build failed
    exit /b 6
)

rem ---- report ---------------------------------------------------------------
echo.
echo [done] %CONFIG% -^> Dist\x64\%CONFIG%
set "DIST=%ROOT%\Dist\x64\%CONFIG%"
for %%E in (Arena.exe viewer.exe gaitnet-mcp.exe envcheck.exe) do (
    if exist "%DIST%\%%E" echo        %%E
)
exit /b 0

:usage
echo Build everything: sim, viewer, Arena ^(the editor^), MCP server, envcheck.
echo.
echo   build.cmd                  Release, viewer on
echo   build.cmd debug            Debug build
echo   build.cmd fresh            wipe build\ and reconfigure first
echo   build.cmd noviewer         skip the GLFW viewer
echo   build.cmd target Arena     build one target only
echo.
echo Options combine, e.g.  build.cmd fresh debug noviewer
echo.
echo Environment overrides:
echo   VCPKG_ROOT       vcpkg checkout (default: the sibling MASS one)
echo   CMAKE_GENERATOR  Visual Studio generator (default: Visual Studio 18 2026)
exit /b 0
