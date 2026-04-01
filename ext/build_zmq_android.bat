@echo off
setlocal EnableDelayedExpansion

REM ============================================================
REM Cross-compile libzmq for Android arm64-v8a
REM
REM Prerequisites:
REM   - Android SDK with NDK >= 29.0 installed
REM   - CMake (from Android SDK or system PATH)
REM   - Git
REM
REM Output:
REM   ..\lib\android\libzmq.so       (shared library)
REM   ..\lib\android\libzmq.a        (static library)
REM
REM Usage:
REM   build_zmq_android.bat           (default: shared + static)
REM   build_zmq_android.bat clean     (remove build dir)
REM ============================================================

set "SCRIPT_DIR=%~dp0"
set "SDK=%LOCALAPPDATA%\Android\Sdk"
set "NDK_DIR=%SDK%\ndk"
set "ZMQ_SRC=%SCRIPT_DIR%libzmq"
set "BUILD_DIR=%SCRIPT_DIR%libzmq_build_android"
set "OUT_DIR=%SCRIPT_DIR%..\lib\android"
set "ANDROID_API=21"
set "ANDROID_ABI=arm64-v8a"

REM ===== Handle clean =====
if /i "%~1"=="clean" (
    echo Cleaning libzmq build...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    echo Done.
    exit /b 0
)

REM ===== Find NDK =====
if not exist "%NDK_DIR%" (
    echo ERROR: NDK directory not found: %NDK_DIR%
    exit /b 1
)
for /f "delims=" %%V in ('dir "%NDK_DIR%" /b /ad ^| sort /r') do set "NDK_VER=%%V" & goto :found_ndk
echo ERROR: No NDK versions found
exit /b 1
:found_ndk
set "NDK=%NDK_DIR%\%NDK_VER%"
set "TOOLCHAIN_FILE=%NDK%\build\cmake\android.toolchain.cmake"

if not exist "%TOOLCHAIN_FILE%" (
    echo ERROR: Android toolchain file not found: %TOOLCHAIN_FILE%
    exit /b 1
)

REM ===== Find CMake and Ninja from Android SDK =====
set "CMAKE_BIN="
set "NINJA_BIN="

REM Try Android SDK cmake first (has bundled ninja)
for /f "delims=" %%D in ('dir "%SDK%\cmake" /b /ad 2^>nul ^| sort /r') do (
    if exist "%SDK%\cmake\%%D\bin\cmake.exe" (
        set "CMAKE_BIN=%SDK%\cmake\%%D\bin\cmake.exe"
        if exist "%SDK%\cmake\%%D\bin\ninja.exe" (
            set "NINJA_BIN=%SDK%\cmake\%%D\bin\ninja.exe"
        )
        goto :found_cmake
    )
)
REM Fall back to system cmake
where cmake >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    for /f "delims=" %%P in ('where cmake') do set "CMAKE_BIN=%%P"
    where ninja >nul 2>&1
    if %ERRORLEVEL% EQU 0 (
        for /f "delims=" %%P in ('where ninja') do set "NINJA_BIN=%%P"
    )
    goto :found_cmake
)
echo ERROR: CMake not found. Install via Android SDK Manager or system PATH.
exit /b 1
:found_cmake

echo.
echo ===== LIBZMQ ANDROID BUILD =====
echo NDK      = %NDK%
echo NDK_VER  = %NDK_VER%
echo CMAKE    = %CMAKE_BIN%
echo NINJA    = %NINJA_BIN%
echo ABI      = %ANDROID_ABI%
echo API      = %ANDROID_API%
echo SRC      = %ZMQ_SRC%
echo BUILD    = %BUILD_DIR%
echo OUT      = %OUT_DIR%
echo =================================
echo.

REM ===== Clone libzmq if not present =====
if not exist "%ZMQ_SRC%\CMakeLists.txt" (
    echo Cloning libzmq...
    git clone --depth 1 --branch v4.3.5 https://github.com/zeromq/libzmq.git "%ZMQ_SRC%"
    if errorlevel 1 (
        echo ERROR: Failed to clone libzmq
        exit /b 1
    )
)

REM ===== Create build directory =====
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

REM ===== Configure with CMake =====
echo.
echo [1/3] Configuring libzmq with CMake...
"%CMAKE_BIN%" -S "%ZMQ_SRC%" -B "%BUILD_DIR%" ^
    -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN_FILE%" ^
    -DANDROID_ABI=%ANDROID_ABI% ^
    -DANDROID_PLATFORM=android-%ANDROID_API% ^
    -DANDROID_STL=c++_static ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DBUILD_SHARED=ON ^
    -DBUILD_STATIC=ON ^
    -DBUILD_TESTS=OFF ^
    -DWITH_DOCS=OFF ^
    -DWITH_PERF_TOOL=OFF ^
    -DWITH_LIBSODIUM=OFF ^
    -DWITH_TLS=OFF ^
    -DWITH_MILITANT=OFF ^
    -DENABLE_DRAFTS=OFF ^
    -DCMAKE_MAKE_PROGRAM="%NINJA_BIN%" ^
    -G "Ninja"

if errorlevel 1 (
    echo ERROR: CMake configure failed
    exit /b 1
)

REM ===== Build =====
echo.
echo [2/3] Building libzmq...
"%CMAKE_BIN%" --build "%BUILD_DIR%" --config Release -j
if errorlevel 1 (
    echo ERROR: CMake build failed
    exit /b 1
)

REM ===== Copy output =====
echo.
echo [3/3] Copying output to %OUT_DIR%...

REM Find and copy shared library
for /r "%BUILD_DIR%" %%F in (libzmq.so) do (
    echo   Copying %%F
    copy /Y "%%F" "%OUT_DIR%\libzmq.so" >nul
)

REM Find and copy static library
for /r "%BUILD_DIR%" %%F in (libzmq.a) do (
    echo   Copying %%F
    copy /Y "%%F" "%OUT_DIR%\libzmq.a" >nul
)

REM Verify output
if not exist "%OUT_DIR%\libzmq.so" (
    echo WARNING: libzmq.so not found in build output
    echo Searching for any zmq libraries...
    dir /s /b "%BUILD_DIR%\*.so" "%BUILD_DIR%\*.a" 2>nul
) else (
    echo.
    echo ===== BUILD SUCCESS =====
    echo   %OUT_DIR%\libzmq.so
    if exist "%OUT_DIR%\libzmq.a" echo   %OUT_DIR%\libzmq.a
    echo ==========================
)

exit /b 0
