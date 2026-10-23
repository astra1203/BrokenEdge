"""Replace two colliding legacy points; preserve their explicitly temporary status."""
import json
from pathlib import Path
import unreal

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert 'L_SpaceCenterExtraction' in world.get_name()
asset = unreal.load_asset('/Game/Extraction/Maps/DA_SpaceCenterGameplay')
groups = list(asset.spawn_groups)
fixes = {'LegacyGroup1': ((-1700,-300),(-1500,-600)),
         'LegacyGroup2': ((2200,4200),(1800,3800))}
report = []
for group in groups:
    old,new = fixes[str(group.id)]
    positions = list(group.positions)
    for i,t in enumerate(positions):
        if (t.translation.x,t.translation.y) == old:
            t.translation = unreal.Vector(*new,600)
            positions[i] = t
    for t in positions:
        hit = unreal.StingerAssetLibrary.trace_traversal_surface(world,t.translation,t.translation-unreal.Vector(0,0,592))
        assert hit and hit[1].z >= .7, str(t)
        center = hit[0]+unreal.Vector(0,0,95)
        assert unreal.StingerAssetLibrary.traversal_capsule_clear(world,center,center+unreal.Vector(0,0,.01),34,92), str(t)
        report.append({'group':str(group.id),'position':[t.translation.x,t.translation.y,t.translation.z],
                       'floor':[hit[0].x,hit[0].y,hit[0].z],'radius':34,'half_height':92})
    group.set_editor_property('positions',positions)
    group.set_editor_property('reference','Legacy fixture, two obstructed points corrected by editor capsule queries; NOT official locations')
asset.set_editor_property('spawn_groups',groups)
assert unreal.EditorAssetLibrary.save_loaded_asset(asset)
out = Path(unreal.Paths.project_saved_dir())/'Extraction/SpaceCenterExpansion01/SpawnCollisionAudit.json'
out.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print('TEMPORARY_SPAWNS_CHECKED',len(report))
