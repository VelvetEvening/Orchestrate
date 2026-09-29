Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# The shared library only defines functions. Every system/process operation is mocked.
. (Join-Path $PSScriptRoot '../tools/scheduled-shutdown/shutdown-lib.ps1')
$script:ShutdownToolRoot = 'C:\Fixtures\current tool'
$script:stopped = [System.Collections.Generic.List[int]]::new()
$script:removed = 0
$script:processes = @(
    [pscustomobject]@{ ProcessId=101; CreationDate='first'; Name='powershell.exe'; CommandLine='powershell.exe -NoProfile -File "C:\Fixtures\current tool\shutdown-reminder.ps1" -CountdownSeconds 10' },
    [pscustomobject]@{ ProcessId=102; CreationDate='second'; Name='pwsh.exe'; CommandLine='pwsh.exe -File "C:\Fixtures\other tool\shutdown-reminder.ps1"' },
    [pscustomobject]@{ ProcessId=103; CreationDate='third'; Name='powershell.exe'; CommandLine='powershell.exe -File "C:\Fixtures\current tool\shutdown-reminder.ps1.backup"' },
    [pscustomobject]@{ ProcessId=104; CreationDate='fourth'; Name='powershell.exe'; CommandLine='powershell.exe -Command "Write-Output -File C:\Fixtures\current tool\shutdown-reminder.ps1"' },
    [pscustomobject]@{ ProcessId=105; CreationDate='fifth'; Name='powershell.exe'; CommandLine='powershell.exe -File shutdown-reminder.ps1' },
    [pscustomobject]@{ ProcessId=106; CreationDate='sixth'; Name='pwsh.exe'; CommandLine='pwsh.exe -File "c:\fixtures\CURRENT TOOL\shutdown-reminder.ps1"' }
)
function Get-CimInstance {
    param($ClassName, $Filter, $ErrorAction)
    if ($ClassName -ne 'Win32_Process') { throw 'Unexpected CIM class' }
    return @($script:processes | Where-Object { $Filter -eq "Name='$($_.Name)'" })
}
function Stop-Process {
    param([int]$Id, [switch]$Force, $ErrorAction)
    $script:stopped.Add($Id)
}
function Remove-ShutdownCountdownFile { $script:removed++ }
function Assert-Equal($actual, $expected) {
    if ($actual -cne $expected) { throw "Expected [$expected], got [$actual]" }
}
Assert-Equal ((Get-ShutdownReminderProcess).ProcessId -join ',') '101,106'
Assert-Equal (Stop-ShutdownCountdownWindow) $true
Assert-Equal ($script:stopped -join ',') '101,106'
Assert-Equal $script:removed 1
$script:stopped.Clear()
Assert-Equal (Stop-ShutdownCountdownWindow -ScriptPath 'C:\Fixtures\other tool\shutdown-reminder.ps1') $true
Assert-Equal ($script:stopped -join ',') '102'
Assert-Equal $script:removed 1
$script:stopped.Clear()
Assert-Equal (@(Get-ShutdownReminderProcess -ScriptPath 'relative\shutdown-reminder.ps1').Count) 0
Assert-Equal (Stop-ShutdownCountdownWindow -ScriptPath 'C:\missing\shutdown-reminder.ps1') $false
Assert-Equal $script:stopped.Count 0
Assert-Equal (Get-ShutdownProcessScriptPath 'pwsh.exe -comm Invoke-Something -File "C:\Fixtures\current tool\shutdown-reminder.ps1"') ''
Assert-Equal (Get-ShutdownProcessScriptPath 'pwsh.exe -File C:shutdown-reminder.ps1') ''
Assert-Equal (Get-ShutdownProcessScriptPath 'powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "C:\Fixtures\current tool\shutdown-reminder.ps1"') (Get-ShutdownReminderPath)
Write-Output 'PASS exact path, quoted paths, case, foreign folder, suffix, command text, relative path and explicit old-path takeover'

# Run the real control entry point against a tiny fake library. This exercises
# Windows PowerShell's redirected UTF-8 output without accessing tasks/shutdown.
$encodingRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('orchestrate-encoding-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $encodingRoot
$controlFixture = Join-Path $encodingRoot 'shutdown-control.ps1'
$libraryFixture = Join-Path $encodingRoot 'shutdown-lib.ps1'
try {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../tools/scheduled-shutdown/shutdown-control.ps1') -Destination $controlFixture
    $fakeLibrary = @'
function Get-ShutdownTaskSnapshot { return $null }
function Publish-ShutdownState { param($LastCommand, $Result, $Summary) return $null }
function Write-ShutdownSnapshotLines { param($State) Write-Host ([string][char]0x4e2d + [char]0x6587) }
'@
    [System.IO.File]::WriteAllText($libraryFixture, $fakeLibrary, [System.Text.Encoding]::UTF8)
    $probe = New-Object System.Diagnostics.Process
    $probe.StartInfo.FileName = Join-Path $PSHOME 'powershell.exe'
    $probe.StartInfo.Arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $controlFixture + '" -Action status'
    $probe.StartInfo.UseShellExecute = $false
    $probe.StartInfo.CreateNoWindow = $true
    $probe.StartInfo.RedirectStandardOutput = $true
    $probe.StartInfo.RedirectStandardError = $true
    $probe.StartInfo.StandardOutputEncoding = New-Object System.Text.UTF8Encoding($false, $true)
    $null = $probe.Start()
    $captured = $probe.StandardOutput.ReadToEnd()
    $diagnostics = $probe.StandardError.ReadToEnd()
    $probe.WaitForExit()
    Assert-Equal $probe.ExitCode 0
    Assert-Equal $captured.Trim() ([string][char]0x4e2d + [char]0x6587)
    Assert-Equal $diagnostics ''
    $probe.Dispose()
    Write-Output 'PASS real control startup emits UTF-8 Chinese against an isolated fake library'
} finally {
    # Exact files only; never recursively remove a computed directory.
    foreach ($file in @($controlFixture, $libraryFixture)) {
        if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file -Force }
    }
    [System.IO.Directory]::Delete($encodingRoot)
}
