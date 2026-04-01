@echo off
setlocal EnableDelayedExpansion

set "SDK=%LOCALAPPDATA%\Android\Sdk"
set "NDK_MIN=25.1.8937393"
set "NDK_DIR=%SDK%\ndk"
set "OBJ=%TEMP%\mv_objs"
set "OUT=..\lib\android"

if exist "%OBJ%" rmdir /s /q "%OBJ%"
mkdir "%OBJ%" >nul & if not exist "%OUT%" mkdir "%OUT%"

REM ===== Find latest NDK =====
for /f "delims=" %%V in ('dir "%NDK_DIR%" /b /ad ^| sort /r') do set "NDK_VER=%%V" & goto :v
echo ERROR: No NDK found & goto :fail

:v
if "%NDK_VER%" LSS "%NDK_MIN%" echo ERROR: NDK too old & goto :fail

set "NDK=%NDK_DIR%\%NDK_VER%"
set "TC=%NDK%\toolchains\llvm\prebuilt\windows-x86_64\bin"
if not exist "%TC%\clang++.exe" echo ERROR: clang++ missing & goto :fail

REM ===== Check if libzmq.a is available for ZMQ support =====
set "ZMQ_LIB=%OUT%\libzmq.a"
set "HAS_ZMQ=0"
if exist "%ZMQ_LIB%" set "HAS_ZMQ=1"

REM ===== Print selected config =====
echo.
echo ===== ANDROID BUILD CONFIG =====
echo SDK      = %SDK%
echo NDK_MIN  = %NDK_MIN%
echo NDK_DIR  = %NDK_DIR%
echo NDK_USED = %NDK_VER%
echo TOOLCHAIN= %TC%
if "%HAS_ZMQ%"=="1" (
    echo ZMQ      = ENABLED ^(%ZMQ_LIB%^)
) else (
    echo ZMQ      = DISABLED ^(run ext\build_zmq_android.bat to enable^)
)
echo ================================
echo.

set "PATH=%PATH%;%TC%"

REM ===== Resolve MV_DEBUG_IO (default=0 unless set by parent) =====
if not defined MV_DEBUG_IO set "MV_DEBUG_IO=0"
echo MV_DEBUG  = %MV_DEBUG_IO%
echo.

set "CFLAGS=--target=aarch64-linux-android21 -fPIC -O2 -DMV_DEBUG_IO=%MV_DEBUG_IO% -I.\\ -I.\\..\\\\ext\\\\include -I.\\..\\\\ext\\\\"
set "LDFLAGS=-shared -fPIC -O2 -static-libstdc++ -llog"

REM ===== Release build =====
echo.
echo [RELEASE] Building base lib...
clang++ %CFLAGS% %LDFLAGS% -o "%OUT%\libmultiverse_client.so" multiverse_client.cpp || goto :fail

echo [RELEASE] Building transports...
clang++ %CFLAGS% -DUSE_TCP=1 -c transport\tcp_client_transport.cpp -o "%OBJ%\tcp.o" || goto :fail
clang++ %CFLAGS% -DUSE_UDP=1 -c transport\udp_client_transport.cpp -o "%OBJ%\udp.o" || goto :fail

echo [RELEASE] Building combined lib (TCP+UDP)...
clang++ %CFLAGS% %LDFLAGS% -DUSE_TCP=1 -DUSE_UDP=1 ^
  -o "%OUT%\libmultiverse_client_tcp_udp.so" multiverse_client.cpp "%OBJ%\tcp.o" "%OBJ%\udp.o" || goto :fail

if "%HAS_ZMQ%"=="1" call :build_zmq_release

REM ===== Debug build (only when MV_DEBUG_IO=1) =====
if not "%MV_DEBUG_IO%"=="1" goto :skip_debug

set "OUT_DBG=%OUT%\debug"
if not exist "%OUT_DBG%" mkdir "%OUT_DBG%"
set "CFLAGS_DBG=--target=aarch64-linux-android21 -fPIC -g -O0 -DMV_DEBUG_IO=1 -I.\\ -I.\\..\\\\ext\\\\include -I.\\..\\\\ext\\\\"
set "LDFLAGS_DBG=-shared -fPIC -g -O0 -static-libstdc++ -llog"

echo.
echo [DEBUG] Building transports...
clang++ %CFLAGS_DBG% -DUSE_TCP=1 -c transport\tcp_client_transport.cpp -o "%OBJ%\tcp_dbg.o" || goto :fail
clang++ %CFLAGS_DBG% -DUSE_UDP=1 -c transport\udp_client_transport.cpp -o "%OBJ%\udp_dbg.o" || goto :fail

echo [DEBUG] Building combined lib (TCP+UDP)...
clang++ %CFLAGS_DBG% %LDFLAGS_DBG% -DUSE_TCP=1 -DUSE_UDP=1 ^
  -o "%OUT_DBG%\libmultiverse_client_tcp_udp.so" multiverse_client.cpp "%OBJ%\tcp_dbg.o" "%OBJ%\udp_dbg.o" || goto :fail

if "%HAS_ZMQ%"=="1" call :build_zmq_debug
goto :done

:skip_debug
echo.
echo [DEBUG] Skipped (run "setup.bat debug" to enable)

:done
rmdir /s /q "%OBJ%" & echo DONE & exit /b 0

:fail
rmdir /s /q "%OBJ%" 2>nul & echo FAILED & exit /b 1

:build_zmq_release
echo [RELEASE] Building ZMQ transport...
clang++ %CFLAGS% -DUSE_ZMQ=1 -c transport\zmq_client_transport.cpp -o "%OBJ%\zmq.o" || goto :fail
echo [RELEASE] Building combined lib (TCP+UDP+ZMQ)...
clang++ %CFLAGS% %LDFLAGS% -DUSE_TCP=1 -DUSE_UDP=1 -DUSE_ZMQ=1 -o "%OUT%\libmultiverse_client_all.so" multiverse_client.cpp "%OBJ%\tcp.o" "%OBJ%\udp.o" "%OBJ%\zmq.o" -Wl,--whole-archive "%ZMQ_LIB%" -Wl,--no-whole-archive || goto :fail
exit /b 0

:build_zmq_debug
echo [DEBUG] Building ZMQ transport...
clang++ %CFLAGS_DBG% -DUSE_ZMQ=1 -c transport\zmq_client_transport.cpp -o "%OBJ%\zmq_dbg.o" || goto :fail
echo [DEBUG] Building combined lib (TCP+UDP+ZMQ)...
clang++ %CFLAGS_DBG% %LDFLAGS_DBG% -DUSE_TCP=1 -DUSE_UDP=1 -DUSE_ZMQ=1 -o "%OUT_DBG%\libmultiverse_client_all.so" multiverse_client.cpp "%OBJ%\tcp_dbg.o" "%OBJ%\udp_dbg.o" "%OBJ%\zmq_dbg.o" -Wl,--whole-archive "%ZMQ_LIB%" -Wl,--no-whole-archive || goto :fail
exit /b 0