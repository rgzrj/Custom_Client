param(
    [string]$QtRoot = 'D:\Qt6',
    [string]$Python = 'D:\YOLOV8\python.exe',
    [string]$Executable = '',
    [string]$VideoTs = '',
    [string]$HevcFile = '',
    [ValidateSet('1','2','3','4','6','7','101','102','103','104','106','107')]
    [string]$RobotId = '',
    [ValidateSet('0','1')][string]$FragmentBase = '0',
    [switch]$VideoFaults,
    [switch]$Build
)
$ErrorActionPreference = 'Stop'
if ($VideoTs -and $HevcFile) { throw 'Choose one synthetic video format, not both.' }
if ($VideoFaults -and -not $HevcFile) { throw 'VideoFaults requires a generated HevcFile.' }
$projectDirectory = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path -LiteralPath $Python)) {
    throw 'Python is required for the short-lived synthetic peer.'
}
. (Join-Path $PSScriptRoot 'environment.ps1')
$tools = Get-Rm27Environment -QtRoot $QtRoot
if (-not $Executable) {
    $Executable = Join-Path $projectDirectory 'build-rm2026-native\rm27_terminal.exe'
    if ($Build -or -not (Test-Path -LiteralPath $Executable)) {
        $buildDirectory = Join-Path $projectDirectory 'build-rm2026-native'
        & $tools.cmake -S $projectDirectory -B $buildDirectory -G Ninja `
            -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$($tools.qtPrefix)" `
            -DRM27_BUILD_APP=ON -DRM27_BUILD_RM2026_PROBE=ON -DRM27_RM2026_LOCAL_HUD=ON `
            -DRM27_RM2026_VIDEO_HUD=OFF `
            '-DRM2026_PROTOC=D:/YOLOV8/Library/bin/protoc.exe'
        if ($LASTEXITCODE -ne 0) { throw 'Historical HUD configuration failed.' }
        & $tools.cmake --build $buildDirectory --target rm27_terminal --parallel 4
        if ($LASTEXITCODE -ne 0) { throw 'Historical HUD build failed.' }
    }
}
if (-not (Test-Path -LiteralPath $Executable)) { throw 'Historical test executable does not exist.' }
Write-Host '主 HUD 合成测试：血量、基地、前哨、金币更新 -> 过期清空 -> 断线 -> 恢复；自动结束，禁止真实控制。'
$demoArguments = @('--exe', $Executable)
if ($RobotId) { $demoArguments += @('--robot-id', $RobotId) }
if ($HevcFile) {
    if (-not (Test-Path -LiteralPath $HevcFile -PathType Leaf)) { throw 'Local synthetic HEVC file does not exist.' }
    $demoArguments += @('--hevc-file', (Resolve-Path -LiteralPath $HevcFile).Path,
        '--fragment-base', $FragmentBase, '--phase-seconds', '2')
    Write-Host '合成 HEVC：8 字节分片 -> 组帧 -> 解码 -> 原 HUD；MQTT/视频故障独立验收，不是硬件配对。'
    if ($VideoFaults) {
        $demoArguments += '--video-faults'
        Write-Host '故障注入：关键帧丢片、坏 HEVC、截断报文 -> 有包但旧画面过期 -> 新关键帧恢复。'
    }
}
if ($VideoTs) {
    if (-not (Test-Path -LiteralPath $VideoTs -PathType Leaf)) { throw 'Local test TS file does not exist.' }
    $demoArguments += @('--video-ts', (Resolve-Path -LiteralPath $VideoTs).Path, '--phase-seconds', '2')
    Write-Host '视频联调：本机 MPEG-TS 解码 -> 视频过期（裁判数据继续） -> 新帧恢复；不是正式图传分包。'
}
& $Python -S (Join-Path $projectDirectory 'tests\rm2026_observer_demo.py') @demoArguments
if ($LASTEXITCODE -ne 0) { throw "Synthetic local demo exited with code $LASTEXITCODE" }
