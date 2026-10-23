"""Read bounded original gameplay packages; never promote extracted points automatically."""
import json
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'Scripts/SpaceCenter'))
from source_packages import extract

packages = ['/Game/Maps/SpaceCenter_Level1/GameplayConfig/SpaceCenter_Level1_GameplayConfig',
            '/Game/Maps/SpaceCenter_Level2/GameplayConfig/SpaceCenter_Level2_GameplayConfig',
            '/Game/Maps/SpaceCenter_Ma1/GenericConfig/SpaceCenter_Persistent']
out = root/'Saved/Extraction/SpaceCenterExpansion01/OriginalGameplay'
out.mkdir(parents=True,exist_ok=True)
result = extract(packages,recursive=False)
(out/'Extraction.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
request = {'mode':'inspect','source':str(root/'Saved/SpaceCenter/Source/DeltaForce'),
           'packages':packages,'output':str(out/'Decoded')}
path = out/'Request.json'
path.write_text(json.dumps(request,ensure_ascii=False,indent=2),encoding='utf-8')
with (out/'Converter.log').open('w',encoding='utf-8') as log:
    done = subprocess.run([str(root/'Saved/SpaceCenter/Tools/dotnet/dotnet.exe'),
                           str(root/'Scripts/SpaceCenter/Converter/bin/Release/net10.0/SpaceCenter.Converter.dll'),
                           str(path)],stdout=log,stderr=subprocess.STDOUT)
print('SOURCE_GAMEPLAY_INSPECTED',done.returncode,str(out))
sys.exit(done.returncode)
