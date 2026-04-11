@echo off
setlocal enabledelayedexpansion

:: remix-comp-proxy build script (d3d9.dll proxy)
:: Usage: build.bat [release|debug] [--name OutputName] [--comp CompDir]
::
:: Examples:
::   build.bat                          Build release d3d9.dll
::   build.bat debug                    Build debug d3d9.dll
::   build.bat release --name Warband   Build release d3d9.dll (Warband variant)
::   build.bat release --name Warband --comp ..\..\patches\Warband\proxy\comp
::                                      Build per-game variant from custom comp dir

set "ROOT=%~dp0"
set "CONFIG=release"
set "NAME=remix-comp-proxy"
set "COMP_DIR=%ROOT%src\comp"
set "CUSTOM_COMP=0"

:: Parse args
:parse_args
if "%~1"=="" goto :args_done
if /i "%~1"=="release" ( set "CONFIG=release" & shift & goto :parse_args )
if /i "%~1"=="debug"   ( set "CONFIG=debug"   & shift & goto :parse_args )
if /i "%~1"=="--name"  ( set "NAME=%~2-comp-proxy"  & shift & shift & goto :parse_args )
if /i "%~1"=="--comp"  ( set "COMP_DIR=%~2" & set "CUSTOM_COMP=1" & shift & shift & goto :parse_args )
echo Unknown argument: %~1
exit /b 1
:args_done

:: Find Visual Studio
set "VCVARS="
for %%p in (
    "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
    "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
) do (
    if exist %%p set "VCVARS=%%~p"
)
if "%VCVARS%"=="" (
    echo ERROR: Visual Studio 2022 not found
    exit /b 1
)
call "%VCVARS%" x86 >nul 2>&1
if errorlevel 1 ( echo ERROR: vcvarsall.bat failed & exit /b 1 )

:: Paths
set "SRC=%ROOT%src"
set "DEPS=%ROOT%deps"
set "OUT=%ROOT%build\bin\%CONFIG%"
set "OBJ=%ROOT%build\obj\%CONFIG%"

:: Per-game output: d3d9.dll goes to the game's patches folder when --comp is custom
if "!CUSTOM_COMP!"=="1" (
    for %%G in ("!COMP_DIR!\..\..") do set "GAME_DIR=%%~fG"
    set "GAME_OUT=!GAME_DIR!\build\!CONFIG!"
    set "GAME_OBJ=!GAME_DIR!\build\obj\!CONFIG!"
) else (
    set "GAME_OUT=!OUT!"
    set "GAME_OBJ=!OBJ!\comp"
)

mkdir "%OUT%" 2>nul
mkdir "%OBJ%\minhook" 2>nul
mkdir "%OBJ%\imgui" 2>nul
mkdir "%OBJ%\shared" 2>nul
mkdir "%GAME_OUT%" 2>nul
mkdir "%GAME_OBJ%" 2>nul

:: Include paths
set "INC=/I"%SRC%" /I"%DEPS%\bridge_api" /I"%DEPS%\dxsdk\Include" /I"%DEPS%\imgui" /I"%DEPS%\minhook\include""

:: Lib search path
set "LIBPATH=/LIBPATH:"%DEPS%\dxsdk\Lib\x86""

:: Common compiler flags
set "CF=/nologo /std:c++latest /W4 /MP /EHsc /Zm100"
set "CF=%CF% /wd4239 /wd4369 /wd4505 /wd4996 /wd5311 /wd6001 /wd6385 /wd6386 /wd26812"
set "CF=%CF% /D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS /D_WINDOWS /DWIN32"

:: Config-specific flags
if /i "%CONFIG%"=="release" (
    set "CF=%CF% /O2 /GL /Gy /Oi /GF /MT /Zi /DNDEBUG /WX"
    set "LF=/LTCG /OPT:REF /OPT:ICF"
) else (
    set "CF=%CF% /Od /MTd /ZI /DDEBUG /D_DEBUG"
    set "LF="
)

echo.
echo === remix-comp-proxy build (d3d9.dll proxy) ===
echo Config:  %CONFIG%
echo Output:  %GAME_OUT%\d3d9.dll
echo CompDir: %COMP_DIR%
echo.

:: -------------------------------------------------------
:: Step 1: minhook (C static lib)
:: -------------------------------------------------------
echo [1/4] minhook
set "MH_SRC=%DEPS%\minhook\src"
cl /nologo /c /W0 /MP /MT /O2 ^
    /I"%DEPS%\minhook\include" ^
    /Fo"%OBJ%\minhook\\" ^
    "%MH_SRC%\buffer.c" ^
    "%MH_SRC%\hook.c" ^
    "%MH_SRC%\trampoline.c" ^
    "%MH_SRC%\hde\hde32.c" ^
    "%MH_SRC%\hde\hde64.c"
