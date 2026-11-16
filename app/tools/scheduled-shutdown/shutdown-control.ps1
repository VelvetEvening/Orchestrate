# 定时关机的唯一控制入口，Orchestrate 里的每条命令都对应这里的一个 -Action。
# 每条操作结束后都会覆盖写 state/current.json，Orchestrate 在命令运行结束后会重新读取它。
param(
    [ValidateSet('status', 'enable', 'disable', 'set-time', 'set-countdown', 'cancel', 'shutdown-now', 'uninstall')]
    [string]$Action = 'status',
    [string]$Time = '',
    [switch]$WeekdaysOnly,
    [switch]$Daily,
    [int]$CountdownSeconds = 0
)

$ErrorActionPreference = 'Stop'
# QProcess reads a UTF-8 byte stream; Windows PowerShell 5.1 otherwise uses the
# active console code page and can corrupt Chinese when redirected to the app.
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$OutputEncoding = [Console]::OutputEncoding
. (Join-Path $PSScriptRoot 'shutdown-lib.ps1')

function Complete-ShutdownAction {
    # Result 为空时按计划任务的实际情况推断（查看状态用）。
    param(
        [string]$Result = '',
        [string]$Summary = ''
    )
    $state = Publish-ShutdownState -LastCommand $Action -Result $Result -Summary $Summary
    Write-ShutdownSnapshotLines -State $state
    exit $(if ($Result -eq 'failure') { 1 } else { 0 })
}

# 按现有设置重新登记计划任务，只替换传入的那一项；没安装时按默认值安装。
function Update-ShutdownTask {
    param($Snapshot, [string]$NewTime = '', [int]$NewCountdown = 0, $NewWeekdaysOnly = $null)
    $targetTime = if ($NewTime) { $NewTime } else { Get-ShutdownTime $Snapshot }
    $targetCountdown = if ($NewCountdown -gt 0) { $NewCountdown } else { Get-ShutdownCountdownSeconds $Snapshot }
    $targetWeekdays = if ($NewWeekdaysOnly -ne $null) { [bool]$NewWeekdaysOnly } else { [bool]$Snapshot.WeekdaysOnly }
    return (Set-ShutdownTask -Time $targetTime -CountdownSeconds $targetCountdown -WeekdaysOnly $targetWeekdays)
}

# 立刻拉起一次真实倒计时弹窗，等它写出心跳，Orchestrate 紧接着读状态时就能看到剩余秒数。
function Start-ShutdownCountdown {
    param([Parameter(Mandatory)][int]$Seconds)
    Remove-ShutdownCountdownFile
    Start-Process -FilePath 'powershell.exe' -WindowStyle Hidden -ArgumentList (
        '-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "{0}" -CountdownSeconds {1}' -f (Get-ShutdownReminderPath), $Seconds
    ) | Out-Null
    for ($attempt = 0; $attempt -lt 40; $attempt++) {
        if (Test-Path -LiteralPath (Get-ShutdownCountdownPath)) { break }
        Start-Sleep -Milliseconds 100
    }
    Start-Sleep -Milliseconds 400
}

$snapshot = Get-ShutdownTaskSnapshot

