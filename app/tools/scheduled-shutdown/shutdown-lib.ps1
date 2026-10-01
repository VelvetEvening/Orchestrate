# 共享库：被各脚本点源引用，本身不产生副作用。
# 负责三件事：读取计划任务真实配置、读写 Orchestrate 要求的当前状态 JSON、读写倒计时心跳文件。

$script:ShutdownToolRoot         = $PSScriptRoot
$script:ShutdownTaskName         = '定时关机'
$script:ShutdownToolId           = 'scheduled-shutdown'
$script:ShutdownStateSchema      = 'orchestrate-state/v1'
$script:ShutdownDefaultTime      = '00:10'
$script:ShutdownDefaultCountdown = 60

function Get-ShutdownStateDirectory { return (Join-Path $script:ShutdownToolRoot 'state') }

function Get-ShutdownStatePath { return (Join-Path (Get-ShutdownStateDirectory) 'current.json') }

function Get-ShutdownCountdownPath { return (Join-Path (Get-ShutdownStateDirectory) 'countdown.json') }

function Get-ShutdownReminderPath { return (Join-Path $script:ShutdownToolRoot 'shutdown-reminder.ps1') }

function Get-ShutdownTaskName { return $script:ShutdownTaskName }

# 先写临时文件再替换正式文件，避免 Orchestrate 读到半截 JSON。
function Write-JsonFileAtomic {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)]$Value
    )
    $directory = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $directory)) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }
    $json = ConvertTo-Json -InputObject $Value -Depth 8
    $temporary = Join-Path $directory ('.{0}.{1}.tmp' -f [System.IO.Path]::GetFileName($Path), [System.Guid]::NewGuid().ToString('N'))
    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($temporary, $json, $encoding)
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}

