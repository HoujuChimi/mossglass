@echo off
setlocal

rem Build Native Swamp Bottle with MinGW-w64.
rem Usage:
rem   build_mingw.bat [SDL2_MINGW_ROOT]
rem Example:
rem   build_mingw.bat C:\SDL2\x86_64-w64-mingw32
rem If omitted, the script uses the SDL2_DIR environment variable, then .\SDL2.

if not "%~1"=="" set "SDL2_DIR=%~1"
if not defined SDL2_DIR set "SDL2_DIR=SDL2"

if not exist "%SDL2_DIR%\include\SDL2\SDL.h" (
    echo SDL2 headers not found at "%SDL2_DIR%\include\SDL2\SDL.h"
    echo Pass the x86_64-w64-mingw32 folder of an SDL2 MinGW development package.
    exit /b 1
)

where g++ >nul 2>nul
if errorlevel 1 (
    echo g++ was not found on PATH.
    exit /b 1
)

rem Embed the application icon (multi-size ICO) as a Windows resource.
where windres >nul 2>nul
if errorlevel 1 (
    echo windres was not found on PATH.
    exit /b 1
)
windres app.rc -O coff -o app.res
if errorlevel 1 exit /b 1

g++ -std=c++17 -O2 -Wall -Wextra ecobottle.cpp app.res -o EcoBottle.exe ^
    -I "%SDL2_DIR%\include\SDL2" -L "%SDL2_DIR%\lib" ^
    -lmingw32 -lSDL2main -lSDL2 -lgdi32 -luser32 -lole32 -luuid -ladvapi32 -mwindows ^
    -static-libgcc -static-libstdc++
if errorlevel 1 exit /b 1

rem The resource object is a build intermediate; the icon is already in the exe.
del /q app.res

copy /Y "%SDL2_DIR%\bin\SDL2.dll" . >nul

rem MinGW-w64 POSIX-thread builds still import libwinpthread-1.dll.  Copy it
rem next to the executable so the result is self-contained apart from SDL2.dll.
for %%I in (g++.exe) do set "MINGW_BIN=%%~dp$PATH:I"
if exist "%MINGW_BIN%libwinpthread-1.dll" copy /Y "%MINGW_BIN%libwinpthread-1.dll" . >nul

echo.
echo Built EcoBottle.exe and copied its runtime DLLs beside it.
echo Run EcoBottle.exe, hold left mouse to drag, right click for the observation panel.
endlocal
