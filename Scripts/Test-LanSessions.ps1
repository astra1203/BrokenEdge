param(
    [Parameter(Mandatory = $true)]
    [string]$EngineRoot,
    [ValidateSet('FreeForAll', 'Teams')]
    [string[]]$MatchTypes = @('FreeForAll', 'Teams')
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe'
$project = Join-Path $projectRoot 'BrokenEdge.uproject'
$resultRoot = Join-Path $projectRoot ('Saved/SessionTests/' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $resultRoot -Force | Out-Null
$ownedProcesses = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()

function Start-SessionTest([string]$Role, [string]$MatchType, [string]$Name) {
    $log = Join-Path $resultRoot ($Name + '.log')
    $arguments = @(
        ('"' + $project + '"'), '/Game/Maps/Lobby', '-game', '-nullrhi',
        '-nosound', '-unattended', '-nosplash', '-NoLoadingScreen',
        '-LogCmds="LogOnlineSession Verbose,LogOnline Verbose"',
        ('-SessionTestRole=' + $Role), ('-SessionTestMatchType=' + $MatchType),
        '-ExecCmds="Automation RunTests MultiplayerSessions.LAN.MenuFlow"',
        '-TestExit="Automation Test Queue Empty"', ('-abslog="' + $log + '"')
    )
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
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
    # 无主机时，确认搜索结束和按钮恢复后能再次搜索。
    $empty = Start-SessionTest 'Empty' 'FreeForAll' 'Empty'
    Wait-SessionTest $empty
    foreach ($matchType in $MatchTypes) {
        $hostRun = Start-SessionTest 'Host' $matchType ("Host-" + $matchType)
        $deadline = (Get-Date).AddSeconds(60)
        $listening = $false
        while ((Get-Date) -lt $deadline -and !$hostRun.Process.HasExited) {
            if (Test-Path -LiteralPath $hostRun.Log) {
                $listening = [bool](Select-String -LiteralPath $hostRun.Log -Pattern 'listening on port' -Quiet)
            }
            if ($listening) { break }
            Start-Sleep -Seconds 1
            $hostRun.Process.Refresh()
        }
        if (!$listening) { throw "Host did not listen: $($hostRun.Log)" }

        # 房间可见但模式不匹配时，不应错误加入。
        $otherType = if ($matchType -eq 'FreeForAll') { 'Teams' } else { 'FreeForAll' }
        $mismatch = Start-SessionTest 'Empty' $otherType ("Mismatch-" + $matchType)
        Wait-SessionTest $mismatch
        if (!(Select-String -LiteralPath $mismatch.Log -Pattern 'Find complete: Success=1 Results=[1-9]' -Quiet)) {
            throw "Mismatch test did not discover the host: $($mismatch.Log)"
        }

        $clientRun = Start-SessionTest 'Join' $matchType ("Join-" + $matchType)
        Wait-SessionTest $clientRun
        Wait-SessionTest $hostRun
    }
    Write-Host "All LAN menu tests passed. Logs: $resultRoot"
}
finally {
    # 仅清理此脚本创建的测试进程。
    foreach ($process in $ownedProcesses) {
        $process.Refresh()
        if (!$process.HasExited) { Stop-Process -Id $process.Id }
    }
}
