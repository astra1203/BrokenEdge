param(
    [string]$EngineRoot = 'C:\UnrealProjects\UnrealEngine-5.8.3-release',
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
$editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe'
$project = Join-Path $projectRoot 'BrokenEdge.uproject'
$resultRoot = Join-Path $projectRoot ('Saved/SessionTests/Dedicated-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $resultRoot -Force | Out-Null
$ownedProcesses = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
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
    Write-Output ([pscustomobject]@{ Process = $process; Log = $log; Name = $Name })
}

function Wait-SessionTest($Run) {
    $deadline = (Get-Date).AddSeconds(210)
    while (!$Run.Process.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 1
        $Run.Process.Refresh()
    }
    if (!$Run.Process.HasExited) { throw "Timed out: $($Run.Log)" }
    $text = Get-Content -LiteralPath $Run.Log -Raw -Encoding UTF8
    if ($text -notmatch 'Test Completed\. Result=\{(成功|Success)\}' -or
        $text -match 'Test Completed\. Result=\{(失败|Fail|Failed)\}|Fatal error:') {
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
}
finally {
    # 仅清理此脚本创建的测试进程。
    foreach ($process in $ownedProcesses) {
        $process.Refresh()
        if (!$process.HasExited) { Stop-Process -Id $process.Id }
    }
}
