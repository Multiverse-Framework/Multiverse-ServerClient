@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM Minor versions you actually have installed
set "PY_MINORS=8 9 10 11 12 13 14"

for %%M in (%PY_MINORS%) do call :BUILD_ONE %%M
DEL /Q *.exp *.lib *.obj >NUL 2>NUL
echo Done.
exit /b 0

:BUILD_ONE
setlocal EnableExtensions EnableDelayedExpansion
set "M=%~1"
set "PY=python3.%M%"

echo ------------------------------------------------------------
echo Building multiverse_client pybind for !PY!...

REM Query python paths
set "TMP1=%TEMP%\mv_py_inc.txt"
set "TMP2=%TEMP%\mv_pyb_inc.txt"
set "TMP3=%TEMP%\mv_ext_suffix.txt"
set "TMP4=%TEMP%\mv_py_libname.txt"

"!PY!" -c "import sysconfig; print(sysconfig.get_path('include') or '')" > "!TMP1!"
"!PY!" -c "import pybind11; print(pybind11.get_include() or '')" > "!TMP2!"
"!PY!" -c "import sysconfig; print(sysconfig.get_config_var('EXT_SUFFIX') or '.pyd')" > "!TMP3!"
"!PY!" -c "import sys; print(f'python{sys.version_info[0]}{sys.version_info[1]}.lib')" > "!TMP4!"

set /p PY_INC=<"!TMP1!"
set /p PYB_INC=<"!TMP2!"
set /p EXT_SUFFIX=<"!TMP3!"
set /p PY_LIBNAME=<"!TMP4!"

del /q "!TMP1!" "!TMP2!" "!TMP3!" "!TMP4!" >nul 2>nul

for %%D in ("!PY_INC!\..") do set "PY_ROOT=%%~fD"
set "PY_LIBDIR=!PY_ROOT!\libs"

set "OUT=..\lib\windows\multiverse_client_pybind!EXT_SUFFIX!"

echo   PY_INC    = !PY_INC!
echo   PYB_INC   = !PYB_INC!
echo   PY_LIBDIR = !PY_LIBDIR!
echo   PY_LIB    = !PY_LIBNAME!
echo   OUT       = !OUT!

where cl >nul 2>nul
if errorlevel 1 (
  echo ERROR: cl.exe not found. Use x64 Native Tools Command Prompt for VS.
  endlocal & exit /b 1
)

if not exist "..\lib\windows\multiverse_client_all.lib" (
  echo ERROR: Missing ..\lib\windows\multiverse_client_all.lib
  echo        Build it first: nmake clean ^&^& nmake variants_static
  endlocal & exit /b 1
)

REM Optional: link pythonXY.lib (you DO have it, as you showed)
if not exist "!PY_LIBDIR!\!PY_LIBNAME!" (
  echo ERROR: Missing !PY_LIBDIR!\!PY_LIBNAME!
  endlocal & exit /b 1
)

if exist "!OUT!" del /q "!OUT!"

cl /nologo /O2 /EHsc /std:c++17 /MD /LD ^
  /I "!PY_INC!" ^
  /I "!PYB_INC!" ^
  /I "..\ext\include" ^
  /I "..\ext" ^
  multiverse_client_pybind.cpp ^
  /link /DLL ^
  /OUT:"!OUT!" ^
  /LIBPATH:"..\lib\windows" ^
  /LIBPATH:"!PY_LIBDIR!" ^
  multiverse_client_all.lib ws2_32.lib libzmq-mt-4_3_5.lib "!PY_LIBNAME!"

if errorlevel 1 (
  echo ERROR building for !PY!
  endlocal & exit /b 1
)

if not exist "!OUT!" (
  echo ERROR: build finished but output not found: !OUT!
  endlocal & exit /b 1
)

for %%F in ("!OUT!") do set "OUT_SIZE=%%~zF"
echo === OUT size: !OUT_SIZE! bytes
if !OUT_SIZE! LSS 100000 (
  echo ERROR: Output is too small; not a real .pyd. Aborting.
  endlocal & exit /b 1
)

echo.
endlocal & exit /b 0
