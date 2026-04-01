@echo off
setlocal EnableDelayedExpansion

for /f %%a in ('powershell -Command "[int](Get-Date -UFormat %%s)"') do set START_TIME=%%a

REM --------------------------------------------------------------------
REM Parse arguments: setup.bat [debug]
REM --------------------------------------------------------------------
set "MV_DEBUG_IO=0"
if /i not "%~1"=="debug" goto :no_debug
set "MV_DEBUG_IO=1"
echo === DEBUG MODE ENABLED - MV_DEBUG_IO=1 ===
echo.
:no_debug

set "CURRENT_DIR=%~dp0"
cd %CURRENT_DIR%

set "EXT_DIR=%CURRENT_DIR%ext"
set "MSYS2_DIR=%EXT_DIR%\msys2"
set "BIN_DIR=%CURRENT_DIR%bin"

REM --------------------------------------------------------------------
REM Create bin directory if not exists
REM --------------------------------------------------------------------
if not exist "%BIN_DIR%" (
    mkdir "%BIN_DIR%"
)

REM --------------------------------------------------------------------
REM MSYS2 Installation / Update
REM --------------------------------------------------------------------
if not exist "%MSYS2_DIR%" (
    echo MSYS2 not found. Installing...
    mkdir "%MSYS2_DIR%"
    powershell -NoProfile -Command "C:\Windows\System32\curl.exe --ssl-no-revoke -L -o '%MSYS2_DIR%\msys2-x86_64-20241208.exe' 'https://github.com/msys2/msys2-installer/releases/download/2025-02-21/msys2-x86_64-20250221.exe'; %MSYS2_DIR%\msys2-x86_64-20241208.exe in --confirm-command --accept-messages --root %MSYS2_DIR%"
    powershell -NoProfile -Command "%MSYS2_DIR%\msys2_shell.cmd -defterm -here -no-start -c 'pacman -Syu --noconfirm'"
    powershell -NoProfile -Command "%MSYS2_DIR%\msys2_shell.cmd -defterm -here -no-start -c 'pacman -Sy --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-make'"
    powershell -NoProfile -Command "%MSYS2_DIR%\msys2_shell.cmd -defterm -here -no-start -c 'pacman -Syu --noconfirm'"
    echo MSYS2 installation complete.
) else (
    echo MSYS2 already installed. Updating packages...
    powershell -NoProfile -Command "%MSYS2_DIR%\msys2_shell.cmd -defterm -here -no-start -c 'pacman -Syu --noconfirm'"
    echo Updating packages complete.
)

REM --------------------------------------------------------------------
REM Rust Installation / Check
REM --------------------------------------------------------------------
echo.
echo Checking Rust installation...

REM Add cargo to PATH for current session (in case it was just installed)
set "PATH=%USERPROFILE%\.cargo\bin;%PATH%"

where cargo >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo Rust not found. Installing Rust via rustup...
    powershell -NoProfile -Command "Invoke-WebRequest -Uri 'https://win.rustup.rs/x86_64' -OutFile '%TEMP%\rustup-init.exe'"
    "%TEMP%\rustup-init.exe" -y --default-toolchain stable
    del "%TEMP%\rustup-init.exe"
    set "PATH=%USERPROFILE%\.cargo\bin;%PATH%"
    echo Rust installation complete.
) else (
    echo Rust is already installed.
    cargo --version
    rustc --version
)

REM Verify Rust is available
where cargo >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Rust installation failed or PATH not updated.
    echo Please restart your terminal and run this script again.
    pause
    exit /b 1
)

REM --------------------------------------------------------------------
REM Build multiverse_server (C++ - using Makefile via MSYS2)
REM --------------------------------------------------------------------
echo.
echo ============================================================
echo Building multiverse_server [C++]
echo ============================================================
cd multiverse_server_cpp
powershell -NoProfile -Command "%MSYS2_DIR%\msys2_shell.cmd -defterm -here -no-start -mingw64 -c 'mingw32-make clean && mingw32-make all MV_DEBUG_IO=%MV_DEBUG_IO%'"
cd ..

REM --------------------------------------------------------------------
REM Build multiverse_client (C++ - using Makefile via MSYS2)
REM --------------------------------------------------------------------
echo.
echo ============================================================
echo Building multiverse_client [C++]
echo ============================================================
cd multiverse_client

REM Android build
if exist "%LOCALAPPDATA%\Android\Sdk" (
    echo.
    echo ============================================================
    echo Building libzmq for Android (ZMQ transport prerequisite)
    echo ============================================================
    if not exist "%CURRENT_DIR%lib\android\libzmq.a" (
        call "%EXT_DIR%\build_zmq_android.bat"
        if %ERRORLEVEL% NEQ 0 (
            echo WARNING: libzmq build failed - ZMQ transport will be disabled
        )
    ) else (
        echo libzmq.a already present, skipping rebuild.
        echo         ^(run ext\build_zmq_android.bat clean then re-run setup to force^)
    )
    echo.
    echo Building multiverse_client for Android...
    call build_android.bat
) else (
    echo.
    echo Android SDK not found. Skipping Android build.
)

echo.
echo Building multiverse_client with MSYS2...
powershell -NoProfile -Command "%MSYS2_DIR%\msys2_shell.cmd -defterm -here -no-start -mingw64 -c 'mingw32-make clean && mingw32-make all MV_DEBUG_IO=%MV_DEBUG_IO%'"

REM MSVC build (if Makefile.nmake exists)
if exist "Makefile.nmake" (
    echo.
    echo Building multiverse_client with MSVC...
    for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        call "%%i\VC\Auxiliary\Build\vcvarsall.bat" x64
    )
    nmake clean -f Makefile.nmake
    nmake -f Makefile.nmake
    call build_pybind.bat
)
cd ..

REM --------------------------------------------------------------------
REM Build multiverse_server_rust (Rust - NATIVE Windows, not MSYS2)
REM --------------------------------------------------------------------
echo.
echo ============================================================
echo Building multiverse_server_rust [Rust]
echo ============================================================
cd multiverse_server_rust

echo Cleaning Rust build...
cargo clean

echo Building Rust release...
cargo build --release

REM Copy binary to bin directory
if exist "target\release\multiverse_server_rust.exe" (
    copy /Y "target\release\multiverse_server_rust.exe" "%BIN_DIR%\multiverse_server_rust.exe"
    echo Installed: %BIN_DIR%\multiverse_server_rust.exe
) else (
    echo ERROR: Rust build failed - binary not found
    echo Check if Cargo.toml has the correct binary name
    dir /B target\release\*.exe 2>nul
)

cd ..

REM --------------------------------------------------------------------
REM Build Summary
REM --------------------------------------------------------------------
echo.
echo ============================================================
echo Build Summary
echo ============================================================
echo Binaries in %BIN_DIR%:
echo.
dir /B "%BIN_DIR%\*.exe" 2>nul

for /f %%a in ('powershell -Command "[int](Get-Date -UFormat %%s)"') do set END_TIME=%%a
set /a ELAPSED=%END_TIME% - %START_TIME%

echo.
echo ============================================================
echo Build completed in %ELAPSED% seconds
echo ============================================================

endlocal