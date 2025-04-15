@echo off
setlocal

where mingw32-make >nul 2>&1
if errorlevel 1 (
    echo "mingw32-make.exe not found!"
    exit /b 1
)

echo Building multiverse_server...
cd multiverse_server
mingw32-make clean
mingw32-make
cd ..

echo Building multiverse_client...
cd multiverse_client
mingw32-make clean
mingw32-make
cd ..
endlocal
