param(
    [Alias('Engine')][string]$EngineRoot,
    [switch]$Packaged,
    [string]$PackageRoot = 'Packaged',
    [switch]$Render,
    [switch]$SwitchMode,
    [int]$LanGameUniqueId = 0,
    [ValidateSet('FreeForAll', 'Teams')]
    [string[]]$MatchTypes = @('FreeForAll', 'Teams'),
    [string]$BindAddress = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$editor = $null
if (!$Packaged) {
    if (!$EngineRoot) { $EngineRoot = $env:BROKENEDGE_UE_ROOT }
    if (!$EngineRoot) {
        $localConfig = Join-Path $projectRoot 'Scripts/local_config.json'
        if (Test-Path -LiteralPath $localConfig) {
            $EngineRoot = (Get-Content -LiteralPath $localConfig -Encoding UTF8 | ConvertFrom-Json).engine
        }
    }
    if (!$EngineRoot) { throw 'Specify -EngineRoot, BROKENEDGE_UE_ROOT or Scripts/local_config.json (engine).' }
    if (![IO.Path]::IsPathRooted($EngineRoot)) { $EngineRoot = Join-Path $projectRoot $EngineRoot }
    $python = Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/Python3/Win64/python.exe'
    if (!(Test-Path -LiteralPath $python)) { throw "Engine Python not found: $python" }
    $EngineRoot = & $python (Join-Path $PSScriptRoot 'Common/ue_env.py') --engine $EngineRoot
    if ($LASTEXITCODE) { throw 'Engine validation failed' }
    $editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe'
}
$project = Join-Path $projectRoot 'BrokenEdge.uproject'
$resultRoot = Join-Path $projectRoot ('Saved/SessionTests/Dedicated-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $resultRoot -Force | Out-Null
$ownedProcesses = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
$runReport = [ordered]@{
    engine_root = if ($Packaged) { $null } else { $EngineRoot }
    engine_version = if ($Packaged) { $null } else { Get-Content -LiteralPath (Join-Path $EngineRoot 'Engine/Build/Build.version') -Encoding UTF8 | ConvertFrom-Json }
    runtime_kind = if ($Packaged) { 'packaged; engine provenance not supplied' } else { 'editor game/server' }
    git_head = (& git -C $projectRoot rev-parse HEAD)
    git_status = ((& git -C $projectRoot status --porcelain) -join "`n")
    script = $PSCommandPath
    script_sha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
    ue_env_sha256 = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'Common/ue_env.py') -Algorithm SHA256).Hash.ToLowerInvariant()
    parameters = $PSBoundParameters
    report_path = (Join-Path $resultRoot 'Run.json')
    cache_environment = @{
        'UE-LocalDataCachePath' = ${env:UE-LocalDataCachePath}
        'UE-ZenDataPath' = ${env:UE-ZenDataPath}
        'UE-ZenSubprocessDataPath' = ${env:UE-ZenSubprocessDataPath}
    }
    started_utc = [DateTime]::UtcNow.ToString('o')
    result = 'running'
    processes = [System.Collections.Generic.List[object]]::new()
}
if ($BindAddress) {
    $parsedAddress = [System.Net.IPAddress]::Parse($BindAddress)
    if ($parsedAddress.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork) { throw 'BindAddress must be IPv4.' }
}

function Start-SessionTest([string]$Role, [string]$MatchType, [string]$Name, [int]$Port = 7777) {
    $log = Join-Path $resultRoot ($Name + '.log')
    $arguments = @()
    $executable = $editor
    if ($Packaged) {
        $relative = if ($Role -in @('Server', 'Advertise')) {
            'Server/WindowsServer/BrokenEdge/Binaries/Win64/BrokenEdgeServer.exe'
        } else {
            'Client/Windows/BrokenEdge/Binaries/Win64/BrokenEdge.exe'
        }
        $executable = Join-Path (Join-Path $projectRoot $PackageRoot) $relative
    } else {
        $arguments += ('"' + $project + '"')
        $arguments += if ($Role -in @('Server', 'Advertise')) { '-server' } else { '-game' }
        $arguments += '-DisablePython'
    }
    if (!(Test-Path -LiteralPath $executable)) { throw "Executable not found: $executable" }
    $arguments += @(
        '/Game/Maps/Lobby',
        '-nosound', '-unattended', '-nosplash', '-NoLoadingScreen',
        '-LogCmds="LogOnlineSession Verbose,LogOnline Verbose"',
        ('-SessionTestRole=' + $Role), ('-SessionTestMatchType=' + $MatchType),
        ('-port=' + $Port), '-SessionTestExpectedRooms=2',
        ('-MatchType=' + $MatchType), '-NumPublicConnections=2', '-MinPlayersToStart=2',
        ('-abslog="' + $log + '"')
    )
    if ($Render -and $Role -notin @('Server', 'Advertise', 'Empty')) {
        $arguments += @('-RenderOffscreen', '-windowed', '-ResX=1280', '-ResY=800',
            ('-SessionTestScreenshotDir="' + $resultRoot + '"'))
    } else {
        $arguments += '-nullrhi'
    }
    if ($Role -ne 'Advertise') {
        if ($SwitchMode) { $arguments += '-SessionTestSwitchMode' }
        $arguments += @('-ExecCmds="Automation RunTests MultiplayerSessions.LAN.MenuFlow"', '-TestExit="Automation Test Queue Empty"')
    }
    if ($BindAddress) { $arguments += ('-MULTIHOME=' + $BindAddress) }
    # Isolate automation discovery without stopping other running game servers.
    if ($LanGameUniqueId) { $arguments += ('-ini:Engine:[LANSession]:LanGameUniqueId=' + $LanGameUniqueId) }
    $process = Start-Process -FilePath $executable -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $ownedProcesses.Add($process)
    $runReport.processes.Add([ordered]@{ role = $Role; name = $Name; pid = $process.Id; command = @($executable) + $arguments })
    $runReport | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $resultRoot 'Run.json') -Encoding UTF8
    Write-Output ([pscustomobject]@{ Process = $process; Log = $log; Name = $Name })
}

