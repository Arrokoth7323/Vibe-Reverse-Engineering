@echo off
setlocal

set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS%" (
    echo ERROR: vcvarsall.bat not found at %VCVARS%
    exit /b 1
)

call "%VCVARS%" x86 >nul 2>&1

set "SRCDIR=%~dp0"
cd /d "%SRCDIR%"

if not exist build mkdir build

cl /O2 /MT /W3 /EHsc /nologo main.cpp /Fe:build\d3d9.dll /link /DLL /DEF:d3d9.def /OUT:build\d3d9.dll kernel32.lib user32.lib
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)

echo Build succeeded: build\d3d9.dll
