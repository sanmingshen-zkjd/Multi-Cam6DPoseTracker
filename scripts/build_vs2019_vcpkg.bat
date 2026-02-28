@echo off
setlocal enabledelayedexpansion

REM ====== Configure these paths ======
set VCPKG_ROOT=C:\vcpkg
REM If you use your own Qt 5.15 install, set QT_BIN to its bin directory for windeployqt:
REM set QT_BIN=C:\Qt\5.15.2\msvc2019_64\bin

echo [1/3] Configure (VS2019 x64)...
cmake -S .. -B ..\build -G "Visual Studio 16 2019" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
if errorlevel 1 goto :err

echo [2/3] Build Release...
cmake --build ..\build --config Release
if errorlevel 1 goto :err

echo [3/3] Done.
echo Run: ..\build\Release\multicam_rig_toolkit_qt.exe --cam 0 --cam 1 --board 9 6 --square 0.025
exit /b 0

:err
echo Build failed.
exit /b 1
