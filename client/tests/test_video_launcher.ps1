# Test only launcher selection/forwarding; no build, process or socket is started.
$ErrorActionPreference = 'Stop'
$launcher = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\scripts\run-terminal.ps1') -Raw
$tokens = $null
$errors = $null
[void][System.Management.Automation.Language.Parser]::ParseInput($launcher, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Launcher syntax error.' }
$boundary = ". (Join-Path `$PSScriptRoot 'environment.ps1')"
$call = "& (Join-Path `$PSScriptRoot 'run-rm2026-video.ps1')"
if (-not $launcher.Contains($boundary) -or -not $launcher.Contains($call)) { throw 'Launcher boundary changed; update this check.' }
$prefix = $launcher.Substring(0, $launcher.IndexOf($boundary)).Replace($call, 'Test-VideoEntry')
# Dynamically parsed blocks have no file-owned PSScriptRoot; supply the test root.
$launcherTestRoot = Join-Path $PSScriptRoot '..\scripts'
$prefix = $prefix.Replace('$PSScriptRoot', '$launcherTestRoot')
$entry = [scriptblock]::Create($prefix)
function Test-VideoEntry {
    param($QtRoot, [switch]$Build, $VideoPort, $FragmentBase, [switch]$ExperimentalVideo,
        $BindAddress, $SourceAddress, [switch]$AllowNetworkVideo, $DurationMs)
    $PSBoundParameters
}
$valid = @{ OfficialUdpVideo = $true; BindAddress = '127.0.0.1'; SourceAddress = '127.0.0.1'; FragmentBase = '0' }
$result = & $entry @valid
if ($result.VideoPort -ne 3334 -or -not $result.ExperimentalVideo -or $result.Build -or $result.AllowNetworkVideo `
    -or $result.BindAddress -ne '127.0.0.1' -or $result.SourceAddress -ne '127.0.0.1' -or $result.FragmentBase -ne '0') {
    throw 'Default official-video forwarding failed.'
}
$result = & $entry @valid -VideoPort 39006 -DurationMs 1000 -Build -AllowNetworkVideo -QtRoot 'D:\CustomQt'
if ($result.VideoPort -ne 39006 -or $result.DurationMs -ne 1000 -or -not $result.Build `
    -or -not $result.AllowNetworkVideo -or $result.QtRoot -ne 'D:\CustomQt') { throw 'Explicit forwarding failed.' }
$oneBased = $valid.Clone()
$oneBased.FragmentBase = '1'
if ((& $entry @oneBased).FragmentBase -ne '1') { throw 'One-based fragments lost.' }
foreach ($invalid in @(@{ OfficialUdpVideo = $true }, @{ BindAddress = '127.0.0.1' },
    @{ SourceAddress = '127.0.0.1' }, @{ VideoPort = 8080 }, @{ FragmentBase = '0' },
    @{ AllowNetworkVideo = $true }, @{ DurationMs = 1000 })) {
    $failed = $false
    try { & $entry @invalid | Out-Null } catch { $failed = $true }
    if (-not $failed) { throw 'Missing mode/configuration was not rejected.' }
}
foreach ($switch in @('Video','SimMqtt','SimUdpVideo')) {
    $conflict = $valid.Clone()
    $conflict[$switch] = if ($switch -eq 'Video') { 'unused.mp4' } else { $true }
    $failed = $false
    try { & $entry @conflict | Out-Null } catch { $failed = $true }
    if (-not $failed) { throw "Conflicting option $switch was not rejected." }
}
if (@(& $entry @valid -CheckOnly).Count) { throw 'CheckOnly launched a video entry.' }
$removedSwitchRejected = $false
try { & $entry -NoAutoVideo | Out-Null } catch { $removedSwitchRejected = $true }
if (-not $removedSwitchRejected) { throw 'Removed switch NoAutoVideo was accepted.' }
Write-Output 'Official UDP video launcher checks passed (mocked, no hardware).'

# Stop at validation: do not resolve FFmpeg, inspect devices, build or launch.
foreach ($scriptName in @('run-rm2026-video.ps1')) {
    $pairingScript = Get-Content -LiteralPath (Join-Path $PSScriptRoot "../scripts/$scriptName") -Raw
    [void][System.Management.Automation.Language.Parser]::ParseInput($pairingScript, [ref]$tokens, [ref]$errors)
    if ($errors.Count) { throw "Syntax error in $scriptName" }
    $validation = [scriptblock]::Create($pairingScript.Substring(0, $pairingScript.IndexOf('$projectDirectory =')))
    $argsForScript = @{ ExperimentalVideo = $true; VideoPort = 39006; FragmentBase = '0' }
    & $validation @argsForScript -PairingRobotId 103 -PairingRegion japan -PairingConfirmed
    foreach ($bad in @(@{ PairingConfirmed = $true }, @{ PairingRegion = 'japan' },
        @{ PairingRobotId = '5' }, @{ PairingRobotId = '03' }, @{ PairingRobotId = '3'; PairingRegion = 'invalid' })) {
        $failed = $false
        try { & $validation @argsForScript @bad | Out-Null } catch { $failed = $true }
        if (-not $failed) { throw "Invalid pairing options accepted by $scriptName" }
    }
}
Write-Output 'Pairing launcher validation passed (no hardware).'
