[CmdletBinding()]
param(
    [string]$QtRoot = 'D:\Qt6',
    [string]$Video = '',
    [switch]$SimMqtt,
    [switch]$SimUdpVideo,
    [switch]$OfficialUdpVideo,
    [string]$BindAddress = '',
    [string]$SourceAddress = '',
    [ValidateRange(1,65535)][int]$VideoPort = 3334,
    [string]$FragmentBase = '',
    [switch]$AllowNetworkVideo,
    [ValidateRange(0,60000)][int]$DurationMs = 0,
    [switch]$Build,
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
if ($Video -and $SimUdpVideo) { throw '-Video and -SimUdpVideo cannot be used together.' }
if ($OfficialUdpVideo) {
    if ($Video -or $SimMqtt -or $SimUdpVideo) {
        throw '-OfficialUdpVideo cannot be combined with replay or simulation switches.'
    }
    if (-not $BindAddress -or -not $SourceAddress -or $FragmentBase -notin @('0','1')) {
        throw '-OfficialUdpVideo requires -BindAddress, -SourceAddress and -FragmentBase 0 or 1. Do not guess device addresses or fragment numbering.'
    }
} elseif (@('BindAddress','SourceAddress','VideoPort','FragmentBase','AllowNetworkVideo','DurationMs') |
    Where-Object { $PSBoundParameters.ContainsKey($_) }) {
    throw 'Network video parameters require -OfficialUdpVideo.'
}
$projectDirectory = Split-Path -Parent $PSScriptRoot
if ($OfficialUdpVideo -and -not $CheckOnly) {
    # Reuse the isolated HEVC receiver.
    & (Join-Path $PSScriptRoot 'run-rm2026-video.ps1') -QtRoot $QtRoot -Build:$Build `
        -VideoPort $VideoPort -FragmentBase $FragmentBase -ExperimentalVideo `
        -BindAddress $BindAddress -SourceAddress $SourceAddress `
        -AllowNetworkVideo:$AllowNetworkVideo -DurationMs $DurationMs
    return
}
. (Join-Path $PSScriptRoot 'environment.ps1')
$toolchain = Get-Rm27Environment -QtRoot $QtRoot
if ($CheckOnly) { & (Join-Path $PSScriptRoot 'check-environment.ps1') -QtRoot $QtRoot; return }
$cmake = $toolchain.cmake
$ninja = $toolchain.ninja
$executable = Join-Path $projectDirectory 'build-gui\rm27_terminal.exe'
if ($Build -or -not (Test-Path -LiteralPath $executable)) {
    & $cmake -S $projectDirectory -B (Join-Path $projectDirectory 'build-gui') -G Ninja "-DCMAKE_PREFIX_PATH=$($toolchain.qtPrefix)" "-DCMAKE_MAKE_PROGRAM=$ninja" -DCMAKE_BUILD_TYPE=Debug -DRM27_RM2026_LOCAL_HUD=OFF -DRM27_BUILD_RM2026_PROBE=OFF -DRM27_RM2026_VIDEO_HUD=OFF
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & $cmake --build (Join-Path $projectDirectory 'build-gui') --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Build failed. Close the terminal app and retry.' }
}
$launchArguments = @()
if ($Video) { $launchArguments += @('--video', (Resolve-Path -LiteralPath $Video).Path) }
if ($SimMqtt) { $launchArguments += '--sim-mqtt' }
if ($SimUdpVideo) { $launchArguments += '--sim-udp-video' }
& $executable @launchArguments
if ($LASTEXITCODE -ne 0) { throw "Terminal exited with code $LASTEXITCODE" }
