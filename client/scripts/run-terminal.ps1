[CmdletBinding()]
param(
    [string]$QtRoot = 'D:\Qt6',
    [string]$Video = '',
    [switch]$SimMqtt,
    [switch]$SimUdpVideo,
    [string]$Protocol = '',
    [string]$MqttHost = '',
    [ValidateRange(1,65535)][int]$MqttPort = 3333,
    [ValidateSet(1,2,3,4,6,7,101,102,103,104,106,107)][int]$RobotId = 3,
    [switch]$ProtocolTest,
    [switch]$DiagnosticsJson,
    [switch]$OfficialUdpVideo,
    [string]$BindAddress = '',
    [string]$SourceAddress = '',
    [ValidateRange(1,65535)][int]$VideoPort = 3334,
    [string]$FragmentBase = '',
    [switch]$AllowNetworkVideo,
    [ValidateSet('1','2','3','4','6','7','101','102','103','104','106','107')]
    [string]$PairingRobotId = '',
    [ValidateSet('global','japan')][string]$PairingRegion = 'global',
    [switch]$PairingConfirmed,
    [ValidateRange(0,60000)][int]$DurationMs = 0,
    [switch]$Build,
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
if ($Video -and $SimUdpVideo) { throw '-Video and -SimUdpVideo cannot be used together.' }
if ($OfficialUdpVideo -and -not $Protocol) { $Protocol = 'rm2026-v2.0.0' }
$mqttSelected = $MqttHost -or $PSBoundParameters.ContainsKey('MqttPort') -or $PSBoundParameters.ContainsKey('RobotId')
if ($Protocol) {
    if ($Protocol -ne 'rm2026-v2.0.0' -or (($mqttSelected -or -not $OfficialUdpVideo) -and
        (-not $MqttHost -or -not $PSBoundParameters.ContainsKey('MqttPort') -or -not $PSBoundParameters.ContainsKey('RobotId')))) {
        throw '-Protocol requires rm2026-v2.0.0 and explicit -MqttHost, -MqttPort and -RobotId.'
    }
    if ($Video -or $SimMqtt -or $SimUdpVideo) {
        throw 'Protocol data cannot be combined with simulation or replay.'
    }
} elseif ($MqttHost -or $PSBoundParameters.ContainsKey('MqttPort') -or
    $PSBoundParameters.ContainsKey('RobotId') -or $ProtocolTest -or $DiagnosticsJson) {
    throw 'MQTT parameters require -Protocol.'
}
if ($OfficialUdpVideo) {
    if ($Video -or $SimMqtt -or $SimUdpVideo) {
        throw '-OfficialUdpVideo cannot be combined with replay or simulation switches.'
    }
    if (-not $BindAddress -or -not $SourceAddress -or $FragmentBase -notin @('0','1')) {
        throw '-OfficialUdpVideo requires -BindAddress, -SourceAddress and -FragmentBase 0 or 1. Do not guess device addresses or fragment numbering.'
    }
} elseif (@('BindAddress','SourceAddress','VideoPort','FragmentBase','AllowNetworkVideo') |
    Where-Object { $PSBoundParameters.ContainsKey($_) }) {
    throw 'Network video parameters require -OfficialUdpVideo.'
}
if ($DurationMs -and -not $Protocol -and -not $OfficialUdpVideo) { throw '-DurationMs requires protocol or official video.' }
if ($PairingRobotId) {
    if (-not $OfficialUdpVideo -or $ProtocolTest -or ($mqttSelected -and [int]$PairingRobotId -ne $RobotId)) {
        throw 'Pairing requires ordinary HEVC video, matching requested MQTT identity, and no protocol-test mode.'
    }
} elseif ($PairingConfirmed -or $PSBoundParameters.ContainsKey('PairingRegion')) {
    throw 'PairingRegion and PairingConfirmed require PairingRobotId.'
}
$projectDirectory = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'environment.ps1')
$toolchain = Get-Rm27Environment -QtRoot $QtRoot
if ($CheckOnly) { & (Join-Path $PSScriptRoot 'check-environment.ps1') -QtRoot $QtRoot; return }
$cmake = $toolchain.cmake
$ninja = $toolchain.ninja
$executable = Join-Path $projectDirectory 'build-gui\rm27_terminal.exe'
if ($Build -or -not (Test-Path -LiteralPath $executable)) {
    & $cmake -S $projectDirectory -B (Join-Path $projectDirectory 'build-gui') -G Ninja "-DCMAKE_PREFIX_PATH=$($toolchain.qtPrefix)" "-DCMAKE_MAKE_PROGRAM=$ninja" -DCMAKE_BUILD_TYPE=Debug -DRM27_RM2026_LOCAL_HUD=OFF -DRM27_BUILD_RM2026_PROBE=OFF -DRM27_UPSTREAM_BACKEND=ON
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & $cmake --build (Join-Path $projectDirectory 'build-gui') --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Build failed. Close the terminal app and retry.' }
}
$launchArguments = @()
if ($Video) { $launchArguments += @('--video', (Resolve-Path -LiteralPath $Video).Path) }
if ($SimMqtt) { $launchArguments += '--sim-mqtt' }
if ($SimUdpVideo) { $launchArguments += '--sim-udp-video' }
if ($Protocol) {
    $launchArguments += @('--protocol', $Protocol)
    if ($mqttSelected) {
        $launchArguments += @('--mqtt-host', $MqttHost, '--mqtt-port', "$MqttPort", '--robot-id', "$RobotId")
    }
    if ($ProtocolTest) { $launchArguments += '--protocol-test' }
    if ($DiagnosticsJson) { $launchArguments += '--diagnostics-json' }
    if ($DurationMs) { $launchArguments += @('--duration-ms', "$DurationMs") }
}
if ($OfficialUdpVideo) {
    $launchArguments += @('--hevc-udp', '--video-bind', $BindAddress, '--video-source', $SourceAddress,
        '--video-port', "$VideoPort", '--fragment-base', $FragmentBase)
    if ($AllowNetworkVideo) { $launchArguments += '--allow-network-video' }
}
if ($PairingRobotId) {
    $launchArguments += @('--pairing-robot-id', $PairingRobotId, '--pairing-region', $PairingRegion)
    if ($PairingConfirmed) { $launchArguments += '--pairing-confirmed' }
}
& $executable @launchArguments
if ($LASTEXITCODE -ne 0) { throw "Terminal exited with code $LASTEXITCODE" }
