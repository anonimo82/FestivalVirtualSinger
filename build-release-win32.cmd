@echo off
setlocal

where msbuild >nul 2>nul
if errorlevel 1 (
  echo ERROR: MSBuild was not found.
  echo Open a "Developer Command Prompt for VS" with the v120 toolset.
  exit /b 2
)

if not exist wxWidgets\include\wx\wx.h (
  echo ERROR: wxWidgets headers are missing.
  exit /b 3
)

if not exist wxWidgets\lib\vc_lib (
  echo ERROR: wxWidgets static libraries are missing.
  exit /b 4
)

msbuild FestivalSingModeWx.sln ^
  /m ^
  /t:Rebuild ^
  /p:Configuration=Release ^
  /p:Platform=Win32

if errorlevel 1 exit /b %errorlevel%

if not exist FestivalSingModeWx.exe (
  echo ERROR: the build finished without producing FestivalSingModeWx.exe.
  exit /b 5
)

echo.
echo Build completata: FestivalSingModeWx.exe
exit /b 0
