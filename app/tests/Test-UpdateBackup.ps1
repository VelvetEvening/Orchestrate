param([string]$Updater = (Join-Path $PSScriptRoot '../updater/Update-Orchestrate.ps1'))
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. ([scriptblock]::Create([IO.File]::ReadAllText((Resolve-Path -LiteralPath $Updater), [Text.Encoding]::UTF8)))
function Assert([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Put([string]$Path, [string]$Text) {
    $null = New-Item -ItemType Directory -Path (Split-Path $Path -Parent) -Force
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function Get-CimInstance { return @() }
$root = Join-Path ([IO.Path]::GetTempPath()) ('Orchestrate-backup-tests-' + [guid]::NewGuid().ToString('N'))
$originalLocation = Get-Location
$nativeDirectory = [Environment]::CurrentDirectory
$null = New-Item -ItemType Directory -Path $root
function Fixture([string]$Name) {
    $install = Join-Path $root $Name
    $workspace = Join-Path $root ('.Orchestrate-update-' + $Name)
    $backup = Join-Path $root ($Name + '-backup-20261001-120000-1234abcd')
    Put (Join-Path $install 'Orchestrate.exe') 'new executable'
    Put (Join-Path $install 'build-info.json') '{"version":"1.1.4"}'
    Put (Join-Path $install 'data/orchestrate.sqlite3') 'current user data'
    Put (Join-Path $backup 'build-info.json') '{"version":"1.1.3"}'
    Put (Join-Path $backup 'data/orchestrate.sqlite3') 'old user data'
    Put (Join-Path $backup 'Orchestrate.exe') 'old executable'
    Put (Join-Path $workspace 'release.zip') 'download'
    $plan = @{ install_directory = $install; workspace = $workspace; version = '1.1.4' }
    $result = @{ success = $true; tasks_restored = $true; version = '1.1.4'; workspace = $workspace;
        backup_directory = $backup; message = 'updated'; cleanup_pending = $false }
    Write-UpdateJson (Join-Path $install 'data/update-result.json') $result
    return @{ install = $install; workspace = $workspace; backup = $backup; plan = $plan; result = $result }
}
try {
    $f = Fixture 'success'
    $unrelated = Join-Path $root 'success-backup-20260901-120000-abcd1234'
    Put (Join-Path $unrelated 'keep.txt') 'historical data'
    Complete-UpdateCleanup $f.plan $f.result
    Assert (-not (Test-Path -LiteralPath $f.backup)) 'Successful backup not removed.'
    Assert (-not (Test-Path -LiteralPath $f.workspace)) 'Successful workspace not removed.'
    Assert ((Get-Content -LiteralPath (Join-Path $f.install 'data/orchestrate.sqlite3') -Raw) -eq 'current user data') 'Live data changed.'
    Assert (Test-Path -LiteralPath (Join-Path $unrelated 'keep.txt')) 'Historical backup swept.'
    Assert ($f.result.backup_removed -and -not $f.result.cleanup_pending) 'Successful cleanup status incorrect.'
    Complete-UpdateCleanup $f.plan $f.result
    Write-Output 'PASS successful cleanup, idempotence, live data and historical backup preservation'

    foreach ($kind in @('failed', 'tasksfailed', 'unknown', 'pendingjournal', 'badjournal')) {
        $f = Fixture $kind
        switch ($kind) {
            failed { $f.result.success = $false }
            tasksfailed { $f.result.tasks_restored = $false }
            unknown { $f.result.Remove('tasks_restored') }
            pendingjournal { Write-UpdateJson (Join-Path $f.workspace 'suspended-tasks.json') @(@{ restored = $false }) }
            badjournal { Put (Join-Path $f.workspace 'suspended-tasks.json') '{broken' }
        }
        Complete-UpdateCleanup $f.plan $f.result
        Assert (Test-Path -LiteralPath (Join-Path $f.backup 'data/orchestrate.sqlite3')) "Backup lost for $kind"
        if ($kind -ne 'unknown') { Assert (Test-Path -LiteralPath $f.workspace) "Recovery workspace lost for $kind" }
    }
    Write-Output 'PASS failure, unknown completion and pending/corrupt task journal preservation'

    foreach ($kind in @('installtarget', 'parenttarget', 'foreigntarget', 'newerversion', 'badmetadata', 'versionmismatch', 'badworkspace')) {
        $f = Fixture $kind
        switch ($kind) {
            installtarget { $f.result.backup_directory = $f.install }
            parenttarget { $f.result.backup_directory = $root }
            foreigntarget { $f.result.backup_directory = $unrelated }
            newerversion { Put (Join-Path $f.backup 'build-info.json') '{"version":"9.0.0"}' }
            badmetadata { Put (Join-Path $f.backup 'build-info.json') '{broken' }
            versionmismatch { $f.result.version = '1.1.3' }
            badworkspace { $f.plan.workspace = $f.install }
        }
        Complete-UpdateCleanup $f.plan $f.result
        Assert (Test-Path -LiteralPath (Join-Path $f.backup 'data/orchestrate.sqlite3')) "Unsafe cleanup for $kind"
        Assert (Test-Path -LiteralPath (Join-Path $f.install 'Orchestrate.exe')) "Installation deleted for $kind"
        Assert ($f.result.success -and $f.result.cleanup_pending) "Failure status incorrect for $kind"
    }
    Write-Output 'PASS exact ownership, version, workspace and metadata guards'

    $f = Fixture 'locked'
    $handle = [IO.File]::Open((Join-Path $f.backup 'Orchestrate.exe'), 'Open', 'Read', 'None')
    try { Complete-UpdateCleanup $f.plan $f.result } finally { $handle.Dispose() }
    Assert ($f.result.success -and $f.result.cleanup_pending -and $f.result.cleanup_error) 'Locked backup treated as failed update.'
    Assert (Test-Path -LiteralPath (Join-Path $f.backup 'build-info.json')) 'Retry identity lost.'
    Complete-UpdateCleanup $f.plan $f.result
    Assert ($f.result.backup_removed -and -not $f.result.cleanup_pending) 'Locked backup retry failed.'
    Write-Output 'PASS locked backup keeps retry metadata and succeeds after unlock'

    $f = Fixture 'legacy'
    # Simulate an older updater that already cleaned its workspace.
    $resolvedWorkspace = (Resolve-Path -LiteralPath $f.workspace).Path
    Assert ((Split-Path $resolvedWorkspace -Parent) -eq $root) 'Unsafe fixture path.'
    Remove-Item -LiteralPath $resolvedWorkspace -Recurse -Force
    Remove-CompletedUpdateWorkspaces $f.install
    Assert (-not (Test-Path -LiteralPath $f.backup)) 'Legacy successful backup not cleaned without workspace.'
    $f = Fixture 'legacyfailed'
    $f.result.tasks_restored = $false
    Write-UpdateJson (Join-Path $f.install 'data/update-result.json') $f.result
    $resolvedWorkspace = (Resolve-Path -LiteralPath $f.workspace).Path
    Assert ((Split-Path $resolvedWorkspace -Parent) -eq $root) 'Unsafe fixture path.'
    Remove-Item -LiteralPath $resolvedWorkspace -Recurse -Force
    Remove-CompletedUpdateWorkspaces $f.install
    Assert (Test-Path -LiteralPath $f.backup) 'Failed task restoration legacy backup removed.'
    Write-Output 'PASS successful legacy cleanup without workspace; unresolved recovery preserved'

    $f = Fixture 'nestedlink'
    $protected = Join-Path $root 'protected'
    Put (Join-Path $protected 'keep.txt') 'must survive'
    $link = Join-Path $f.backup 'linked'
    $null = New-Item -ItemType Junction -Path $link -Target $protected
    try {
        Complete-UpdateCleanup $f.plan $f.result
        Assert ($f.result.cleanup_pending -and (Test-Path -LiteralPath $f.backup)) 'Linked backup accepted.'
        Assert ((Get-Content -LiteralPath (Join-Path $protected 'keep.txt') -Raw) -eq 'must survive') 'Junction target changed.'
    } finally { [IO.Directory]::Delete($link) }
    Write-Output 'PASS nested junction cannot escape backup cleanup boundary'

    $f = Fixture 'serialized'
    $workerScript = Join-Path $root 'lock-worker.ps1'
    $ready = Join-Path $root 'lock-ready'
    $released = Join-Path $root 'lock-released'
    $workerText = @'
param([string]$Updater, [string]$Install, [string]$Ready, [string]$Released)
. ([scriptblock]::Create([IO.File]::ReadAllText($Updater, [Text.Encoding]::UTF8)))
Invoke-WithUpdateCleanupLock $Install {
    [IO.File]::WriteAllText($Ready, 'held')
    Start-Sleep -Milliseconds 1500
    [IO.File]::WriteAllText($Released, 'done')
}
'@
    Put $workerScript $workerText
    $updaterAbsolute = (Resolve-Path -LiteralPath $Updater).Path
    $worker = Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -WindowStyle Hidden -PassThru -ArgumentList @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $workerScript + '"'),
        '-Updater', ('"' + $updaterAbsolute + '"'), '-Install', ('"' + $f.install + '"'),
        '-Ready', ('"' + $ready + '"'), '-Released', ('"' + $released + '"'))
    try {
        $deadline = [datetime]::UtcNow.AddSeconds(10)
        while (-not (Test-Path -LiteralPath $ready) -and [datetime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 50 }
        Assert (Test-Path -LiteralPath $ready) 'Cleanup lock worker did not start.'
        Invoke-WithUpdateCleanupLock $f.install.Replace('\', '/').ToUpperInvariant() {
            Assert (Test-Path -LiteralPath $released) 'Installation cleanup operations overlapped.'
        }
        Assert ($worker.WaitForExit(5000)) 'Cleanup lock worker did not stop.'
    } finally { if (-not $worker.HasExited) { $worker.Kill(); $null = $worker.WaitForExit(5000) } }
    Write-Output 'PASS cleanup serialization across processes and path spelling variants'
} finally {
    Set-Location -LiteralPath $originalLocation
    [Environment]::CurrentDirectory = $nativeDirectory
    $resolved = (Resolve-Path -LiteralPath $root).Path
    Assert ((Split-Path $resolved -Parent).TrimEnd('\') -eq [IO.Path]::GetTempPath().TrimEnd('\')) 'Unsafe fixture parent.'
    Assert ((Split-Path $resolved -Leaf) -match '^Orchestrate-backup-tests-[0-9a-f]{32}$') 'Unsafe fixture name.'
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
