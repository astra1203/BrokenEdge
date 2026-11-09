"""Fixed native 1080p inspection; editor-only mode avoids retaining a duplicate PIE world."""
import json
import os
from pathlib import Path
import struct
import sys
import time
import traceback
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
sys.path.insert(0,str(ROOT/'Scripts/SpaceCenter'))
from restoration_common import read,write_json,sha,input_path,PROFILE
OUT=Path(os.environ['SPACECENTER_RUN_DIR'])
INTEGRATED=os.environ.get('SPACECENTER_RESTORE_TASK')=='capture-integrated'
EDITOR_ONLY=os.environ.get('SPACECENTER_CAPTURE_WORLD')=='editor'
CAMERAS=input_path('cameras',ROOT/'Scripts/SpaceCenter'/('restoration_cameras_final.json' if INTEGRATED else 'restoration_cameras.json'))
CONFIG=read(CAMERAS)
if PROFILE['phase']=='7D':assert len(CONFIG['shots'])>=20,'7D requires the complete fixed camera set'
LIB=unreal.StingerAssetLibrary
report={'task':'capture','result':'running','resolution':CONFIG['resolution'],'quality':CONFIG['quality'],
        'camera_config_sha256':sha(CAMERAS),'captures':[],
        'reference_status':CONFIG['reference_status'],'capture_world':'editor' if EDITOR_ONLY else 'pie'}
state={'phase':'boot','elapsed':0.,'index':0}
started=time.monotonic()


def advance(phase):state.update(phase=phase,elapsed=0.)


def finish(error=None):
    report.update(result='failed' if error else 'passed',elapsed_seconds=time.monotonic()-started)
    if error:report['error']=error
    write_json(OUT/'Report.json',report)
    unreal.unregister_slate_post_tick_callback(handle)
    if not EDITOR_ONLY:unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def console(command):
    pawn=state['pawn']
    unreal.SystemLibrary.execute_console_command(pawn,command,pawn.get_controller())


