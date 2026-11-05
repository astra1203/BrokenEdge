"""Bounded conversion of the exact dropped meshes referenced by official item tables."""
import ast
import json
from pathlib import Path
import sqlite3
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Scripts/SpaceCenter'))
from source_packages import extract, package_key
OUT = ROOT/'Saved/Extraction/WorldModels'
tables = json.loads((OUT/'ItemAssets/Packages.json').read_text(encoding='utf-8'))
rows = {k:v for t in tables if not t['package'].endswith('_Blasting') for k,v in t['objects'][0]['Rows'].items()}
collectibles = json.loads((ROOT/'Scripts/Extraction/data/spacecenter_collectibles.json').read_text(encoding='utf-8'))['items']
selected = {x['id']:x['official_id'] for x in collectibles}
gear_tree = ast.parse((ROOT/'Scripts/Extraction/configure_gear.py').read_text(encoding='utf-8'))
gear = next(ast.literal_eval(n.value) for n in gear_tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='GEAR' for t in n.targets))
selected.update({x[0]:str(x[1]) for x in gear if x[0].startswith(('Bag_','Rig_'))})
selected.update(Helmet2='11010002001',Helmet3='11010003001',Vest2='11050002001',Vest3='11050003001',
                FragGrenade='21010000001',Bag_Test='11080004002')
db = sqlite3.connect(ROOT/'Saved/SpaceCenter/catalog.sqlite')
manifest = {'items':{},'missing':[]}
packages = set()
for ident,official in sorted(selected.items()):
    row = rows.get(str(official),{})
    mesh = next((row.get(k,{}).get('AssetPathName') for k in ['ItemDroppedMeshPath','Item3PMeshPath','ItemUIMeshPath']
                 if row.get(k,{}).get('AssetPathName','None')!='None'),None)
    if not mesh or not db.execute("SELECT 1 FROM entries WHERE package=? AND ext='.uasset'",(package_key(mesh.split('.')[0]),)).fetchone():
        manifest['missing'].append(dict(id=ident,official=official,mesh=mesh))
        continue
    package = mesh.split('.')[0]
    packages.add(package)
    manifest['items'][ident] = dict(official=official,source=package)
manifest['packages'] = sorted(packages)
(OUT/'Selection.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print('WORLD_MODELS_SELECTION',len(manifest['items']),len(packages),'missing',len(manifest['missing']),flush=True)
# Cooked FName numeric suffixes are stored separately from the name table used by
# source_packages' preflight. Include each selected model's own material/texture
# folders, rather than guessing a truncated package name or extracting whole ZIPs.
dependencies = set()
for package in sorted(packages):
    parent = package.rsplit('/',1)[0]
    prefix = parent.replace('/Game/','DeltaForce/Content/').lower()+'/'
    for (path,) in db.execute("SELECT path FROM entries WHERE package>=? AND package<? AND ext='.uasset'",(prefix,prefix+'\uffff')):
        if '/material' in path.lower() or '/texture' in path.lower(): dependencies.add(path.removesuffix('.uasset'))
    prefix = parent.replace('/Game/','DeltaForce/Content/GenTexDirHD/Game/').lower()+'/'
    dependencies.update(path.removesuffix('.uasset') for (path,) in db.execute(
        "SELECT path FROM entries WHERE package>=? AND package<? AND ext='.uasset'",(prefix,prefix+'\uffff')))
result = extract(sorted(packages|dependencies),recursive=True,limit_gib=5)
# Some props share a material in a neighbouring prop folder. Resolve only an
# unambiguous numeric FName suffix in that exact folder, never a global name guess.
shared = set()
for missing in result['missing']:
    if '/material' not in missing.lower(): continue
    prefix = missing.replace('/Game/','DeltaForce/Content/').lower()
    candidates = [path.removesuffix('.uasset') for (path,) in db.execute(
        "SELECT path FROM entries WHERE package>=? AND package<? AND ext='.uasset'",(prefix,prefix+'\uffff'))]
    if len(candidates)==1: shared.add(candidates[0])
if shared:
    result['shared_material_extraction'] = extract(sorted(shared),recursive=True,limit_gib=.5)
(OUT/'Extraction.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
(OUT/'Export.json').write_text(json.dumps(dict(mode='export',export_materials=True,
    source=str(ROOT/'Saved/SpaceCenter/Source/DeltaForce'),output=str(OUT/'Converted'),packages=sorted(packages)),indent=2),encoding='utf-8')
print('WORLD_MODELS_READY',result['bytes'],flush=True)