if errorlevel 1 goto :fail
lib /nologo /OUT:"%OUT%\minhook.lib" "%OBJ%\minhook\*.obj"
if errorlevel 1 goto :fail

:: -------------------------------------------------------
:: Step 2: imgui (C++ static lib, no PCH)
:: -------------------------------------------------------
echo [2/4] imgui
set "IM=%DEPS%\imgui"
cl /nologo /c /W0 /MP /std:c++latest /MT /O2 /EHsc ^
    /I"%IM%" ^
    /Fo"%OBJ%\imgui\\" ^
    "%IM%\imgui.cpp" ^
    "%IM%\imgui_demo.cpp" ^
    "%IM%\imgui_draw.cpp" ^
    "%IM%\imgui_tables.cpp" ^
    "%IM%\imgui_widgets.cpp" ^
    "%IM%\backends\imgui_impl_dx9.cpp" ^
    "%IM%\backends\imgui_impl_win32.cpp" ^
    "%IM%\misc\cpp\imgui_stdlib.cpp"
if errorlevel 1 goto :fail
lib /nologo /OUT:"%OUT%\imgui.lib" "%OBJ%\imgui\*.obj"
if errorlevel 1 goto :fail

:: -------------------------------------------------------
:: Step 3: _shared (C++ static lib with PCH)
:: -------------------------------------------------------
echo [3/4] _shared

:: Create PCH
cl /nologo /c %CF% %INC% ^
    /Yc"std_include.hpp" /Fp"%OBJ%\shared\shared.pch" ^
    /Fo"%OBJ%\shared\std_include.obj" ^
    "%SRC%\shared\std_include.cpp"
if errorlevel 1 goto :fail

:: Compile sources
cl /nologo /c %CF% %INC% ^
    /Yu"std_include.hpp" /Fp"%OBJ%\shared\shared.pch" ^
    /Fo"%OBJ%\shared\\" ^
    "%SRC%\shared\common\config.cpp" ^
    "%SRC%\shared\common\dinput_hook_v1.cpp" ^
    "%SRC%\shared\common\dinput_hook_v2.cpp" ^
    "%SRC%\shared\common\ffp_state.cpp" ^
    "%SRC%\shared\common\flags.cpp" ^
    "%SRC%\shared\common\imgui_helper.cpp" ^
    "%SRC%\shared\common\loader.cpp" ^
    "%SRC%\shared\common\remix_api.cpp" ^
    "%SRC%\shared\globals.cpp" ^
    "%SRC%\shared\utils\hooking.cpp" ^
    "%SRC%\shared\utils\memory.cpp" ^
    "%SRC%\shared\utils\utils.cpp"
if errorlevel 1 goto :fail
lib /nologo /OUT:"%OUT%\_shared.lib" "%OBJ%\shared\*.obj"
if errorlevel 1 goto :fail

:: -------------------------------------------------------
:: Step 4: comp DLL (C++ with PCH, links everything)
:: -------------------------------------------------------
echo [4/4] %NAME% (d3d9.dll)

:: Create PCH
cl /nologo /c %CF% %INC% /I"%COMP_DIR%\.." ^
    /Yc"std_include.hpp" /Fp"%GAME_OBJ%\comp.pch" ^
    /Fo"%GAME_OBJ%\std_include.obj" ^
    "%COMP_DIR%\std_include.cpp"
if errorlevel 1 goto :fail

:: Compile sources -- glob all .cpp files in comp dir (except std_include.cpp)
set "COMP_SRCS="
for /r "%COMP_DIR%" %%f in (*.cpp) do (
    if /i not "%%~nxf"=="std_include.cpp" (
        set "COMP_SRCS=!COMP_SRCS! "%%f""
    )
)

cl /nologo /c %CF% %INC% /I"%COMP_DIR%\.." ^
    /Yu"std_include.hpp" /Fp"%GAME_OBJ%\comp.pch" ^
    /Fo"%GAME_OBJ%\\" ^
    %COMP_SRCS%
if errorlevel 1 goto :fail

:: Link DLL as d3d9.dll proxy (exports via .def, no d3d9.lib import)
link /nologo /DLL /SUBSYSTEM:WINDOWS /DEBUG /PDBCompress %LF% %LIBPATH% ^
    /DEF:"%ROOT%d3d9.def" ^
    /OUT:"%GAME_OUT%\d3d9.dll" ^
    /PDB:"%GAME_OUT%\%NAME%.pdb" ^
    "%GAME_OBJ%\*.obj" ^
    "%OUT%\_shared.lib" ^
    "%OUT%\imgui.lib" ^
    "%OUT%\minhook.lib" ^
    d3dx9.lib psapi.lib user32.lib gdi32.lib shell32.lib advapi32.lib ole32.lib
