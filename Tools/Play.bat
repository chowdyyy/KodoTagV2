@echo off
setlocal enabledelayedexpansion
REM ============================================================
REM  Kodo Tag V2 - one-click PLAY
REM  Double-click this file. On the first run it compiles the
REM  game (can take several minutes); after that it just launches.
REM  Requires: Unreal Engine 5.7 (Epic Games Launcher) + Visual
REM  Studio 2022 with the "Game development with C++" workload.
REM  See PLAYING.md for full setup help.
REM ============================================================

REM --- Locate the project (this script lives in <project>\Tools) ---
set "TOOLS=%~dp0"
for %%I in ("%TOOLS%..") do set "ROOT=%%~fI"
set "PROJECT=%ROOT%\KodoTagV2.uproject"
set "LOGDIR=%ROOT%\Saved\Logs"
if not exist "%LOGDIR%" mkdir "%LOGDIR%"
set "LOG=%LOGDIR%\Play.log"

if not exist "%PROJECT%" (
    echo Could not find KodoTagV2.uproject next to the Tools folder.
    echo Make sure you ran this from inside the downloaded project.
    pause
    exit /b 1
)

REM --- Find the Unreal Engine install ---
set "ENGINE="
for /f "usebackq delims=" %%P in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%TOOLS%FindEngine.ps1" 2^>"%LOGDIR%\FindEngine.diag.txt"`) do set "ENGINE=%%P"

if not defined ENGINE (
    echo.
    echo  Unreal Engine 5.7 was not found on this PC.
    echo  Install it for free from the Epic Games Launcher ^(see PLAYING.md^), then run this again.
    echo.
    pause
    exit /b 1
)
echo Using Unreal Engine: %ENGINE%
echo.

REM --- Compile the game (incremental: slow the first time, fast afterwards) ---
echo Building Kodo Tag V2 ^(first run may take several minutes, please wait^)...
echo Build started %DATE% %TIME% > "%LOG%"
call "%ENGINE%\Engine\Build\BatchFiles\Build.bat" KodoTagV2Editor Win64 Development -Project="%PROJECT%" -WaitMutex >> "%LOG%" 2>&1
if not !ERRORLEVEL!==0 (
    echo.
    echo  BUILD FAILED. Open this log to see why: %LOG%
    echo  Most common cause: Visual Studio 2022 with the C++ game workload is not installed.
    echo.
    pause
    exit /b !ERRORLEVEL!
)

REM --- Launch the game in a standalone window (not the editor) ---
echo Launching Kodo Tag V2...
start "KodoTagV2" "%ENGINE%\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJECT%" -game -windowed -ResX=1600 -ResY=900
exit /b 0
