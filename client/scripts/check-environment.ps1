param([string]$QtRoot = 'D:\Qt6')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'environment.ps1')
$null = Get-Rm27Environment -QtRoot $QtRoot
Write-Host '本检查只读系统网络状态，不修改网卡、IP、DNS，不连接裁判系统。'
$endpoints = @(Get-NetUDPEndpoint -ErrorAction Stop | Where-Object { $_.LocalPort -in 39001,39002,39003 })
$endpoints += @(Get-NetTCPConnection -State Listen -ErrorAction Stop | Where-Object { $_.LocalPort -eq 39004 })
foreach ($endpoint in $endpoints) {
    Write-Warning "本机联调端口 $($endpoint.LocalPort) 已由 PID $($endpoint.OwningProcess) 占用，请确认是否是已运行的客户端/模拟器。不会自动关闭进程。"
}
if (-not $endpoints.Count) { Write-Host '默认联调接收端口当前未占用。此检查不能检测所有 UDP 发送会话。' }
