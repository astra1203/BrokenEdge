param([string]$ReportPath)
$ErrorActionPreference = 'Stop'
$scriptPath = Join-Path $PSScriptRoot '../Test-LanSessions.ps1'
$parseTokens = $null
$parseErrors = $null
$tree = [System.Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$parseTokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors -join "`n") }
$definition = $tree.Find({
    param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Wait-SessionTest'
}, $true)
if (!$definition) { throw 'Missing Wait-SessionTest function' }
# Load only the completion checker; this suite must never start a game process.
. ([scriptblock]::Create($definition.Extent.Text))
$runReport = @{ processes = @() }
$mock = [pscustomobject]@{ HasExited = $true; ExitCode = 0 }
$mock | Add-Member -MemberType ScriptMethod -Name WaitForExit -Value { }
$mock | Add-Member -MemberType ScriptMethod -Name Refresh -Value { }
$fixtureLog = [IO.Path]::GetTempFileName()
try {
    $cases = @(
        @{ name='EnglishSuccess'; text='Test Completed. Result={Success}'; code=0; expected=$true },
        @{ name='ChineseSuccess'; text=('Test Completed. Result={' + [char]0x6210 + [char]0x529f + '}'); code=0; expected=$true },
        @{ name='CrashAfterSuccess'; text='Test Completed. Result={Success}'; code=1; expected=$false },
        @{ name='Failure'; text='Test Completed. Result={Failure}'; code=0; expected=$false },
        @{ name='MixedFailure'; text="Test Completed. Result={Success}`nTest Completed. Result={Failure}"; code=0; expected=$false },
        @{ name='Empty'; text=''; code=0; expected=$false }
    )
    $results = foreach ($case in $cases) {
        $mock.ExitCode = $case.code
        $case.text | Set-Content -LiteralPath $fixtureLog -Encoding UTF8
        $accepted = $true
        try { Wait-SessionTest ([pscustomobject]@{ Process=$mock; Log=$fixtureLog; Name=$case.name }) }
        catch { $accepted = $false }
        if ($accepted -ne $case.expected) { throw ('Unexpected completion result: ' + $case.name) }
        [pscustomobject]@{ case=$case.name; accepted=$accepted; expected=$case.expected }
    }
    try {
        & $scriptPath -EngineRoot (Join-Path ([IO.Path]::GetTempPath()) ('NoSuchEngine-' + [guid]::NewGuid().ToString('N')))
        throw 'Invalid engine was accepted'
    } catch {
        if ($_.Exception.Message -notlike 'Engine Python not found:*') { throw }
    }
    $results += [pscustomobject]@{ case='InvalidEngine'; accepted=$false; expected=$false }
    if ($ReportPath) { $results | ConvertTo-Json | Set-Content -LiteralPath $ReportPath -Encoding UTF8 }
    Write-Host ('PASS: ' + $results.Count + ' LAN runner checks; no game process launched')
} finally {
    Remove-Item -LiteralPath $fixtureLog -Force
}
