param(
    [string]$BuildDir = (Join-Path $PSScriptRoot 'build-qt')
)

$ErrorActionPreference = 'Stop'
$windeployqt = 'D:\CodeTools\Qt\Qt\6.11.2\mingw_64\bin\windeployqt.exe'
$executable = Join-Path $BuildDir 'Orchestrate.exe'

if (-not (Test-Path -LiteralPath $windeployqt)) {
    throw "找不到 windeployqt：$windeployqt"
}
if (-not (Test-Path -LiteralPath $executable)) {
    throw "找不到构建产物：$executable；请先运行 cmake --build。"
}

& $windeployqt --release --no-translations --compiler-runtime $executable
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt 失败，退出码：$LASTEXITCODE"
}

Write-Host "部署完成：$executable"
Write-Host '现在可以直接双击 Orchestrate.exe 运行，无需依赖当前 PowerShell 的 PATH。'
