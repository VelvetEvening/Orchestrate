param(
    [int]$CountdownSeconds = 60
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# 状态库只用于上报；即使它缺失或报错，倒计时弹窗本身也必须照常工作。
$heartbeatPath = Join-Path $PSScriptRoot 'state\countdown.json'
$libraryPath = Join-Path $PSScriptRoot 'shutdown-lib.ps1'
$libraryLoaded = $false
if (Test-Path -LiteralPath $libraryPath) {
    try {
        . $libraryPath
        $libraryLoaded = $true
    } catch {
        $libraryLoaded = $false
    }
}

function Write-Heartbeat {
    param(
        [Parameter(Mandatory)][int]$Remaining,
        [Parameter(Mandatory)][datetime]$Deadline
    )
    if (-not $libraryLoaded) { return }
    try {
        $payload = [ordered]@{
            pid               = $PID
            countdown_seconds = $script:totalSeconds
            remaining_seconds = [Math]::Max(0, $Remaining)
            started_at        = $script:startedAt.ToString('yyyy-MM-ddTHH:mm:sszzz')
            deadline          = $Deadline.ToString('yyyy-MM-ddTHH:mm:sszzz')
            updated_at        = (Get-Date).ToString('yyyy-MM-ddTHH:mm:sszzz')
        }
        Write-JsonFileAtomic -Path $heartbeatPath -Value $payload
    } catch { }
}

function Clear-Heartbeat {
    if (Test-Path -LiteralPath $heartbeatPath) {
        Remove-Item -LiteralPath $heartbeatPath -Force -ErrorAction SilentlyContinue
    }
}

# 发出真正的关机命令。shutdown.exe 失败时会往 stderr 写内容并返回非 0，
# 不能让它以异常形式冒出来，否则脚本会在写状态之前就退出。
function Invoke-ReminderShutdown {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $null = & shutdown.exe /s /f /t 30 /c '定时关机将在 30 秒后执行。如需取消，请在 Orchestrate 里运行「取消本次关机」，或双击工具目录里的 cancel-shutdown.bat' 2>&1
        return ($LASTEXITCODE -eq 0)
    } finally {
        $ErrorActionPreference = $previous
    }
}

# 弹窗收尾都要覆盖写一次状态，Orchestrate 刷新时看到的就是最近一次的真实结果。
function Publish-ReminderOutcome {
    param(
        [Parameter(Mandatory)][string]$Result,
        [Parameter(Mandatory)][string]$Summary,
        [Parameter(Mandatory)][string]$LastCommand
    )
    if (-not $libraryLoaded) { return }
    try {
        Publish-ShutdownState -LastCommand $LastCommand -Pending $null -PendingProvided $true `
            -Result $Result -Summary $Summary | Out-Null
    } catch { }
}

$script:totalSeconds = [Math]::Max(1, $CountdownSeconds)
$script:startedAt    = Get-Date
$script:remaining    = $script:totalSeconds
$script:deadline     = $script:startedAt.AddSeconds($script:totalSeconds)
$script:timeUp       = $false
$script:cancelled    = $false

function Format-Remaining {
    param([int]$Sec)
    if ($Sec -ge 60) {
        return '{0} 分 {1} 秒' -f [Math]::Floor($Sec / 60), ($Sec % 60)
    }
    return "$Sec 秒"
}

$form = New-Object System.Windows.Forms.Form
$form.Text            = '定时关机'
$form.ClientSize      = New-Object System.Drawing.Size(400, 175)
$form.StartPosition   = 'CenterScreen'
$form.TopMost         = $true
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox     = $false
$form.MinimizeBox     = $false
$form.ShowInTaskbar   = $true

$labelMain = New-Object System.Windows.Forms.Label
$labelMain.Bounds    = New-Object System.Drawing.Rectangle(10, 20, 380, 42)
$labelMain.TextAlign = 'MiddleCenter'
$labelMain.Font      = New-Object System.Drawing.Font('Microsoft YaHei UI', 14, [System.Drawing.FontStyle]::Bold)

$labelSub = New-Object System.Windows.Forms.Label
$labelSub.Bounds    = New-Object System.Drawing.Rectangle(10, 68, 380, 26)
$labelSub.TextAlign = 'MiddleCenter'
$labelSub.ForeColor = [System.Drawing.Color]::DimGray
$labelSub.Font      = New-Object System.Drawing.Font('Microsoft YaHei UI', 9.75)
$labelSub.Text      = '正在使用电脑？点击下方按钮取消；不操作将自动关机'

$btnCancel = New-Object System.Windows.Forms.Button
$btnCancel.Text   = '取消本次关机'
$btnCancel.Bounds = New-Object System.Drawing.Rectangle(110, 110, 180, 42)
$btnCancel.Font   = New-Object System.Drawing.Font('Microsoft YaHei UI', 11)
$btnCancel.Add_Click({
    $script:cancelled = $true
    $form.Close()
})

$form.Controls.AddRange(@($labelMain, $labelSub, $btnCancel))
# 回车/Esc 都绑定到取消：倒计时窗口里误敲键盘也不能误触发关机
$form.AcceptButton = $btnCancel
$form.CancelButton = $btnCancel

$updateUi = {
    $labelMain.Text = "电脑将在 $(Format-Remaining $script:remaining)后关机"
}
& $updateUi
Write-Heartbeat -Remaining $script:remaining -Deadline $script:deadline

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 1000
$timer.Add_Tick({
    # 按秒递减，机器休眠时计时器不跳，唤醒后剩下的秒数仍然是可见、可取消的。
    $script:remaining--
    if ($script:remaining -le 0) {
        $script:timeUp = $true
        $timer.Stop()
        $form.Close()
    } else {
        & $updateUi
        Write-Heartbeat -Remaining $script:remaining -Deadline (Get-Date).AddSeconds($script:remaining)
    }
})

# 关机只在倒计时自然走完时执行；点 X / Esc / 结束进程等任何手动关闭都不会触发关机
$timer.Start()
[void]$form.ShowDialog()
$timer.Dispose()

Clear-Heartbeat

if ($script:timeUp) {
    # 留 30 秒系统级缓冲，期间运行 shutdown /a 仍可反悔
    if (Invoke-ReminderShutdown) {
        Publish-ReminderOutcome -Result 'success' -LastCommand 'scheduled-run' `
            -Summary '倒计时已走完，已发出关机命令：30 秒系统缓冲后关机'
    } else {
        Publish-ReminderOutcome -Result 'failure' -LastCommand 'scheduled-run' `
            -Summary '倒计时已走完，但关机命令执行失败；请检查当前账户是否有权关机'
    }
} else {
    $reason = if ($script:cancelled) { '在弹窗里点了取消' } else { '倒计时弹窗被手动关闭' }
    Publish-ReminderOutcome -Result 'skipped' -LastCommand 'countdown-cancelled' `
        -Summary ('本次关机已取消：{0}（原定倒计时 {1} 秒）' -f $reason, $script:totalSeconds)
}
