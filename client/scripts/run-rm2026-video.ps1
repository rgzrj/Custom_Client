param(
    [Parameter(Mandatory=$true)][ValidateRange(1,65535)][int]$VideoPort,
    [Parameter(Mandatory=$true)][ValidateSet('0','1')][string]$FragmentBase,
    [switch]$ExperimentalVideo,
    [string]$BindAddress = '127.0.0.1',
    [string]$SourceAddress = '127.0.0.1',
    [switch]$AllowNetworkVideo,
    [ValidateSet('1','2','3','4','6','7','101','102','103','104','106','107')][string]$PairingRobotId,
    [ValidateSet('global','japan')][string]$PairingRegion = 'global',
    [switch]$PairingConfirmed,
    [string]$QtRoot = 'D:\Qt6',
    [ValidateRange(0,60000)][int]$DurationMs = 0,
    [switch]$Build
)
$ErrorActionPreference = 'Stop'
if (-not $ExperimentalVideo) { throw 'Use -ExperimentalVideo to acknowledge unverified RM2026 framing assumptions.' }
if (-not $PairingRobotId -and ($PairingConfirmed -or $PSBoundParameters.ContainsKey('PairingRegion'))) {
    throw 'PairingRegion and PairingConfirmed require an explicit PairingRobotId.'
}
$videoBindIp = $null
$videoSourceIp = $null
if (-not [System.Net.IPAddress]::TryParse($BindAddress, [ref]$videoBindIp) -or
    -not [System.Net.IPAddress]::TryParse($SourceAddress, [ref]$videoSourceIp) -or
    $videoBindIp.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork -or
    $videoSourceIp.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork) {
    throw 'BindAddress and SourceAddress must be literal IPv4 addresses.'
}
if ((-not [System.Net.IPAddress]::IsLoopback($videoBindIp) -or
    -not [System.Net.IPAddress]::IsLoopback($videoSourceIp)) -and -not $AllowNetworkVideo) {
    throw 'Non-loopback video requires -AllowNetworkVideo and an explicitly selected source.'
}
$projectDirectory = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'environment.ps1')
$videoTools = Get-Rm27Environment -QtRoot $QtRoot
$buildDirectory = Join-Path $projectDirectory 'build-rm2026-video'
$executable = Join-Path $buildDirectory 'rm27_terminal.exe'
if ($Build -or -not (Test-Path -LiteralPath $executable)) {
    & $videoTools.cmake -S $projectDirectory -B $buildDirectory -G Ninja `
        "-DCMAKE_PREFIX_PATH=$($videoTools.qtPrefix)" "-DCMAKE_MAKE_PROGRAM=$($videoTools.ninja)" `
        -DCMAKE_BUILD_TYPE=Debug -DRM27_BUILD_APP=ON -DRM27_RM2026_VIDEO_HUD=ON `
        -DRM27_RM2026_LOCAL_HUD=OFF -DRM27_BUILD_RM2026_PROBE=OFF
    if ($LASTEXITCODE -ne 0) { throw 'HEVC experiment configuration failed.' }
    & $videoTools.cmake --build $buildDirectory --target rm27_terminal --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'HEVC experiment build failed.' }
}
$cache = Get-Content -LiteralPath (Join-Path $buildDirectory 'CMakeCache.txt') -Raw
if ($cache -notmatch '(?m)^RM27_RM2026_VIDEO_HUD:BOOL=ON\r?$') { throw 'Not a HEVC experiment build.' }
Write-Host "RM2026 read-only HEVC experiment. Bind $BindAddress : $VideoPort; source $SourceAddress. No sender started;"
$videoArguments = @('--protocol','rm2026-v2.0.0','--experimental-video',
    '--video-port',"$VideoPort",'--fragment-base',$FragmentBase,
    '--video-bind',$BindAddress,'--video-source',$SourceAddress)
if ($AllowNetworkVideo) { $videoArguments += '--allow-network-video' }
if ($DurationMs) { $videoArguments += @('--duration-ms',"$DurationMs") }
if ($PairingRobotId) { $videoArguments += @('--pairing-robot-id',$PairingRobotId,'--pairing-region',$PairingRegion) }
if ($PairingConfirmed) { $videoArguments += '--pairing-confirmed' }
& $executable @videoArguments
if ($LASTEXITCODE -ne 0) { throw "HEVC experiment exited with code $LASTEXITCODE" }
