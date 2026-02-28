@echo off
setlocal

REM ====== Set your Qt 5.15 msvc2019 bin path (where windeployqt.exe is) ======
set QT_BIN=C:\Qt\5.15.2\msvc2019_64\bin

set EXE=..\build\Release\multicam_rig_toolkit_qt.exe
if not exist %EXE% (
  echo EXE not found: %EXE%
  exit /b 1
)

echo Running windeployqt...
"%QT_BIN%\windeployqt.exe" "%EXE%"
if errorlevel 1 (
  echo windeployqt failed.
  exit /b 1
)

echo Done. You may also need to add vcpkg bin to PATH for OpenCV/Ceres DLLs:
echo   set PATH=%%PATH%%;C:\vcpkg\installed\x64-windows\bin
