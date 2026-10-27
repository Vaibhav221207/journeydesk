@echo off
setlocal
cd /d "%~dp0"
where cmake >nul 2>nul
if errorlevel 1 (
  echo Install CMake and a C++20 compiler to build from source.
  echo The bundled bin\journeydesk.exe runs without a compiler.
  exit /b 1
)
if not exist build\CMakeCache.txt (
  where ninja >nul 2>nul
  if not errorlevel 1 (
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
  ) else (
    cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
  )
) else (
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
)
if errorlevel 1 exit /b 1
cmake --build build --parallel 2
if errorlevel 1 exit /b 1
ctest --test-dir build --output-on-failure
if errorlevel 1 exit /b 1
echo Build and tests passed. Run start.bat to open Journey Desk.
