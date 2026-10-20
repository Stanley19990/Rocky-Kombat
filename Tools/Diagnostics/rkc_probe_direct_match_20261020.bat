@echo off
setlocal
set OUT=C:\RockyKombat\Saved\Evidence\Recording
if not exist "%OUT%" mkdir "%OUT%"
cd /d C:\RockyKombat\Packaged\Windows\RockyKombat\Binaries\Win64
echo launching direct match, no front end, windowed
start "" RockyKombat.exe -rkc.FrontEnd=0 -windowed -ResX=1280 -ResY=720 -d3d11
ping -n 15 127.0.0.1 >nul
ffmpeg -hide_banner -loglevel error -f gdigrab -i desktop -frames:v 1 -y "%OUT%\probe_directmatch_14s.png"
ping -n 4 127.0.0.1 >nul
ffmpeg -hide_banner -loglevel error -f gdigrab -i desktop -frames:v 1 -y "%OUT%\probe_directmatch_17s.png"
taskkill /F /IM RockyKombat.exe >nul 2>&1
echo probe finished
endlocal
