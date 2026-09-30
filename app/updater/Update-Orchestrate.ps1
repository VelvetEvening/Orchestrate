param(
    [string]$PlanPath,
    [switch]$Prepare,
    [switch]$Apply,
    [switch]$CleanupCompleted,
    [string]$InstallDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$env:PSModulePath = (Join-Path $PSHOME 'Modules') + ';' + $env:PSModulePath

function Write-UpdateJson([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
}

function Assert-RelativePath([string]$Path) {
    if (-not $Path -or $Path.Contains('\') -or $Path -match '[:\x00-\x1f]' -or $Path.StartsWith('/')) {
        throw "Unsafe package path: $Path"
    }
    foreach ($part in $Path.Split('/')) {
        if (-not $part -or $part -in @('.', '..') -or $part -match '[. ]$|[<>"|?*]' -or
            $part -match '^(?i:CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\.|$)') {
            throw "Unsafe package path: $Path"
        }
    }
}

function Assert-OwnedPath([string]$Path) {
    Assert-RelativePath $Path
    if ($Path -match '^(?i:data)/|(^|/)(?i:state)/|(?i:\.sqlite3?)(?:$|-)|(?i:\.log)$' -or
        $Path -eq 'files.sha256') {
        throw "Package attempts to replace personal data: $Path"
    }
}

function Assert-NoLinks([string]$Root) {
    # Reject junctions before recursion so no copy/removal can traverse outside this installation.
    foreach ($item in Get-ChildItem -LiteralPath $Root -Force) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked path requires manual upgrade: $($item.FullName)" }
        if ($item.PSIsContainer) { Assert-NoLinks $item.FullName }
    }
}

function Read-PayloadManifest([string]$Root, [switch]$Verify) {
    $manifestPath = Join-Path $Root 'files.sha256'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'Missing files.sha256; use manual upgrade for this installation.' }
    $files = @{}
    foreach ($line in [IO.File]::ReadAllLines($manifestPath)) {
        if ($line -notmatch '^([0-9a-fA-F]{64})  (.+)$') { throw 'Invalid files.sha256 entry.' }
        $hash = $Matches[1].ToLowerInvariant()
        $relative = $Matches[2]
        Assert-OwnedPath $relative
        if ($files.ContainsKey($relative)) { throw "Duplicate manifest entry: $relative" }
        $files[$relative] = $hash
        if ($Verify) {
            $path = Join-Path $Root $relative
            if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
                (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hash) {
                throw "Payload checksum mismatch: $relative"
            }
        }
    }
    if (-not $files.ContainsKey('Orchestrate.exe') -or -not $files.ContainsKey('build-info.json')) {
        throw 'Package manifest is incomplete.'
    }
    if ($Verify) {
        foreach ($item in Get-ChildItem -LiteralPath $Root -Recurse -File -Force) {
            $relative = $item.FullName.Substring($Root.Length + 1).Replace('\', '/')
            if ($relative -ne 'files.sha256' -and -not $files.ContainsKey($relative)) { throw "Unlisted payload: $relative" }
        }
    }
    return $files
}

function Read-UpdatePlan([string]$Path, $SavedPlan = $null) {
    $plan = if ($SavedPlan) { $SavedPlan } else { Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json }
    $install = [IO.Path]::GetFullPath([string]$plan.install_directory).TrimEnd('\', '/')
    $workspace = [IO.Path]::GetFullPath([string]$plan.workspace).TrimEnd('\', '/')
    if ($install -eq [IO.Path]::GetPathRoot($install).TrimEnd('\', '/') -or
        -not (Test-Path -LiteralPath (Join-Path $install 'Orchestrate.exe') -PathType Leaf)) { throw 'Invalid installation directory.' }
    if ((Split-Path $workspace -Parent) -ne (Split-Path $install -Parent) -or
        (Split-Path $workspace -Leaf) -notmatch '^\.Orchestrate-update-[a-zA-Z0-9]+$' -or
        [IO.Path]::GetFullPath($Path) -ne (Join-Path $workspace 'plan.json')) { throw 'Invalid update workspace.' }
    if ([string]$plan.archive_sha256 -notmatch '^[0-9a-f]{64}$' -or
        [string]$plan.version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$' -or [long]$plan.process_id -le 0) { throw 'Invalid update metadata.' }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $digest = $sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($install.Replace('\', '/').ToLowerInvariant())) }
    finally { $sha.Dispose() }
    $mutexName = 'Local\Orchestrate-' + ([BitConverter]::ToString($digest).Replace('-', '').ToLowerInvariant())
    if ($plan.mutex_name -cne $mutexName) { throw 'Invalid installation mutex.' }
    if ((Get-Item -LiteralPath $workspace).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked workspace is not supported.' }
    if ((Get-Item -LiteralPath $install).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked installation is not supported.' }
    Assert-NoLinks $workspace
    $plan.install_directory = $install
    $plan.workspace = $workspace
    return $plan
}

function Expand-VerifiedPackage($Plan) {
    $archivePath = Join-Path $Plan.workspace 'release.zip'
    if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Plan.archive_sha256) {
        throw 'Archive checksum mismatch.'
    }
    $extractRoot = Join-Path $Plan.workspace 'payload'
    if (Test-Path -LiteralPath $extractRoot) { throw 'Payload directory already exists.' }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($archivePath)
    try {
        $paths = @{}
        $total = [long]0
        $packageName = "Orchestrate-v$($Plan.version)-windows-x64"
        foreach ($entry in $zip.Entries) {
            $name = $entry.FullName.Replace('\', '/').TrimEnd('/')
            Assert-RelativePath $name
            if ($name -ne $packageName -and -not $name.StartsWith($packageName + '/', [StringComparison]::Ordinal)) {
                throw 'Unexpected ZIP root directory.'
            }
            if ($paths.ContainsKey($name)) { throw "Duplicate ZIP path: $name" }
            $paths[$name] = $true
            $unixMode = ($entry.ExternalAttributes -shr 16) -band 0xf000
            if ($unixMode -eq 0xa000 -or ($entry.ExternalAttributes -band 0x400)) { throw 'ZIP links are not supported.' }
            $total += $entry.Length
            if ($total -gt 2GB -or $zip.Entries.Count -gt 10000) { throw 'ZIP exceeds extraction limits.' }
        }
    } finally { $zip.Dispose() }
    [IO.Compression.ZipFile]::ExtractToDirectory($archivePath, $extractRoot)
    $root = Join-Path $extractRoot $packageName
    Assert-NoLinks $root
    $null = Read-PayloadManifest $root -Verify
    $info = Get-Content -LiteralPath (Join-Path $root 'build-info.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($info.version -ne $Plan.version -or $info.release_tag -ne "v$($Plan.version)" -or $info.architecture -ne 'windows-x64') {
        throw 'Package build information differs from Release.'
    }
    return $root
}

function Get-InstallationTasks([string]$Install) {
    $nativeRoot = $Install.TrimEnd('\', '/').Replace('/', '\') + '\'
    foreach ($task in Get-ScheduledTask -ErrorAction Stop) {
        foreach ($action in $task.Actions) {
            foreach ($field in @('Execute', 'Arguments', 'WorkingDirectory')) {
                if ($action.PSObject.Properties[$field] -and $action.$field -and
                    (([string]$action.$field).Replace('/', '\').TrimEnd('\') + '\').IndexOf(
                        $nativeRoot, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                    $task
                    break
                }
            }
        }
    }
}

function Get-TaskDefinition($Task) {
    # Disabling a task changes only Settings/Enabled. Compare everything else
    # before restoring so a replaced/edited task is never silently re-enabled.
    $xml = [xml](Export-ScheduledTask -TaskName $Task.TaskName -TaskPath $Task.TaskPath -ErrorAction Stop)
    $enabled = $xml.SelectSingleNode('/*[local-name()="Task"]/*[local-name()="Settings"]/*[local-name()="Enabled"]')
    if ($enabled) { $null = $enabled.ParentNode.RemoveChild($enabled) }
    return $xml.OuterXml
}

function Suspend-UpdateTasks($Plan, $Suspended) {
    foreach ($task in @(Get-InstallationTasks $Plan.install_directory | Sort-Object TaskPath, TaskName -Unique)) {
        if ([string]$task.State -eq 'Disabled') { continue }
        if ([string]$task.State -eq 'Running') { throw "请先结束运行中的计划任务：$($task.TaskPath)$($task.TaskName)。" }
        $record = @{ TaskName = $task.TaskName; TaskPath = $task.TaskPath; definition = Get-TaskDefinition $task; restored = $false }
        $null = $Suspended.Add($record)
        # Journal the intent before changing Windows; retain it if recovery fails.
        Write-UpdateJson (Join-Path $Plan.workspace 'suspended-tasks.json') @($Suspended.ToArray())
        $null = Disable-ScheduledTask -TaskName $task.TaskName -TaskPath $task.TaskPath -ErrorAction Stop
    }
}

function Resume-UpdateTasks($Plan, $Suspended) {
    foreach ($record in $Suspended) {
        try {
            $task = Get-ScheduledTask -TaskName $record.TaskName -TaskPath $record.TaskPath -ErrorAction Stop
            if ((Get-TaskDefinition $task) -cne $record.definition) { throw '任务定义已被修改，请手动确认其启用状态。' }
            if ([string]$task.State -eq 'Disabled') {
                $null = Enable-ScheduledTask -TaskName $record.TaskName -TaskPath $record.TaskPath -ErrorAction Stop
            }
            $record.restored = $true
        } catch { "恢复计划任务 $($record.TaskPath)$($record.TaskName) 失败：$($_.Exception.Message)" }
    }
    if ($Suspended.Count) {
        Write-UpdateJson (Join-Path $Plan.workspace 'suspended-tasks.json') @($Suspended.ToArray())
    }
}

function Assert-ToolsStopped([string]$Install, [switch]$AllowEnabledTasks) {
    $nativeRoot = $Install.TrimEnd('\', '/').Replace('/', '\') + '\'
    foreach ($process in Get-CimInstance Win32_Process -Filter "Name = 'powershell.exe' OR Name = 'pwsh.exe'") {
        if ($process.ProcessId -ne $PID -and $process.CommandLine -and
            $process.CommandLine.Replace('/', '\').IndexOf($nativeRoot, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            throw '请先停止程序目录内运行中的工具（包括定时关机倒计时），再安装更新。'
        }
    }
    foreach ($task in Get-InstallationTasks $Install) {
        if ([string]$task.State -eq 'Running' -or (-not $AllowEnabledTasks -and [string]$task.State -ne 'Disabled')) {
            throw "引用程序目录的计划任务仍在运行或未能暂停：$($task.TaskPath)$($task.TaskName)。"
        }
    }
}

function Remove-UpdateWorkspace($Plan) {
    $install = [IO.Path]::GetFullPath([string]$Plan.install_directory).TrimEnd('\', '/')
    $workspace = [IO.Path]::GetFullPath([string]$Plan.workspace).TrimEnd('\', '/')
    $parent = Split-Path $install -Parent
    if (-not $parent -or (Split-Path $workspace -Parent) -ne $parent -or
        (Split-Path $workspace -Leaf) -notmatch '^\.Orchestrate-update-[a-zA-Z0-9]+$' -or $workspace -eq $install) {
        throw 'Unsafe update cleanup path.'
    }
    if (-not (Test-Path -LiteralPath $workspace)) { return }
    if ((Get-Item -LiteralPath $workspace -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked cleanup directory.' }
    Assert-NoLinks $workspace
    # The old launcher used the workspace as its native working directory,
    # which locks that directory on Windows even after Set-Location alone.
    Set-Location -LiteralPath $parent
    [Environment]::CurrentDirectory = $parent
    # Keep proof of completion until large payloads are gone. A locked ZIP or
    # DLL must not remove the metadata needed for a later startup retry.
    foreach ($entry in Get-ChildItem -LiteralPath $workspace -Force) {
        if ($entry.Name -in @('plan.json', 'result.json', 'startup.json', 'suspended-tasks.json')) { continue }
        Remove-Item -LiteralPath $entry.FullName -Recurse -Force
    }
    Remove-Item -LiteralPath $workspace -Recurse -Force
}

function Complete-UpdateCleanup($Plan, $Result) {
    $resultPath = Join-Path $Plan.install_directory 'data/update-result.json'
    $Result.cleanup_pending = $true
    $Result.workspace = $Plan.workspace
    $Result.cleanup_plan = $Plan
    Write-UpdateJson $resultPath $Result
    try {
        Remove-UpdateWorkspace $Plan
        $Result.cleanup_pending = $false
        $Result.Remove('cleanup_error')
        $Result.Remove('cleanup_plan')
    } catch {
        $Result.cleanup_error = "更新已成功，临时文件稍后重试清理：$($_.Exception.Message)"
    }
    Write-UpdateJson $resultPath $Result
}

function Remove-CompletedUpdateWorkspaces([string]$Install) {
    $install = [IO.Path]::GetFullPath($Install).TrimEnd('\', '/')
    $parent = Split-Path $install -Parent
    # Also handles the first upgrade performed by a pre-cleanup updater.
    foreach ($directory in Get-ChildItem -LiteralPath $parent -Directory -Force -Filter '.Orchestrate-update-*') {
        try {
            $current = Get-Content -LiteralPath (Join-Path $install 'data/update-result.json') -Raw -Encoding UTF8 | ConvertFrom-Json
            $savedPlan = $null
            if ($current.success -eq $true -and $current.PSObject.Properties['cleanup_pending'] -and $current.cleanup_pending -and
                $current.PSObject.Properties['cleanup_plan'] -and $current.workspace -eq $directory.FullName) {
                $savedPlan = $current.cleanup_plan
            }
            $plan = Read-UpdatePlan (Join-Path $directory.FullName 'plan.json') $savedPlan
            if ($plan.install_directory -ne $install -or $plan.workspace -ne $directory.FullName) { continue }
            $running = @(Get-CimInstance Win32_Process -Filter "Name = 'powershell.exe' OR Name = 'pwsh.exe'" -ErrorAction Stop |
                Where-Object { $_.ProcessId -ne $PID -and $_.CommandLine -and
                    $_.CommandLine.Replace('/', '\').IndexOf($directory.FullName, [StringComparison]::OrdinalIgnoreCase) -ge 0 })
            if ($running.Count) { continue }
            if ($savedPlan) {
                $saved = @{}
                foreach ($property in $current.PSObject.Properties) { $saved[$property.Name] = $property.Value }
                Complete-UpdateCleanup $plan $saved
                continue
            }
            $result = Get-Content -LiteralPath (Join-Path $plan.workspace 'result.json') -Raw -Encoding UTF8 | ConvertFrom-Json
            $health = Get-Content -LiteralPath (Join-Path $plan.workspace 'startup.json') -Raw -Encoding UTF8 | ConvertFrom-Json
            $info = Get-Content -LiteralPath (Join-Path $install 'build-info.json') -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($result.success -ne $true -or $health.version -ne $plan.version -or
                [version]$info.version -lt [version]$plan.version -or -not $result.backup_directory) { continue }
            $backup = [IO.Path]::GetFullPath([string]$result.backup_directory)
            if ((Split-Path $backup -Parent) -ne $parent -or
                -not (Split-Path $backup -Leaf).StartsWith((Split-Path $install -Leaf) + '-backup-', [StringComparison]::OrdinalIgnoreCase)) { continue }
            $journal = Join-Path $plan.workspace 'suspended-tasks.json'
            if (Test-Path -LiteralPath $journal) {
                $pending = @(Get-Content -LiteralPath $journal -Raw -Encoding UTF8 | ConvertFrom-Json | Where-Object { -not $_.restored })
                if ($pending.Count) { continue }
            }
            $otherCleanupPending = $current.PSObject.Properties['cleanup_pending'] -and $current.cleanup_pending -and
                $current.workspace -ne $directory.FullName
            if ($current.success -eq $true -and $current.backup_directory -eq $backup -and -not $otherCleanupPending) {
                $saved = @{}
                foreach ($property in $current.PSObject.Properties) { $saved[$property.Name] = $property.Value }
                Complete-UpdateCleanup $plan $saved
            } else { Remove-UpdateWorkspace $plan }
        } catch {
            # Incomplete, failed, linked, foreign or still-busy workspaces are
            # recovery material, never candidates for speculative deletion.
            Write-Verbose "Skipped update workspace $($directory.FullName): $($_.Exception.Message)"
            continue
        }
    }
}

function Move-UpdateDirectory([string]$Source, [string]$Destination) {
    [IO.Directory]::Move($Source, $Destination)
}

function Install-PreparedPackage($Plan) {
    $install = [string]$Plan.install_directory
    Assert-ToolsStopped $install
    Assert-NoLinks $install
    $oldInfo = Get-Content -LiteralPath (Join-Path $install 'build-info.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if ([version]$Plan.version -le [version]$oldInfo.version) { throw 'Update must be newer than the installed version.' }
    $oldFiles = Read-PayloadManifest $install
    $payload = Join-Path $Plan.workspace "payload/Orchestrate-v$($Plan.version)-windows-x64"
    Assert-NoLinks $payload
    $newFiles = Read-PayloadManifest $payload -Verify
    $newInfo = Get-Content -LiteralPath (Join-Path $payload 'build-info.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($newInfo.version -ne $Plan.version -or $newInfo.release_tag -ne "v$($Plan.version)" -or $newInfo.architecture -ne 'windows-x64') {
        throw 'Prepared package metadata changed.'
    }
    $stage = Join-Path $Plan.workspace 'staged'
    if (Test-Path -LiteralPath $stage) { throw 'Staging directory already exists.' }
    $null = New-Item -ItemType Directory -Path $stage
    Get-ChildItem -LiteralPath $install -Force | Copy-Item -Destination $stage -Recurse -Force
    foreach ($relative in $oldFiles.Keys) {
        $path = Join-Path $stage $relative
        if (Test-Path -LiteralPath $path) {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Expected an installed file: $relative" }
            Remove-Item -LiteralPath $path -Force
        }
    }
    foreach ($relative in $newFiles.Keys) {
        $destination = Join-Path $stage $relative
        $null = New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force
        Copy-Item -LiteralPath (Join-Path $payload $relative) -Destination $destination -Force
    }
    Copy-Item -LiteralPath (Join-Path $payload 'files.sha256') -Destination (Join-Path $stage 'files.sha256') -Force
    $backup = Join-Path (Split-Path $install -Parent) ((Split-Path $install -Leaf) + '-backup-' +
        (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    $moved = $false
    try {
        Move-UpdateDirectory $install $backup
        $moved = $true
        Move-UpdateDirectory $stage $install
    } catch {
        if ($moved -and -not (Test-Path -LiteralPath $install)) {
            try { Move-UpdateDirectory $backup $install }
            catch { throw "恢复旧目录失败。完整备份位于 $backup；请手动放回 $install。$($_.Exception.Message)" }
        }
        throw
    }
    return $backup
}

function Start-UpdatedApplication([string]$Install, [string]$HealthPath = '') {
    if ($HealthPath) {
        return Start-Process -FilePath (Join-Path $Install 'Orchestrate.exe') -WorkingDirectory $Install -WindowStyle Hidden `
            -ArgumentList ('--update-health-file "' + $HealthPath + '"') -PassThru
    }
    $null = Start-Process -FilePath (Join-Path $Install 'Orchestrate.exe') -WorkingDirectory $Install -WindowStyle Hidden
}

function Confirm-UpdatedStartup($Process, [string]$HealthPath, [string]$Version) {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while ([DateTime]::UtcNow -lt $deadline) {
        $Process.Refresh()
        if ($Process.HasExited) { throw '新版程序启动后退出。' }
        if (Test-Path -LiteralPath $HealthPath -PathType Leaf) {
            $health = Get-Content -LiteralPath $HealthPath -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($health.version -ne $Version -or [int]$health.process_id -ne $Process.Id) { throw '新版启动确认信息不匹配。' }
            return
        }
        Start-Sleep -Milliseconds 200
    }
    throw '新版程序未能在 30 秒内确认启动，将尝试恢复旧版。'
}

function Invoke-UpdatePlan([string]$Path, [switch]$PrepareOnly) {
    $plan = Read-UpdatePlan $Path
    if ($PrepareOnly) {
        Assert-NoLinks $plan.install_directory
        $null = Read-PayloadManifest $plan.install_directory
        Assert-ToolsStopped $plan.install_directory -AllowEnabledTasks
        $null = Expand-VerifiedPackage $plan
        Write-UpdateJson (Join-Path $plan.workspace 'result.json') @{ success = $true; message = '更新文件已准备就绪。' }
        return
    }
    # The application owns the same mutex. Wait for its normal exit and acquire it
    # before touching SQLite, user files or program files.
    $process = Get-Process -Id ([int]$plan.process_id) -ErrorAction SilentlyContinue
    if ($process) {
        if ($process.MainModule.FileName -ne (Join-Path $plan.install_directory 'Orchestrate.exe')) { throw '原应用进程身份不匹配。' }
        if (-not $process.WaitForExit(60000)) { throw '应用未退出，未替换任何程序文件。' }
    }
    $mutex = [Threading.Mutex]::new($false, [string]$plan.mutex_name)
    $locked = $false
    $backup = ''
    $newProcess = $null
    $startupConfirmed = $false
    $restartOld = $false
    $suspended = New-Object 'System.Collections.Generic.List[object]'
    $result = $null
    try {
        try { $locked = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $locked = $true }
        if (-not $locked) { throw '另一个 Orchestrate 实例正在运行，未替换任何程序文件。' }
        $restartOld = $true
        Assert-ToolsStopped $plan.install_directory -AllowEnabledTasks
        Suspend-UpdateTasks $plan $suspended
        $backup = Install-PreparedPackage $plan
        $restartOld = $false
        $healthPath = Join-Path $plan.workspace 'startup.json'
        $mutex.ReleaseMutex()
        $locked = $false
        $newProcess = Start-UpdatedApplication $plan.install_directory $healthPath
        Confirm-UpdatedStartup $newProcess $healthPath $plan.version
        $startupConfirmed = $true
        $message = "已更新至 $($plan.version)。旧版程序和数据备份：$backup"
        $result = @{ success = $true; message = $message; backup_directory = $backup;
            version = $plan.version; previous_process_id = $plan.process_id; process_id = $newProcess.Id }
    } catch {
        $failureMessage = $_.Exception.Message
        if ($backup -and -not $startupConfirmed) {
            if ($newProcess -and -not $newProcess.HasExited) { $newProcess.Kill(); $null = $newProcess.WaitForExit(5000) }
            if (-not $locked) {
                try { $locked = $mutex.WaitOne(5000) } catch [Threading.AbandonedMutexException] { $locked = $true }
            }
            try {
                if (-not $locked) { throw '无法锁定目录。' }
                $failed = Join-Path $plan.workspace 'failed-installation'
                Move-UpdateDirectory $plan.install_directory $failed
                Move-UpdateDirectory $backup $plan.install_directory
                $backup = ''
                $restartOld = $true
            } catch { $failureMessage += " 恢复旧版失败，完整备份位于 $backup。$($_.Exception.Message)" }
        }
        $result = @{ success = $false; message = "更新失败：$failureMessage" }
        throw
    } finally {
        $restoreErrors = @()
        $installationExists = Test-Path -LiteralPath (Join-Path $plan.install_directory 'Orchestrate.exe') -PathType Leaf
        if (($startupConfirmed -or $restartOld) -and $installationExists) {
            try { $restoreErrors = @(Resume-UpdateTasks $plan $suspended) }
            catch { $restoreErrors += "保存计划任务恢复记录失败：$($_.Exception.Message)" }
        } elseif ($suspended.Count) {
            $restoreErrors = @('程序目录尚未恢复，计划任务保持暂停；请先恢复完整备份，再手动启用任务。')
        }
        if ($locked) { $mutex.ReleaseMutex() }
        $mutex.Dispose()
        if ($locked -and $restartOld -and -not $startupConfirmed -and $installationExists) {
            Start-UpdatedApplication $plan.install_directory
        }
        if ($result) {
            $result.tasks_restored = ($restoreErrors.Count -eq 0)
            if ($restoreErrors.Count) { $result.message += "`n" + ($restoreErrors -join "`n") }
            Write-UpdateJson (Join-Path $plan.workspace 'result.json') $result
            if (Test-Path -LiteralPath (Join-Path $plan.install_directory 'data') -PathType Container) {
                Write-UpdateJson (Join-Path $plan.install_directory 'data/update-result.json') $result
                if ($startupConfirmed -and -not $restoreErrors.Count) { Complete-UpdateCleanup $plan $result }
            }
        }
    }
}

if ($MyInvocation.InvocationName -ne '.') {
    try {
        if ($CleanupCompleted) {
            if (-not $InstallDirectory -or $Prepare -or $Apply -or $PlanPath) { throw 'Invalid cleanup arguments.' }
            # The app can start before the legacy updater has finished writing
            # its result. Bounded retries also wait out Windows directory locks.
            for ($attempt = 0; $attempt -lt 3; $attempt++) {
                if ($attempt) { Start-Sleep -Seconds 2 }
                Remove-CompletedUpdateWorkspaces $InstallDirectory
            }
            exit 0
        }
        if (-not $PlanPath -or $Prepare -eq $Apply) { throw 'Select exactly one of -Prepare or -Apply.' }
        Invoke-UpdatePlan $PlanPath -PrepareOnly:$Prepare
        exit 0
    } catch {
        if ($PlanPath -and (Test-Path -LiteralPath (Split-Path $PlanPath -Parent))) {
            Write-UpdateJson (Join-Path (Split-Path $PlanPath -Parent) 'result.json') @{
                success = $false; message = "更新失败：$($_.Exception.Message)"
            }
        }
        Write-Error $_ -ErrorAction Continue
        exit 1
    }
}
