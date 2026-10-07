"""Resolve native GLB slots through original render sections, including unused cooked slots."""
import argparse
import collections
import json
import struct
from restoration_common import WORK,read,write_json,run_converter,canonical


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--tag',required=True);parser.add_argument('--manifest',required=True)
    args=parser.parse_args();manifest=read(args.manifest)
    out=WORK/'SlotRuns'/args.tag;out.mkdir(parents=True,exist_ok=False)
    meshes=manifest['meshes'];paths=sorted(meshes);records={}
    for index in range(0,len(paths),60):
        batch=paths[index:index+60]
        recovery=[p for p in batch if meshes[p]['metadata'].get('recovery')]
        results=run_converter('mesh-slots',batch,out/('Batch-%04d'%index),recovery=recovery)
        for result in results:
            records[canonical(result['package'])]=next(o for o in result['objects'] if o['class']=='StaticMesh')
        print('RESTORE_SLOTS',index+len(batch),'/',len(paths),flush=True)
    by_mesh=collections.defaultdict(list)
    for actor in manifest['actors']:by_mesh[actor['mesh']].append(actor)
    mappings={};errors=[];unused=0;reordered=0;equivalent=[]
    for path,entry in meshes.items():
        with open(entry['file'],'rb') as file:
            head=file.read(20);doc=json.loads(file.read(struct.unpack_from('<I',head,12)[0]))
        lod=records[path]['export_lods'][0]
        sections=[s for s in lod['sections'] if s['triangles']>0]
        slots=[]
        for material in doc.get('materials',[]):
            candidates=sorted({s['material_index'] for s in sections if s['name']==material['name']})
            if not candidates:
                errors.append({'mesh':path,'name':material['name'],'error':'no_source_render_section'});continue
            differing=[a['id'] for a in by_mesh[path] if len({a['materials'][i] for i in candidates})!=1]
            if differing:
                errors.append({'mesh':path,'name':material['name'],'error':'merged_distinct_overrides','actors':differing});continue
            if len(candidates)>1:equivalent.append({'mesh':path,'name':material['name'],'source_slots':candidates})
            slots.append({'name':material['name'],'source_slot':candidates[0],'equivalent_source_slots':candidates})
        unused+=len(entry['metadata']['materials'])-len({i for slot in slots for i in slot['equivalent_source_slots']})
        reordered+=any(i!=s['source_slot'] for i,s in enumerate(slots))
        mappings[path]=slots
    write_json(out/'SlotBindings.json',mappings)
    write_json(out/'Report.json',{'result':'failed' if errors else 'passed','meshes':len(meshes),'unused_source_slots':unused,
                               'reordered_meshes':reordered,'equivalent_merged_slots':equivalent,'errors':errors})
    if errors:raise RuntimeError('Unresolved source slots: '+str(len(errors)))
    write_json(WORK/'SlotBindings.json',mappings)


if __name__=='__main__':main()
