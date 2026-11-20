param(
    [string]$BuildDir = (Join-Path $PSScriptRoot 'build-qt')
)

$ErrorActionPreference = 'Stop'
$cache = Join-Path $BuildDir 'CMakeCache.txt'
$configuredCMake = Select-String -LiteralPath $cache -Pattern '^CMAKE_COMMAND:INTERNAL=(.+)$' -Encoding UTF8 -ErrorAction SilentlyContinue
if (-not $configuredCMake) {
    throw "请先用 CMake 配置构建目录 $BuildDir。"
}

$cmake = $configuredCMake.Matches[0].Groups[1].Value
& $cmake --build $BuildDir --target DeployOrchestrate
if ($LASTEXITCODE -ne 0) {
    throw "部署失败（退出码 $LASTEXITCODE）。"
}
