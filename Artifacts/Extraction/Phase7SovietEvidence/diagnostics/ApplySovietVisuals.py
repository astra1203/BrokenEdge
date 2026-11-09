import unreal, math
assert unreal.SovietMilitaryBuilder.configure_backdrop(3000.0)>0
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/SovietMilitary/L_SovietMilitaryExtraction')
n=0
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
 if a.get_class().get_name()=='BP_TreesBackdropActor_C':
  for c in a.get_components_by_class(unreal.StaticMeshComponent):
   if c.static_mesh and c.static_mesh.get_name()=='SM_TreesBackdrop':
    c.set_editor_property('min_draw_distance',3000.0);n+=1
assert levels.save_current_level()
d=unreal.load_asset('/Game/Extraction/Maps/DA_SovietMilitaryGameplay')
groups=list(d.spawn_groups)
for g in groups:
 positions=[]
 for t in g.positions:
  p=t.translation
  positions.append(unreal.Transform(location=p,rotation=unreal.Rotator(yaw=math.degrees(math.atan2(-p.y,-p.x)))))
 g.set_editor_property('positions',positions)
d.set_editor_property('spawn_groups',groups)
assert unreal.EditorAssetLibrary.save_loaded_asset(d,only_if_is_dirty=False)
print('SOVIET_VISUALS_SAVED',n)

