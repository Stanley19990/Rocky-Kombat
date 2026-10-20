@echo off
setlocal
set OUT=C:\RockyKombat\Saved\Evidence\Recording
if not exist "%OUT%" mkdir "%OUT%"
set TS=acceptance_20261020_1850

cd /d C:\RockyKombat\Packaged\Windows\RockyKombat\Binaries\Win64
echo launching packaged build, windowed 1280x720, -d3d11
start "" RockyKombat.exe -rkc.Script=playthrough -windowed -ResX=1280 -ResY=720 -d3d11
timeout /t 4 /nobreak >nul

echo capturing 150s of desktop + microphone array
ffmpeg -hide_banner -loglevel warning -f gdigrab -framerate 15 -i desktop -f dshow -i audio="Microphone Array (Realtek Audio)" -t 150 -c:v libx264 -preset ultrafast -crf 24 -pix_fmt yuv420p -c:a aac -b:a 128k -y "%OUT%\%TS%.mp4"
echo FFMPEG_EXIT=%ERRORLEVEL%

taskkill /F /IM RockyKombat.exe >nul 2>&1
echo capture finished
endlocal