function Read-JsonFile {
    param([Parameter(Mandatory)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    try {
        $raw = Get-Content -LiteralPath $Path -Raw -Encoding UTF8
        if ([string]::IsNullOrWhiteSpace($raw)) { return $null }
        return ($raw | ConvertFrom-Json)
    } catch {
        return $null
    }
}

function Get-ShutdownProcessScriptPath {
    param([string]$CommandLine)
    # Only a real -File argument identifies a managed script.
    $parts = @([regex]::Matches($CommandLine, '"[^"]*"|\S+') | ForEach-Object { $_.Value.Trim('"') })
    for ($index = 1; $index -lt $parts.Count; $index++) {
        if ($parts[$index] -ieq '-File') {
            if ($index + 1 -ge $parts.Count) { return '' }
            $path = $parts[$index + 1]
            if ($path -notmatch '^(?:[A-Za-z]:[\\/]|\\\\)') { return '' }
            try { return [System.IO.Path]::GetFullPath($path) } catch { return '' }
        }
        # Accept only switches used by managed launches; consume option values so
        # an arbitrary -Command abbreviation cannot smuggle a later -File token.
        if ($parts[$index] -iin @('-NoProfile', '-NoLogo', '-NonInteractive', '-Sta', '-Mta')) { continue }
        if ($parts[$index] -iin @('-ExecutionPolicy', '-WindowStyle', '-InputFormat', '-OutputFormat')) {
            $index++
            continue
        }
        return ''
    }
    return ''
}

function Get-ShutdownReminderProcess {
    param([string]$ScriptPath = (Get-ShutdownReminderPath))
    if (-not [System.IO.Path]::IsPathRooted($ScriptPath)) { return @() }
    $expected = [System.IO.Path]::GetFullPath($ScriptPath)
    $found = @()
    foreach ($processName in @('powershell.exe', 'pwsh.exe')) {
        $processes = @()
        try {
            $processes = @(Get-CimInstance Win32_Process -Filter "Name='$processName'" -ErrorAction Stop)
        } catch {
            $processes = @()
        }
        foreach ($process in $processes) {
            if ($process.CommandLine -and (Get-ShutdownProcessScriptPath -CommandLine $process.CommandLine) -ieq $expected) {
                $found += $process
            }
        }
    }
    return $found
}

# 读取计划任务里真实保存的配置，脚本报出来的时间/倒计时都以它为准，不另存一份配置。
function Get-ShutdownTaskSnapshot {
    $snapshot = [pscustomobject]@{
        Exists           = $false
        Enabled          = $false
        State            = 'not_installed'
        Time             = ''
        WeekdaysOnly     = $false
        CountdownSeconds = 0
        ScriptPath       = ''
        PointsHere       = $false
        NextRunTime      = $null
        LastRunTime      = $null
        LastTaskResult   = $null
    }

    $task = $null
    try {
        $task = Get-ScheduledTask -TaskName $script:ShutdownTaskName -ErrorAction Stop
    } catch {
        return $snapshot
    }

    $snapshot.Exists = $true
    $snapshot.State = [string]$task.State
    $snapshot.Enabled = [string]$task.State -ne 'Disabled'

    if ($task.Actions -and $task.Actions.Count -gt 0) {
        $arguments = [string]$task.Actions[0].Arguments
        $match = [regex]::Match($arguments, '-CountdownSeconds\s+(\d+)')
        if ($match.Success) { $snapshot.CountdownSeconds = [int]$match.Groups[1].Value }
        $fileMatch = [regex]::Match($arguments, '-File\s+"([^"]+)"')
        if ($fileMatch.Success) { $snapshot.ScriptPath = $fileMatch.Groups[1].Value }
    }
    # Orchestrate 整个文件夹可以搬走；计划任务仍指向旧位置时需要重新登记。
    if ($snapshot.ScriptPath) {
        try {
            $snapshot.PointsHere = [System.IO.Path]::GetFullPath($snapshot.ScriptPath) -ieq [System.IO.Path]::GetFullPath((Get-ShutdownReminderPath))
        } catch { }
    }

    if ($task.Triggers -and $task.Triggers.Count -gt 0) {
        $trigger = $task.Triggers[0]
        $snapshot.WeekdaysOnly = [string]$trigger.CimClass.CimClassName -like '*Weekly*'
        $boundary = [string]$trigger.StartBoundary
        if ($boundary) {
            try { $snapshot.Time = ([datetime]::Parse($boundary)).ToString('HH:mm') } catch { }
        }
    }

    try {
        $info = Get-ScheduledTaskInfo -TaskName $script:ShutdownTaskName -ErrorAction Stop
        $snapshot.NextRunTime = $info.NextRunTime
        $snapshot.LastRunTime = $info.LastRunTime
        $snapshot.LastTaskResult = $info.LastTaskResult
    } catch { }

    return $snapshot
}

# 倒计时心跳：只有进程还活着才算有效，避免机器异常退出后残留一份假状态。
function Get-ShutdownPendingCountdown {
    $data = Read-JsonFile -Path (Get-ShutdownCountdownPath)
    if ($data -eq $null) { return $null }

    $processId = 0
    try { $processId = [int]$data.pid } catch { $processId = 0 }
    if ($processId -le 0) { return $null }

    $alive = $false
    foreach ($process in (Get-ShutdownReminderProcess)) {
        if ([int]$process.ProcessId -eq $processId) { $alive = $true; break }
    }
    if (-not $alive) { return $null }

    $remaining = 0
    try { $remaining = [int]$data.remaining_seconds } catch { $remaining = 0 }
    if ($data.deadline) {
        try {
            $deadline = [datetimeoffset]::Parse([string]$data.deadline)
            $remaining = [int][Math]::Max(0, [Math]::Ceiling(($deadline - [datetimeoffset]::Now).TotalSeconds))
        } catch { }
    }

    return [pscustomobject]@{
        Pid              = $processId
        RemainingSeconds = $remaining
    }
}

function Remove-ShutdownCountdownFile {
    $path = Get-ShutdownCountdownPath
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
    }
}

# 关掉还开着的倒计时弹窗；进程被强杀时不会走到关机分支。
function Stop-ShutdownCountdownWindow {
    param([string]$ScriptPath = (Get-ShutdownReminderPath))
    $killed = $false
    foreach ($process in (Get-ShutdownReminderProcess -ScriptPath $ScriptPath)) {
        try {
            # Recheck identity immediately before stopping: a stale PID alone is insufficient.
            $current = @(Get-ShutdownReminderProcess -ScriptPath $ScriptPath | Where-Object {
                $_.ProcessId -eq $process.ProcessId -and $_.CreationDate -eq $process.CreationDate
            })
            if ($current.Count -ne 1) { continue }
            Stop-Process -Id $process.ProcessId -Force -ErrorAction Stop
            $killed = $true
        } catch { }
    }
    if ([System.IO.Path]::GetFullPath($ScriptPath) -ieq [System.IO.Path]::GetFullPath((Get-ShutdownReminderPath))) {
        Remove-ShutdownCountdownFile
    }
    return $killed
}

# 中止已排队的关机。没有关机在排队时 shutdown.exe 会往 stderr 写提示并返回非 0，
# 那是正常情况，不能当错误抛出（$ErrorActionPreference='Stop' 下会中断整个脚本）。
function Invoke-ShutdownAbort {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $null = & shutdown.exe /a 2>&1
        return ($LASTEXITCODE -eq 0)
    } finally {
        $ErrorActionPreference = $previous
    }
}

