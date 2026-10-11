param([Parameter(Mandatory=$true)][string]$Archive)
$ErrorActionPreference = 'Stop'
$Archive = (Resolve-Path -LiteralPath $Archive).Path
$expected = ((Get-Content -LiteralPath "$Archive.sha256" -Raw) -split '\s+')[0]
if ((Get-FileHash -LiteralPath $Archive).Hash -ne $expected) { throw 'ZIP 校验失败。' }
$checkDirectory = Join-Path ([IO.Path]::GetTempPath()) ('rm27-sim-check-' + [guid]::NewGuid().ToString('N'))
Expand-Archive -LiteralPath $Archive -DestinationPath $checkDirectory
$packageDirectory = (Get-ChildItem -LiteralPath $checkDirectory -Directory | Select-Object -First 1).FullName
$inventory = Get-Content -LiteralPath "$packageDirectory\dependency-inventory.json" -Raw | ConvertFrom-Json
foreach ($entry in $inventory) {
    $entryPath = [IO.Path]::GetFullPath((Join-Path $packageDirectory $entry.path))
    if ([IO.Path]::IsPathRooted($entry.path) -or
        -not $entryPath.StartsWith($packageDirectory + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw '清单路径越界。'
    }
    if ((Get-FileHash -LiteralPath $entryPath).Hash -ne $entry.sha256) { throw "文件校验失败：$($entry.path)" }
}
if (Get-ChildItem -LiteralPath $packageDirectory -Filter 'Edit.md' -Recurse) { throw '本机日志不能进入发布包。' }
$previous = @{}
foreach ($name in @('PATH','PYTHONHOME','PYTHONPATH','TCL_LIBRARY','TK_LIBRARY')) {
    $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
$process = $null
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    foreach ($name in @('PYTHONHOME','PYTHONPATH','TCL_LIBRARY','TK_LIBRARY')) {
        [Environment]::SetEnvironmentVariable($name, $null, 'Process')
    }
    $report = Join-Path $checkDirectory 'self-test.json'
    $process = Start-Process -FilePath "$packageDirectory\rm27_simulator.exe" -WorkingDirectory $checkDirectory `
        -ArgumentList @('--self-test-report', "`"$report`"") -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(15000)) { throw '便携程序自检超时。' }
    if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $report)) { throw '便携程序自检失败。' }
    $result = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
    if (-not $result.ok -or -not $result.frozen -or $result.networkStarted -or $result.cameraOpened) { throw '自检结果无效。' }
    Write-Output "PACKAGE_CHECK=passed (隔离 PATH/Python/Tk；仍需另一台干净电脑验收)"
    Write-Output "CHECK_DIRECTORY=$checkDirectory"
} finally {
    if ($process -and -not $process.HasExited) { Stop-Process -Id $process.Id }
    foreach ($name in $previous.Keys) { [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process') }
}
