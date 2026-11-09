"""Native reconstruction stages; production map changes occur only in integrate."""
import hashlib
import json
import os
from pathlib import Path
import sys
import traceback
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
sys.path.insert(0,str(ROOT/'Scripts/SpaceCenter'))
from restoration_common import WORK, read, write_json, sha, identity
OUT=Path(os.environ['SPACECENTER_RUN_DIR'])
TASK=os.environ['SPACECENTER_RESTORE_TASK']
MANIFEST=read(os.environ['SPACECENTER_RESTORE_MANIFEST'])
DEST='/Game/Maps/SpaceCenter/Restored'
LIB=unreal.EditorAssetLibrary
TOOLS=unreal.AssetToolsHelpers.get_asset_tools()
EDIT=unreal.MaterialEditingLibrary
STATE_FILE=WORK/'NativeAssets.json'
state=read(STATE_FILE) if STATE_FILE.exists() else {}
report={'task':TASK,'created':[],'reused':0,'errors':[]}


def asset(entry,kind,role=None):
    key=kind+':'+entry['source']+(':'+role if role else '')
    digest=sha(entry['file'])
    previous=state.get(key)
    if previous and previous['sha256']==digest and previous.get('asset') and LIB.does_asset_exist(previous['asset']):
        report['reused']+=1
        return previous['asset']
    if previous and (previous['sha256']!=digest or not previous.get('pending')):
        raise RuntimeError('Changed import input requires explicit asset revision: '+key)
    if kind=='mesh' and entry.get('native_asset'):
        path=entry['native_asset']
        assert LIB.does_asset_exist(path),path
        report['reused']+=1
    else:
        folder=DEST+('/Terrain/' if kind=='terrain' else '/Assets/')+identity(key)
        cls=unreal.Texture2D if kind=='texture' else unreal.StaticMesh
        paths=[p for p in LIB.list_assets(folder,recursive=True) if isinstance(LIB.load_asset(p),cls)] if LIB.does_directory_exist(folder) else []
        if paths:
            if not previous or not previous.get('pending') or len(paths)!=1:
                raise RuntimeError('Untracked native assets in '+folder)
            imported=LIB.load_asset(paths[0]).get_editor_property('asset_import_data')
            if not imported or Path(imported.get_first_filename()).resolve()!=Path(entry['file']).resolve():
                raise RuntimeError('Pending import source differs: '+folder)
        state[key]={'sha256':digest,'pending':True,'folder':folder,'source_file':entry['file']}
        write_json(STATE_FILE,state)
        task=unreal.AssetImportTask()
        task.filename=entry['file'];task.destination_path=folder
        task.automated=True;task.save=True;task.replace_existing=False
        if not paths:TOOLS.import_asset_tasks([task])
        paths=[p for p in LIB.list_assets(folder,recursive=True) if isinstance(LIB.load_asset(p),cls)]
        if len(paths)!=1:raise RuntimeError('Expected one '+kind+' in '+folder+', found '+str(len(paths)))
        obj=LIB.load_asset(paths[0]);path=obj.get_path_name()
        if kind in ['mesh','terrain']:
            body=obj.get_editor_property('body_setup')
            assert body,path
            body.set_editor_property('collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
            body.set_editor_property('double_sided_geometry',True)
            if kind=='terrain':
                settings=obj.get_editor_property('nanite_settings')
                settings.set_editor_property('fallback_target',unreal.NaniteFallbackTarget.PERCENT_TRIANGLES)
                settings.set_editor_property('fallback_percent_triangles',1.)
                settings.set_editor_property('fallback_relative_error',0.)
                obj.set_editor_property('nanite_settings',settings)
        else:
            obj.set_editor_property('srgb',role=='color')
            obj.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_NORMALMAP if role=='normal' else
                                    unreal.TextureCompressionSettings.TC_BC7 if role=='normal_data' else
                                    unreal.TextureCompressionSettings.TC_MASKS if role=='data' else unreal.TextureCompressionSettings.TC_DEFAULT)
        LIB.save_loaded_asset(obj)
        report['created'].append({'key':key,'asset':path})
    state[key]={'asset':path,'sha256':digest}
    write_json(STATE_FILE,state)
    return path


