@echo off
setlocal

cd /d "%~dp0"

set LOG=build.txt
break > "%LOG%"

where cmake >nul 2>nul
if errorlevel 1 (
    echo [ERROR] cmake not found in PATH.
    exit /b 1
)

where g++ >nul 2>nul
if errorlevel 1 (
    echo [ERROR] g++ not found in PATH. Install MSYS2 UCRT64 and add it to PATH.
    exit /b 1
)

where ninja >nul 2>nul
if errorlevel 1 (
    echo [ERROR] ninja not found in PATH. Install MSYS2 UCRT64 and add it to PATH.
    exit /b 1
)

echo [1/2] Configuring project with CMake...
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DCMAKE_MAKE_PROGRAM=ninja >> "%LOG%" 2>&1
if errorlevel 1 (
    echo [ERROR] CMake configuration failed. See %LOG% for details.
    type "%LOG%"
    exit /b 1
)

echo [2/2] Building Kestrel...
cmake --build build --config Release >> "%LOG%" 2>&1
set BUILD_RESULT=%errorlevel%

type "%LOG%"

if not "%BUILD_RESULT%"=="0" (
    echo [ERROR] Build failed. Full output saved to %LOG%.
    exit /b 1
)

if not exist build\Kestrel.exe (
    echo [ERROR] Build reported success but Kestrel.exe was not produced. See %LOG%.
    exit /b 1
)

echo.
echo Build succeeded: build\Kestrel.exe
echo Full build log saved to %LOG%
endlocal
