[CmdletBinding()]
param([string]$ArtifactDirectory = '', [string]$Repository = 'Stanley19990/Rocky-Kombat')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (!$ArtifactDirectory) { $ArtifactDirectory = Join-Path $root 'Dist/Android' }
$infoPath = Join-Path $ArtifactDirectory 'BUILD_INFO.json'
$info = Get-Content -LiteralPath $infoPath -Raw | ConvertFrom-Json
$apk = Join-Path $ArtifactDirectory $info.apk
if (!(Test-Path -LiteralPath $apk) -or [IO.Path]::GetExtension($apk) -ne '.apk') { throw 'An actual APK is required' }
$hash = (Get-FileHash -LiteralPath $apk -Algorithm SHA256).Hash.ToLowerInvariant()
if ($hash -ne $info.sha256) { throw 'APK checksum does not match BUILD_INFO.json' }
if ($info.sourceDirty) { throw 'Build source was dirty. Commit the source and rebuild before publication.' }
if ($Repository -ne 'Stanley19990/Rocky-Kombat') { throw 'This publisher is scoped to Stanley19990/Rocky-Kombat' }
$tag = "v$($info.version)"
$credentialLines = $null; $token = $null; $headers = $null
try {
    $credentialLines = @("protocol=https`nhost=github.com`n" | git credential fill)
    if ($LASTEXITCODE -ne 0) { throw 'GitHub credential lookup failed' }
    foreach ($line in $credentialLines) { if ($line.StartsWith('password=')) { $token = $line.Substring(9) } }
    if (!$token) { throw 'No GitHub credential available; APK remains ready locally.' }
    $headers = @{ Authorization="Bearer $token"; Accept='application/vnd.github+json'; 'X-GitHub-Api-Version'='2022-11-28'; 'User-Agent'='RockyKombat-mobile-release' }
    $base = "https://api.github.com/repos/$Repository"
    $repo = Invoke-RestMethod -Uri $base -Headers $headers
    if (!$repo.permissions.push) { throw 'GitHub credential has no repository write permission' }
    $existing = $null
    try { $existing = Invoke-RestMethod -Uri "$base/releases/tags/$tag" -Headers $headers }
    catch { if (!$_.Exception.Response -or [int]$_.Exception.Response.StatusCode -ne 404) { throw } }
    if ($existing) { throw "Release $tag already exists. Refusing to replace published assets." }
    $body = @"
Internal Android alpha: $($info.version), build $($info.build), ARM64. Unreal $($info.engine.MajorVersion).$($info.engine.MinorVersion).$($info.engine.PatchVersion).

Download **$($info.apk)** to an ARM64 Android phone (Android 8.0/API 26 or newer). Open the APK, allow installation from that browser/file manager if prompted, then install. Game content is included; no separate OBB is needed. This is a disposable debug-signed test package.

First match: **Play > Rocky Boxing Arena > Play Against Computer > choose your fighter > choose the CPU fighter > Champion > Fight**. Left stick moves. Right buttons are Jab, Heavy, Block (hold), and Dodge. Choose Rematch on Results.

Known limits: four-pose animations remain provisional; no dedicated hit-reaction/get-up sequence. Android device rendering, performance, audio and touch have not been verified on a connected phone. Online Versus has no deployed public endpoint. Offline human-versus-CPU is the intended test.

**iPhone/iPad:** this APK cannot install on iOS. No IPA/TestFlight build is included; a Mac with Xcode and an Apple Developer/App Store Connect account are still needed.

Source revision: $($info.sourceRevision)

SHA-256 ($($info.apk)): $hash

BUILD_INFO.json records the build and its limits. SHA256SUMS.txt lets you verify the download.
"@
    $payload = @{ tag_name=$tag; target_commitish=$info.sourceRevision; name="Rocky Kombat Android alpha - $($info.version) (build $($info.build))"; body=$body; draft=$true; prerelease=$true } | ConvertTo-Json
    $release = Invoke-RestMethod -Method Post -Uri "$base/releases" -Headers $headers -ContentType 'application/json' -Body $payload
    $releaseId = $release.id
    $uploadBase = "https://uploads.github.com/repos/$Repository/releases/$releaseId/assets"
    foreach ($file in @($apk, $infoPath, (Join-Path $ArtifactDirectory 'SHA256SUMS.txt'))) {
        if (!(Test-Path -LiteralPath $file)) { throw "Missing release attachment: $file" }
        $name = [IO.Path]::GetFileName($file)
        $mime = if ($name.EndsWith('.apk')) { 'application/vnd.android.package-archive' } elseif ($name.EndsWith('.json')) { 'application/json' } else { 'text/plain' }
        $asset = Invoke-RestMethod -Method Post -Uri ($uploadBase+'?name='+[Uri]::EscapeDataString($name)) -Headers $headers -ContentType $mime -InFile $file -TimeoutSec 1800
        if ($asset.state -ne 'uploaded' -or $asset.size -ne (Get-Item -LiteralPath $file).Length) { throw "Upload incomplete: $name; release remains a draft" }
    }
    $release = Invoke-RestMethod -Method Patch -Uri "$base/releases/$releaseId" -Headers $headers -ContentType 'application/json' -Body (@{draft=$false; prerelease=$true} | ConvertTo-Json)
    $published = Invoke-RestMethod -Uri "$base/releases/tags/$tag" -Headers $headers
    $asset = @($published.assets | Where-Object { $_.name -eq $info.apk })
    if ($published.draft -or !$published.prerelease -or $asset.Count -ne 1) { throw 'Published pre-release verification failed' }
    # Fetch the public download without credentials and compare the entire file.
    $downloadCheck = Join-Path $root "Saved/Android/download-verification-$($info.build).apk"
    Invoke-WebRequest -UseBasicParsing -Uri $asset[0].browser_download_url -OutFile $downloadCheck -TimeoutSec 1800
    $downloadHash = (Get-FileHash -LiteralPath $downloadCheck -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($downloadHash -ne $hash) { throw 'Public APK download checksum mismatch' }
    $receipt = @{ releaseUrl=$published.html_url; apkUrl=$asset[0].browser_download_url; tag=$tag; sha256=$hash; publicDownloadVerified=$true; sourceRevision=$info.sourceRevision }
    $receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $ArtifactDirectory 'RELEASE_INFO.json') -Encoding UTF8
    Write-Output "RELEASE_URL=$($receipt.releaseUrl)"
    Write-Output "APK_URL=$($receipt.apkUrl)"
    Write-Output "PUBLIC_DOWNLOAD_SHA256=$downloadHash"
} finally { $credentialLines=$null; $token=$null; $headers=$null }
