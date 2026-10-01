param(
    [string]$ReleaseTag = 'v1.1.6',
    [string]$QtRoot = 'D:/CodeTools/Qt/Qt/6.11.2/mingw_64',
    [string]$MinGWRoot = 'D:/CodeTools/Qt/Qt/Tools/mingw1310_64',
    [string]$CMakeExe = 'D:/CodeTools/Qt/Qt/Tools/CMake_64/bin/cmake.exe',
    [string]$NinjaExe = 'D:/CodeTools/Qt/Qt/Tools/Ninja/ninja.exe',
    [string]$QtSourceDir = '',
    [string]$OutputDir = (Join-Path $PSScriptRoot '../dist')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
function Run([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
function Write-Utf8([string]$Path, [string]$Text) {
    [System.IO.File]::WriteAllText($Path, $Text, [System.Text.UTF8Encoding]::new($false))
}

$repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$outputRoot = [System.IO.Path]::GetFullPath($OutputDir)
# Validate ignore rules before creating any archive, build directory or staging file.
& git -C $repository check-ignore --no-index -q -- (Join-Path $outputRoot '.package-ignore-check')
if ($LASTEXITCODE -ne 0) { throw 'OutputDir must be inside this repository and covered by .gitignore.' }
$changes = @(& git -C $repository status --porcelain --untracked-files=normal)
if ($LASTEXITCODE -ne 0 -or $changes.Count -ne 0) { throw 'Commit or amend source changes before packaging; the package is built from git archive HEAD.' }
$sourceCommit = (& git -C $repository rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source commit.' }
$cmakeText = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'CMakeLists.txt') -Raw
$versionMatch = [regex]::Match($cmakeText, 'project\(Orchestrate VERSION ([0-9]+\.[0-9]+\.[0-9]+)')
if (-not $versionMatch.Success) { throw 'Cannot read application version from CMakeLists.txt.' }
$version = $versionMatch.Groups[1].Value
if ($ReleaseTag -notmatch '^v[0-9]+\.[0-9]+(?:\.[0-9]+)?$') { throw 'ReleaseTag must have the form v1.0 or v1.0.0.' }
$tagVersion = $ReleaseTag.Substring(1)
if ($tagVersion.Split('.').Count -eq 2) { $tagVersion += '.0' }
if ([version]$tagVersion -ne [version]$version) { throw 'ReleaseTag does not match the application version.' }
if (-not $QtSourceDir) { $QtSourceDir = Join-Path (Split-Path $QtRoot -Parent) 'Src' }
foreach ($required in @($CMakeExe, $NinjaExe, "$QtRoot/bin/qtpaths.exe", "$QtRoot/bin/windeployqt.exe",
        "$MinGWRoot/bin/g++.exe", "$QtSourceDir/qtbase/LICENSES", "$QtSourceDir/qtsvg/LICENSES", "$MinGWRoot/licenses")) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing build/deployment dependency: $required" }
}
$packageName = "Orchestrate-$ReleaseTag-windows-x64"
$zipPath = Join-Path $outputRoot ($packageName + '.zip')
if (Test-Path -LiteralPath $zipPath) { throw "Archive already exists: $zipPath. Use another ignored OutputDir to preserve the previous package." }
$runRoot = Join-Path $outputRoot ('work-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$null = New-Item -ItemType Directory -Path $runRoot -Force
$sourceArchive = Join-Path $runRoot 'source.zip'
Run git @('-C', $repository, 'archive', '--format=zip', "--output=$sourceArchive", $sourceCommit)
$sourceRoot = Join-Path $runRoot 'source'
Expand-Archive -LiteralPath $sourceArchive -DestinationPath $sourceRoot
$buildRoot = Join-Path $runRoot 'build'
$installRoot = Join-Path $runRoot 'install'
$packageRoot = Join-Path $runRoot $packageName
$originalPath = $env:PATH
try {
    $env:PATH = "$QtRoot/bin;$MinGWRoot/bin;$originalPath"
    $qtVersion = (& "$QtRoot/bin/qtpaths.exe" --query QT_VERSION).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Could not identify Qt version.' }
    Run $CMakeExe @('-S', "$sourceRoot/app", '-B', $buildRoot, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=OFF', "-DCMAKE_PREFIX_PATH=$QtRoot",
        "-DCMAKE_MAKE_PROGRAM=$NinjaExe", "-DCMAKE_CXX_COMPILER=$MinGWRoot/bin/g++.exe")
    Run $CMakeExe @('--build', $buildRoot, '--parallel', '3')
    # Tests remain local and are run in app/build-qt before packaging.
    # CMake installs only the executable and tool sources, excluding tool state.
    Run $CMakeExe @('--install', $buildRoot, '--prefix', $installRoot, '--config', 'Release')
    $null = New-Item -ItemType Directory -Path $packageRoot
    Get-ChildItem -LiteralPath (Join-Path $installRoot 'bin') | Copy-Item -Destination $packageRoot -Recurse
    $executable = Join-Path $packageRoot 'Orchestrate.exe'
    $extraSqlPlugins = @(Get-ChildItem -LiteralPath "$QtRoot/plugins/sqldrivers" -Filter '*.dll' |
        Where-Object { $_.BaseName -ne 'qsqlite' } | ForEach-Object { $_.BaseName })
    # This Widgets application uses raster rendering and SQLite only.
    Run "$QtRoot/bin/windeployqt.exe" @('--release', '--no-translations', '--compiler-runtime',
        '--no-opengl-sw', '--no-system-d3d-compiler', '--skip-plugin-types', 'generic,networkinformation',
        '--exclude-plugins', ($extraSqlPlugins -join ','), '--dir', $packageRoot, $executable)
    Write-Utf8 (Join-Path $packageRoot 'qt.conf') "[Paths]`nPrefix=.`nPlugins=.`n"

    foreach ($module in @('qtbase', 'qtsvg')) {
        $moduleRoot = (Resolve-Path -LiteralPath (Join-Path $QtSourceDir $module)).Path
        $licenseRoot = Join-Path $packageRoot "licenses/$module"
        $null = New-Item -ItemType Directory -Path $licenseRoot -Force
        Copy-Item -LiteralPath (Join-Path $moduleRoot 'LICENSES') -Destination $licenseRoot -Recurse
        foreach ($notice in Get-ChildItem -LiteralPath (Join-Path $moduleRoot 'src') -Recurse -File |
                Where-Object { $_.Name -match '^(LICENSE|COPYING|NOTICE|COPYRIGHT)' -or $_.Name -eq 'qt_attribution.json' }) {
            $relative = $notice.FullName.Substring($moduleRoot.Length + 1)
            $target = Join-Path $licenseRoot $relative
            $null = New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force
            Copy-Item -LiteralPath $notice.FullName -Destination $target
        }
    }
    foreach ($runtime in @('gcc', 'mingw-w64', 'winpthreads')) {
        $target = Join-Path $packageRoot "licenses/mingw/$runtime"
        $null = New-Item -ItemType Directory -Path $target -Force
        Get-ChildItem -LiteralPath "$MinGWRoot/licenses/$runtime" | Copy-Item -Destination $target -Recurse
    }
    $qtSeries = ($qtVersion.Split('.')[0..1] -join '.')
    foreach ($name in @('README.txt', 'THIRD-PARTY-NOTICES.txt')) {
        $template = Get-Content -LiteralPath "$sourceRoot/app/packaging/$name" -Raw -Encoding UTF8
        Write-Utf8 (Join-Path $packageRoot $name) ($template.Replace('@VERSION@', $version).Replace('@QT_VERSION@', $qtVersion).Replace('@QT_SERIES@', $qtSeries))
    }
    $info = [ordered]@{ version = $version; release_tag = $ReleaseTag; source_commit = $sourceCommit
        source_url = "https://github.com/VelvetEvening/Orchestrate/tree/$sourceCommit"
        architecture = 'windows-x64'; configuration = 'Release'; qt_version = $qtVersion
        compiler = 'MinGW GCC'; built_at_utc = [DateTime]::UtcNow.ToString('o') }
    Write-Utf8 (Join-Path $packageRoot 'build-info.json') (($info | ConvertTo-Json) + "`n")

    foreach ($required in @('Orchestrate.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Sql.dll', 'Qt6Network.dll',
            'libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll', 'platforms/qwindows.dll',
            'sqldrivers/qsqlite.dll', 'tls/qschannelbackend.dll', 'updater/Update-Orchestrate.ps1',
            'tools/scheduled-shutdown/orchestrate-tool.json')) {
        if (-not (Test-Path -LiteralPath (Join-Path $packageRoot $required))) { throw "Missing runtime file: $required" }
    }
    if ((Get-Item -LiteralPath $executable).VersionInfo.ProductVersion -ne $version) { throw 'Executable product version differs from CMake version.' }
    $payload = @(Get-ChildItem -LiteralPath $packageRoot -File -Recurse)
    foreach ($file in $payload) {
        $relative = $file.FullName.Substring($packageRoot.Length + 1).Replace('\', '/')
        if ($relative -match '^(data|state|tests|CMakeFiles|\.git)/|(^|/)state/|\.sqlite3?($|-)|\.log$|CMakeCache\.txt$|\.pdb$') {
            throw "Unexpected runtime or build data in package: $relative"
        }
    }
    $manifest = foreach ($file in $payload | Sort-Object FullName) {
        $relative = $file.FullName.Substring($packageRoot.Length + 1).Replace('\', '/')
        '{0}  {1}' -f (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $relative
    }
    Write-Utf8 (Join-Path $packageRoot 'files.sha256') (($manifest -join "`n") + "`n")
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::CreateFromDirectory($packageRoot, $zipPath, [System.IO.Compression.CompressionLevel]::Optimal, $true)
    $zipHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Utf8 ($zipPath + '.sha256') ("$zipHash  $packageName.zip`n")
    $info['archive_sha256'] = $zipHash
    $info['archive_bytes'] = (Get-Item -LiteralPath $zipPath).Length
    $info['package_directory'] = $packageRoot
    $info['build_directory'] = $buildRoot
    Write-Utf8 (Join-Path $outputRoot ($packageName + '.build.json')) (($info | ConvertTo-Json) + "`n")
    Write-Output "Portable archive: $zipPath"
    Write-Output "SHA256: $zipHash"
} finally {
    $env:PATH = $originalPath
}