try {
    switch ($Action) {

        'status' {
            Complete-ShutdownAction -Result ''
        }

        'enable' {
            # 没安装、或计划任务还指向别处（比如 Orchestrate 文件夹搬过家）时，先按现有设置重新登记。
            $installed = $snapshot.Exists
            $moved = $snapshot.Exists -and -not $snapshot.PointsHere
            if ($moved -and $snapshot.ScriptPath -and
                [System.IO.Path]::IsPathRooted($snapshot.ScriptPath) -and
                [System.IO.Path]::GetFileName($snapshot.ScriptPath) -ieq 'shutdown-reminder.ps1') {
                # Explicit takeover of this named task's old action when enabling after a move.
                $null = Stop-ShutdownCountdownWindow -ScriptPath $snapshot.ScriptPath
            }
            if (-not $installed -or $moved) {
                $snapshot = Update-ShutdownTask -Snapshot $snapshot
            }
            if ($snapshot.Enabled -and $installed -and -not $moved) {
                Complete-ShutdownAction -Result 'already' -Summary '定时关机本来就是启用状态'
            }
            Enable-ScheduledTask -TaskName (Get-ShutdownTaskName) | Out-Null
            $after = Get-ShutdownTaskSnapshot
            $prefix = if (-not $installed) { '定时关机已安装并启用' } elseif ($moved) { '已改用 Orchestrate 自带的脚本并启用' } else { '定时关机已启用' }
            Complete-ShutdownAction -Result 'success' `
                -Summary ('{0}：{1} 弹窗，倒计时 {2} 秒' -f $prefix, (Format-ShutdownSchedule $after), (Get-ShutdownCountdownSeconds $after))
        }

        'disable' {
            # 手动倒计时不依赖计划任务；即使任务未安装或已停用，也必须先取消本次关机。
            $killed = Stop-ShutdownCountdownWindow
            $aborted = Invoke-ShutdownAbort
            $result = 'already'
            if (-not $snapshot.Exists) {
                $summary = '定时关机尚未安装，无需停用计划任务'
            } elseif (-not $snapshot.Enabled) {
                $summary = '定时关机本来就是停用状态'
            } else {
                Disable-ScheduledTask -TaskName (Get-ShutdownTaskName) | Out-Null
                $result = 'success'
                $summary = '定时关机已关闭，到点不再弹窗、不再关机；时间和倒计时设置保留'
            }
            if ($killed) {
                $result = 'success'
                $summary += '；已取消正在进行的倒计时'
            }
            if ($aborted) {
                $result = 'success'
                $summary += '；已中止排队中的关机'
            }
            Complete-ShutdownAction -Result $result -Summary $summary
        }

        'set-time' {
            if (-not $Time) { throw '缺少 -Time HH:mm' }
            if ($WeekdaysOnly -and $Daily) { throw '不能同时指定 -WeekdaysOnly 和 -Daily' }
            $weekdays = if ($WeekdaysOnly) { $true } elseif ($Daily) { $false } else { $null }
            $installed = $snapshot.Exists
            $after = Update-ShutdownTask -Snapshot $snapshot -NewTime $Time -NewWeekdaysOnly $weekdays
            $summary = '弹窗时间已改为 {0}' -f (Format-ShutdownSchedule $after)
            if (-not $installed) { $summary += '（定时关机已按此时间安装并启用）' }
            elseif (-not $after.Enabled) { $summary += '（定时关机仍处于关闭状态）' }
            Complete-ShutdownAction -Result 'success' -Summary $summary
        }

        'set-countdown' {
            if ($CountdownSeconds -le 0) { throw '缺少 -CountdownSeconds' }
            $installed = $snapshot.Exists
            $after = Update-ShutdownTask -Snapshot $snapshot -NewCountdown $CountdownSeconds
            $summary = '弹窗倒计时已改为 {0} 秒' -f (Get-ShutdownCountdownSeconds $after)
            if (-not $installed) { $summary += '（定时关机已按默认时间安装并启用）' }
            elseif (-not $after.Enabled) { $summary += '（定时关机仍处于关闭状态）' }
            Complete-ShutdownAction -Result 'success' -Summary $summary
        }

        'cancel' {
            $killed = Stop-ShutdownCountdownWindow
            $aborted = Invoke-ShutdownAbort
            if (-not ($killed -or $aborted)) {
                Complete-ShutdownAction -Result 'already' -Summary '当前没有进行中的关机，无需取消'
            }
            $summary = '已取消本次关机'
            if ($killed) { $summary += '（关闭了倒计时弹窗）' }
            if ($aborted) { $summary += '（中断了已排队的关机）' }
            Complete-ShutdownAction -Result 'success' -Summary $summary
        }

        'shutdown-now' {
            $seconds = Get-ShutdownCountdownSeconds $snapshot
            Start-ShutdownCountdown -Seconds $seconds
            Complete-ShutdownAction -Result 'running' `
                -Summary ('已弹出关机倒计时：{0} 秒后关机，期间可在弹窗或 Orchestrate 里取消' -f $seconds)
        }

        'uninstall' {
            [void](Stop-ShutdownCountdownWindow)
            Invoke-ShutdownAbort | Out-Null
            if (-not $snapshot.Exists) {
                Complete-ShutdownAction -Result 'already' -Summary '定时关机尚未安装，无需卸载'
            }
            Unregister-ScheduledTask -TaskName (Get-ShutdownTaskName) -Confirm:$false
            Complete-ShutdownAction -Result 'skipped' `
                -Summary '已删除计划任务，定时关机不再运行；运行「开启定时关机」可重新安装'
        }
    }
} catch {
    Complete-ShutdownAction -Result 'failure' -Summary ('操作失败：' + $_.Exception.Message)
}
