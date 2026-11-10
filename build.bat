@echo off
setlocal enabledelayedexpansion

set CXX=g++
set RC=windres
set CFLAGS=-std=c++17 -O2 -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -Isrc
set LIBS=-lgdiplus -lgdi32 -luser32 -lmsimg32 -lshell32 -lshlwapi -lole32 -luuid -ladvapi32

where %CXX% >nul 2>nul
if errorlevel 1 (
  echo error: g++ was not found in PATH.
  exit /b 1
)

if not exist obj mkdir obj
if not exist bin mkdir bin

echo [1/3] compiling resources
%RC% -I res res\r00tshot.rc -O coff -o obj\r00tshot.res
if errorlevel 1 goto fail

echo [2/3] building bin\r00tshot.exe
%CXX% %CFLAGS% -municode -mwindows -static ^
  src\main.cpp src\overlay.cpp src\capture.cpp src\util.cpp obj\r00tshot.res ^
  -o bin\r00tshot.exe %LIBS%
if errorlevel 1 goto fail

echo [3/3] building bin\r00tshot-setup.exe
%CXX% %CFLAGS% -static ^
  src\installer.cpp src\util.cpp ^
  -o bin\r00tshot-setup.exe %LIBS%
if errorlevel 1 goto fail

echo.
echo Build finished.
echo   bin\r00tshot.exe        background capture tool
echo   bin\r00tshot-setup.exe  command line installer
exit /b 0

:fail
echo.
echo Build failed.
exit /b 1
