"""Independent repeatability, archive immutability and foliage-physics duplicate checks."""
import argparse
import collections
import math
from pathlib import Path
from restoration_common import ROOT,BASE,WORK,read,sha,write_json
from restore_terrain import generate


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--tag',required=True);args=parser.parse_args()
    out=WORK/'VerificationRuns'/args.tag;out.mkdir(parents=True,exist_ok=False)
    generate(args.tag)
    original=read(WORK/'TerrainRuns/Phase7CTerrain01/Manifest.json')
    repeat=read(WORK/'TerrainRuns'/args.tag/'Manifest.json')
    assert [(t['id'],t['sha256'],t['position']) for t in original['tiles']]==[(t['id'],t['sha256'],t['position']) for t in repeat['tiles']]
    by_package=collections.defaultdict(list)
    for actor in read(WORK/'FoliageRuns/Phase7CFoliage05/Decoded.json'):
        if actor.get('physics_validated'):by_package[actor['source_package']].append(actor)
    duplicate_records=[];physics_count=0
    for package in read(WORK/'ProbeRuns/Phase7CProbe06/Inspect/Parsed/Packages.json'):
        for obj in package.get('objects',[]):
            if obj.get('Type')!='OpenWorldFoliageAsset':continue
            physics=obj['Properties'].get('PhysicsData',{});base=physics.get('Position',{})
            matched=set()
            for index,body in enumerate(physics.get('SerialBodyInstances',[])):
                physics_count+=1;position=[base[k]+body['Location'][k] for k in 'XYZ']
                model=body['BodySetup']['ObjectName'].split("'")[1].rstrip("'")
                candidates=[a for a in by_package[package['package']] if model.lower().startswith(a['mesh'].split('/')[-1].lower())
                            and math.dist(position,[a['transform']['Translation'][k] for k in 'XYZ'])<=.01]
                assert len(candidates)==1,(package['package'],index,len(candidates))
                actor=candidates[0];t=actor['transform']
                assert max(abs(t['Rotation'][k]-body['Rotation'][k]) for k in 'XYZW')<1e-6
                assert max(abs(t['Scale3D'][k]-body['Scaling'][k]) for k in 'XYZ')<1e-6
                if actor['id'] in matched:duplicate_records.append({'package':package['package'],'physics_index':index,'instance':actor['id'],'position':position})
                matched.add(actor['id'])
            assert len(matched)==len(by_package[package['package']])
    archives=[];source=Path(read(ROOT/'Scripts/SpaceCenter/north_config.json')['source'])
    for entry in read(BASE/'ArchiveIndex.json')['archives']:
        current=(source/entry['archive']).stat()
        assert current.st_size==entry['size'] and current.st_mtime_ns==entry['mtime_ns'],entry['archive']
        archives.append(entry['archive'])
    report={'result':'passed','terrain_tiles':len(repeat['tiles']),'terrain_all_bytes_repeat':True,
            'terrain_max_roundtrip_error_cm':repeat['max_roundtrip_error_cm'],'archives_unchanged_size_mtime':archives,
            'foliage_physics_records':physics_count,'duplicate_physics_records':duplicate_records,
            'boundary':'Archive identity verified by original size/mtime; recorded extracted source hashes and immutable archives are retained.'}
    write_json(out/'Report.json',report)
    print('RESTORATION_SOURCE_CHECK',len(repeat['tiles']),physics_count,len(duplicate_records),flush=True)


if __name__=='__main__':main()