function Format-ShutdownDateTime {
    param($Value)
    if ($Value -eq $null) { return '' }
    try {
        $moment = [datetime]$Value
        if ($moment.Year -le 1999) { return '' }
        return $moment.ToString('yyyy-MM-dd HH:mm')
    } catch {
        return ''
    }
}

function Get-ShutdownTime {
    param($Snapshot)
    if ($Snapshot -and $Snapshot.Time) { return $Snapshot.Time }
    return $script:ShutdownDefaultTime
}

function Format-ShutdownSchedule {
    param($Snapshot)
    if (-not $Snapshot.Exists) { return '未安装' }
    if ($Snapshot.WeekdaysOnly) { return ('工作日 ' + (Get-ShutdownTime $Snapshot)) }
    return ('每天 ' + (Get-ShutdownTime $Snapshot))
}

function Get-ShutdownCountdownSeconds {
    param($Snapshot)
    if ($Snapshot -and $Snapshot.CountdownSeconds -gt 0) { return $Snapshot.CountdownSeconds }
    return $script:ShutdownDefaultCountdown
}

function New-ShutdownStateItem {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$Result,
        [string]$Summary = ''
    )
    return ([ordered]@{ name = $Name; result = $Result; summary = $Summary })
}

function Get-ShutdownLastRunText {
    param($Snapshot)
    if ($Snapshot.LastTaskResult -eq $null) { return '未获取到执行记录' }
    $code = [int]$Snapshot.LastTaskResult
    if ($code -eq 0) { return '上次弹窗已正常走完' }
    if ($code -eq 267009) { return '当前有一次弹窗正在运行' }
    if ($code -eq 267011) { return '尚未执行过' }
    return "上次退出码 $code"
}

function Get-ShutdownSnapshotItems {
    param($Snapshot, $Pending)

    $items = New-Object System.Collections.ArrayList

    if (-not $Snapshot.Exists) {
        [void]$items.Add((New-ShutdownStateItem -Name '计划任务' -Result 'skipped' -Summary '尚未安装；运行「开启定时关机」即可按默认设置安装'))
    } elseif (-not $Snapshot.Enabled) {
        [void]$items.Add((New-ShutdownStateItem -Name '计划任务' -Result 'disabled' -Summary '已停用：计划任务保留，但到点不再弹窗、不再关机'))
    } else {
        [void]$items.Add((New-ShutdownStateItem -Name '计划任务' -Result 'success' -Summary ('已启用：' + (Format-ShutdownSchedule $Snapshot))))
    }

    if ($Snapshot.Exists -and -not $Snapshot.PointsHere) {
        [void]$items.Add((New-ShutdownStateItem -Name '脚本位置' -Result 'failure' `
            -Summary ('计划任务运行的是另一处的脚本：{0}；运行「开启定时关机」改用 Orchestrate 自带的脚本' -f $Snapshot.ScriptPath)))
    }

    [void]$items.Add((New-ShutdownStateItem -Name '弹窗倒计时' -Result 'success' -Summary ('{0} 秒' -f (Get-ShutdownCountdownSeconds $Snapshot))))

    if ($Snapshot.Exists -and $Snapshot.Enabled) {
        $next = Format-ShutdownDateTime $Snapshot.NextRunTime
        if ($next) {
            [void]$items.Add((New-ShutdownStateItem -Name '下次触发' -Result 'success' -Summary $next))
        } else {
            [void]$items.Add((New-ShutdownStateItem -Name '下次触发' -Result 'skipped' -Summary '暂时没有排定的触发时间'))
        }
    }

    if ($Pending -ne $null) {
        [void]$items.Add((New-ShutdownStateItem -Name '倒计时弹窗' -Result 'running' `
            -Summary ('剩余 {0} 秒，走完后将执行 shutdown /s /f /t 30' -f $Pending.RemainingSeconds)))
    }

    if ($Snapshot.Exists) {
        [void]$items.Add((New-ShutdownStateItem -Name '最近一次执行' -Result 'success' -Summary (Get-ShutdownLastRunText $Snapshot)))
    }

    return $items.ToArray()
}

