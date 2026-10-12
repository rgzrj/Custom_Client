[CmdletBinding(DefaultParameterSetName = 'Archive')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Archive')][string]$Archive,
    [Parameter(Mandatory = $true, ParameterSetName = 'Directory')][string]$PackageDirectory
)
$ErrorActionPreference = 'Stop'
if ($PSCmdlet.ParameterSetName -eq 'Archive') {
$Archive = (Resolve-Path -LiteralPath $Archive).Path
$expectedHash = ((Get-Content -LiteralPath "$Archive.sha256" -Raw) -split '\s+')[0]
if ((Get-FileHash -LiteralPath $Archive).Hash -ne $expectedHash) { throw 'ZIP checksum mismatch.' }
$checkDirectory = Join-Path ([IO.Path]::GetTempPath()) ('rm27-package-check-' + [guid]::NewGuid().ToString('N'))
Expand-Archive -LiteralPath $Archive -DestinationPath $checkDirectory
$packageDirectory = (Get-ChildItem -LiteralPath $checkDirectory -Directory | Select-Object -First 1).FullName
$inventory = Get-Content -LiteralPath (Join-Path $packageDirectory 'dependency-inventory.json') -Raw | ConvertFrom-Json
foreach ($entry in $inventory) {
    $entryPath = [IO.Path]::GetFullPath((Join-Path $packageDirectory $entry.path))
    if ([IO.Path]::IsPathRooted($entry.path) -or
        -not $entryPath.StartsWith($packageDirectory + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Inventory path escapes the package.'
    }
    if ((Get-FileHash -LiteralPath $entryPath).Hash -ne $entry.sha256) { throw "File checksum mismatch: $($entry.path)" }
}
if (Get-ChildItem -LiteralPath $packageDirectory -Filter 'Edit.md' -Recurse) { throw 'Local edit log must not be distributed.' }
} else {
    $packageDirectory = (Resolve-Path -LiteralPath $PackageDirectory).Path
    $checkDirectory = Join-Path ([IO.Path]::GetTempPath()) ('rm27-package-check-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $checkDirectory | Out-Null
}
$previousEnvironment = @{}
foreach ($name in @('PATH', 'QT_QPA_PLATFORM', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'RM27_SETTINGS_FILE', 'RM27_ARTIFACT_ROOT')) {
    $previousEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
$testProcess = $null
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    foreach ($name in @('QT_QPA_PLATFORM', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH')) {
        [Environment]::SetEnvironmentVariable($name, $null, 'Process')
    }
    $env:RM27_SETTINGS_FILE = Join-Path $checkDirectory 'isolated-connection.ini'
    $env:RM27_ARTIFACT_ROOT = $checkDirectory
    $arguments = @('--duration-ms', '8000')
    if (Test-Path -LiteralPath (Join-Path $packageDirectory 'demo.mp4')) {
        $arguments += @('--video', ('"{0}"' -f (Join-Path $packageDirectory 'demo.mp4')))
    }
    $launchOptions = @{
        FilePath = (Join-Path $packageDirectory 'rm27_terminal.exe')
        WorkingDirectory = $checkDirectory
        PassThru = $true
        WindowStyle = 'Hidden'
        RedirectStandardOutput = (Join-Path $checkDirectory 'stdout.txt')
        RedirectStandardError = (Join-Path $checkDirectory 'stderr.txt')
    }
    if ($arguments.Count) { $launchOptions.ArgumentList = $arguments }
    $testProcess = Start-Process @launchOptions
    Start-Sleep -Seconds 5
    $testProcess.Refresh()
    if ($testProcess.HasExited) { throw "Packaged app exited early: $($testProcess.ExitCode). See $checkDirectory" }
    $modulePaths = @($testProcess.Modules | Where-Object { $_.ModuleName -match '^(Qt6|avcodec|avformat|avutil|swscale|swresample|ffmpegmediaplugin|qwindows|libgcc|libstdc|libwinpthread)' })
    foreach ($requiredModule in @('Qt6Quick.dll', 'qwindows.dll', 'avcodec-61.dll', 'avutil-59.dll', 'swscale-8.dll')) {
        if (-not @($modulePaths | Where-Object { $_.ModuleName -eq $requiredModule }).Count) {
            throw "GUI runtime module was not loaded: $requiredModule"
        }
    }
    if (@($modulePaths | Where-Object { -not $_.FileName.StartsWith($packageDirectory + '\', [StringComparison]::OrdinalIgnoreCase) }).Count) {
        throw 'Runtime DLLs were loaded from outside the extracted package.'
    }
    Write-Output 'PACKAGE_CHECK=passed (isolated PATH; clean-machine testing still required)'
    Write-Output ('RUNTIME_DLLS_VERIFIED=' + $modulePaths.Count)
    Write-Output ('CHECK_DIRECTORY=' + $checkDirectory)
} finally {
    if ($testProcess -and -not $testProcess.HasExited) {
        $null = $testProcess.CloseMainWindow()
        if (-not $testProcess.WaitForExit(3000)) { $testProcess.Kill() }
    }
    foreach ($name in $previousEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $previousEnvironment[$name], 'Process')
    }
}
