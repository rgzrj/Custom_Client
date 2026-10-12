# Validate the ordinary launcher arguments without starting processes or sockets.
$ErrorActionPreference = 'Stop'
$launcher = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../scripts/run-terminal.ps1') -Raw
$tokens = $null; $errors = $null
[void][System.Management.Automation.Language.Parser]::ParseInput($launcher, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Launcher syntax error.' }
if ($launcher.Contains("& (Join-Path $PSScriptRoot 'run-rm2026-video.ps1')")) { throw 'Ordinary launcher still delegates video.' }
$prefix = $launcher.Substring(0, $launcher.IndexOf('$projectDirectory ='))
$tail = $launcher.Substring($launcher.IndexOf('$launchArguments = @()'))
$tail = $tail.Substring(0, $tail.IndexOf('& $executable @launchArguments'))
$entry = [scriptblock]::Create($prefix + "`nif (`$CheckOnly) { return };`n" + $tail + "`n`$launchArguments")
$valid = @{ OfficialUdpVideo = $true; BindAddress = '127.0.0.1'; SourceAddress = '127.0.0.1'; FragmentBase = '0' }
$result = @(& $entry @valid)
if (($result -join '|') -ne '--protocol|rm2026-v2.0.0|--hevc-udp|--video-bind|127.0.0.1|--video-source|127.0.0.1|--video-port|3334|--fragment-base|0') { throw 'Video-only ordinary arguments wrong.' }
$result = @(& $entry @valid -Protocol rm2026-v2.0.0 -MqttHost 127.0.0.1 -MqttPort 39005 -RobotId 103 -ProtocolTest -DiagnosticsJson -VideoPort 39006 -DurationMs 1000 -AllowNetworkVideo)
foreach ($expected in @('--hevc-udp','--mqtt-host','39005','--robot-id','103','--protocol-test','--diagnostics-json','39006','--duration-ms','1000','--allow-network-video')) {
    if ($expected -notin $result) { throw "Combined argument lost: $expected" }
}
$oneBased = $valid.Clone(); $oneBased.FragmentBase = '1'
if ((& $entry @oneBased)[-1] -ne '1') { throw 'One-based fragments lost.' }
foreach ($invalid in @(@{ OfficialUdpVideo = $true }, @{ BindAddress = '127.0.0.1' },
    @{ SourceAddress = '127.0.0.1' }, @{ VideoPort = 8080 }, @{ FragmentBase = '0' },
    @{ AllowNetworkVideo = $true }, @{ DurationMs = 1000 }, @{ Protocol = 'rm2026-v2.0.0' })) {
    $failed = $false
    try { & $entry @invalid | Out-Null } catch { $failed = $true }
    if (-not $failed) { throw 'Missing mode/configuration was not rejected.' }
}
foreach ($switch in @('Video','SimMqtt','SimUdpVideo')) {
    $conflict = $valid.Clone(); $conflict[$switch] = if ($switch -eq 'Video') { 'unused.mp4' } else { $true }
    $failed = $false
    try { & $entry @conflict | Out-Null } catch { $failed = $true }
    if (-not $failed) { throw "Conflicting option $switch was not rejected." }
}
if (@(& $entry @valid -CheckOnly).Count) { throw 'CheckOnly generated launch arguments.' }
$failed = $false
try { & $entry -NoAutoVideo | Out-Null } catch { $failed = $true }
if (-not $failed) { throw 'Removed NoAutoVideo switch accepted.' }
Write-Output 'Ordinary HEVC launcher checks passed (no hardware).'

$result = @(& $entry @valid -PairingRobotId 103 -PairingRegion japan -PairingConfirmed)
foreach ($expected in @('--pairing-robot-id','103','--pairing-region','japan','--pairing-confirmed')) {
    if ($expected -notin $result) { throw "Pairing argument lost: $expected" }
}
foreach ($bad in @(@{ PairingConfirmed = $true }, @{ PairingRegion = 'japan' },
    @{ PairingRobotId = '5' }, @{ PairingRobotId = '03' },
    @{ PairingRobotId = '3'; PairingRegion = 'invalid' },
    @{ PairingRobotId = '3'; ProtocolTest = $true },
    @{ PairingRobotId = '3'; MqttHost = '127.0.0.1'; MqttPort = 39005; RobotId = 103 })) {
    $failed = $false
    try { & $entry @valid @bad | Out-Null } catch { $failed = $true }
    if (-not $failed) { throw 'Invalid pairing options accepted.' }
}
$failed = $false
try { & $entry -PairingRobotId 3 | Out-Null } catch { $failed = $true }
if (-not $failed) { throw 'Pairing without HEVC was accepted.' }
Write-Output 'Ordinary pairing launcher validation passed (no hardware).'
