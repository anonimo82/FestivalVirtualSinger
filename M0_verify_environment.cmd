@echo off
setlocal EnableExtensions

cd /d "%~dp0"
set "FAIL=0"

echo FestivalVirtualSinger - M0 environment verification
echo ==================================================

echo.
echo [Project files]
call :check_file "FestivalSingModeWx.sln"
call :check_file "FestivalSingModeWx.vcxproj"
call :check_file "main.cpp"
call :check_file "festival_bridge.cpp"
call :check_file "song_model.cpp"
call :check_file "piano_roll.cpp"
call :check_file "tone_preview.cpp"

echo.
echo [wxWidgets]
call :check_dir "wxWidgets\include"
call :check_dir "wxWidgets\include\msvc"
call :check_dir "wxWidgets\lib\vc_lib"

echo.
echo [Festival runtime]
call :check_file "festival_runtime\FestivalTTSCOM.dll"
call :check_file "festival_runtime\festival_home\festival\lib\init.scm"
call :check_file "festival_runtime\festival_home\festival\lib\singing-mode.scm"
call :check_file "festival_runtime\festival_home\festival\lib\Singing.v0_1.dtd"

echo.
echo [Configuration checks]
findstr /C:"^    return musicalBpm * 50.0 / 60.0;" song_model.cpp >nul
if errorlevel 1 (
    echo [FAIL] Festival singing BPM conversion not found.
    set "FAIL=1"
) else (
    echo [ OK ] Festival singing BPM conversion 50/60 found.
)

findstr /C:"<PlatformToolset>v120</PlatformToolset>" FestivalSingModeWx.vcxproj >nul
if errorlevel 1 (
    echo [FAIL] Visual Studio v120 toolset not found.
    set "FAIL=1"
) else (
    echo [ OK ] Visual Studio v120 toolset found.
)

findstr /C:"<Platform>Win32</Platform>" FestivalSingModeWx.vcxproj >nul
if errorlevel 1 (
    echo [FAIL] Win32 target not found.
    set "FAIL=1"
) else (
    echo [ OK ] Win32 target found.
)

echo.
if "%FAIL%"=="0" (
    echo RESULT: environment appears complete for the M0 build.
    exit /b 0
) else (
    echo RESULT: one or more required items are missing.
    echo Copy wxWidgets and the Festival runtime into the expected local folders,
    echo then run this script again.
    exit /b 1
)

:check_file
if exist "%~1" (
    echo [ OK ] %~1
) else (
    echo [MISS] %~1
    set "FAIL=1"
)
exit /b 0

:check_dir
if exist "%~1\" (
    echo [ OK ] %~1\
) else (
    echo [MISS] %~1\
    set "FAIL=1"
)
exit /b 0