function Get-ShutdownResult {
    param($Snapshot, $Pending)
    if ($Pending -ne $null) { return 'running' }
    if (-not $Snapshot.Exists) { return 'skipped' }
    if (-not $Snapshot.Enabled) { return 'disabled' }
    if (-not $Snapshot.PointsHere) { return 'partial_success' }
    return 'success'
}

function Get-ShutdownSummary {
    param($Snapshot, $Pending)

    if (-not $Snapshot.Exists) {
        return '定时关机尚未安装，运行「开启定时关机」即可启用'
    }

    $countdown = Get-ShutdownCountdownSeconds $Snapshot

    if (-not $Snapshot.Enabled) {
        $summary = '定时关机已停用，到点不会弹窗、不会关机'
        if ($Pending -ne $null) { $summary += '（但还有一个倒计时弹窗在运行）' }
        return $summary
    }

    if ($Pending -ne $null) {
        return '关机倒计时进行中：剩余 {0} 秒（弹窗时长 {1} 秒）' -f $Pending.RemainingSeconds, $countdown
    }

    $summary = '{0} 弹窗，倒计时 {1} 秒' -f (Format-ShutdownSchedule $Snapshot), $countdown
    $next = Format-ShutdownDateTime $Snapshot.NextRunTime
    if ($next) { $summary += "；下次触发 $next" }
    return $summary
}

# 命令脚本统一走这里：读计划任务 + 倒计时心跳，写一份最新状态并返回便于打印。
function Publish-ShutdownState {
    param(
        [string]$LastCommand = '',
        $Pending,
        [bool]$PendingProvided = $false,
        [string]$Result = '',
        [string]$Summary = ''
    )
    $snapshot = Get-ShutdownTaskSnapshot
    if (-not $PendingProvided) { $Pending = Get-ShutdownPendingCountdown }
    if (-not $Result) { $Result = Get-ShutdownResult -Snapshot $snapshot -Pending $Pending }
    if (-not $Summary) { $Summary = Get-ShutdownSummary -Snapshot $snapshot -Pending $Pending }
    $state = [ordered]@{
        schema       = $script:ShutdownStateSchema
        tool_id      = $script:ShutdownToolId
        current_date = (Get-Date).ToString('yyyy-MM-dd')
        updated_at   = (Get-Date).ToString('yyyy-MM-ddTHH:mm:sszzz')
        result       = $Result
        summary      = $Summary
        last_command = $LastCommand
        items        = @(Get-ShutdownSnapshotItems -Snapshot $snapshot -Pending $Pending)
        # 机器可读的当前设置：Orchestrate 的参数表单用它预填“弹窗时间 / 倒计时”等默认值。
        settings     = [ordered]@{
            installed         = [bool]$snapshot.Exists
            enabled           = [bool]$snapshot.Enabled
            time              = Get-ShutdownTime $snapshot
            countdown_seconds = Get-ShutdownCountdownSeconds $snapshot
            weekdays_only     = [bool]$snapshot.WeekdaysOnly
        }
    }
    Write-JsonFileAtomic -Path (Get-ShutdownStatePath) -Value $state
    return [pscustomobject]@{
        Snapshot = $snapshot
        Pending  = $Pending
        Result   = $Result
        Summary  = $Summary
    }
}

