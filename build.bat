@echo off
setlocal

set "CONFIG="
if "%~1"=="" set "CONFIG=Debug"
if /i "%~1"=="debug" set "CONFIG=Debug"
if /i "%~1"=="release" set "CONFIG=Release"
if not defined CONFIG (
    echo usage: %~nx0 [debug^|release]
    exit /b 1
)

cd /d "%~dp0build" || exit /b 1

cmake --build . --config %CONFIG% || exit /b 1

copy /y "%~dp0bin\%CONFIG%\Markus.dll" "C:\Program Files\Guild Wars 2\addons\Markus.dll" || exit /b 1
