@echo off
title Sri Mart - Dev Mode (Rebuild + Run)
rem Portable dev loop: builds with CMake then runs. No hardcoded user paths.
rem Env overrides: PG_CTL, PGDATA_DIR, PORT, VCPKG_TOOLCHAIN, VCPKG_TRIPLET, MSYS2_BIN.

echo ========================================
echo   Sri Mart - Dev Mode
echo ========================================

if defined PG_CTL (
    echo [1/4] Starting PostgreSQL...
    "%PG_CTL%" -D "%PGDATA_DIR%" -l "%~dp0pgsql.log" start
    timeout /t 2 >nul
) else (
    echo [1/4] Skipping PostgreSQL start (set PG_CTL to enable).
)

if defined MSYS2_BIN set PATH=%MSYS2_BIN%;%PATH%
echo [2/4] Compiler on PATH (cmake --version):
cmake --version || (echo ERROR: cmake not found on PATH. & pause & exit /b 1)

if not defined VCPKG_TOOLCHAIN (
    echo ERROR: set VCPKG_TOOLCHAIN to vcpkg.cmake, e.g.
    echo   set VCPKG_TOOLCHAIN=C:\dev\vcpkg\scripts\buildsystems\vcpkg.cmake
    pause
    exit /b 1
)
if not defined VCPKG_TRIPLET set VCPKG_TRIPLET=x64-mingw-dynamic
if not defined PORT set PORT=8080

echo [3/4] Building...
cd /d "%~dp0"
cmake -B build -S . -G "MinGW Makefiles" -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" -DVCPKG_TARGET_TRIPLET=%VCPKG_TRIPLET% || (pause & exit /b 1)
cmake --build build || (pause & exit /b 1)

echo [4/4] Starting Sri Mart server...
echo.
echo ========================================
echo   Open http://localhost:%PORT% in browser
echo ========================================
echo.
cd /d "%~dp0build"
sri_mart.exe
