@echo off
setlocal
cd /d "%~dp0"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build-release.ps1"
set ERR=%ERRORLEVEL%

echo.
if not "%ERR%"=="0" (
    echo Release build failed with exit code %ERR%.
    pause
    exit /b %ERR%
)

echo Release build finished successfully.
pause
exit /b 0
