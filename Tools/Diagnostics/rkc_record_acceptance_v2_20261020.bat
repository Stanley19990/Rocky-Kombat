@echo off
setlocal
set OUT=C:\RockyKombat\Saved\Evidence\Recording
if not exist "%OUT%" mkdir "%OUT%"
set TS=acceptance_v2_20261020_1900

cd /d C:\RockyKombat\Packaged\Windows\RockyKombat\Binaries\Win64
echo launching packaged build, TSR off, windowed 1280x720
start "" RockyKombat.exe -rkc.Script=playthrough -windowed -ResX=1280 -ResY=720 -d3d11 -ExecCmds="r.TSR.Enable=0,r.ScreenPercentage=100"
ping -n 13 127.0.0.1 >nul

echo capturing 140s of desktop + microphone array
ffmpeg -hide_banner -loglevel warning -f gdigrab -framerate 15 -i desktop -f dshow -i audio="Microphone Array (Realtek Audio)" -t 140 -c:v libx264 -preset ultrafast -crf 22 -pix_fmt yuv420p -c:a aac -b:a 128k -y "%OUT%\%TS%.mp4"
echo FFMPEG_EXIT=%ERRORLEVEL%

taskkill /F /IM RockyKombat.exe >nul 2>&1
echo capture finished
endlocal
