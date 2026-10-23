[CmdletBinding()]
param(
    [ValidateSet('Development','Shipping')][string]$Configuration = 'Development',
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8'
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$engineDir = Join-Path $EngineRoot 'Engine'
$required = @(
    (Join-Path $engineDir 'Binaries\Android\UnrealGame.target'),
    (Join-Path $engineDir 'Build\Android\Java'),
    (Join-Path $env:JAVA_HOME 'bin\java.exe'),
    (Join-Path $env:NDKROOT 'source.properties')
)
foreach ($requiredPath in $required) {
    if (-not (Test-Path -LiteralPath $requiredPath)) { throw "Required Android build input missing: $requiredPath" }
}
$version = @{}
foreach ($line in [IO.File]::ReadAllLines((Join-Path $PSScriptRoot 'version.txt'))) {
    if ($line -match '^(VERSION_DISPLAY|STORE_VERSION)=(.+)$') { $version[$matches[1]] = $matches[2].Trim() }
}
if (-not $version.VERSION_DISPLAY -or $version.STORE_VERSION -notmatch '^\d+$') { throw 'Invalid Android version.txt' }
$engineConfig = [IO.File]::ReadAllText((Join-Path $projectRoot 'Config\DefaultEngine.ini'))
foreach ($setting in @("VersionDisplayName=$($version.VERSION_DISPLAY)","StoreVersion=$($version.STORE_VERSION)",'bPackageDataInsideApk=True','bBuildForArm64=True','bBuildForX8664=False','bBuildForES31=True','bSupportsVulkan=False','bShowLaunchImage=True')) {
    if ($engineConfig -notmatch ('(?m)^' + [regex]::Escape($setting) + '\r?$')) { throw "Android config/version mismatch: $setting" }
}
$stamp = [DateTime]::UtcNow.ToString('yyyyMMdd_HHmmss')
if (!(Test-Path -LiteralPath (Join-Path $projectRoot 'Build/Android/res/drawable/icon.png'))) { throw 'Missing Rockycat launcher artwork. Run Tools/Android/prepare_branding.ps1.' }
$archive = Join-Path $projectRoot "Saved\Android\test_$stamp"
$output = Join-Path $projectRoot 'Dist\Android'
New-Item -ItemType Directory -Path $archive,$output -Force | Out-Null
$logPath = Join-Path $archive 'package.log'
$uat = Join-Path $engineDir 'Build\BatchFiles\RunUAT.bat'
$uatArguments = @(
    'BuildCookRun', "-project=$projectRoot\RockyKombat.uproject", '-noP4', '-utf8output',
    '-platform=Android', "-clientconfig=$Configuration", '-cookflavor=ETC2',
    '-build', '-cook', '-stage', '-package', '-pak', '-iostore', '-compressed',
    '-archive', "-archivedirectory=$archive", '-nocompileeditor', '-nohotreload',
    # Avoid UBA retaining a second large header cache on this memory-limited PC.
    '-UbtArgs=-NoUBA',
    '-map=/Engine/Maps/Entry'
)
& $uat @uatArguments 2>&1 | Tee-Object -FilePath $logPath
if ($LASTEXITCODE -ne 0) { throw "Android BuildCookRun failed with exit code $LASTEXITCODE. Log: $logPath" }
$apks = @(Get-ChildItem -LiteralPath $archive -Recurse -Filter '*.apk' -File)
if ($apks.Count -ne 1) { throw "Expected one fresh ARM64 APK in this archive, found $($apks.Count): $archive" }
$buildTools = Join-Path $env:ANDROID_HOME 'build-tools\36.0.0'
$badging = @(& (Join-Path $buildTools 'aapt.exe') dump badging $apks[0].FullName 2>&1)
if ($LASTEXITCODE -ne 0) { throw 'aapt could not read the APK manifest' }
$badgingText = $badging -join "`n"
foreach ($expected in @("name='com.rockycat.rockykombat'", "versionName='$($version.VERSION_DISPLAY)'", "versionCode='$($version.STORE_VERSION)'", "native-code: 'arm64-v8a'")) {
    if (-not $badgingText.Contains($expected)) { throw "APK manifest verification failed: $expected" }
}
& (Join-Path $buildTools 'apksigner.bat') verify --verbose --print-certs $apks[0].FullName
if ($LASTEXITCODE -ne 0) { throw 'APK signature verification failed' }
& (Join-Path $buildTools 'zipalign.exe') -c -P 16 4 $apks[0].FullName
if ($LASTEXITCODE -ne 0) { throw 'APK zip alignment verification failed' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$apkZip = [IO.Compression.ZipFile]::OpenRead($apks[0].FullName)
try {
    $embeddedContent = @($apkZip.Entries | Where-Object { $_.FullName -match '^assets/.*\.(pak|ucas|utoc|obb(?:\.png)?)$' })
    if ($embeddedContent.Count -eq 0) { throw 'APK has no embedded game content; refusing to publish an incomplete install' }
    $contentEntries = @($embeddedContent | ForEach-Object { $_.FullName })
} finally { $apkZip.Dispose() }
$apkName = "RockyKombat-$($version.VERSION_DISPLAY)-build$($version.STORE_VERSION)-arm64.apk"
$apkPath = Join-Path $output $apkName
if (Test-Path -LiteralPath $apkPath) { throw "Versioned APK already exists; increment StoreVersion before replacing it: $apkPath" }
Copy-Item -LiteralPath $apks[0].FullName -Destination $apkPath
$hash = (Get-FileHash -LiteralPath $apkPath -Algorithm SHA256).Hash.ToLowerInvariant()
$sourceRevision = (& git -C $projectRoot rev-parse HEAD).Trim()
$sourceDirty = @(& git -C $projectRoot status --porcelain --untracked-files=no).Count -gt 0
$buildInfo = [ordered]@{
    apk=$apkName; version=$version.VERSION_DISPLAY; build=[int]$version.STORE_VERSION
    configuration=$Configuration; architecture='arm64-v8a'; package='com.rockycat.rockykombat'
    sha256=$hash; bytes=(Get-Item -LiteralPath $apkPath).Length; builtUtc=[DateTime]::UtcNow.ToString('o')
    sourceRevision=$sourceRevision; sourceDirty=$sourceDirty
    engine=(Get-Content -LiteralPath (Join-Path $engineDir 'Build\Build.version') -Raw | ConvertFrom-Json)
    signing='Local Android debug identity (internal test only)'; embeddedContent=$contentEntries
    graphics='OpenGL ES 3.2 only; Vulkan disabled in this compatibility candidate.'
    startupStatus='Previous build exits at splash on an unconnected user phone. Cause unconfirmed; this candidate is not phone-verified.'
    androidDeviceTests='Not run by packaging; device testing must be recorded separately.'
    knownIssues=@('Startup crash reported on build 3; compatibility changes in build 4 have not been verified on that phone.','Android rendering, performance, audible output and touch controls still require device testing.','Four-pose walking/punch artwork remains provisional.','No dedicated hit-reaction or get-up artwork.','Online Versus requires a separately hosted service; this build has no public online endpoint.','Android APK only: iPhones require a separate signed iOS build, which is not included.')
}
$utf8 = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Join-Path $output 'BUILD_INFO.json'), ($buildInfo | ConvertTo-Json -Depth 5), $utf8)
[IO.File]::WriteAllText((Join-Path $output 'SHA256SUMS.txt'), "$hash  $apkName`n", $utf8)
Write-Output "APK_READY=$apkPath"
Write-Output "SHA256=$hash"
