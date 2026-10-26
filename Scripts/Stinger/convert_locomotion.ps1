param()
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$config = Get-Content -LiteralPath (Join-Path $projectRoot 'Scripts/SpaceCenter/sample_config.json') -Encoding UTF8 | ConvertFrom-Json
$cueRoot = Join-Path $projectRoot 'Saved/SpaceCenter/Tools/CUE4Parse'
$dotnet = Join-Path $projectRoot 'Saved/SpaceCenter/Tools/dotnet/dotnet.exe'
$python = Join-Path $config.engine 'Engine/Binaries/ThirdParty/Python3/Win64/python.exe'
if (!(Test-Path -LiteralPath $dotnet)) { throw 'Run Scripts/SpaceCenter/bootstrap.ps1 first.' }
if ((& git -C $cueRoot rev-parse HEAD).Trim() -ne $config.cue_commit) { throw 'Unexpected CUE4Parse revision.' }
& git -C $cueRoot submodule update --init --recursive CUE4Parse-Natives/ACL/external/acl
if ($LASTEXITCODE) { throw 'Pinned ACL dependencies could not be loaded.' }
$nativeRoot = Join-Path $cueRoot 'CUE4Parse-Natives'
& cmake -S $nativeRoot -B (Join-Path $nativeRoot 'builddir') -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE) { throw 'Native configure failed.' }
& cmake --build (Join-Path $nativeRoot 'builddir') --config Release --target install
if ($LASTEXITCODE) { throw 'Native build failed.' }
& $dotnet build (Join-Path $projectRoot 'Scripts/SpaceCenter/Converter/SpaceCenter.Converter.csproj') -c Release -p:CUE4PARSE_SKIP_NATIVE=true
if ($LASTEXITCODE) { throw 'Converter build failed.' }
$converterRoot = Join-Path $projectRoot 'Scripts/SpaceCenter/Converter/bin/Release/net10.0'
Copy-Item -LiteralPath (Join-Path $nativeRoot 'bin/Release/CUE4Parse-Natives.dll') -Destination $converterRoot
& $python (Join-Path $PSScriptRoot 'prepare_locomotion.py')
if ($LASTEXITCODE) { throw 'Bounded extraction failed.' }
foreach ($mode in @('inspect', 'export')) {
    & $dotnet (Join-Path $converterRoot 'SpaceCenter.Converter.dll') (Join-Path $projectRoot "Saved/Stinger/Locomotion/$mode-request.json")
    if ($LASTEXITCODE) { throw "$mode failed. Inspect Saved/Stinger/Locomotion/$mode." }
}
