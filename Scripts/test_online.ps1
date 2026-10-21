param([int]$Port = 18080)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$engine = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$node = 'C:\Program Files\nodejs\node.exe'
$evidence = Join-Path $root 'Saved/Evidence'
New-Item -ItemType Directory -Force -Path $evidence | Out-Null
$serverProcess = $null
$clients = @()
try {
    $serverProcess = Start-Process -FilePath $node -ArgumentList @('server.mjs', "$Port") -WorkingDirectory (Join-Path $root 'Services/multiplayer') -WindowStyle Hidden -PassThru
    $ready = $false
    for ($attempt=0; $attempt -lt 40; $attempt++) {
        try { $health=Invoke-RestMethod "http://127.0.0.1:$Port/health"; if ($health.status -eq 'ok') { $ready=$true; break } } catch {}
        Start-Sleep -Milliseconds 250
    }
    if (!$ready) { throw 'Multiplayer service did not start' }
    foreach ($role in @('host','guest')) {
        $name = if ($role -eq 'host') { 'RKC-Test-Host' } else { 'RKC-Test-Guest' }
        $log = Join-Path $evidence "online_$role.log"
        $arguments = @("`"$root/RockyKombat.uproject`"", '-game', '-nullrhi', '-NoSound', '-nosplash', '-unattended', '-rkc.FrontEnd=1', "-rkc.OnlineUrl=ws://127.0.0.1:$Port/play", "-rkc.PlayerName=$name", "-rkc.OnlineTest=$role", "-abslog=`"$log`"")
        $clients += Start-Process -FilePath $engine -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru
    }
    $deadline = (Get-Date).AddSeconds(160)
    while ((Get-Date) -lt $deadline -and @($clients | Where-Object { !$_.HasExited }).Count -gt 0) { Start-Sleep -Milliseconds 500 }
    foreach ($role in @('host','guest')) {
        $log = Join-Path $evidence "online_$role.log"
        $pass = @(Select-String -Path $log -Pattern 'RKC_ONLINE_MATCH_PASS')
        $fail = @(Select-String -Path $log -Pattern 'RKC_ONLINE_.*FAIL|Fatal error|Assertion failed')
        if ($pass.Count -ne 2 -or $fail.Count -gt 0) { throw "Online $role failed: inspect $log" }
        $pass.Line
    }
    'ONLINE_ENGINE_TEST: PASS (two real Unreal clients, match and rematch; loopback only)'
} finally {
    foreach ($process in $clients) { if (!$process.HasExited) { Stop-Process -Id $process.Id -Force } }
    if ($serverProcess -and !$serverProcess.HasExited) { Stop-Process -Id $serverProcess.Id -Force }
}
