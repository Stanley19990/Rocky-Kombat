param(
    [Parameter(Mandatory = $true)][int]$ProcessId,
    [Parameter(Mandatory = $true)][string]$Text
)
$wsh = New-Object -ComObject WScript.Shell
$proc = Get-Process -Id $ProcessId -ErrorAction Stop
if (-not $wsh.AppActivate($proc.Id)) { Write-Error "could not activate pid $ProcessId"; exit 1 }
Start-Sleep -Milliseconds 600
$wsh.SendKeys($Text)
"sent to pid $ProcessId : $Text"
