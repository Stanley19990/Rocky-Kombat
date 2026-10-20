<#
  Rocky Kombat - patch AndroidRuntimeSettings for a release-signed build.

  Writes the release keystore, its alias/password, and the version fields into
  the project's Config\DefaultEngine.ini, scoped to the
  [/Script/AndroidRuntimeSettings.AndroidRuntimeSettings] section only.

  The caller MUST restore the original file afterwards (package_android.bat
  keeps a .presign copy and restores it even when the build fails).

  Parameters are passed by the batch wrapper; no secret is ever echoed.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ConfigPath,
    [Parameter(Mandatory=$true)][string]$KeystorePath,
    [Parameter(Mandatory=$true)][string]$KeystorePassword,
    [Parameter(Mandatory=$true)][string]$VersionDisplay,
    [Parameter(Mandatory=$true)][string]$StoreVersion,
    [string]$KeyAlias = 'rkc-release'
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $ConfigPath)) {
    throw "Config not found: $ConfigPath"
}
if (-not (Test-Path -LiteralPath $KeystorePath)) {
    throw "Keystore not found: $KeystorePath"
}

# Read as UTF8, preserve every line we are not deliberately changing.
$lines = [System.IO.File]::ReadAllLines($ConfigPath)
$out = New-Object 'System.Collections.Generic.List[string]'

$sectionPattern = '^\s*\[(?<name>[^\]]+)\]'
$inTargetSection = $false
$sectionSeen = $false
$patched = @{}

foreach ($line in $lines) {
    $m = [regex]::Match($line, $sectionPattern)
    if ($m.Success) {
        # Leaving the section: if we never saw the keys, append them before we go.
        if ($inTargetSection -and -not $sectionSeen) { $sectionSeen = $true }
        $inTargetSection = ($m.Groups['name'].Value -eq '/Script/AndroidRuntimeSettings.AndroidRuntimeSettings')
    }

    $newLine = $line
    if ($inTargetSection -and $line -match '^\s*(?<key>[A-Za-z0-9_]+)\s*=') {
        $key = $matches['key']
        switch ($key) {
            'KeyStore'         { $newLine = "KeyStore=$KeystorePath";           $patched[$key] = $true }
            'KeyAlias'         { $newLine = "KeyAlias=$KeyAlias";               $patched[$key] = $true }
            'KeyStorePassword' { $newLine = "KeyStorePassword=$KeystorePassword"; $patched[$key] = $true }
            'KeyPassword'      { $newLine = "KeyPassword=$KeystorePassword";    $patched[$key] = $true }
            'VersionDisplayName' { $newLine = "VersionDisplayName=$VersionDisplay"; $patched[$key] = $true }
            'StoreVersion'     { $newLine = "StoreVersion=$StoreVersion";        $patched[$key] = $true }
            default            { }
        }
    }
    $out.Add($newLine)
}

$required = @('KeyStore','KeyAlias','KeyStorePassword','KeyPassword','VersionDisplayName','StoreVersion')
$missing = $required | Where-Object { -not $patched.ContainsKey($_) }
if ($missing.Count -gt 0) {
    throw ("AndroidRuntimeSettings section is missing required keys: " + ($missing -join ', ') +
           ". Refusing to build with partial signing config.")
}

[System.IO.File]::WriteAllLines($ConfigPath, $out, (New-Object System.Text.UTF8Encoding($false)))

# Report what changed, never the secret values.
Write-Host ("  signing overlay applied: keystore={0} alias={1} version={2} code={3}" -f `
    (Split-Path $KeystorePath -Leaf), $KeyAlias, $VersionDisplay, $StoreVersion)
