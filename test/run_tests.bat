@echo off
REM Builds and runs every host test. Exit code is non-zero if any test fails.
REM Pass "nopause" (as the VSCode task does) to skip the closing prompt.
setlocal enabledelayedexpansion
cd /d "%~dp0"

if "%CXX%"=="" set "CXX=g++"
where "%CXX%" >nul 2>&1
if errorlevel 1 (
    echo No C++ compiler found: %CXX%
    echo.
    echo Install one in an MSYS2 UCRT64 shell:
    echo     pacman -S mingw-w64-ucrt-x86_64-gcc
    echo.
    echo The VSCode task adds C:\msys64\ucrt64\bin to PATH; if your MSYS2 is
    echo elsewhere, update the PATH entry in .vscode/tasks.json.
    if not "%~1"=="nopause" pause
    exit /b 1
)

set "FLAGS=-std=c++17 -Wall -Wextra -Wshadow -Wstringop-truncation -O1 -Istubs"
if not exist build mkdir build

set FAILED=0
for %%F in (test_*.cpp) do (
    "%CXX%" %FLAGS% "%%F" ..\src\SimcomA76xx.cpp -o "build\%%~nF.exe"
    if errorlevel 1 (
        echo BUILD FAILED: %%F
        set FAILED=1
    ) else (
        "build\%%~nF.exe"
        if errorlevel 1 set FAILED=1
    )
)

if !FAILED! neq 0 (echo === TESTS FAILED ===) else (echo === ALL TESTS PASSED ===)
if not "%~1"=="nopause" pause
exit /b !FAILED!
