param(
    [string]$Python = '',
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(-[a-zA-Z0-9.-]+)?$')]
    [string]$Version = '0.2.0-preview',
    [string]$ReferenceProject = ''
)
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
if ($ReferenceProject) { & (Join-Path $PSScriptRoot 'check-contract.ps1') -ReferenceProject $ReferenceProject }
if (-not $Python) { $Python = Join-Path $projectDirectory '.build-tools\venv\Scripts\python.exe' }
if (-not (Test-Path -LiteralPath $Python)) {
    throw '请先按 README 在 .build-tools/venv 安装 requirements-build.txt，或用 -Python 指定独立打包环境。'
}
$Python = (Resolve-Path -LiteralPath $Python).Path
$env:PYTHONUTF8 = '1'
$pin = Get-Content -LiteralPath (Join-Path $projectDirectory 'toolchain.json') -Raw | ConvertFrom-Json
$installerVersion = & $Python -c 'import PyInstaller; print(PyInstaller.__version__)'
if ($LASTEXITCODE -ne 0 -or $installerVersion -ne $pin.pyinstallerVersion) { throw "需要固定 PyInstaller $($pin.pyinstallerVersion)。" }
$pythonVersion = & $Python -c 'import platform; print(platform.python_version())'
if ($LASTEXITCODE -ne 0 -or $pythonVersion -ne $pin.buildPythonVersion) { throw "发布基线需要 Python $($pin.buildPythonVersion)。" }
$tkVersion = & $Python -c "import tkinter as tk; r=tk.Tk(); r.withdraw(); print(r.tk.call('package', 'provide', 'Tk')); r.destroy()"
if ($LASTEXITCODE -ne 0 -or $tkVersion -ne $pin.tclTkVersion) { throw "发布基线需要 Tcl/Tk $($pin.tclTkVersion)。" }
Push-Location $projectDirectory
try {
    $testOutput = & $Python -c "import sys,unittest; suite=unittest.defaultTestLoader.discover('simulator', pattern='test_*.py'); result=unittest.TextTestRunner(stream=sys.stdout,verbosity=2).run(suite); sys.exit(not result.wasSuccessful() or bool(result.skipped))"
    $testExitCode = $LASTEXITCODE
    $testOutput | Write-Host
    if ($testExitCode -ne 0) { throw '模拟器回归失败或有跳过项，不创建发布包。' }
    $basePrefix = (& $Python -c 'import sys; print(sys.base_prefix)').Trim()
    # Conda 的 Tk 依赖位于 Library/bin，仅影响当前打包进程。
    $env:PATH = "$basePrefix;$basePrefix\Library\bin;$env:PATH"
    $timestamp = [DateTimeOffset]::UtcNow.ToOffset([TimeSpan]::FromHours(8))
    $packageName = "RM27-Simulator-$Version-windows-x64-$($timestamp.ToString('yyyyMMdd-HHmmss'))"
    $buildDirectory = Join-Path $projectDirectory "build-packages\$packageName"
    $packageDirectory = Join-Path $projectDirectory "dist\$packageName"
    if (Test-Path -LiteralPath $packageDirectory) { throw '目标已存在，请稍后重试；不会覆盖旧包。' }
    New-Item -ItemType Directory -Path $buildDirectory | Out-Null
    & $Python -m PyInstaller --onedir --windowed --noupx --name rm27_simulator `
        --paths $projectDirectory --distpath $buildDirectory --workpath "$buildDirectory\work" `
        --specpath $buildDirectory (Join-Path $projectDirectory 'simulator\control_console.py')
    if ($LASTEXITCODE -ne 0) { throw 'PyInstaller 打包失败。' }
    New-Item -ItemType Directory -Force -Path (Join-Path $projectDirectory 'dist') | Out-Null
    $compiledDirectory = (Resolve-Path -LiteralPath (Join-Path $buildDirectory 'rm27_simulator')).Path
    $workspacePrefix = [IO.Path]::GetFullPath($projectDirectory).TrimEnd('\') + '\'
    $packageDirectory = [IO.Path]::GetFullPath($packageDirectory)
    if (-not $compiledDirectory.StartsWith($workspacePrefix, [StringComparison]::OrdinalIgnoreCase) -or
        -not $packageDirectory.StartsWith($workspacePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw '发布目录超出本项目，拒绝移动。'
    }
    Move-Item -LiteralPath $compiledDirectory -Destination $packageDirectory
    Copy-Item -LiteralPath (Join-Path $projectDirectory 'packaging\README.txt') -Destination $packageDirectory
    Copy-Item -LiteralPath (Join-Path $projectDirectory 'packaging\THIRD_PARTY.md') -Destination $packageDirectory
    $testOutput | Set-Content -LiteralPath (Join-Path $packageDirectory 'test-summary.txt') -Encoding UTF8
    $licenses = Join-Path $packageDirectory 'licenses'
    New-Item -ItemType Directory -Path $licenses | Out-Null
    $pythonLicense = Join-Path $basePrefix 'LICENSE_PYTHON.txt'
    if (-not (Test-Path -LiteralPath $pythonLicense)) { $pythonLicense = Join-Path $basePrefix 'LICENSE.txt' }
    Copy-Item -LiteralPath $pythonLicense -Destination (Join-Path $licenses 'Python.txt')
    $tkLicense = Join-Path $basePrefix 'Library\lib\tk8.6\license.terms'
    if (-not (Test-Path -LiteralPath $tkLicense)) { $tkLicense = Join-Path $basePrefix 'tcl\tk8.6\license.terms' }
    Copy-Item -LiteralPath $tkLicense -Destination (Join-Path $licenses 'Tcl-Tk.txt')
    $installerLicense = (& $Python -c "import importlib.metadata as m; d=m.distribution('pyinstaller'); print(next(str(d.locate_file(f)) for f in d.files if str(f).endswith('COPYING.txt')))").Trim()
    Copy-Item -LiteralPath $installerLicense -Destination (Join-Path $licenses 'PyInstaller.txt')
    Get-ChildItem -LiteralPath (Join-Path $projectDirectory 'packaging\licenses') -File | Copy-Item -Destination $licenses
    # 固定的本机 Conda 运行库来源；缺许可就拒绝打包，不能只带 DLL。
    foreach ($dependency in @('bzip2-1.0.8-h2bbff1b_6', 'libffi-3.4.4-hd77b12b_1',
        'libzlib-1.3.1-h02ab6af_0', 'ucrt-10.0.22621.0-haa95532_0',
        'vc14_runtime-14.44.35208-h4927774_10', 'tk-8.6.15-hf199647_0')) {
        $sourceLicenses = Join-Path $basePrefix "pkgs\$dependency\info\licenses"
        if (-not (Test-Path -LiteralPath $sourceLicenses)) { throw "缺少运行库许可：$sourceLicenses" }
        Copy-Item -LiteralPath $sourceLicenses -Destination (Join-Path $licenses $dependency) -Recurse
    }
    $manifest = [ordered]@{
        version=$Version; builtAt=$timestamp.ToString('yyyy-MM-ddTHH:mm:sszzz'); platform='windows-x64'
        python=$pythonVersion; tclTk=$tkVersion
        pyinstaller=$installerVersion; sourceCommit=(& git -C $projectDirectory rev-parse HEAD).Trim()
        sourceDirty=[bool](& git -C $projectDirectory status --porcelain -- .)
        tests='Python unittest passed without skips; see test-summary.txt. Package self-test uses check-windows-package.ps1.'
        cleanMachineVerified=$false; endpoints='127.0.0.1 only; project simulation'
    }
    $manifest | ConvertTo-Json | Set-Content -LiteralPath "$packageDirectory\manifest.json" -Encoding UTF8
    $inventory = @(Get-ChildItem -LiteralPath $packageDirectory -File -Recurse | ForEach-Object {
        [ordered]@{path=$_.FullName.Substring($packageDirectory.Length + 1).Replace('\','/');
            bytes=$_.Length; sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}
    })
    $inventory | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath "$packageDirectory\dependency-inventory.json" -Encoding UTF8
    $archive = "$packageDirectory.zip"
    Compress-Archive -LiteralPath $packageDirectory -DestinationPath $archive -CompressionLevel Optimal
    "$((Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant())  $([IO.Path]::GetFileName($archive))" |
        Set-Content -LiteralPath "$archive.sha256" -Encoding ASCII
    Write-Output "PACKAGE=$archive"
} finally { Pop-Location }
