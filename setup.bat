@echo off
setlocal EnableDelayedExpansion

for /f %%a in ('powershell -Command "[int](Get-Date -UFormat %%s)"') do set START_TIME=%%a

set "CURRENT_DIR=%~dp0"
cd %CURRENT_DIR%
set "EXT_DIR=%CURRENT_DIR%ext"
set "MSYS2_DIR=%EXT_DIR%\msys2"
set "MINGW32_MAKE_EXE=%MSYS2_DIR%\mingw64\bin\mingw32-make.exe"

if not exist "%MSYS2_DIR%" (
    mkdir "%MSYS2_DIR%"
    powershell -NoProfile -Command "curl -o '%MSYS2_DIR%\msys2-x86_64-20241208.exe' 'https://github.com/msys2/msys2-installer/releases/download/2025-02-21/msys2-x86_64-20250221.exe'; %MSYS2_DIR%\msys2-x86_64-20241208.exe in --confirm-command --accept-messages --root %MSYS2_DIR%"
    powershell -NoProfile -Command "%MSYS2_DIR%\msys2_shell.cmd -defterm -here -no-start -c 'pacman -Syu --noconfirm'"
    powershell -NoProfile -Command "%MSYS2_DIR%\msys2_shell.cmd -defterm -here -no-start -c 'pacman -Sy --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-make'"
    powershell -NoProfile -Command "%MSYS2_DIR%\msys2_shell.cmd -defterm -here -no-start -c 'pacman -Syu --noconfirm'"
    echo MSYS2 installation complete.
) else (
    echo MSYS2 already installed. Updating packages...
    powershell -NoProfile -Command "%MSYS2_DIR%\msys2_shell.cmd -defterm -here -no-start -c 'pacman -Syu --noconfirm'"
    echo Updating packages complete.
)

echo Building multiverse_server...
cd multiverse_server
%MINGW32_MAKE_EXE% clean
%MINGW32_MAKE_EXE%
cd ..

echo Building multiverse_client...
cd multiverse_client
%MINGW32_MAKE_EXE% clean -f Makefile.mingw
%MINGW32_MAKE_EXE% -f Makefile.mingw

for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    call "%%i\VC\Auxiliary\Build\vcvarsall.bat" x64
)
nmake clean -f Makefile.nmake
nmake -f Makefile.nmake
cd ..

for /f %%a in ('powershell -Command "[int](Get-Date -UFormat %%s)"') do set END_TIME=%%a
set /a ELAPSED=%END_TIME% - %START_TIME%

echo Build completed in %ELAPSED% seconds

endlocal
