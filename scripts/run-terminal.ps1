param(
    [string]$QtRoot = 'D:\Qt6',
    [string]$Video = '',
    [switch]$SimMqtt,
    [switch]$SimUdpVideo,
    [switch]$Build
)
$ErrorActionPreference = 'Stop'
if ($Video -and $SimUdpVideo) { throw '-Video and -SimUdpVideo cannot be used together.' }
$projectDirectory = Split-Path -Parent $PSScriptRoot
$qtDirectories = @(Get-ChildItem -LiteralPath $QtRoot -Directory |
    Where-Object { $_.Name -match '^6\.\d+\.\d+$' } |
    Sort-Object { [version]$_.Name } -Descending)
$qtDirectory = $qtDirectories | Where-Object {
    Test-Path -LiteralPath (Join-Path $_.FullName 'mingw_64\bin\Qt6Multimedia.dll')
} | Select-Object -First 1
if (-not $qtDirectory) { throw 'Qt MinGW with Multimedia was not found. Use -QtRoot to select your Qt installation.' }
$qtBin = Join-Path $qtDirectory.FullName 'mingw_64\bin'
$compiler = Get-ChildItem -LiteralPath (Join-Path $QtRoot 'Tools') -Directory -Filter 'mingw*' |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $compiler) { throw 'The Qt MinGW compiler is missing.' }
$cmake = Join-Path $QtRoot 'Tools\CMake_64\bin\cmake.exe'
$ninja = Join-Path $QtRoot 'Tools\Ninja\ninja.exe'
$env:PATH = "$qtBin;$($compiler.FullName)\bin;$(Split-Path $ninja);$env:PATH"
$executable = Join-Path $projectDirectory 'build-gui\rm27_terminal.exe'
if ($Build -or -not (Test-Path -LiteralPath $executable)) {
    & $cmake -S $projectDirectory -B (Join-Path $projectDirectory 'build-gui') -G Ninja "-DCMAKE_PREFIX_PATH=$($qtDirectory.FullName)\mingw_64" "-DCMAKE_MAKE_PROGRAM=$ninja" -DCMAKE_BUILD_TYPE=Debug
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
