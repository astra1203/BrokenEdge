"""Decode bounded SpaceCenter loot packages; preserve source archives and provenance."""
import argparse
import json
import sqlite3
import subprocess
import sys
from pathlib import Path

root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'Scripts/SpaceCenter'))
from source_packages import extract

parser=argparse.ArgumentParser()
parser.add_argument('--tag',required=True)
parser.add_argument('--include-interactors',action='store_true')
parser.add_argument('--templates-only',action='store_true')
args=parser.parse_args()
assert args.tag and all(c.isalnum() or c in '-_' for c in args.tag)
out=root/'Saved/Extraction'/args.tag
out.mkdir(parents=True,exist_ok=False)
with sqlite3.connect(root/'Saved/SpaceCenter/catalog.sqlite') as db:
    paths=[r[0] for r in db.execute("SELECT path FROM entries WHERE package >= 'deltaforce/content/maps/spacecenter' AND package < 'deltaforce/content/maps/spacecentes' AND ext='.umap' ORDER BY path")
           if 'loot' in r[0].lower() or (args.include_interactors and ('/InterActors/' in r[0] or '/GameplayConfig/' in r[0]))]
packages=['/Game/'+p.removeprefix('DeltaForce/Content/').removesuffix('.umap') for p in paths]
if args.templates_only:
    packages=['/Game/BluePrints/Interact/BP_Interactor_Container_SafeBox',
              '/Game/BluePrints/Interact/BP_Interact_SmallSafebox']
assert packages and len(packages)<500
result=extract(packages,recursive=False)
(out/'Extraction.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
request={'mode':'inspect','source':str(root/'Saved/SpaceCenter/Source/DeltaForce'),
         'packages':packages,'output':str(out/'Decoded')}
path=out/'Request.json'
path.write_text(json.dumps(request,ensure_ascii=False,indent=2),encoding='utf-8')
with (out/'Converter.log').open('w',encoding='utf-8') as log:
    done=subprocess.run([str(root/'Saved/SpaceCenter/Tools/dotnet/dotnet.exe'),
        str(root/'Scripts/SpaceCenter/Converter/bin/Release/net10.0/SpaceCenter.Converter.dll'),str(path)],stdout=log,stderr=subprocess.STDOUT)
print('SOURCE_LOOT_INSPECTED',len(packages),done.returncode,str(out),flush=True)
sys.exit(done.returncode)
