@echo off
REM NC3000 emulator launcher - 32-bit build for Windows XP (double-click me).
REM
REM Same as run_nc3000.cmd but starts nc3000_xp.exe (i686 + msvcrt + SDL2 2.0.22).
REM Build it first with:  cd src_nc3000  &&  build.ps1 -XP
REM
REM To use another ROM set, change the ROM= line below
REM   user's own device dump : %~dp0roms\nc3000
REM   reference set + music  : %~dp0roms\nc3000_dl\nc3000
REM   Lee's original 3.4 set : %~dp0roms\lee2\nc3000

setlocal
set ROM=%~dp0roms\nc3000
if not exist "%ROM%.nor" (
    echo [ERROR] ROM not found: %ROM%.nor
    echo Edit the ROM= line in this file to point at your rom set.
    pause
    exit /b 1
)

cd /d "%~dp0src_nc3000"
if not exist nc3000_xp.exe (
    echo [ERROR] nc3000_xp.exe not found in %CD%
    echo Build it with:  build.ps1 -XP
    pause
    exit /b 1
)
nc3000_xp.exe --nc3000 --rom "%ROM%"
endlocal
