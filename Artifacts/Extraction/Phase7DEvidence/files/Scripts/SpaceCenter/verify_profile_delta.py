"""Check stable source identities and record every material reconstruction change."""
import argparse
import collections
from restoration_common import *

p=argparse.ArgumentParser();p.add_argument('--manifest',required=True);p.add_argument('--tag',required=True)
args=p.parse_args();out=WORK/'DeltaRuns'/args.tag;out.mkdir(parents=True,exist_ok=False)
previous=input_path('previous_manifest','Manifest.json')
old=read(previous);new=read(args.manifest)
before={a['id']:a for a in old['actors']};after={a['id']:a for a in new['actors']}
assert len(before)==len(old['actors']) and len(after)==len(new['actors'])
assert not before.keys()-after.keys(),'Expansion lost previous source instances'
assert all(after[key]==value for key,value in before.items()),'Previous source transform, material or region changed'
assert not any('BadTips' in r['overlay'] and r['overlay_strength']>0 for r in new['materials'].values())
changes=[]
for source,recipe in new['materials'].items():
    prior=old['materials'].get(source)
    if recipe!=prior:
        fields={key:{'before':prior.get(key) if prior else None,'after':value}
                for key,value in recipe.items() if not prior or prior.get(key)!=value}
        changes.append({'source':source,'changes':fields,'source_chain':recipe['source_chain'],'notes':recipe['notes']})
write_json(out/'MaterialChanges.json',changes)
write_json(out/'ReconstructionNotes.json',[{'source':s,'chain':r['source_chain'],'notes':r['notes']}
    for s,r in new['materials'].items() if len(r['notes'])>1 or 'No decodable' in ' '.join(r['notes'])])
report={'result':'passed','previous_manifest_sha256':sha(previous),'manifest_sha256':sha(args.manifest),
        'previous_instances_unchanged':len(before),'new_instances':len(after)-len(before),
        'all_instances':len(after),'terrain_tiles':len(new['terrain']['tiles']),
        'material_changes':len(changes),'active_diagnostic_overlays':0,
        'region_counts':dict(collections.Counter(a['region'] for a in new['actors']))}
write_json(out/'Report.json',report);print(report,flush=True)
