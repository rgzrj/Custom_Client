function Get-Rm27Environment {
    param([string]$QtRoot = 'D:\Qt6')
    $ErrorActionPreference = 'Stop'
    $pin = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\toolchain.json') -Raw | ConvertFrom-Json
    $qtPrefix = Join-Path $QtRoot "$($pin.qtVersion)\mingw_64"
    $qtBin = Join-Path $qtPrefix 'bin'
    $compilerDirectory = Join-Path $QtRoot $pin.compilerDirectory
    $cmake = Join-Path $QtRoot 'Tools\CMake_64\bin\cmake.exe'
    $ninja = Join-Path $QtRoot 'Tools\Ninja\ninja.exe'
    $ctest = Join-Path $QtRoot 'Tools\CMake_64\bin\ctest.exe'
    $compiler = Join-Path $compilerDirectory 'bin\g++.exe'
    foreach ($path in @($compiler, $cmake, $ninja, $ctest,
        (Join-Path $qtBin 'Qt6Multimedia.dll'), (Join-Path $qtBin 'Qt6Quick.dll'),
        (Join-Path $qtPrefix 'plugins\multimedia\ffmpegmediaplugin.dll'),
        (Join-Path $qtBin 'windeployqt.exe'))) {
        if (-not (Test-Path -LiteralPath $path)) {
            throw "缺少环境组件：$path。请安装 toolchain.json 指定的 Qt/MinGW/Multimedia/CMake/Ninja，或用 -QtRoot 指定安装目录。"
        }
    }
    $compilerVersion = (& $compiler -dumpfullversion).Trim()
    $cmakeVersion = ((& $cmake --version)[0] -replace '^cmake version ', '').Trim()
    $ninjaVersion = (& $ninja --version).Trim()
    $qtVersion = (Get-Item -LiteralPath (Join-Path $qtBin 'Qt6Core.dll')).VersionInfo.ProductVersion
    if ($qtVersion -notlike "$($pin.qtVersion)*" -or $compilerVersion -ne $pin.compilerVersion -or
        $cmakeVersion -ne $pin.cmakeVersion -or $ninjaVersion -ne $pin.ninjaVersion) {
        throw "工具链版本不匹配：Qt=$qtVersion MinGW=$compilerVersion CMake=$cmakeVersion Ninja=$ninjaVersion。请按 toolchain.json 安装；升级后需重新验证再调整固定版本。"
    }
    $env:PATH = "$qtBin;$compilerDirectory\bin;$(Split-Path $ninja);$env:PATH"
    Write-Host "环境检查通过：Qt $($pin.qtVersion) / MinGW $compilerVersion / CMake $cmakeVersion / Ninja $ninjaVersion"
    [PSCustomObject]@{ qtPrefix=$qtPrefix; qtBin=$qtBin; qtVersion=$pin.qtVersion;
        compilerDirectory=$compilerDirectory; cmake=$cmake; ctest=$ctest; ninja=$ninja }
}
