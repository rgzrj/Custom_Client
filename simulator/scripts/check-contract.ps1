param([Parameter(Mandatory=$true)][string]$ReferenceProject)
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$referenceDirectory = (Resolve-Path -LiteralPath $ReferenceProject).Path
if ($referenceDirectory -eq $projectDirectory) { throw '请指定另一项目，不能与自身比较。' }
foreach ($relative in @('contracts\sim-v1-fixtures.json', 'contracts\模拟接口-v1.md')) {
    $local = Join-Path $projectDirectory $relative
    $reference = Join-Path $referenceDirectory $relative
    if ((Get-FileHash -LiteralPath $local).Hash -ne (Get-FileHash -LiteralPath $reference).Hash) {
        throw "联调契约漂移：$relative；两端同步修改并分别运行解析/生成测试，不自动覆盖。"
    }
}
Write-Output 'CONTRACT_CHECK=passed（双端文档与固定样例字节一致；仍需各自单测）'