function Wait-SessionTest($Run) {
    $deadline = (Get-Date).AddSeconds(210)
    while (!$Run.Process.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 1
        $Run.Process.Refresh()
    }
    if (!$Run.Process.HasExited) { throw "Timed out: $($Run.Log)" }
    $Run.Process.WaitForExit()
    foreach ($entry in $runReport.processes) {
        if ($entry.pid -eq $Run.Process.Id) { $entry.exit_code = $Run.Process.ExitCode }
    }
    $text = Get-Content -LiteralPath $Run.Log -Raw -Encoding UTF8
    if ($Run.Process.ExitCode -ne 0 -or $text -notmatch 'Test Completed\. Result=\{(\u6210\u529f|Success)\}' -or
        $text -match 'Test Completed\. Result=\{(\u5931\u8d25|Fail(?:ed|ure)?)\}|Fatal error:') {
        throw "Test failed (exit $($Run.Process.ExitCode)): $($Run.Log)"
    }
    Write-Host "PASS $($Run.Name)"
}

try {
    # 无专服时，确认搜索结束和按钮恢复后能再次搜索。
    $empty = Start-SessionTest 'Empty' 'FreeForAll' 'Empty'
    Wait-SessionTest $empty
    foreach ($matchType in $MatchTypes) {
        $hostRun = Start-SessionTest 'Server' $matchType ("Server-" + $matchType)
        $deadline = (Get-Date).AddSeconds(90)
        $listening = $false
        while ((Get-Date) -lt $deadline -and !$hostRun.Process.HasExited) {
            if (Test-Path -LiteralPath $hostRun.Log) {
                $text = Get-Content -LiteralPath $hostRun.Log -Raw -Encoding UTF8
                $listening = $text -match 'listening on port' -and $text -match 'Create complete: Success=1'
            }
            if ($listening) { break }
            Start-Sleep -Seconds 1
            $hostRun.Process.Refresh()
        }
        if (!$listening) { throw "Dedicated server did not register and listen: $($hostRun.Log)" }

        # 同时启动另一模式的房间，验证列表不再按菜单模式过滤或自动加入。
        $otherType = if ($matchType -eq 'FreeForAll') { 'Teams' } else { 'FreeForAll' }
        $otherRun = Start-SessionTest 'Advertise' $otherType ("Other-" + $matchType) 7778
        $deadline = (Get-Date).AddSeconds(90)
        $otherListening = $false
        while ((Get-Date) -lt $deadline -and !$otherRun.Process.HasExited) {
            if (Test-Path -LiteralPath $otherRun.Log) {
                $text = Get-Content -LiteralPath $otherRun.Log -Raw -Encoding UTF8
                $otherListening = $text -match 'listening on port' -and $text -match 'Create complete: Success=1'
            }
            if ($otherListening) { break }
            Start-Sleep -Seconds 1
            $otherRun.Process.Refresh()
        }
        if (!$otherListening) { throw "Second room did not register: $($otherRun.Log)" }

        $clientRun = Start-SessionTest 'Join' $matchType ("Client1-" + $matchType)
        $clientTwo = Start-SessionTest 'Cancel' $matchType ("Client2-" + $matchType)
        Wait-SessionTest $clientRun
        Wait-SessionTest $clientTwo
        Wait-SessionTest $hostRun
        $otherRun.Process.Refresh()
        if (!$otherRun.Process.HasExited) { Stop-Process -Id $otherRun.Process.Id }
    }
    Write-Host "All room browser and readiness tests passed. Logs: $resultRoot"
    $runReport.result = 'passed'
}
catch {
    $runReport.result = 'failed'
    $runReport.error = $_.Exception.Message
    throw
}
finally {
    # 仅清理此脚本创建的测试进程。
    foreach ($process in $ownedProcesses) {
        $process.Refresh()
        if (!$process.HasExited) { Stop-Process -Id $process.Id }
    }
    $runReport.exit_code = if ($runReport.result -eq 'passed') { 0 } else { 1 }
    $runReport.finished_utc = [DateTime]::UtcNow.ToString('o')
    $runReport | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $resultRoot 'Run.json') -Encoding UTF8
}
