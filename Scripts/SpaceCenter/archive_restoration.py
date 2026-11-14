"""Archive a verified restoration profile with byte-preserving, bounded-memory copies."""
import argparse
import datetime
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

from restoration_common import ROOT, WORK, PROFILE, input_path
PHASE=PROFILE['phase']
ROUTES=['old','north','east'] if PHASE=='7D' else ['old','north']


def read(path):return json.loads(path.read_text(encoding='utf-8'))
def write(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def main():
    parser=argparse.ArgumentParser()
    for group in ['packages','rules','streaming','regression','performance']:
        parser.add_argument('--'+group+'-tag',required=True)
    parser.add_argument('--audit-tag',required=True)
    parser.add_argument('--capture-tag',required=True)
    parser.add_argument('--east-tag',required=PHASE=='7D')
    parser.add_argument('--spectator-tag',required=PHASE=='7D',help='Successful packaged three-rendered-client spectator regression')
    parser.add_argument('--baseline-capture',required=PHASE=='7D',help='Repository-relative directory of same-camera 7C captures')
    parser.add_argument('--source-verification-tag',default='Phase7CSourceVerification01' if PHASE=='7C' else None,required=PHASE=='7D')
    parser.add_argument('--traverse-tag',default='Phase7CTraverse03' if PHASE=='7C' else None,required=PHASE=='7D')
    parser.add_argument('--initial-integration-tag',default='Phase7CIntegrate01' if PHASE=='7C' else None,required=PHASE=='7D')
    parser.add_argument('--streaming-a-tag',help='Previously completed rendered-A run when resuming the group at rendered B')
    parser.add_argument('--regression-run',action='append',default=[],metavar='NAME=TAG',
                        help='Explicit successful run for an earlier case when resuming regression')
    args=parser.parse_args()
    groups={g:ROOT/'Saved/Extraction'/getattr(args,g+'_tag') for g in ['packages','rules','streaming','regression','performance']}
    for group,path in groups.items():
        assert read(path/'Report.json')['result']=='passed',(group,'Incomplete acceptance')
    for group,names in {'packages':['server','client'],'rules':['inventory','persistence','equipment','progression'],
                        'performance':ROUTES}.items():
        steps=read(groups[group]/'Report.json')['steps']
        assert [s['name'] for s in steps]==names and all(s['exit_code']==0 for s in steps),(group,'Partial group')
        for name in names:
            folder=ROOT/'Saved/Extraction'/(getattr(args,group+'_tag')+'-'+name)
            filename='Report.json' if group=='performance' else 'Run.json'
            case=read(folder/filename)
            if group=='packages':
                assert case['exit_code']==0,(folder,'Unsuccessful package')
                assert {'BuildCookRun','-build','-cook','-stage','-archive'}.issubset(case['command'])
                assert ('-serverconfig=Development' if name=='server' else '-clientconfig=Development') in case['command']
            else:
                assert case['result']=='passed',(folder,'Unsuccessful case')
            assert read(folder.with_name(folder.name+'-resources')/'Report.json')['result']=='passed'
    regression_names=['progression','equipment','squad','pvpve','lifecycle','inventory','settlement','disconnect','optic']
    overrides={}
    for entry in args.regression_run:
        name,tag=entry.split('=',1)
        assert name in regression_names and name not in overrides and tag
        overrides[name]=tag
    regression_runs={}
    shared_inputs=['Scripts/Extraction/run_network.py','Source/BrokenEdge/Tests/ExtractionDriverTests.cpp',
                   'Source/BrokenEdge/Extraction/Core/ExtractionPlayerController.h',
                   'Source/BrokenEdge/Extraction/Core/ExtractionPlayerController.cpp',
                   'BrokenEdge.uproject','Content/Maps/SpaceCenter/L_SpaceCenterExtraction.umap',
                   'Content/Maps/SpaceCenter/L_SpaceCenterNorth.umap']
    spectator_run=None
    if PHASE=='7D':
        folder=ROOT/'Saved/Extraction'/args.spectator_tag
        run=read(folder/'Report.json')
        assert run['result']=='passed' and run['options']['packaged']
        assert run['options']['scenario']=='spectator' and run['options']['rendered_clients']==3
        assert set(run['processes'])=={'Server','A','B','C'}
        for path in shared_inputs:
            assert run['input_sha256'][path]==sha(ROOT/path),(folder,'Stale spectator regression input',path)
        assert read(folder.with_name(folder.name+'-resources')/'Report.json')['result']=='passed'
        spectator_run=folder.relative_to(ROOT).as_posix()
    for route in ROUTES:
        folder=ROOT/'Saved/Extraction'/(args.performance_tag+'-'+route)
        run=read(folder/'Report.json');options=run['options']
        assert options['packaged'] and options['scenario']=='performance' and options['performance_route']==route
        assert options['performance_samples']==3 and options['performance_seconds']==180
        route_inputs=['Scripts/Extraction/streaming_scenarios.py','Scripts/SpaceCenter/north_route.json']
        if PHASE=='7D':route_inputs+=['Scripts/SpaceCenter/route.json']
        if route=='east':route_inputs+=['Scripts/SpaceCenter/east_route.json']
        for path in shared_inputs+route_inputs:
            assert run['input_sha256'][path]==sha(ROOT/path),(folder,'Stale performance input',path)
    scenario_inputs={'progression':['progression_scenarios.py'],
                     'equipment':['equipment_scenarios.py','optic_scenarios.py'],
                     'squad':['phase5_scenarios.py'],'pvpve':['phase5_scenarios.py'],'lifecycle':['phase5_scenarios.py'],
                     'inventory':['inventory_scenarios.py'],'settlement':['storage_scenarios.py'],
                     'disconnect':[], 'optic':['equipment_scenarios.py','optic_scenarios.py',
                                             'verify_optic_capture.py','optic_calibration.py']}
    for name in regression_names:
        tag=overrides.get(name,args.regression_tag+'-'+name)
        folder=ROOT/'Saved/Extraction'/tag;run=read(folder/'Report.json')
        assert run['result']=='passed' and run['options']['packaged'],(tag,'Missing successful packaged regression')
        assert run['options']['scenario']==('equipment' if name=='optic' else name)
        if name in regression_names[:5]:assert run['options']['players']==6
        assert run['options']['lag']==(name in ['progression','pvpve'])
        if name=='optic':
            assert run['options']['optic_validation'] and run['options']['resolution']=='1920x1080'
        relevant=shared_inputs+['Scripts/Extraction/'+p for p in scenario_inputs[name]]
        if name in ['equipment','optic']:relevant+=['Content/Extraction/Items/DA_Item_RedDot.uasset']
        for path in relevant:assert run['input_sha256'][path]==sha(ROOT/path),(tag,'Stale tested input',path)
        assert read(folder.with_name(folder.name+'-resources')/'Report.json')['result']=='passed'
        regression_runs[name]=folder.relative_to(ROOT).as_posix()
    streaming_runs={}
    # The unchanged old-area route predates run_network's input ledger. Verify
    # it against each accepted run's base commit rather than inventing a past hash.
    old_route='Scripts/SpaceCenter/route.json'
    for name in ['two-rendered-a','two-rendered-b','six-streaming','six-northcombat','six-streaming-lag','six-northcombat-lag']:
        tag=args.streaming_a_tag if name=='two-rendered-a' and args.streaming_a_tag else args.streaming_tag+'-'+name
        folder=ROOT/'Saved/Extraction'/tag
        run=read(folder/'Report.json')
        assert run['result']=='passed',(tag,'Missing successful streaming acceptance')
        assert run['options']['packaged']
        assert read(folder.with_name(folder.name+'-resources')/'Report.json')['result']=='passed'
        if name.startswith('two-rendered-'):
            role=name[-1].upper()
            assert run['options']['players']==2 and run['options']['streaming_loops']==10
            assert run['options']['rendered_role']==run['traversing_role']==role
            names={c['name'] for c in run['checks']}
            assert {'10 actual reload and unload transitions','Streaming preserves container identities and items',
                    'Second streaming round returns normally'}.issubset(names)
        else:
            assert run['options']['players']==6 and run['options']['lag']==name.endswith('-lag')
        # These are the inputs executed by the streaming scenario. Unrelated
        # equipment fixtures are recorded by run_network but are not used here.
        relevant=['Scripts/Extraction/run_network.py','Source/BrokenEdge/Tests/ExtractionDriverTests.cpp',
                   'Source/BrokenEdge/Extraction/Core/ExtractionPlayerController.h',
                   'Source/BrokenEdge/Extraction/Core/ExtractionPlayerController.cpp',
                  'Scripts/Extraction/streaming_scenarios.py','Scripts/Extraction/north_combat_scenarios.py',
                  'Scripts/SpaceCenter/north_route.json','BrokenEdge.uproject',
                  'Content/Maps/SpaceCenter/L_SpaceCenterExtraction.umap','Content/Maps/SpaceCenter/L_SpaceCenterNorth.umap']
        for path in relevant:assert run['input_sha256'][path]==sha(ROOT/path),(tag,'Stale tested input',path)
        original_route=subprocess.check_output(['git','show',run['base_commit']+':'+old_route],cwd=ROOT)
        assert original_route.replace(b'\r\n',b'\n')==(ROOT/old_route).read_bytes().replace(b'\r\n',b'\n'),(tag,'Old route changed')
        streaming_runs[name]=folder.relative_to(ROOT).as_posix()
    assert read(groups['performance']/'Performance.json')['target_passed'],'Performance target failed'
    east_runs={}
    if PHASE=='7D':
        east_group=ROOT/'Saved/Extraction'/args.east_tag
        group=read(east_group/'Report.json')
        assert group['result']=='passed' and [s['name'] for s in group['steps']]==['two-rendered-a','two-rendered-b']
        for role in ['A','B']:
            folder=ROOT/'Saved/Extraction'/(args.east_tag+'-two-rendered-'+role.lower())
            run=read(folder/'Report.json')
            assert run['result']=='passed' and run['options']['packaged'] and run['options']['scenario']=='east'
            assert run['options']['players']==2 and run['traversing_role']==run['options']['rendered_role']==role
            for name in shared_inputs+['Scripts/Extraction/east_scenarios.py','Scripts/SpaceCenter/east_route.json']:
                assert run['input_sha256'][name]==sha(ROOT/name),(folder,'Stale east input',name)
            assert read(folder.with_name(folder.name+'-resources')/'Report.json')['result']=='passed'
            east_runs[role]=folder.relative_to(ROOT).as_posix()
        groups['east']=east_group
    marker=read(ROOT/'Scripts/SpaceCenter/restoration_active.json')
    manifest_path=ROOT/marker['manifest_file'];manifest=read(manifest_path)
    assert sha(manifest_path)==marker['manifest_sha256']
    audit=WORK/'EditorRuns'/args.audit_tag
    capture=WORK/'EditorRuns'/args.capture_tag
    for path in [audit,capture]:
        inspected=read(path/'Run.json')
        assert inspected['manifest_sha256']==marker['manifest_sha256']
        assert inspected['result']==read(path/'Report.json')['result']=='passed'
        assert inspected['map_sha256_before']==inspected['map_sha256_after']
        for name,digest in inspected['map_sha256_after'].items():assert sha(ROOT/name)==digest,(path,'Stale map inspection')
    capture_report=read(capture/'Report.json')
    assert len(capture_report['captures'])>=(20 if PHASE=='7D' else 12)
    assert capture_report.get('native_render_resolution',capture_report.get('viewport_size'))==[1920,1080]
    if PHASE=='7D':
        traversal=WORK/'EditorRuns'/args.traverse_tag
        traversal_run=read(traversal/'Run.json');traversal_report=read(traversal/'Report.json')
        assert traversal_run['result']==traversal_report['result']=='passed'
        assert traversal_run['task']=='traverse-integrated'
        assert traversal_report['map']=='/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
        assert traversal_run['manifest_sha256']==marker['manifest_sha256']
        assert traversal_run['map_sha256_before']==traversal_run['map_sha256_after']
        for name,digest in traversal_run['map_sha256_after'].items():
            assert sha(ROOT/name)==digest,(traversal,'Stale traversal map')
        route=input_path('traverse_route',ROOT/'Scripts/SpaceCenter/route.json')
        assert traversal_report['route_sha256']==sha(route),'Stale integrated traversal route'
        assert [c['name'] for c in traversal_report['checks']]==[s['name'] for s in read(route)['segments']]
        assert all(c['passed'] for c in traversal_report['checks'])
        terrain=read(traversal/'TerrainCollision.json')
        assert terrain['result']=='passed' and terrain['tiles']==143 and len(terrain['probes'])>=1001 and terrain['max_error_cm']<=1
    assert capture_report['quality']==1
    assert capture_report['map']=='/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
    assert capture_report['camera_config_sha256']==sha(input_path('cameras',ROOT/'Scripts/SpaceCenter/restoration_cameras_final.json'))
    visual=read(capture/'VisualReview.json')
    assert visual['result']=='passed' and len(visual['captures'])==len(capture_report['captures'])
    assert {c['file']:c['sha256'] for c in visual['captures']}=={c['file']:c['sha256'] for c in capture_report['captures']}
    if PHASE=='7D':
        baseline=(ROOT/args.baseline_capture).resolve()
        baseline.relative_to(ROOT/'Saved/SpaceCenter')
        baseline_report=read(baseline/'Report.json')
        assert baseline_report['result']==read(baseline/'Run.json')['result']=='passed'
        assert baseline_report['native_render_resolution']==[1920,1080] and baseline_report['quality']==1
        old_cameras={c['name']:(c['position'],c['target']) for c in baseline_report['captures']}
        new_cameras={c['name']:(c['position'],c['target']) for c in capture_report['captures']}
        assert len(old_cameras)==14 and all(new_cameras.get(k)==v for k,v in old_cameras.items())
        assert visual['baseline_capture']==args.baseline_capture
        comparison_file=ROOT/visual['matched_preview_review']
        assert sha(comparison_file)==visual['matched_preview_review_sha256']
        comparison=read(comparison_file)
        assert comparison['result']=='passed' and comparison['baseline_capture']==args.baseline_capture
        preview=read(comparison_file.with_name('Report.json'))
        assert preview['capture_world']==baseline_report['capture_world']=='standalone'
        assert {c['file']:c['sha256'] for c in comparison['captures']}=={c['file']:c['sha256'] for c in preview['captures']}
    assert read(audit/'Report.json')['map']=='/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
    assert read(WORK/'RepeatVerification.json')['result']=='passed'
    assert read(WORK/'VerificationRuns'/args.source_verification_tag/'Report.json')['result']=='passed'
    destination=ROOT/('Artifacts/Extraction/Phase'+PHASE+'Evidence')
    assert not destination.exists(),'Final evidence already exists; do not overwrite it'
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    out=destination.with_name(destination.name+'.partial-'+stamp)
    assert destination.resolve().parent==out.resolve().parent==(ROOT/'Artifacts/Extraction').resolve()
    out.mkdir(parents=True,exist_ok=False)
    records=[];copied=set()

    def copy(source):
        source=source.resolve()
        if source in copied:return
        copied.add(source)
        relative=source.relative_to(ROOT)
        compress=source.suffix.lower() in {'.json','.jsonl','.log','.csv','.umap'} and source.stat().st_size>65536
        target=out/'files'/relative
        if compress:target=target.with_name(target.name+'.gz')
        target.parent.mkdir(parents=True,exist_ok=True)
        before=sha(source)
        if compress:
            with source.open('rb') as inp,target.open('wb') as raw:
                with gzip.GzipFile(fileobj=raw,mode='wb',filename='',mtime=0) as dest:shutil.copyfileobj(inp,dest,1024*1024)
        else:shutil.copyfile(source,target)
        with (gzip.open(target,'rb') if compress else target.open('rb')) as stream:
            restored=hashlib.file_digest(stream,'sha256').hexdigest()
        assert restored==before,(source,'Archive bytes differ')
        records.append({'source':relative.as_posix(),'archive':target.relative_to(out).as_posix(),
                        'source_bytes':source.stat().st_size,'source_sha256':before,'gzip':compress})

    metadata={'Report.json','Run.json','Process.json','Request.json','TerrainCollision.json','References.json',
              'Extractions.json','SourceExtractions.json'}
    if PHASE=='7D':metadata.update({'Selection.json','MeshMetadata.json','Decoded.json','Manifest.json','Graph.json','Textures.json','Meshes.json'})
    for path in sorted(WORK.rglob('*')):
        if path.is_file() and path.name in metadata and 'Import' not in path.relative_to(WORK).parts:copy(path)
    for path in [manifest_path,WORK/'NativeAssets.json',WORK/'SlotBindings.json',WORK/'RepeatVerification.json',
                 WORK/'PreviewBindings.json',WORK/'IntegrationBindings.json']:
        copy(path)
    integration=ROOT/marker['integration_report']
    editor_tags={args.initial_integration_tag,integration.parent.name,args.audit_tag,args.capture_tag,args.traverse_tag}
    if PHASE=='7D':editor_tags.update(p.name for p in (WORK/'EditorRuns').iterdir() if p.is_dir())
    for tag in sorted(editor_tags):
        for path in sorted((WORK/'EditorRuns'/tag).rglob('*')):
            if path.is_file():copy(path)
    for path in (WORK/'ReferenceRuns').rglob('*'):
        if path.is_file():copy(path)
    if PHASE=='7D':
        for folder in [baseline,WORK/'DeltaRuns']:
            for path in folder.rglob('*'):
                if path.is_file():copy(path)
    # Retain all diagnostic/resource results, including failed attempts. The
    # acceptance groups below identify the successful final runs explicitly.
    for folder in sorted((ROOT/'Saved/Extraction').glob('Phase'+PHASE+'*')):
        if not folder.is_dir():continue
        for path in folder.iterdir():
            if path.is_file() and (path.name in metadata or path.suffix in {'.log','.csv','.png','.json','.jsonl'}):copy(path)
        for path in (folder/'Automation').rglob('*.json'):copy(path)
    inputs=[p for folder in [ROOT/'Scripts/SpaceCenter',ROOT/'Scripts/Extraction',ROOT/'Source/BrokenEdge/SpaceCenter',ROOT/'Source/StingerTools']
            for p in folder.rglob('*') if p.is_file() and p.suffix in {'.py','.json','.cs','.csproj','.cpp','.h'} and not {'bin','obj','__pycache__'}.intersection(p.parts)]
    inputs.append(ROOT/'Source/BrokenEdge/Tests/ExtractionDriverTests.cpp')
    inputs.extend(ROOT/('Source/BrokenEdge/Extraction/ExtractionPlayerController.'+ext) for ext in ['h','cpp'])
    for path in inputs:copy(path)
    native=[p for p in (ROOT/'Content/Maps/SpaceCenter').rglob('*') if p.is_file() and p.suffix in {'.uasset','.umap'}]
    binaries=[p for target in ['Client','Server'] for p in (ROOT/'Packaged'/target).rglob('*')
              if p.is_file() and p.suffix in {'.exe','.pak','.utoc','.ucas'}]
    write(out/'NativeAssets.json',{p.relative_to(ROOT).as_posix():{'bytes':p.stat().st_size,'sha256':sha(p)} for p in native})
    write(out/'PackagedArtifacts.json',{p.relative_to(ROOT).as_posix():{'bytes':p.stat().st_size,'sha256':sha(p)} for p in binaries})
    summary={'phase':PHASE,'result':'passed','created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
             'base_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
             'marker':marker,'acceptance_groups':{k:str(v.relative_to(ROOT)) for k,v in groups.items()},
             'streaming_runs':streaming_runs,
             'east_runs':east_runs,
             'spectator_regression':spectator_run,
             'regression_runs':regression_runs,
             'traverse':str((WORK/'EditorRuns'/args.traverse_tag).relative_to(ROOT)),
             'audit':read(audit/'Report.json'),'capture':read(capture/'Report.json'),
             'actors':len(manifest['actors']),'terrain_tiles':len(manifest['terrain']['tiles']),
             'boundary':'Original-source geometry and visual material reconstruction. Stripped source shaders and exact source-version camera matching are not claimed recovered. Same-host Development/Null multiplayer acceptance only.',
             'copied_files':records}
    write(out/'Summary.json',summary)
    index={p.relative_to(out).as_posix():{'bytes':p.stat().st_size,'sha256':sha(p)} for p in sorted(out.rglob('*')) if p.is_file()}
    write(out/'ArchiveIndex.json',index)
    for name,entry in index.items():
        p=out/name;assert p.stat().st_size==entry['bytes'] and sha(p)==entry['sha256'],name
    index_hash=sha(out/'ArchiveIndex.json')
    out.rename(destination)
    print(json.dumps({'result':'archive_verified','folder':destination.relative_to(ROOT).as_posix(),
                      'files':len(index),'bytes':sum(v['bytes'] for v in index.values()),'index_sha256':index_hash}))


if __name__=='__main__':main()
