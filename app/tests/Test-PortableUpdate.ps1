param([string]$Updater = (Join-Path $PSScriptRoot '../updater/Update-Orchestrate.ps1'))
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. ([scriptblock]::Create([IO.File]::ReadAllText((Resolve-Path -LiteralPath $Updater), [Text.Encoding]::UTF8)))
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Assert([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Put([string]$Root, [string]$Relative, [string]$Text) {
    $path = Join-Path $Root $Relative
    $null = New-Item -ItemType Directory -Path (Split-Path $path -Parent) -Force
    [IO.File]::WriteAllText($path, $Text, [Text.UTF8Encoding]::new($false))
}
function Manifest([string]$Root) {
    $lines = @(foreach ($file in Get-ChildItem -LiteralPath $Root -Recurse -File | Sort-Object FullName) {
        $relative = $file.FullName.Substring($Root.Length + 1).Replace('\', '/')
        if ($relative -ne 'files.sha256') { (Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant() + '  ' + $relative }
    })
    Put $Root 'files.sha256' (($lines -join "`n") + "`n")
}
function Assert-ToolsStopped([string]$Install) { }
$root = Join-Path ([IO.Path]::GetTempPath()) ('Orchestrate-update-tests-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $root
try {
    $install = Join-Path $root 'app with spaces'
    $workspace = Join-Path $root '.Orchestrate-update-tests'
    $payloadRoot = Join-Path $root 'source/Orchestrate-v1.1.0-windows-x64'
    $null = New-Item -ItemType Directory -Path $workspace
    Put $install 'Orchestrate.exe' 'old executable'
    Put $install 'obsolete.dll' 'old runtime'
    Put $install 'tools/test/script.ps1' 'old tool'
    Put $install 'build-info.json' '{"version":"1.0.0","release_tag":"v1.0.0","architecture":"windows-x64"}'
    Manifest $install
    Put $install 'data/orchestrate.sqlite3' 'personal database bytes'
    Put $install 'tools/test/state/current.json' '{"personal":true}'
    Put $install 'tools/custom/custom.ps1' 'external tool'
    Put $install 'tools/test/preferences.ini' 'custom configuration'
    Put $payloadRoot 'Orchestrate.exe' 'new executable'
    Put $payloadRoot 'new.dll' 'new runtime'
    Put $payloadRoot 'tools/test/script.ps1' 'new tool'
    Put $payloadRoot 'build-info.json' '{"version":"1.1.0","release_tag":"v1.1.0","architecture":"windows-x64"}'
    Manifest $payloadRoot
    $archive = Join-Path $workspace 'release.zip'
    [IO.Compression.ZipFile]::CreateFromDirectory($payloadRoot, $archive, [IO.Compression.CompressionLevel]::Optimal, $true)
    $plan = [pscustomobject]@{ install_directory = $install; workspace = $workspace; version = '1.1.0';
        archive_sha256 = (Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant() }
    $null = Expand-VerifiedPackage $plan
    $backup = Install-PreparedPackage $plan
    Assert ((Get-Content -LiteralPath (Join-Path $install 'Orchestrate.exe') -Raw) -eq 'new executable') 'Executable was not replaced.'
    Assert (-not (Test-Path -LiteralPath (Join-Path $install 'obsolete.dll'))) 'Obsolete DLL survived.'
    foreach ($relative in @('data/orchestrate.sqlite3', 'tools/test/state/current.json', 'tools/custom/custom.ps1', 'tools/test/preferences.ini')) {
        Assert ((Get-FileHash -LiteralPath (Join-Path $install $relative)).Hash -eq (Get-FileHash -LiteralPath (Join-Path $backup $relative)).Hash) "Personal file changed: $relative"
    }
    Assert ((Get-Content -LiteralPath (Join-Path $backup 'Orchestrate.exe') -Raw) -eq 'old executable') 'Backup is incomplete.'
    Write-Output 'PASS update, complete backup, personal data/configuration, external tools and obsolete runtime cleanup'

    # Start from another old copy and inject failure between the two directory moves.
    $rollbackInstall = Join-Path $root 'rollback-app'
    Copy-Item -LiteralPath $backup -Destination $rollbackInstall -Recurse
    $rollbackWorkspace = Join-Path $root '.Orchestrate-update-rollback'
    $null = New-Item -ItemType Directory -Path $rollbackWorkspace
    Copy-Item -LiteralPath (Join-Path $workspace 'payload') -Destination $rollbackWorkspace -Recurse
    $rollbackPlan = [pscustomobject]@{ install_directory = $rollbackInstall; workspace = $rollbackWorkspace; version = '1.1.0' }
    function Move-UpdateDirectory([string]$Source, [string]$Destination) {
        if ((Split-Path $Source -Leaf) -eq 'staged') { throw 'Injected replacement failure' }
        [IO.Directory]::Move($Source, $Destination)
    }
    $failed = $false
    try { $null = Install-PreparedPackage $rollbackPlan } catch { $failed = $true }
    Assert $failed 'Injected replacement failure did not fail.'
    Assert ((Get-Content -LiteralPath (Join-Path $rollbackInstall 'Orchestrate.exe') -Raw) -eq 'old executable') 'Rollback failed.'
    Assert ((Get-Content -LiteralPath (Join-Path $rollbackInstall 'data/orchestrate.sqlite3') -Raw) -eq 'personal database bytes') 'Rollback lost personal data.'
    Write-Output 'PASS replacement failure restores the old installation and database'

    function Move-UpdateDirectory([string]$Source, [string]$Destination) { [IO.Directory]::Move($Source, $Destination) }
    foreach ($shouldFail in @($true, $false)) {
        $caseName = if ($shouldFail) { 'startup-failure' } else { 'startup-success' }
        $caseInstall = Join-Path $root $caseName
        $caseWorkspace = Join-Path $root ('.Orchestrate-update-' + $caseName.Replace('-', ''))
        Copy-Item -LiteralPath $backup -Destination $caseInstall -Recurse
        $null = New-Item -ItemType Directory -Path $caseWorkspace
        Copy-Item -LiteralPath (Join-Path $workspace 'payload') -Destination $caseWorkspace -Recurse
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $digest = $sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($caseInstall.Replace('\', '/').ToLowerInvariant())) }
        finally { $sha.Dispose() }
        $casePlan = @{ install_directory = $caseInstall; workspace = $caseWorkspace; version = '1.1.0'; process_id = 2147483647;
            archive_sha256 = $plan.archive_sha256; mutex_name = 'Local\Orchestrate-' + ([BitConverter]::ToString($digest).Replace('-', '').ToLowerInvariant()) }
        Write-UpdateJson (Join-Path $caseWorkspace 'plan.json') $casePlan
        $script:startCount = 0
        function Start-UpdatedApplication([string]$Install, [string]$HealthPath = '') {
            $script:startCount++
            if ($HealthPath) {
                $fake = [pscustomobject]@{ HasExited = $false; Id = 12345 }
                $fake | Add-Member ScriptMethod Kill { $this.HasExited = $true }
                $fake | Add-Member ScriptMethod WaitForExit { return $true }
                return $fake
            }
        }
        function Confirm-UpdatedStartup($Process, [string]$HealthPath, [string]$Version) {
            if ($shouldFail) { throw 'Injected startup failure' }
        }
        $failed = $false
        try { Invoke-UpdatePlan (Join-Path $caseWorkspace 'plan.json') } catch { $failed = $true }
        Assert ($failed -eq $shouldFail) 'Startup result was handled incorrectly.'
        $expected = if ($shouldFail) { 'old executable' } else { 'new executable' }
        Assert ((Get-Content -LiteralPath (Join-Path $caseInstall 'Orchestrate.exe') -Raw) -eq $expected) 'Startup rollback/install failed.'
        Assert ((Get-Content -LiteralPath (Join-Path $caseInstall 'data/orchestrate.sqlite3') -Raw) -eq 'personal database bytes') 'Startup handling lost database.'
        $result = Get-Content -LiteralPath (Join-Path $caseInstall 'data/update-result.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        Assert ($result.success -eq (-not $shouldFail)) 'Update status was not saved.'
        Assert ($script:startCount -eq $(if ($shouldFail) { 2 } else { 1 })) 'Application was not restarted correctly.'
    }
    Write-Output 'PASS startup confirmation and failed-start rollback with old-app restart'

    foreach ($path in @('../escape', 'data/orchestrate.sqlite3', 'tools/test/state/current.json', 'C:/escape', 'tools/test/x:stream', 'NUL.txt', 'tools\\escape', 'data./file')) {
        $failed = $false
        try { Assert-OwnedPath $path } catch { $failed = $true }
        Assert $failed "Unsafe path accepted: $path"
    }
    $newPayload = Join-Path $workspace 'payload/Orchestrate-v1.1.0-windows-x64'
    Put $newPayload 'new.dll' 'corrupted'
    $failed = $false
    try { $null = Read-PayloadManifest $newPayload -Verify } catch { $failed = $true }
    Assert $failed 'Corrupt payload accepted.'
    Write-Output 'PASS path traversal, personal-data overwrite and corruption rejection'
} finally {
    $resolvedRoot = [IO.Path]::GetFullPath($root)
    if (-not $resolvedRoot.StartsWith([IO.Path]::GetTempPath(), [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path $resolvedRoot -Leaf) -notmatch '^Orchestrate-update-tests-[0-9a-f]{32}$') { throw 'Unsafe test cleanup path.' }
    Remove-Item -LiteralPath $resolvedRoot -Recurse -Force
}
