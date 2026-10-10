@echo off
title Sri Mart - Starting...
rem Portable starter: no hardcoded user paths.
rem Override via env: PG_CTL (full path to pg_ctl.exe), PGDATA_DIR, PORT.

echo ========================================
echo   Sri Mart - Starting All Services
echo ========================================

if defined PG_CTL (
    echo [1/3] Starting PostgreSQL...
    "%PG_CTL%" -D "%PGDATA_DIR%" -l "%~dp0pgsql.log" start
    timeout /t 2 >nul
) else (
    echo [1/3] Skipping PostgreSQL start (set PG_CTL to enable).
    echo       Example: set PG_CTL=C:\pgsql\bin\pg_ctl.exe ^& set PGDATA_DIR=C:\pgsql_data
)

if not defined PORT set PORT=8080

echo [2/3] Checking build...
if not exist "%~dp0build\sri_mart.exe" (
    echo ERROR: build\sri_mart.exe not found. Run dev.bat first to build.
    pause
    exit /b 1
)

echo [3/3] Starting Sri Mart server on port %PORT%...
echo.
echo ========================================
echo   Open http://localhost:%PORT% in browser
echo ========================================
echo.
cd /d "%~dp0build"
sri_mart.exe
