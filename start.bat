@echo off
setlocal
cd /d "%~dp0"
if exist "build\journeydesk.exe" (
  "build\journeydesk.exe" --static "%~dp0static" --data "%~dp0data\bookings.json" --open
) else if exist "bin\journeydesk.exe" (
  "bin\journeydesk.exe" --static "%~dp0static" --data "%~dp0data\bookings.json" --open
) else (
  echo The executable was not found. Run build.bat first, or extract the full submission ZIP.
)
pause
