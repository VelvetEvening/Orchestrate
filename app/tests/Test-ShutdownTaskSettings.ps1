Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. ([scriptblock]::Create([IO.File]::ReadAllText((Join-Path $PSScriptRoot '../tools/scheduled-shutdown/shutdown-lib.ps1'), [Text.Encoding]::UTF8)))
# All Task Scheduler entry points are mocked; no real tasks or shutdowns.
function Assert([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Get-ShutdownReminderPath { return (Join-Path $PSScriptRoot '../tools/scheduled-shutdown/shutdown-reminder.ps1') }
function New-ScheduledTaskAction { return 'mock action' }
function New-ScheduledTaskTrigger { return 'mock trigger' }
function New-ScheduledTaskPrincipal { return 'mock principal' }
function New-ScheduledTaskSettingsSet {
    [CmdletBinding()] param([switch]$AllowStartIfOnBatteries, [switch]$DontStopIfGoingOnBatteries,
        [timespan]$ExecutionTimeLimit, [switch]$Disable)
    return [pscustomobject]@{ Enabled = (-not $Disable) }
}
function Get-ScheduledTask {
    [CmdletBinding()] param([string]$TaskName)
    $script:reads++
    if ($script:scenario -eq 'readfailure' -or ($script:scenario -eq 'verifyfailure' -and $script:reads -gt 1)) {
        Write-Error 'Injected task read failure' -Category PermissionDenied
        return
    }
    if (-not $script:present) { Write-Error 'Missing task' -Category ObjectNotFound; return }
    return [pscustomobject]@{ State = $script:state; Principal = 'mock principal' }
}
function Register-ScheduledTask {
    [CmdletBinding()] param([string]$TaskName, $Action, $Trigger, $Principal, $Settings, [switch]$Force)
    $script:registrations++
    Assert ($Settings.Enabled -eq (-not $script:originallyDisabled)) 'Registration did not preserve requested enabled state.'
    if ($script:scenario -eq 'registerfailure' -or $script:registrations -le $script:failAttempts) {
        Write-Error 'Injected registration failure' -Category PermissionDenied
        return
    }
    $script:present = $true
    $script:state = if ($Settings.Enabled -or $script:scenario -in @('recover', 'disablefailure', 'disableineffective')) { 'Ready' } else { 'Disabled' }
}
function Disable-ScheduledTask {
    [CmdletBinding()] param([string]$TaskName)
    $script:disables++
    if ($script:scenario -in @('disablefailure', 'disabledNoDisablePermission')) {
        Write-Error 'Injected disable permission failure' -Category PermissionDenied
        return
    }
    if ($script:scenario -ne 'disableineffective') { $script:state = 'Disabled' }
}
function Get-ShutdownTaskSnapshot {
    return [pscustomobject]@{
        Exists = ($script:scenario -ne 'snapshotfailure')
        Enabled = ($script:state -ne 'Disabled' -or $script:scenario -eq 'snapshotrace')
    }
}
$cases = @(
    @{ Name='disabled'; State='Disabled'; Success=$true; Attempts=1; Disables=0 },
    @{ Name='disabledNoDisablePermission'; State='Disabled'; Success=$true; Attempts=1; Disables=0 },
    @{ Name='enabled'; State='Ready'; Success=$true; Attempts=1; Disables=0 },
    @{ Name='newtask'; State='Missing'; Success=$true; Attempts=1; Disables=0 },
    @{ Name='readfailure'; State='Disabled'; Success=$false; Attempts=0; Disables=0 },
    @{ Name='unknown'; State='Unknown'; Success=$false; Attempts=0; Disables=0 },
    @{ Name='fallbackone'; State='Disabled'; Success=$true; Attempts=2; Disables=0 },
    @{ Name='fallbacktwo'; State='Disabled'; Success=$true; Attempts=3; Disables=0 },
    @{ Name='registerfailure'; State='Disabled'; Success=$false; Attempts=3; Disables=0 },
    @{ Name='recover'; State='Disabled'; Success=$true; Attempts=1; Disables=1 },
    @{ Name='disablefailure'; State='Disabled'; Success=$false; Attempts=1; Disables=1 },
    @{ Name='disableineffective'; State='Disabled'; Success=$false; Attempts=1; Disables=1 },
    @{ Name='verifyfailure'; State='Disabled'; Success=$false; Attempts=1; Disables=0 },
    @{ Name='snapshotfailure'; State='Disabled'; Success=$false; Attempts=1; Disables=0 },
    @{ Name='snapshotrace'; State='Disabled'; Success=$false; Attempts=1; Disables=0 }
)
foreach ($case in $cases) {
    $script:scenario = $case.Name
    $script:state = $case.State
    $script:originallyDisabled = $case.State -eq 'Disabled'
    $script:present = $case.State -ne 'Missing'
    $script:reads = 0; $script:registrations = 0; $script:disables = 0
    $script:failAttempts = if ($case.Name -eq 'fallbackone') { 1 } elseif ($case.Name -eq 'fallbacktwo') { 2 } else { 0 }
    $failed = $false; $result = $null; $message = ''
    try { $result = Set-ShutdownTask -Time '00:10' -CountdownSeconds 240 -WeekdaysOnly $false }
    catch { $failed = $true; $message = $_.Exception.Message }
    Assert ($failed -eq (-not $case.Success)) "Wrong outcome for $($case.Name): $message"
    Assert ($script:registrations -eq $case.Attempts) "Unexpected registration count: $($case.Name)"
    Assert ($script:disables -eq $case.Disables) "Unexpected disable count: $($case.Name)"
    if ($case.Success) { Assert ($result.Enabled -eq (-not $script:originallyDisabled)) "Changed original state: $($case.Name)" }
    if ($case.Name -in @('disablefailure','disableineffective','verifyfailure','snapshotrace')) {
        Assert ($message -match '任务计划程序') "Failure lacks actionable warning: $($case.Name): $message"
    }
    Write-Output "PASS $($case.Name)"
}
Write-Output 'PASS 15 isolated shutdown task settings cases; no real scheduled tasks changed.'
