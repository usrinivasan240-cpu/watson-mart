@echo off
echo Stopping Sri Mart server (press Ctrl+C if running)...
if defined PG_CTL (
    echo Stopping PostgreSQL...
    "%PG_CTL%" -D "%PGDATA_DIR%" stop
) else (
    echo Set PG_CTL to stop PostgreSQL automatically, e.g.
    echo   set PG_CTL=C:\pgsql\bin\pg_ctl.exe ^& set PGDATA_DIR=C:\pgsql_data
)
echo Done.
pause
