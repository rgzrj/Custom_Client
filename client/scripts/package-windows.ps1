param(
    [string]$QtRoot = 'D:\Qt6',
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(-[a-zA-Z0-9.-]+)?$')]
    [string]$Version = '0.2.0-preview',
    [string]$TestVideo = '',
    [string]$ReferenceProject = ''
)
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
if ($ReferenceProject) { & (Join-Path $PSScriptRoot 'check-contract.ps1') -ReferenceProject $ReferenceProject }
. (Join-Path $PSScriptRoot 'environment.ps1')
$toolchain = Get-Rm27Environment -QtRoot $QtRoot
$qtPrefix = $toolchain.qtPrefix
$qtBin = $toolchain.qtBin
$cmake = $toolchain.cmake
$ctest = $toolchain.ctest
$ninja = $toolchain.ninja
$buildDirectory = Join-Path $projectDirectory 'build-release'
if ($TestVideo) { $TestVideo = (Resolve-Path -LiteralPath $TestVideo).Path }
& $cmake -S $projectDirectory -B $buildDirectory -G Ninja "-DCMAKE_PREFIX_PATH=$qtPrefix" `
    "-DCMAKE_MAKE_PROGRAM=$ninja" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON "-DRM27_TEST_VIDEO=$TestVideo" `
    -DRM27_RM2026_LOCAL_HUD=OFF -DRM27_BUILD_RM2026_PROBE=OFF -DRM27_RM2026_VIDEO_HUD=OFF
if ($LASTEXITCODE -ne 0) { throw 'Release configuration failed.' }
& $cmake --build $buildDirectory --parallel
if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
& $ctest --test-dir $buildDirectory --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Release tests failed; no package was created.' }

# Keep old packages for rollback; always create a new timestamped directory.
$timestamp = [DateTimeOffset]::UtcNow.ToOffset([TimeSpan]::FromHours(8))
$packageName = "RM27-Terminal-$Version-windows-x64-$($timestamp.ToString('yyyyMMdd-HHmmss'))"
$packageDirectory = Join-Path $projectDirectory "dist\$packageName"
New-Item -ItemType Directory -Path $packageDirectory | Out-Null
$executable = Join-Path $packageDirectory 'rm27_terminal.exe'
Copy-Item -LiteralPath (Join-Path $buildDirectory 'rm27_terminal.exe') -Destination $executable
& (Join-Path $qtBin 'windeployqt.exe') --release --compiler-runtime --no-translations `
    --skip-plugin-types qmltooling --verbose 0 --qmldir (Join-Path $projectDirectory 'qml') $executable
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
foreach ($required in @('Qt6Core.dll', 'Qt6Quick.dll', 'Qt6Multimedia.dll',
    'platforms\qwindows.dll', 'multimedia\ffmpegmediaplugin.dll',
    'libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $packageDirectory $required))) {
        throw "Missing runtime dependency: $required"
    }
}
if (-not (Get-ChildItem -LiteralPath $packageDirectory -Filter 'avcodec-*.dll')) {
    throw 'FFmpeg decoder libraries were not deployed.'
}
Copy-Item -LiteralPath (Join-Path $projectDirectory 'packaging\README.txt') -Destination $packageDirectory
Copy-Item -LiteralPath (Join-Path $projectDirectory 'packaging\THIRD_PARTY.md') -Destination $packageDirectory
Copy-Item -LiteralPath (Join-Path $projectDirectory 'packaging\qt.conf') -Destination $packageDirectory
if ($TestVideo) {
    Copy-Item -LiteralPath $TestVideo -Destination (Join-Path $packageDirectory 'demo.mp4')
    Copy-Item -LiteralPath (Join-Path $projectDirectory 'packaging\demo.cmd') -Destination $packageDirectory
}
$licenseDirectory = Join-Path $packageDirectory 'licenses'
New-Item -ItemType Directory -Path $licenseDirectory | Out-Null
Get-ChildItem -LiteralPath (Join-Path $QtRoot 'Licenses') -File | Copy-Item -Destination $licenseDirectory
Get-ChildItem -LiteralPath (Join-Path $qtPrefix 'sbom') -Filter '*.spdx.json' -File |
    Copy-Item -Destination $licenseDirectory
foreach ($runtimeLicense in @('gcc', 'winpthreads')) {
    Copy-Item -LiteralPath (Join-Path $toolchain.compilerDirectory "licenses\$runtimeLicense") `
        -Destination $licenseDirectory -Recurse
}
$ffmpegNotice = Join-Path $QtRoot "Docs\Qt-$($toolchain.qtVersion)\qtmultimedia\qtmultimedia-attribution-ffmpeg.html"
if (-not (Test-Path -LiteralPath $ffmpegNotice)) {
    throw 'The Qt Multimedia FFmpeg copyright/license notice is missing; install Qt documentation.'
}
Copy-Item -LiteralPath $ffmpegNotice -Destination (Join-Path $licenseDirectory 'FFmpeg.html')
Copy-Item -LiteralPath (Join-Path $buildDirectory 'Testing\Temporary\LastTest.log') `
    -Destination (Join-Path $packageDirectory 'test-summary.txt')
Copy-Item -LiteralPath (Join-Path $projectDirectory 'src\core\connection_profile.hpp') `
    -Destination (Join-Path $packageDirectory 'configuration-defaults.hpp')
$gitCommit = (& git -C $projectDirectory rev-parse HEAD).Trim()
$manifest = [ordered]@{
    version = $Version
    builtAt = $timestamp.ToString('yyyy-MM-ddTHH:mm:sszzz')
    sourceCommit = $gitCommit
    sourceDirty = [bool](& git -C $projectDirectory status --porcelain -- .)
    platform = 'windows-x64'
    qt = $toolchain.qtVersion
    ffmpeg = (Get-ChildItem -LiteralPath $packageDirectory -Filter 'avcodec-*.dll' | Select-Object -First 1).VersionInfo.ProductVersion
    buildType = 'Release'
    defaultConfigurationSha256 = (Get-FileHash -LiteralPath (Join-Path $packageDirectory 'configuration-defaults.hpp')).Hash
    tests = 'CTest passed; see test-summary.txt. Media playback tests require TestVideo; TS decoding tests require RM27_TEST_TS.'
    cleanMachineVerified = $false
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $packageDirectory 'manifest.json') -Encoding UTF8
$inventory = @(Get-ChildItem -LiteralPath $packageDirectory -File -Recurse | ForEach-Object {
    [ordered]@{
        path = $_.FullName.Substring($packageDirectory.Length + 1).Replace('\', '/')
        bytes = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName).Hash
    }
})
$inventory | ConvertTo-Json -Depth 3 |
    Set-Content -LiteralPath (Join-Path $packageDirectory 'dependency-inventory.json') -Encoding UTF8
$archive = "$packageDirectory.zip"
Compress-Archive -LiteralPath $packageDirectory -DestinationPath $archive -CompressionLevel Optimal
$archiveHash = (Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant()
"$archiveHash  $([IO.Path]::GetFileName($archive))" | Set-Content -LiteralPath "$archive.sha256" -Encoding ASCII
Write-Output "PACKAGE=$archive"
Write-Output "SHA256=$archiveHash"