# 重新登记计划任务。所有“安装 / 改设置 / 改用本目录脚本”的操作共用这一个入口。
function Set-ShutdownTask {
    param(
        [Parameter(Mandatory)][string]$Time,
        [Parameter(Mandatory)][int]$CountdownSeconds,
        [Parameter(Mandatory)][bool]$WeekdaysOnly
    )

    $parsed = [datetime]::MinValue
    if (-not [datetime]::TryParseExact(
            $Time, [string[]]@('HH:mm', 'H:mm', 'HH:mm:ss'),
            [System.Globalization.CultureInfo]::InvariantCulture,
            [System.Globalization.DateTimeStyles]::None, [ref]$parsed)) {
        throw "时间格式不正确：'$Time'。应为 HH:mm，例如 00:10"
    }

    if ($CountdownSeconds -lt 5 -or $CountdownSeconds -gt 3600) {
        throw "倒计时秒数必须在 5 到 3600 之间，当前为 $CountdownSeconds。"
    }

    $reminder = Get-ShutdownReminderPath
    if (-not (Test-Path -LiteralPath $reminder)) { throw "找不到关机脚本：$reminder" }

    $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument (
        '-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "{0}" -CountdownSeconds {1}' -f $reminder, $CountdownSeconds
    )

    if ($WeekdaysOnly) {
        $trigger = New-ScheduledTaskTrigger -Weekly -DaysOfWeek Monday,Tuesday,Wednesday,Thursday,Friday -At $parsed
    } else {
        $trigger = New-ScheduledTaskTrigger -Daily -At $parsed
    }

    $existing = $null
    try { $existing = Get-ScheduledTask -TaskName $script:ShutdownTaskName -ErrorAction Stop }
    catch {
        # Only a genuinely missing task may be installed with the default enabled
        # state. A permission/service error must not erase an unknown preference.
        if ($_.CategoryInfo.Category -ne [System.Management.Automation.ErrorCategory]::ObjectNotFound) {
            throw "无法读取定时关机原状态，未重新登记任务：$($_.Exception.Message)"
        }
    }
    if ($null -ne $existing -and [string]$existing.State -notin @('Disabled', 'Ready', 'Running', 'Queued')) {
        throw '无法确认定时关机原来的启用状态，未重新登记任务。'
    }
    $wasDisabled = ($existing -ne $null -and [string]$existing.State -eq 'Disabled')
    # Preserve disabled state in every registration attempt itself, avoiding an
    # enabled interval while editing a task the user has explicitly turned off.
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit (New-TimeSpan -Hours 1) -Disable:$wasDisabled -ErrorAction Stop

    $registered = $false
    if ($existing -ne $null) {
        try {
            Register-ScheduledTask -TaskName $script:ShutdownTaskName -Action $action -Trigger $trigger `
                -Principal $existing.Principal -Settings $settings -Force -ErrorAction Stop | Out-Null
            $registered = $true
        } catch { }
    }
    if (-not $registered) {
        try {
            $principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive
            Register-ScheduledTask -TaskName $script:ShutdownTaskName -Action $action -Trigger $trigger `
                -Principal $principal -Settings $settings -Force -ErrorAction Stop | Out-Null
            $registered = $true
        } catch { }
    }
    if (-not $registered) {
        try {
            Register-ScheduledTask -TaskName $script:ShutdownTaskName -Action $action -Trigger $trigger `
                -Settings $settings -Force -ErrorAction Stop | Out-Null
        } catch {
            throw "登记计划任务失败：$($_.Exception.Message)"
        }
    }

    if ($wasDisabled) {
        try {
            $verified = Get-ScheduledTask -TaskName $script:ShutdownTaskName -ErrorAction Stop
            if ($null -eq $verified -or [string]$verified.State -ne 'Disabled') {
                # Defensive recovery if Windows or another writer did not retain
                # the requested disabled setting. Never hide a recovery failure.
                Disable-ScheduledTask -TaskName $script:ShutdownTaskName -ErrorAction Stop | Out-Null
                $verified = Get-ScheduledTask -TaskName $script:ShutdownTaskName -ErrorAction Stop
            }
            if ($null -eq $verified -or [string]$verified.State -ne 'Disabled') {
                throw '重新读取后任务仍不是停用状态。'
            }
        } catch {
            throw "设置已登记，但无法确认定时关机仍处于停用状态；任务可能已启用，请立即在任务计划程序中核对并停用。$($_.Exception.Message)"
        }
    }

    $snapshot = Get-ShutdownTaskSnapshot
    if (-not $snapshot.Exists) { throw '设置已登记，但无法读取任务状态，不能确认修改成功；请在任务计划程序中核对。' }
    if ($wasDisabled -and $snapshot.Enabled) {
        throw '设置已登记，但最后复核发现定时关机已启用；请立即在任务计划程序中核对并停用。'
    }
    return $snapshot
}

function Write-ShutdownSnapshotLines {
    param($State)
    $snapshot = $State.Snapshot
    Write-Host ('状态：' + $State.Summary)
    if ($snapshot.Exists) {
        Write-Host ('  触发时间：{0}（{1}）' -f (Format-ShutdownSchedule $snapshot), $(if ($snapshot.Enabled) { '已启用' } else { '已停用' }))
        Write-Host ('  倒计时：{0} 秒' -f (Get-ShutdownCountdownSeconds $snapshot))
        $next = Format-ShutdownDateTime $snapshot.NextRunTime
        Write-Host ('  下次触发：{0}' -f $(if ($next) { $next } else { '未排定' }))
        Write-Host ('  脚本位置：{0}' -f $snapshot.ScriptPath)
    }
    if ($State.Pending -ne $null) {
        Write-Host ('  倒计时弹窗：剩余 {0} 秒' -f $State.Pending.RemainingSeconds)
    }
}
