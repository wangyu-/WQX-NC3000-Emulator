@echo off
REM NC3000 emulator launcher (double-click me).
REM
REM Why a launcher: the emulator loads resource\lcdstripe_*.bmp/json relative to
REM the working directory, and its default --rom path is relative too. So we must
REM cd into src_nc3000 first and pass an explicit --rom path.
REM
REM To use another ROM set, change the ROM= line below
REM   user's own device dump : %~dp0roms\nc3000
REM   reference set + music  : %~dp0roms\nc3000_dl\nc3000
REM   Lee's original 3.4 set : %~dp0roms\lee2\nc3000

setlocal
set ROM=%~dp0roms\nc3000_dl\nc3000
if not exist "%ROM%.nor" (
    echo [ERROR] ROM not found: %ROM%.nor
    echo Edit the ROM= line in this file to point at your rom set.
    pause
    exit /b 1
)

cd /d "%~dp0src_nc3000"
nc3000.exe --nc3000 --rom "%ROM%"
endlocal