def tick(delta):
    try:
        state['elapsed']+=delta
        phase,elapsed=state['phase'],state['elapsed']
        if time.monotonic()-started>1800:raise TimeoutError('Capture timed out: '+phase)
        if phase=='boot' and elapsed>5:
            if INTEGRATED:
                # Inspection-only PIE setup, never save these temporary settings.
                world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
                if not EDITOR_ONLY:world.get_world_settings().set_editor_property('default_game_mode',unreal.EditorAssetLibrary.load_blueprint_class('/Game/Characters/Stinger/Blueprints/BP_StingerTestGameMode'))
                stream=unreal.GameplayStatics.get_streaming_level(world,'/Game/Maps/SpaceCenter/L_SpaceCenterNorth')
                assert stream and stream.get_loaded_level()
                unreal.EditorLevelUtils.set_level_visibility(stream.get_loaded_level(),True,False)
                stream.set_editor_property('initially_loaded',True)
                stream.set_editor_property('initially_visible',True)
                report['map']='/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
                report['inspection_setup']='Production lighting/geometry; visible North, unsaved. '+('Editor camera only; runtime collision is a separate gate.' if EDITOR_ONLY else 'Temporary local test GameMode.')
            unreal.SystemLibrary.execute_console_command(None,'Editor.AsyncAssetCompilationFinishAll')
            unreal.SystemLibrary.collect_garbage()
            if EDITOR_ONLY:
                levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
                state['camera']=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.CameraActor,unreal.Vector(0,0,10000))
                state['camera'].camera_component.set_field_of_view(CONFIG['field_of_view'])
                state['camera'].camera_component.set_aspect_ratio(CONFIG['resolution'][0]/CONFIG['resolution'][1])
                levels.editor_set_game_view(True)
                levels.set_exact_camera_view(True)
                report['render_boundary']='One editor world, native high-resolution scene render at requested dimensions, unchanged camera/lighting and Medium. PIE collision and packaged gameplay are separately verified.'
                advance('camera')
            else:LIB.begin_validation_pie(1);advance('spawn')
        elif phase=='spawn':
            pawns=[p for w in LIB.get_validation_worlds() for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.BrokenEdgeCharacter) if p.is_locally_controlled()]
            if len(pawns)!=1 or elapsed<5:return
            pawn=pawns[0];state['pawn']=pawn;LIB.isolate_validation_input(pawn.get_controller())
            assert LIB.set_validation_viewport_size(pawn.get_controller(),*CONFIG['resolution'])
            state['camera']=LIB.spawn_validation_weapon(pawn.get_world(),unreal.CameraActor,unreal.Vector(0,0,10000))
            state['camera'].camera_component.set_field_of_view(CONFIG['field_of_view'])
            pawn.get_controller().set_view_target_with_blend(state['camera'],0.)
            for command in ['r.VSync 0','t.MaxFPS 60','t.IdleWhenNotForeground 0','r.ScreenPercentage 100']:
                console(command)
            for group in ['ViewDistance','AntiAliasing','Shadow','GlobalIllumination','Reflection','PostProcess','Texture','Effects','Foliage','Shading']:
                console('sg.'+group+'Quality '+str(CONFIG['quality']))
            report['viewport_size']=list(pawn.get_controller().get_viewport_size())
            if report['viewport_size']!=CONFIG['resolution']:raise RuntimeError('Native viewport size differs')
            from restoration_terrain_audit import audit
            terrain=audit(pawn.get_world(),read(os.environ['SPACECENTER_RESTORE_MANIFEST']))
            write_json(OUT/'TerrainCollision.json',terrain)
            report['terrain_collision']={k:v for k,v in terrain.items() if k!='probes'}
            if terrain['result']!='passed':raise RuntimeError('Native terrain collision audit failed')
            # Import/PIE duplication leaves transient editor objects pending GC.
            # Release them before allocating full-resolution screenshot targets.
            unreal.SystemLibrary.collect_garbage()
            advance('camera')
        elif phase=='camera':
            if state['index']>=len(CONFIG['shots']):finish();return
            shot=CONFIG['shots'][state['index']];location=unreal.Vector(*shot['position'])
            camera=state['camera']
            camera.set_actor_location_and_rotation(location,unreal.MathLibrary.find_look_at_rotation(location,unreal.Vector(*shot['target'])),False,True)
            if EDITOR_ONLY:unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).pilot_level_actor(camera)
            # Streaming uses the actual camera view target; allow shader/texture convergence before recording.
            advance('settle')
        elif phase=='settle' and elapsed>12:
            name=CONFIG['shots'][state['index']]['name']+'.png'
            if EDITOR_ONLY:
                state['screenshot_task']=unreal.AutomationLibrary.take_high_res_screenshot(*CONFIG['resolution'],str(OUT/name),state['camera'],delay=0.)
            else:console('HighResShot %dx%d filename=%s'%(*CONFIG['resolution'],OUT/name))
            advance('screenshot')
        elif phase=='screenshot' and elapsed>3:
            shot=CONFIG['shots'][state['index']];path=OUT/(shot['name']+'.png')
            if not path.is_file():return
            data=path.read_bytes()
            if data[:8]!=b'\x89PNG\r\n\x1a\n':raise RuntimeError('Invalid PNG '+str(path))
            size=list(struct.unpack('>II',data[16:24]))
            if size!=CONFIG['resolution']:raise RuntimeError('Capture resolution differs: '+str(size))
            report['native_render_resolution']=size
            report['captures'].append(shot|{'file':path.name,'sha256':sha(path),'size':size})
            write_json(OUT/'Report.json',report)
            print('RESTORE_CAPTURE',shot['name'],flush=True)
            unreal.SystemLibrary.collect_garbage()
            state['index']+=1;advance('camera')
    except Exception:finish(traceback.format_exc())


handle=unreal.register_slate_post_tick_callback(tick)
