param(
    [string]$QtRoot = 'D:\Qt6',
    [ValidateRange(1, 65535)][int]$Port = 39005,
    [string]$RobotId
)
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $projectDirectory 'build-rm2026-native\contracts\rm2026-v2.0.0\native\rm2026_mqtt_viewer.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    throw 'Build rm2026_mqtt_viewer first; see contracts/rm2026-v2.0.0/README.md.'
}
. (Join-Path $PSScriptRoot 'environment.ps1')
$null = Get-Rm27Environment -QtRoot $QtRoot
$observerArguments = @('--protocol', 'rm2026-v2.0.0', '--host', '127.0.0.1', '--port', "$Port")
if ($PSBoundParameters.ContainsKey('RobotId')) { $observerArguments += @('--robot-id', $RobotId) }
& $executable @observerArguments
if ($LASTEXITCODE -ne 0) { throw "Read-only observer exited with code $LASTEXITCODE" }
