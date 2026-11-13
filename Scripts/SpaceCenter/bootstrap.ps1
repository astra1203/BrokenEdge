param([string]$Engine)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$config = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'sample_config.json') -Encoding UTF8 | ConvertFrom-Json
if (!$Engine) { $Engine = $env:BROKENEDGE_UE_ROOT }
if (!$Engine) {
    $localConfig = Join-Path $project 'Scripts/local_config.json'
    if (Test-Path -LiteralPath $localConfig) {
        $Engine = (Get-Content -LiteralPath $localConfig -Encoding UTF8 | ConvertFrom-Json).engine
    }
}
if (!$Engine) { throw 'Specify -Engine, BROKENEDGE_UE_ROOT or Scripts/local_config.json (engine).' }
if (![IO.Path]::IsPathRooted($Engine)) { $Engine = Join-Path $project $Engine }
$python = Join-Path $Engine 'Engine/Binaries/ThirdParty/Python3/Win64/python.exe'
if (!(Test-Path -LiteralPath $python)) { throw "Engine Python not found: $python" }
& $python (Join-Path $project 'Scripts/Common/ue_env.py') --engine $Engine
if ($LASTEXITCODE) { throw 'Engine validation failed' }
$toolRoot = Join-Path $project 'Saved/SpaceCenter/Tools'
New-Item -ItemType Directory -Force -Path $toolRoot | Out-Null
$cueRoot = Join-Path $toolRoot 'CUE4Parse'
if (!(Test-Path -LiteralPath $cueRoot)) {
    & git clone --filter=blob:none --no-checkout https://github.com/FabianFG/CUE4Parse.git $cueRoot
    if ($LASTEXITCODE) { throw 'CUE4Parse clone failed' }
    & git -C $cueRoot checkout $config.cue_commit
    if ($LASTEXITCODE) { throw 'CUE4Parse checkout failed' }
}
$actual = (& git -C $cueRoot rev-parse HEAD).Trim()
if ($actual -ne $config.cue_commit) { throw "Unexpected CUE4Parse revision: $actual" }
$dotnet = Join-Path $toolRoot 'dotnet/dotnet.exe'
if (!(Test-Path -LiteralPath $dotnet)) {
    $installer = Join-Path $toolRoot 'dotnet-install.ps1'
    Invoke-WebRequest -Uri 'https://dot.net/v1/dotnet-install.ps1' -OutFile $installer -UseBasicParsing
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $installer -Version $config.sdk_version -InstallDir (Join-Path $toolRoot 'dotnet') -NoPath
    if ($LASTEXITCODE) { throw '.NET installation failed' }
}
& $python (Join-Path $PSScriptRoot 'patch_cue.py')
if ($LASTEXITCODE) { throw 'CUE4Parse compatibility hook failed' }
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
& $dotnet build (Join-Path $PSScriptRoot 'Converter/SpaceCenter.Converter.csproj') -c Release -p:CUE4PARSE_SKIP_NATIVE=true -v:q
if ($LASTEXITCODE) { throw 'Converter build failed' }
