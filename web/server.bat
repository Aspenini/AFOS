@echo off
REM Simple HTTP server for AFOS web emulator (Windows)
REM Run this script to start a local web server

python -m http.server 8000
if errorlevel 1 (
    echo.
    echo Error: Python not found or http.server module not available.
    echo Please install Python 3 from https://www.python.org/
    echo.
    pause
)

