@echo off
setlocal

where msbuild >nul 2>nul
if errorlevel 1 (
  echo ERRORE: MSBuild non trovato.
  echo Aprire un prompt "Developer Command Prompt for VS" con toolset v120.
  exit /b 2
)

if not exist wxWidgets\include\wx\wx.h (
  echo ERRORE: header wxWidgets mancanti.
  exit /b 3
)

if not exist wxWidgets\lib\vc_lib (
  echo ERRORE: librerie statiche wxWidgets mancanti.
  exit /b 4
)

msbuild FestivalSingModeWx.sln ^
  /m ^
  /t:Rebuild ^
  /p:Configuration=Release ^
  /p:Platform=Win32

if errorlevel 1 exit /b %errorlevel%

if not exist FestivalSingModeWx.exe (
  echo ERRORE: build terminata senza produrre FestivalSingModeWx.exe.
  exit /b 5
)

echo.
echo Build completata: FestivalSingModeWx.exe
exit /b 0
