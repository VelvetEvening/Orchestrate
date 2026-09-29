# Run with Windows PowerShell 5.1; no Pester or external dependencies required.
# Only the parsed disable branch is executed, with every system operation mocked.
# The production script/library is never dot-sourced and no task/process is changed.
param(
    [string]$ControlPath = (Join-Path $PSScriptRoot '..\tools\scheduled-shutdown\shutdown-control.ps1')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Resolve-Path -LiteralPath $ControlPath).Path, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -ne 0) { throw ($parseErrors | Out-String) }

$switches = @($ast.FindAll({
    param($node)
    $node -is [System.Management.Automation.Language.SwitchStatementAst]
}, $true))
$branches = @($switches | ForEach-Object {
    foreach ($clause in $_.Clauses) {
        if ($clause.Item1.Extent.Text -eq "'disable'") { $clause.Item2 }
    }
})
if ($branches.Count -ne 1) { throw 'Expected exactly one disable branch.' }
$branch = $branches[0]

# Fail closed if the branch gains a command that this test has not mocked.
$allowedCommands = @('Stop-ShutdownCountdownWindow', 'Invoke-ShutdownAbort',
    'Disable-ScheduledTask', 'Get-ShutdownTaskName', 'Complete-ShutdownAction', 'Out-Null')
foreach ($command in $branch.FindAll({
    param($node)
    $node -is [System.Management.Automation.Language.CommandAst]
}, $true)) {
    if ($command.GetCommandName() -notin $allowedCommands) {
        throw "Unmocked command in disable branch: $($command.Extent.Text)"
    }
}
$body = $branch.Extent.Text
$disable = [scriptblock]::Create($body.Substring(1, $body.Length - 2))

function Stop-ShutdownCountdownWindow {
    $script:calls.Add('stop')
    return $script:hasCountdown
}
function Invoke-ShutdownAbort {
    $script:calls.Add('abort')
    return $script:hasQueuedShutdown
}
function Get-ShutdownTaskName { return 'AUDIT-NOT-A-REAL-TASK' }
function Disable-ScheduledTask {
    param([string]$TaskName)
    if ($TaskName -ne 'AUDIT-NOT-A-REAL-TASK') { throw 'Unexpected task name.' }
    $script:calls.Add('disable')
}
function Complete-ShutdownAction {
    param([string]$Result, [string]$Summary)
    $script:calls.Add('complete')
    $script:outcome = @{ Result = $Result; Summary = $Summary }
    # Model the production completion function's exit without terminating the suite.
    throw [System.OperationCanceledException]::new('TEST_COMPLETED')
}
function Assert-Equal {
    param($Actual, $Expected, [string]$Context)
    if ($Actual -cne $Expected) {
        throw "$Context -- expected [$Expected], got [$Actual]"
    }
}

$states = @(
    @{ Name = 'absent'; Exists = $false; Enabled = $false },
    @{ Name = 'disabled'; Exists = $true; Enabled = $false },
    @{ Name = 'enabled'; Exists = $true; Enabled = $true }
)
$passed = 0
$failures = @()
foreach ($state in $states) {
    foreach ($countdown in @($false, $true)) {
        foreach ($queued in @($false, $true)) {
            $caseName = "$($state.Name), countdown=$countdown, queued=$queued"
            $snapshot = [pscustomobject]$state
            $script:hasCountdown = $countdown
            $script:hasQueuedShutdown = $queued
            $script:calls = [System.Collections.Generic.List[string]]::new()
            $script:outcome = $null
            try {
                try { & $disable }
                catch [System.OperationCanceledException] {
                    if ($_.Exception.Message -ne 'TEST_COMPLETED') { throw }
                }
                $expectedCalls = if ($state.Enabled) { 'stop,abort,disable,complete' }
                                 else { 'stop,abort,complete' }
                Assert-Equal ($script:calls -join ',') $expectedCalls 'Cleanup order'
                $expectedResult = if ($state.Enabled -or $countdown -or $queued) { 'success' }
                                  else { 'already' }
                Assert-Equal $script:outcome.Result $expectedResult 'Reported result'
                if ($countdown -and -not $script:outcome.Summary.Contains('取消')) {
                    throw 'Summary must report the cancelled countdown.'
                }
                if ($queued -and -not $script:outcome.Summary.Contains('排队')) {
                    throw 'Summary must report the aborted queued shutdown.'
                }
                $passed++
                Write-Output "PASS $caseName"
            } catch {
                $failures += "$caseName : $($_.Exception.Message)"
                Write-Output "FAIL $($failures[-1])"
            }
        }
    }
}
Write-Output "Passed: $passed / 12"
if ($failures.Count -gt 0) { throw "$($failures.Count) regression case(s) failed." }
