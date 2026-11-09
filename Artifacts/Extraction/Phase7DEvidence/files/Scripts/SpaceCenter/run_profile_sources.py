"""Serial, guarded source stages for an explicit reconstruction profile."""
import argparse
import datetime
import subprocess
import sys
from restoration_common import ROOT, WORK, PROFILE, PROFILE_FILE, write_json

p=argparse.ArgumentParser(description=__doc__+' Shared option: --profile PATH')
p.add_argument('--tag',required=True)
p.add_argument('--start-at',default='select')
args=p.parse_args()
assert PROFILE['phase']=='7D','This stage sequence targets the approved 7D inputs'
out=WORK/'SourcePipelineRuns'/args.tag
out.mkdir(parents=True,exist_ok=False)
manifest='Saved/SpaceCenter/Phase7D/ManifestRuns/Phase7DManifest01/Manifest.json'
steps=[
 ('select','restore_scene.py',['select','--tag','Phase7DSelect01']),
 ('probe','probe_restoration.py',['--dependencies','--tag','Phase7DProbe01']),
 ('foliage','restore_foliage.py',['--tag','Phase7DFoliage01','--probe','Phase7DProbe01']),
 ('geometry','complete_restoration_geometry.py',['--tag','Phase7DGeometry01']),
 ('meshes','restore_assets.py',['meshes','--tag','Phase7DMeshes01','--selection','Phase7DSelect02']),
 ('materials','restore_assets.py',['materials','--tag','Phase7DMaterials01','--selection','Phase7DSelect02']),
 ('textures','restore_assets.py',['textures','--tag','Phase7DTextures01','--materials','Phase7DMaterials01']),
 ('heights','recover_phase7b_terrain.py',['--tag','Phase7DHeight01']),
 ('terrain','restore_terrain.py',['--tag','Phase7DTerrain01']),
 ('manifest','build_restoration_manifest.py',['--tag','Phase7DManifest01']),
 ('slots','restore_material_slots.py',['--tag','Phase7DSlots01','--manifest',manifest]),
]
steps=steps[[s[0] for s in steps].index(args.start_at):]
report={'result':'running','profile':str(PROFILE_FILE.relative_to(ROOT)),
        'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'steps':[]}
try:
    for name,script,options in steps:
        command=[sys.executable,str(ROOT/'Scripts/Extraction/resource_guard.py'),'--tag',args.tag+'-'+name,
                 '--timeout','3600','--',sys.executable,str(ROOT/'Scripts/SpaceCenter'/script),
                 *options,'--profile',str(PROFILE_FILE)]
        record={'name':name,'command':command};report['steps'].append(record);write_json(out/'Report.json',report)
        print('SOURCE_STAGE',name,flush=True)
        with (out/(name+'.log')).open('wb') as log:
            child=subprocess.Popen(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
            record['pid']=child.pid;write_json(out/'Report.json',report);record['exit_code']=child.wait()
        write_json(out/'Report.json',report)
        if record['exit_code']:raise RuntimeError(name+' failed; inspect its immutable log')
        print('SOURCE_PASSED',name,flush=True)
    report['result']='passed'
except Exception as error:
    report.update(result='failed',error=repr(error));raise
finally:
    report['finished_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();write_json(out/'Report.json',report)
