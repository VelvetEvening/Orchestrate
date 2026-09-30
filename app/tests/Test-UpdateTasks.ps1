param([string]$Updater = (Join-Path $PSScriptRoot '../updater/Update-Orchestrate.ps1'))
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. ([scriptblock]::Create([IO.File]::ReadAllText((Resolve-Path -LiteralPath $Updater), [Text.Encoding]::UTF8)))
function Assert([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }

# All Windows task/process APIs are replaced before any updater code executes.
function Get-CimInstance { return $script:processes }
function Get-ScheduledTask([string]$TaskName, [string]$TaskPath) {
    if ($TaskName) { return @($script:tasks | Where-Object { $_.TaskName -ceq $TaskName -and $_.TaskPath -ceq $TaskPath }) }
    return $script:tasks
}
function Export-ScheduledTask([string]$TaskName, [string]$TaskPath) {
    $task = Get-ScheduledTask $TaskName $TaskPath
    return '<Task><Settings><Enabled>' + ([string]$task.State -ne 'Disabled').ToString().ToLowerInvariant() +
        '</Enabled></Settings><Actions>' + $task.Definition + '</Actions></Task>'
}
function Disable-ScheduledTask([string]$TaskName, [string]$TaskPath) {
    if ($TaskName -eq $script:disableFailure) { throw 'Injected disable failure' }
    $task = Get-ScheduledTask $TaskName $TaskPath
    $task.State = 'Disabled'
    $script:disabled += $TaskName
}
function Enable-ScheduledTask([string]$TaskName, [string]$TaskPath) {
    if ($TaskName -eq $script:enableFailure) { throw 'Injected enable failure' }
    $task = Get-ScheduledTask $TaskName $TaskPath
    $task.State = 'Ready'
    $script:enabled += $TaskName
}
function New-Task([string]$Name, [string]$State, [string]$Arguments) {
    return [pscustomobject]@{ TaskName = $Name; TaskPath = '\'; State = $State; Definition = $Name;
        Actions = @([pscustomobject]@{ Execute = 'powershell.exe'; Arguments = $Arguments; WorkingDirectory = '' }) }
}
function Reset-Tasks {
    $script:tasks = @(
        (New-Task 'a-enabled' 'Ready' ('-File "' + $install + '\tools\shutdown.ps1"')),
        (New-Task 'b-disabled' 'Disabled' ('-File "' + $install + '\tools\other.ps1"')),
        (New-Task 'c-other-install' 'Ready' ('-File "' + $install + '-other\tools\shutdown.ps1"')),
        (New-Task 'd-enabled' 'Ready' ('-File "' + $install.Replace('\', '/') + '/tools/second.ps1"'))
    )
    $script:disabled = @(); $script:enabled = @(); $script:processes = @()
    $script:disableFailure = ''; $script:enableFailure = ''
}
$root = Join-Path ([IO.Path]::GetTempPath()) ('Orchestrate-task-tests-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $root
$install = Join-Path $root 'app with spaces'
$plan = @{ workspace = $root; install_directory = $install }
try {
    Reset-Tasks
    Assert-ToolsStopped $install -AllowEnabledTasks
    $records = New-Object 'System.Collections.Generic.List[object]'
    Suspend-UpdateTasks $plan $records
    Assert (($script:disabled -join ',') -eq 'a-enabled,d-enabled') 'Wrong tasks were paused.'
    Assert-ToolsStopped $install
    $errors = @(Resume-UpdateTasks $plan $records)
    Assert (-not $errors.Count) 'Task resume failed.'
    Assert (($script:enabled -join ',') -eq 'a-enabled,d-enabled') 'Originally disabled/foreign tasks were enabled.'
    $journal = @(Get-Content (Join-Path $root 'suspended-tasks.json') -Raw | ConvertFrom-Json)
    Assert (@($journal | Where-Object { -not $_.restored }).Count -eq 0) 'Journal did not record recovery.'
    Write-Output 'PASS prepare leaves tasks enabled; apply pauses matching tasks; resume preserves settings and other installations'

    Reset-Tasks
    $script:disableFailure = 'd-enabled'
    $records = New-Object 'System.Collections.Generic.List[object]'
    $failed = $false
    try { Suspend-UpdateTasks $plan $records } catch { $failed = $true }
    Assert $failed 'Injected disable failure was ignored.'
    $errors = @(Resume-UpdateTasks $plan $records)
    Assert (-not $errors.Count -and $script:tasks[0].State -eq 'Ready') 'Earlier tasks stayed disabled after partial failure.'
    Write-Output 'PASS partial pause failure restores tasks already changed'

    foreach ($kind in @('permissions', 'edited')) {
        Reset-Tasks
        $records = New-Object 'System.Collections.Generic.List[object]'
        Suspend-UpdateTasks $plan $records
        if ($kind -eq 'permissions') { $script:enableFailure = 'a-enabled' }
        else { $script:tasks[0].Definition = 'replacement task' }
        $errors = @(Resume-UpdateTasks $plan $records)
        Assert ($errors.Count -eq 1 -and -not $records[0].restored -and $records[1].restored) 'Recovery did not continue or retain failed task evidence.'
        Assert ($script:tasks[0].State -eq 'Disabled') 'Edited or inaccessible task was silently enabled.'
    }
    Write-Output 'PASS resume errors keep journal; changed task definitions are not overwritten'

    Reset-Tasks
    $script:tasks[0].State = 'Running'
    $failed = $false
    try { Assert-ToolsStopped $install -AllowEnabledTasks } catch { $failed = $true }
    Assert $failed 'Running task was accepted.'
    Reset-Tasks
    $script:processes = @([pscustomobject]@{ ProcessId = 1234567; CommandLine = 'powershell -File "' + $install + '\tools\shutdown.ps1"' })
    $failed = $false
    try { Assert-ToolsStopped $install -AllowEnabledTasks } catch { $failed = $true }
    Assert $failed 'Running countdown was accepted.'
    Write-Output 'PASS running tasks/countdowns block replacement without stopping processes'
} finally {
    $resolved = [IO.Path]::GetFullPath($root)
    if ((Split-Path $resolved -Parent) -ne [IO.Path]::GetTempPath().TrimEnd('\', '/') -or
        (Split-Path $resolved -Leaf) -notmatch '^Orchestrate-task-tests-[0-9a-f]{32}$') { throw 'Unsafe test cleanup.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