if errorlevel 1 goto :fail

:: Copy INI to build output (game-specific copy takes priority over template)
if "!CUSTOM_COMP!"=="1" (
    if exist "!GAME_DIR!\remix-comp-proxy.ini" (
        copy /y "!GAME_DIR!\remix-comp-proxy.ini" "%GAME_OUT%\remix-comp-proxy.ini" >nul
    ) else if exist "%ROOT%assets\remix-comp-proxy.ini" (
        copy /y "%ROOT%assets\remix-comp-proxy.ini" "%GAME_OUT%\remix-comp-proxy.ini" >nul
    )
) else (
    if exist "%ROOT%assets\remix-comp-proxy.ini" (
        copy /y "%ROOT%assets\remix-comp-proxy.ini" "%GAME_OUT%\remix-comp-proxy.ini" >nul
    )
)

echo.
echo === 32-bit build succeeded: %GAME_OUT%\d3d9.dll ===

:: -------------------------------------------------------
:: Step 5: 64-bit server-side light emitter
::         (remix_lights.dll + version.dll proxy)
::         Runs in a child cmd.exe — vcvarsall can't switch
::         from x86 to x64 inside the same process.
:: -------------------------------------------------------
echo [5/5] 64-bit light emitter

set "S64=%ROOT%src\server64"
set "OBJ64=%ROOT%build\obj\%CONFIG%\server64"
set "OUT64=%ROOT%build\bin\%CONFIG%\server64"
mkdir "%OBJ64%" 2>nul
mkdir "%OUT64%" 2>nul

:: Write x64 build commands to a temp script and run in a fresh cmd.exe
:: (vcvarsall cannot switch architectures within one process)
set "X64BAT=%ROOT%build\_build_x64.bat"
> "%X64BAT%" echo @echo off
>>"%X64BAT%" echo setlocal disabledelayedexpansion
>>"%X64BAT%" echo call "%VCVARS%" x64 ^>nul 2^>^&1
>>"%X64BAT%" echo if errorlevel 1 exit /b 1
>>"%X64BAT%" echo echo   remix_lights.dll
>>"%X64BAT%" echo cl /nologo /c /std:c++latest /W4 /O2 /MT /EHsc /I"%DEPS%\bridge_api" /I"%SRC%\shared\common" /DWIN32 /D_WINDOWS /DNDEBUG /Fo"%OBJ64%\remix_lights.obj" "%S64%\remix_lights.cpp"
>>"%X64BAT%" echo if errorlevel 1 exit /b 1
>>"%X64BAT%" echo link /nologo /DLL /SUBSYSTEM:WINDOWS /MACHINE:X64 /OUT:"%OUT64%\remix_lights.dll" "%OBJ64%\remix_lights.obj" kernel32.lib user32.lib
>>"%X64BAT%" echo if errorlevel 1 exit /b 1
>>"%X64BAT%" echo echo   version.dll
>>"%X64BAT%" echo cl /nologo /c /std:c++latest /W4 /O2 /MT /EHsc /DWIN32 /D_WINDOWS /DNDEBUG /Fo"%OBJ64%\version_proxy.obj" "%S64%\version_proxy.cpp"
>>"%X64BAT%" echo if errorlevel 1 exit /b 1
>>"%X64BAT%" echo link /nologo /DLL /SUBSYSTEM:WINDOWS /MACHINE:X64 /DEF:"%S64%\version_proxy.def" /OUT:"%OUT64%\version.dll" "%OBJ64%\version_proxy.obj" kernel32.lib
>>"%X64BAT%" echo if errorlevel 1 exit /b 1
:: Disable delayed expansion before running the x64 build script,
:: since vcvarsall.bat uses parentheses that break under !-expansion.
:: Pass needed variables through the endlocal barrier.
endlocal & set "X64BAT=%X64BAT%" & set "GAME_OUT=%GAME_OUT%" & set "OUT64=%OUT64%" & cmd.exe /c "%X64BAT%"
if errorlevel 1 goto :fail
del "%X64BAT%" 2>nul

echo.
echo === Build succeeded ===
echo   32-bit: %GAME_OUT%\d3d9.dll
echo   64-bit: %OUT64%\version.dll + remix_lights.dll
exit /b 0

:fail
echo.
echo === Build FAILED ===
exit /b 1
