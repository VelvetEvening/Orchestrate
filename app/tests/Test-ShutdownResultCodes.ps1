param([string]$Library = (Join-Path $PSScriptRoot '../tools/scheduled-shutdown/shutdown-lib.ps1'))
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. ([scriptblock]::Create([IO.File]::ReadAllText($Library, [Text.Encoding]::UTF8)))
function Assert([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
# Mock all external reads/writes; exercise the real snapshot and state publisher.
function Get-ShutdownReminderPath { return 'C:\fixture\shutdown-reminder.ps1' }
function Get-ShutdownStatePath { return 'not-written.json' }
function Get-ScheduledTask {
    return [pscustomobject]@{ State='Ready'; Actions=@([pscustomobject]@{
        Arguments='-File "C:\fixture\shutdown-reminder.ps1" -CountdownSeconds 240'
    }); Triggers=@() }
}
function Get-ScheduledTaskInfo {
    return [pscustomobject]@{ NextRunTime=(Get-Date).AddDays(1); LastRunTime=(Get-Date).AddHours(-1); LastTaskResult=$script:taskCode }
}
function Get-ShutdownPendingCountdown { return $null }
function Write-JsonFileAtomic {
    param([string]$Path, $Value)
    Assert ($Path -eq 'not-written.json') 'Unexpected write path.'
    $script:stateJson = $Value | ConvertTo-Json -Depth 8
}
$cases = @(
    @{ Value=$null; Result='skipped'; Hex=$null },
    @{ Value=[uint32]0; Result='success'; Hex=$null },
    @{ Value=[uint32]267009; Result='running'; Hex=$null },
    @{ Value=[uint32]267011; Result='skipped'; Hex=$null },
    @{ Value=[uint32]1; Result='failure'; Hex='0x00000001' },
    @{ Value=[uint32]2147483647; Result='failure'; Hex='0x7FFFFFFF' },
    @{ Value=[uint32]2147483648; Result='failure'; Hex='0x80000000' },
    @{ Value=[uint32]4294770688; Result='failure'; Hex='0xFFFD0000' },
    @{ Value=[uint32]4294967295; Result='failure'; Hex='0xFFFFFFFF' },
    @{ Value=[int]-196608; Result='failure'; Hex='0xFFFD0000' },
    @{ Value=[int]-1; Result='failure'; Hex='0xFFFFFFFF' },
    @{ Value=[int]::MinValue; Result='failure'; Hex='0x80000000' }
)
foreach ($case in $cases) {
    $script:taskCode = $case.Value
    $snapshot = Get-ShutdownTaskSnapshot
    Assert ($snapshot.LastTaskResult -eq $case.Value) 'Snapshot changed raw result.'
    $text = Get-ShutdownLastRunText $snapshot
    Assert ((Get-ShutdownLastRunResult $snapshot) -eq $case.Result) "Incorrect result for $($case.Value)"
    if ($case.Hex) { Assert ($text.Contains($case.Hex)) "Missing hex diagnostic for $($case.Value): $text" }
    $script:stateJson = ''
    $published = Publish-ShutdownState -LastCommand 'status'
    $state = $script:stateJson | ConvertFrom-Json
    Assert ($state.schema -eq 'orchestrate-state/v1' -and $state.last_command -eq 'status') 'State publication failed.'
    Assert ($published.Result -eq 'success') 'A historical exit code must not fail a successful status read.'
    $last = @($state.items | Where-Object name -eq '最近一次执行')
    Assert ($last.Count -eq 1 -and $last[0].result -eq $case.Result) 'Wrong historical result in state JSON.'
    Assert ($last[0].summary -eq $text) 'State JSON lost diagnostic text.'
    Write-Output "PASS code=$($case.Value) -> $text"
}
$script:taskCode = [uint32]4294770688
Assert ((Get-ShutdownLastRunText (Get-ShutdownTaskSnapshot)).Contains('4294770688')) 'Original unsigned code was lost.'
# The error-reporting path also publishes state; it must not overflow recursively.
$published = Publish-ShutdownState -LastCommand 'enable' -Result 'failure' -Summary 'mock command failure'
Assert ($published.Result -eq 'failure') 'Command failure was lost while reading task history.'
Write-Output 'PASS 12 result-code cases and failure-state publication; no real task or state file changed.'
