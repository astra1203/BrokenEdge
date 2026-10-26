"""Cold read-only verification of saved bindings, source tracks and opt-in assets."""
import json,math
from pathlib import Path
import unreal

work=Path(unreal.Paths.project_saved_dir())/'Stinger/Locomotion'
root='/Game/Characters/Stinger/Animations/DeltaForceLocomotion'
report={'checks':[],'sequences':[]}
def check(name,passed,detail=None):report['checks'].append(dict(name=name,passed=bool(passed),detail=detail))
manifest=json.loads((work/'Import.json').read_text(encoding='utf-8'))
for entry in manifest['entries'].values():
    for info in entry['sequences']:
        source=unreal.load_asset(info['assets']['Source'])
        mapped=unreal.load_asset(info['assets']['Mapped'])
        check(info['name']+'_load',source is not None and mapped is not None)
        check(info['name']+'_finite_duration',math.isfinite(mapped.get_play_length()) and mapped.get_play_length()>0)
        check(info['name']+'_no_gameplay_notifies',len(unreal.AnimationLibrary.get_animation_notify_events(mapped))==0)
        row=dict(name=info['name'],source=source.get_path_name(),mapped=mapped.get_path_name(),duration=mapped.get_play_length(),coverage=entry['coverage'])
        report['sequences'].append(row)
for name in ('BP_StingerCharacter','BP_StingerCharacter_Repaired'):
    bp=unreal.load_asset('/Game/Characters/Stinger/Blueprints/'+name)
    cdo=unreal.get_default_object(bp.generated_class())
    path=cdo.get_editor_property('mesh').get_editor_property('anim_class').get_path_name()
    check(name+'_saved_animation_class',root in path,path)
gun=unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class('/Game/Blueprints/Weapon/BP_AK47'))
profile=gun.get_editor_property('presentation')
movement=profile.get_editor_property('locomotion')
check('ak_opt_in',movement.get_path_name().startswith(root))
for field in ('standing','crouching','aim_offset','crouch_aim_offset','view_walk','view_run','view_sprint'):
    value=movement.get_editor_property(field)
    check(field+'_saved',value is not None,value.get_path_name() if value else None)
check('source_conversion_complete',all(x['Success'] for x in json.loads((work/'export/ExportResults.json').read_text(encoding='utf-8'))))
report['result']='passed' if all(x['passed'] for x in report['checks']) else 'failed'
(work/'Audit.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
assert report['result']=='passed',json.dumps([x for x in report['checks'] if not x['passed']])
print('LOCOMOTION_COLD_AUDIT',len(report['checks']))