def import_meshes():
    entries=list(MANIFEST['meshes'].values())+[e|{'source':'terrain-node:'+str(e['node'])} for e in MANIFEST['terrain']['tiles']]
    for index,entry in enumerate(entries):
        kind='terrain' if entry['source'].startswith('terrain-node:') else 'mesh'
        asset(entry,kind)
        if index%20==0:
            unreal.SystemLibrary.collect_garbage()
            print('RESTORE_NATIVE_MESH',index,'/',len(entries),flush=True)


def import_textures():
    created=0
    limit=int(os.environ.get('SPACECENTER_IMPORT_LIMIT','0'))
    for index,(key,entry) in enumerate(MANIFEST['texture_bindings'].items()):
        previous=state.get('texture:'+entry['source']+':'+entry['role'])
        asset(entry,'texture',entry['role'])
        created+=not previous or bool(previous.get('pending'))
        if limit and created>=limit:
            report['remaining']=len(MANIFEST['texture_bindings'])-index-1
            break
        if index%20==0:
            unreal.SystemLibrary.collect_garbage()
            print('RESTORE_NATIVE_TEXTURE',index,'/',len(MANIFEST['texture_bindings']),flush=True)


def warm_assets():
    ledger=WORK/'WarmAssets.json';warmed=read(ledger) if ledger.exists() else {}
    entries=[(key,value) for key,value in state.items() if key.startswith(('texture:','mesh:','terrain:','restoration_master:','restoration_material:')) and value.get('sha256')]
    pending=[(k,v) for k,v in entries if warmed.get(k)!=v['sha256']]
    limit=int(os.environ.get('SPACECENTER_IMPORT_LIMIT','250')) or 250
    selected=pending[:limit]
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    for index,(key,value) in enumerate(selected):
        obj=LIB.load_asset(value['asset']);assert obj,value['asset']
        unreal.SystemLibrary.execute_console_command(world,'Editor.AsyncAssetCompilationFinishAll')
        del obj
        warmed[key]=value['sha256'];write_json(ledger,warmed)
        if index%10==0:
            unreal.SystemLibrary.collect_garbage();print('RESTORE_WARM',index,'/',len(selected),flush=True)
    report.update(warmed=len(selected),remaining=len(pending)-len(selected))


def terrain_fidelity():
    report['terrain_settings']=[]
    for tile in MANIFEST['terrain']['tiles']:
        mesh=LIB.load_asset(state['terrain:'+tile['source']]['asset']);settings=mesh.get_editor_property('nanite_settings')
        before={'enabled':settings.enabled,'fallback_target':str(settings.fallback_target),
                'fallback_percent_triangles':settings.fallback_percent_triangles,'fallback_relative_error':settings.fallback_relative_error}
        settings.set_editor_property('fallback_target',unreal.NaniteFallbackTarget.PERCENT_TRIANGLES)
        settings.set_editor_property('fallback_percent_triangles',1.)
        settings.set_editor_property('fallback_relative_error',0.)
        mesh.set_editor_property('nanite_settings',settings)
        LIB.save_loaded_asset(mesh)
        report['terrain_settings'].append({'tile':tile['id'],'before':before,'full_collision_fallback':True})
        if len(report['terrain_settings'])%10==0:unreal.SystemLibrary.collect_garbage()


try:
    if TASK=='meshes':import_meshes()
    elif TASK=='textures':import_textures()
    elif TASK=='warm':warm_assets()
    elif TASK=='terrain-fidelity':terrain_fidelity()
    else:
        from restoration_world import run
        run(globals())
    report['result']='passed'
except Exception:
    report['result']='failed';report['errors'].append(traceback.format_exc())
    raise
finally:
    write_json(OUT/'Report.json',report)
