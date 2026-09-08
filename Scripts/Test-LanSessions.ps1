param(
    [string]$EngineRoot = 'D:\UnrealEngine-5.5.4-release',
    [switch]$Packaged,
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

function Start-SessionTest([string]$Role, [string]$MatchType, [string]$Name) {
    $log = Join-Path $resultRoot ($Name + '.log')
    $arguments = @()
    $executable = $editor
    if ($Packaged) {
        $relative = if ($Role -eq 'Server') {
            'Packaged/Server/WindowsServer/BrokenEdge/Binaries/Win64/BrokenEdgeServer.exe'
        } else {
            'Packaged/Client/Windows/BrokenEdge/Binaries/Win64/BrokenEdge.exe'
        }
        $executable = Join-Path $projectRoot $relative
    } else {
        $arguments += ('"' + $project + '"')
        $arguments += if ($Role -eq 'Server') { '-server' } else { '-game' }
    }
    if (!(Test-Path -LiteralPath $executable)) { throw "Executable not found: $executable" }
    $arguments += @(
        '/Game/Maps/Lobby', '-nullrhi',
        '-nosound', '-unattended', '-nosplash', '-NoLoadingScreen',
        '-LogCmds="LogOnlineSession Verbose,LogOnline Verbose"',
        ('-SessionTestRole=' + $Role), ('-SessionTestMatchType=' + $MatchType),
        ('-MatchType=' + $MatchType), '-NumPublicConnections=2', '-MinPlayersToStart=2',
        '-ExecCmds="Automation RunTests MultiplayerSessions.LAN.MenuFlow"',
        '-TestExit="Automation Test Queue Empty"', ('-abslog="' + $log + '"')
    )
    if ($BindAddress) { $arguments += ('-MULTIHOME=' + $BindAddress) }
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
        throw "Test failed: $($Run.Log)"
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

        # 房间可见但模式不匹配时，不应错误加入。
        $otherType = if ($matchType -eq 'FreeForAll') { 'Teams' } else { 'FreeForAll' }
        $mismatch = Start-SessionTest 'Empty' $otherType ("Mismatch-" + $matchType)
        Wait-SessionTest $mismatch
        if (!(Select-String -LiteralPath $mismatch.Log -Pattern 'Find complete: Success=1 Results=[1-9]' -Quiet)) {
            throw "Mismatch test did not discover the server: $($mismatch.Log)"
        }

        $clientRun = Start-SessionTest 'Join' $matchType ("Client1-" + $matchType)
        $clientTwo = Start-SessionTest 'Join' $matchType ("Client2-" + $matchType)
        Wait-SessionTest $clientRun
        Wait-SessionTest $clientTwo
        Wait-SessionTest $hostRun
    }
    Write-Host "All dedicated server tests passed. Logs: $resultRoot"
}
finally {
    # 仅清理此脚本创建的测试进程。
    foreach ($process in $ownedProcesses) {
        $process.Refresh()
        if (!$process.HasExited) { Stop-Process -Id $process.Id }
    }
}
