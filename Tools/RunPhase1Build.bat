@echo off
setlocal enabledelayedexpansion
REM ============================================================
REM Kodo Tag V2 - Phase 1: regenerate project files + headless
REM compile of KodoTagV2Editor (Win64 Development).
REM Full output -> Saved\Logs\Phase1Build.log
REM ============================================================

set "PROJECT=D:\Games\KodoTagV2\KodoTagV2.uproject"
set "LOGDIR=D:\Games\KodoTagV2\Saved\Logs"
set "LOG=%LOGDIR%\Phase1Build.log"
set "TOOLS=%~dp0"
if not exist "%LOGDIR%" mkdir "%LOGDIR%"

REM --- Detect engine via registry / launcher manifest / drive scan ---
set "ENGINE="
for /f "usebackq delims=" %%P in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%TOOLS%FindEngine.ps1" 2^>"%LOGDIR%\FindEngine.diag.txt"`) do set "ENGINE=%%P"

if not defined ENGINE (
    echo ERROR: No Unreal Engine installation found. > "%LOG%"
    type "%LOGDIR%\FindEngine.diag.txt" >> "%LOG%" 2>nul
    echo ERROR: No Unreal Engine installation found. Diagnostics: %LOGDIR%\FindEngine.diag.txt
    pause
    exit /b 1
)
echo Using engine: %ENGINE% > "%LOG%"
type "%LOGDIR%\FindEngine.diag.txt" >> "%LOG%" 2>nul
echo Using engine: %ENGINE%

REM --- Step 1: regenerate project files ---
REM Launcher-installed engines do not ship GenerateProjectFiles.bat (source builds only),
REM so fall back to invoking UnrealBuildTool -projectfiles directly.
echo. >> "%LOG%"
echo ===== STEP 1: Regenerate project files ===== >> "%LOG%"
if exist "%ENGINE%\Engine\Build\BatchFiles\GenerateProjectFiles.bat" (
    call "%ENGINE%\Engine\Build\BatchFiles\GenerateProjectFiles.bat" -project="%PROJECT%" -game >> "%LOG%" 2>&1
) else (
    "%ENGINE%\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe" -projectfiles -project="%PROJECT%" -game -progress >> "%LOG%" 2>&1
)
echo GENERATE_EXITCODE: !ERRORLEVEL! >> "%LOG%"
echo Project files regenerated (exit !ERRORLEVEL!).

REM --- Step 2: headless compile of the editor target ---
echo. >> "%LOG%"
echo ===== STEP 2: Build KodoTagV2Editor Win64 Development ===== >> "%LOG%"
call "%ENGINE%\Engine\Build\BatchFiles\Build.bat" KodoTagV2Editor Win64 Development -Project="%PROJECT%" -WaitMutex >> "%LOG%" 2>&1
set BUILDRESULT=!ERRORLEVEL!
echo BUILD_EXITCODE: !BUILDRESULT! >> "%LOG%"

if !BUILDRESULT!==0 (
    echo.
    echo ===== BUILD SUCCEEDED =====
) else (
    echo.
    echo ===== BUILD FAILED - exit code !BUILDRESULT! - see %LOG% =====
)
pause
exit /b !BUILDRESULT!
