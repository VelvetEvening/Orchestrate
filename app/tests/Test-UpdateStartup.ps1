param(
    [Parameter(Mandatory)][string]$Application,
    [string]$Updater
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $Updater) { $Updater = Join-Path $PSScriptRoot '../updater/Update-Orchestrate.ps1' }
. ([scriptblock]::Create([IO.File]::ReadAllText((Resolve-Path -LiteralPath $Updater), [Text.Encoding]::UTF8)))
function Assert([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class UpdateTestExit {
    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool PostThreadMessage(uint threadId, uint message, UIntPtr wParam, IntPtr lParam);
}
'@
function Exit-TestApplication($Process) {
    $Process.Refresh()
    if ($Process.HasExited) { return }
    $null = $Process.Handle
    if (-not [UpdateTestExit]::PostThreadMessage([uint32]$Process.Threads[0].Id, 0x0012, [UIntPtr]::Zero, [IntPtr]::Zero)) {
        throw 'Could not post normal event-loop exit.'
    }
    Assert ($Process.WaitForExit(10000)) 'Application did not exit normally.'
    Assert ($Process.ExitCode -eq 0) "Application exited abnormally: '$($Process.ExitCode)'."
}
function Wait-File([string]$Path) {
    $deadline = [datetime]::UtcNow.AddSeconds(15)
    while (-not (Test-Path -LiteralPath $Path) -and [datetime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 100 }
    Assert (Test-Path -LiteralPath $Path) "Timed out waiting for $Path"
}
$root = Join-Path ([IO.Path]::GetTempPath()) ('Orchestrate-startup-tests-' + [guid]::NewGuid().ToString('N'))
$install = Join-Path $root 'app with spaces'
$workspace = Join-Path $root '.Orchestrate-update-startup'
$old = $null; $new = $null; $worker = $null
$originalPlatform = $env:QT_QPA_PLATFORM
$env:QT_QPA_PLATFORM = 'offscreen'
try {
    $null = New-Item -ItemType Directory -Path $install, $workspace, (Join-Path $install 'data') -Force
    Copy-Item -LiteralPath $Application -Destination (Join-Path $install 'Orchestrate.exe')
    # Existing empty SQLite fixture prevents migration from any user AppData.
    [IO.File]::WriteAllBytes((Join-Path $install 'data/orchestrate.sqlite3'), [byte[]]@())
    $version = [Diagnostics.FileVersionInfo]::GetVersionInfo((Resolve-Path $Application)).ProductVersion
    $version = ($version -split '\.')[0..2] -join '.'
    Write-UpdateJson (Join-Path $install 'build-info.json') @{ version = '0.0.1'; release_tag = 'v0.0.1'; architecture = 'windows-x64' }
    $lines = @('Orchestrate.exe', 'build-info.json') | ForEach-Object {
        (Get-FileHash -LiteralPath (Join-Path $install $_)).Hash.ToLowerInvariant() + '  ' + $_
    }
    [IO.File]::WriteAllLines((Join-Path $install 'files.sha256'), $lines)
    $payload = Join-Path $workspace "payload/Orchestrate-v$version-windows-x64"
    $null = New-Item -ItemType Directory -Path $payload -Force
    Copy-Item -LiteralPath $Application -Destination (Join-Path $payload 'Orchestrate.exe')
    Write-UpdateJson (Join-Path $payload 'build-info.json') @{ version = $version; release_tag = "v$version"; architecture = 'windows-x64' }
    $lines = @('Orchestrate.exe', 'build-info.json') | ForEach-Object {
        (Get-FileHash -LiteralPath (Join-Path $payload $_)).Hash.ToLowerInvariant() + '  ' + $_
    }
    [IO.File]::WriteAllLines((Join-Path $payload 'files.sha256'), $lines)
    Copy-Item -LiteralPath $Updater -Destination (Join-Path $workspace 'Update-Orchestrate.ps1')
    $oldHealth = Join-Path $workspace 'startup.json'
    $old = Start-Process -FilePath (Join-Path $install 'Orchestrate.exe') -WorkingDirectory $install -WindowStyle Hidden `
        -ArgumentList ('--update-health-file "' + $oldHealth + '"') -PassThru
    Wait-File $oldHealth
    $health = Get-Content -LiteralPath $oldHealth -Raw | ConvertFrom-Json
    Assert ($health.process_id -eq $old.Id -and $health.version -eq $version) "Startup identity mismatch: runtime $($health.version)/$($health.process_id), file version $version, launched PID $($old.Id)."
    Remove-Item -LiteralPath $oldHealth
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $digest = $sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($install.Replace('\', '/').ToLowerInvariant())) } finally { $sha.Dispose() }
    $plan = @{ install_directory = $install; workspace = $workspace; version = $version; process_id = $old.Id;
        archive_sha256 = ('a' * 64); mutex_name = 'Local\Orchestrate-' + ([BitConverter]::ToString($digest).Replace('-', '').ToLowerInvariant()) }
    Write-UpdateJson (Join-Path $workspace 'plan.json') $plan
    # Real process handoff, mutex, filesystem replacement and startup handshake;
    # only the task API is mocked, so no Windows scheduled task is changed.
    $bootstrap = @'
param([string]$Workspace)
$ErrorActionPreference = 'Stop'
. (Join-Path $Workspace 'Update-Orchestrate.ps1')
function Get-InstallationTasks([string]$Install) { }
[IO.File]::WriteAllText((Join-Path $Workspace 'worker-ready'), 'ready')
Invoke-UpdatePlan (Join-Path $Workspace 'plan.json')
'@
    [IO.File]::WriteAllText((Join-Path $workspace 'worker.ps1'), $bootstrap)
    $worker = Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -WindowStyle Hidden -WorkingDirectory $workspace `
        -ArgumentList ('-NoProfile -ExecutionPolicy Bypass -File "' + (Join-Path $workspace 'worker.ps1') + '" -Workspace "' + $workspace + '"') `
        -RedirectStandardOutput (Join-Path $root 'worker.out') -RedirectStandardError (Join-Path $root 'worker.err') -PassThru
    $null = $worker.Handle # Keep the native handle so PowerShell 5 reports ExitCode after exit.
    Wait-File (Join-Path $workspace 'worker-ready')
    Start-Sleep -Milliseconds 300
    Assert ((Get-Content (Join-Path $install 'build-info.json') -Raw | ConvertFrom-Json).version -eq '0.0.1') 'Files replaced before old process exit.'
    Assert (-not $worker.HasExited) 'Updater did not wait for old process.'
    Exit-TestApplication $old
    Assert ($worker.WaitForExit(20000)) 'Updater failed to finish.'
    Assert ($worker.ExitCode -eq 0) ("Updater exit code '$($worker.ExitCode)': " + (Get-Content (Join-Path $root 'worker.err') -Raw))
    $result = Get-Content (Join-Path $install 'data/update-result.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    Assert ($result.success -and -not $result.cleanup_pending) 'Real update or self-cleanup failed.'
    Assert ($result.previous_process_id -eq $old.Id -and $result.process_id -ne $old.Id) 'Process identity was not changed.'
    $new = Get-Process -Id $result.process_id -ErrorAction Stop
    Assert ($new.MainModule.FileName -ieq (Join-Path $install 'Orchestrate.exe')) 'Restarted wrong executable.'
    Assert (-not (Test-Path -LiteralPath $workspace)) 'Self-running updater workspace survived.'
    Assert (Test-Path -LiteralPath (Join-Path $result.backup_directory 'data/orchestrate.sqlite3')) 'Old data backup was lost.'
    Exit-TestApplication $new
    Write-Output 'PASS real app event-loop readiness, wait for normal old-process exit, mutex handoff, restart and updater self-cleanup'
} catch {
    Write-Output $_.ScriptStackTrace
    Write-Output $_.Exception.Message
    throw
} finally {
    foreach ($process in @($worker, $new, $old)) {
        if ($process) { $process.Refresh(); if (-not $process.HasExited) { $process.Kill(); $null = $process.WaitForExit(5000) } }
    }
    # A failed assertion can occur after the updater starts the new process but
    # before its PID is read. Only collect executables inside this exact fixture.
    foreach ($process in Get-Process -Name Orchestrate -ErrorAction SilentlyContinue) {
        if ($process.MainModule.FileName -ieq (Join-Path $install 'Orchestrate.exe')) {
            $process.Kill(); $null = $process.WaitForExit(5000)
        }
    }
    $env:QT_QPA_PLATFORM = $originalPlatform
    $resolved = [IO.Path]::GetFullPath($root)
    if ((Split-Path $resolved -Parent) -ne [IO.Path]::GetTempPath().TrimEnd('\', '/') -or
        (Split-Path $resolved -Leaf) -notmatch '^Orchestrate-startup-tests-[0-9a-f]{32}$') { throw 'Unsafe test cleanup.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
