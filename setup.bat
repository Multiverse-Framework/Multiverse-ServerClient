@echo off
setlocal

set "CURRENT_DIR=%~dp0"
cd %CURRENT_DIR%
set "EXT_DIR=%CURRENT_DIR%ext"
set "MSYS2_DIR=%EXT_DIR%\msys2"
set "MINGW32_MAKE_EXE=%MSYS2_DIR%\mingw64\bin\mingw32-make.exe"

@REM if not exist "%MSYS2_DIR%" (
@REM     mkdir "%MSYS2_DIR%"
@REM     powershell -NoProfile -Command "curl -o '%MSYS2_DIR%\msys2-x86_64-20241208.exe' 'https://github.com/msys2/msys2-installer/releases/download/2025-02-21/msys2-x86_64-20250221.exe'; %MSYS2_DIR%\msys2-x86_64-20241208.exe in --confirm-command --accept-messages --root %MSYS2_DIR%"
@REM     powershell -NoProfile -Command "%MSYS2_DIR%\msys2_shell.cmd -defterm -here -no-start -c 'pacman -Syu --noconfirm'"
@REM     powershell -NoProfile -Command "%MSYS2_DIR%\msys2_shell.cmd -defterm -here -no-start -c 'pacman -Sy --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-make mingw-w64-x86_64-cmake'"
@REM     powershell -NoProfile -Command "%MSYS2_DIR%\msys2_shell.cmd -defterm -here -no-start -c 'pacman -Syu --noconfirm'"
@REM     echo MSYS2 installation complete.
@REM ) else (
@REM     echo MSYS2 already installed. Updating packages...
@REM     powershell -NoProfile -Command "%MSYS2_DIR%\msys2_shell.cmd -defterm -here -no-start -c 'pacman -Syu --noconfirm'"
@REM     echo Updating packages complete.
@REM )

@REM echo Building multiverse_server...
@REM cd multiverse_server
@REM %MINGW32_MAKE_EXE% clean
@REM %MINGW32_MAKE_EXE%
@REM cd ..

echo Building multiverse_client...
cd multiverse_client
@REM %MINGW32_MAKE_EXE% clean -f Makefile.mingw
@REM %MINGW32_MAKE_EXE% -f Makefile.mingw

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
nmake clean -f Makefile.nmake
nmake -f Makefile.nmake
cd ..
endlocal
