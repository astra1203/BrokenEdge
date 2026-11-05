"""Prepare the military base without overwriting authored SpaceCenter gameplay.

Run after repair_soviet_windows.py, in a fresh editor commandlet. Generator-owned
actors use SMX_ labels. Re-runs preserve the map and rebuild only those actors.
"""
import unreal, json, math
from pathlib import Path

ROOT=Path(unreal.Paths.project_dir())
OUT=ROOT/'Saved/Extraction/SovietMilitary'
OUT.mkdir(parents=True,exist_ok=True)
SOURCE='/Game/UVB80/Levels/UVB80'
LEVEL='/Game/Maps/SovietMilitary/L_SovietMilitaryExtraction'
DEFINITION='/Game/Extraction/Maps/DA_SovietMilitaryGameplay'
lib=unreal.EditorAssetLibrary
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
sml=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.EditorStaticMeshLibrary
tools=unreal.AssetToolsHelpers.get_asset_tools()
helper=unreal.SovietMilitaryBuilder

def save(asset):
    assert lib.save_loaded_asset(asset,only_if_is_dirty=False), asset.get_path_name()

def data(path, cls):
    if lib.does_asset_exist(path): return unreal.load_asset(path)
    factory=unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class',cls)
    return tools.create_asset(path.rsplit('/',1)[1],path.rsplit('/',1)[0],cls,factory)

if not lib.does_asset_exist(LEVEL):
    assert lib.duplicate_asset(SOURCE,LEVEL)
assert helper.configure_backdrop(3000.0)>0
assert levels.load_level(LEVEL)
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property('default_game_mode',unreal.ExtractionGameMode)
unreal.StingerAssetLibrary.finish_validation_asset_compilation()

# Static structural meshes need pawn and weapon collision; decorative clutter stays unchanged.
fixed=[]
for path in lib.list_assets('/Game/UVB80',recursive=True,include_folder=False):
    name=path.rsplit('/',1)[-1].split('.')[0]
    if not name.startswith('SM_'): continue
    structural=any(k in name for k in ('Wall','Floor','Stair','Platform','Hangar','Building','Road','Rock','Doorframe','WindowFrame','Fence'))
    if not structural and name!='SM_Door': continue
    mesh=unreal.load_asset(path)
    if not isinstance(mesh,unreal.StaticMesh): continue
    if name=='SM_Door':
        sml.remove_collisions(mesh)
        sml.add_simple_collisions(mesh,unreal.ScriptCollisionShapeType.BOX)
        mesh.get_editor_property('body_setup').set_editor_property('collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
    else:
        # Preserve door/window holes instead of filling architecture with an enclosing box.
        mesh.get_editor_property('body_setup').set_editor_property('collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    assert helper.refresh_collision(mesh), path
    save(mesh); fixed.append(path)

door_count=0
for a in list(actors.get_all_level_actors()):
    if isinstance(a,unreal.PlayerStart) or a.get_actor_label().startswith('SMX_') and not isinstance(a,unreal.ExtractionDoor):
        actors.destroy_actor(a)
        continue
    if a.get_class().get_name() not in ('BP_DoorActorModular_C','BP_DoorActor_C'): continue
    components=a.get_components_by_class(unreal.StaticMeshComponent)
    for c in components:
        if not c.static_mesh: continue
        if c.static_mesh.get_name()=='SM_Door':
            door=actors.spawn_actor_from_class(unreal.ExtractionDoor,c.get_world_location(),c.get_world_rotation())
            door.set_actor_label('SMX_Door_'+a.get_name())
            door.set_actor_scale3d(c.get_world_scale())
            door.get_editor_property('door').set_static_mesh(c.static_mesh)
            for i in range(c.get_num_materials()): door.get_editor_property('door').set_material(i,c.get_material(i))
            door.set_editor_property('open_sound',unreal.load_asset('/Game/UVB80/Assets/UVB80/Buildings/Door/S_DoorOpen'))
            door.set_editor_property('close_sound',unreal.load_asset('/Game/UVB80/Assets/UVB80/Buildings/Door/S_DoorClose'))
            door_count+=1
        else:
            frame=actors.spawn_actor_from_class(unreal.StaticMeshActor,c.get_world_location(),c.get_world_rotation())
            frame.set_actor_label('SovietDoorFrame_'+a.get_name())
            frame.set_actor_scale3d(c.get_world_scale())
            frame.static_mesh_component.set_static_mesh(c.static_mesh)
            frame.static_mesh_component.set_collision_profile_name('BlockAll')
            for i in range(c.get_num_materials()): frame.static_mesh_component.set_material(i,c.get_material(i))
    actors.destroy_actor(a)

# Keep the playable perimeter inside the landscape; exits remain on the inner side.
cube=unreal.load_asset('/Engine/BasicShapes/Cube')
for label,p,scale in [
    ('North',(15050,0,0),(1,302,100)),('South',(-15050,0,0),(1,302,100)),
    ('East',(0,15050,0),(302,1,100)),('West',(0,-15050,0),(302,1,100))]:
    boundary=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(*p))
    boundary.set_actor_label('SMX_Boundary_'+label)
    boundary.set_actor_scale3d(unreal.Vector(*scale))
    boundary.static_mesh_component.set_static_mesh(cube)
    boundary.static_mesh_component.set_collision_profile_name('BlockAll')
    boundary.static_mesh_component.set_visibility(False)
    boundary.set_actor_hidden_in_game(True)

for a in actors.get_all_level_actors():
    if a.get_class().get_name()=='BP_TreesBackdropActor_C':
        for c in a.get_components_by_class(unreal.StaticMeshComponent):
            if c.static_mesh and c.static_mesh.get_name()=='SM_TreesBackdrop':
                c.set_editor_property('min_draw_distance',3000.0)
    if isinstance(a,unreal.NavMeshBoundsVolume):
        # Surveyed source bounds were only 154 m wide; include perimeter entry/exit routes.
        o,e=a.get_actor_bounds(False)
        scale=a.get_actor_scale3d()
        a.set_actor_scale3d(unreal.Vector(scale.x*15000/e.x,scale.y*15000/e.y,scale.z*5000/e.z))
        a.set_actor_location(unreal.Vector(0,0,500),False,False)
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        if c.static_mesh and any(k in c.static_mesh.get_name() for k in ('Wall','Floor','Stair','Platform','Building','Doorframe')):
            c.set_collision_profile_name('BlockAll')
assert levels.save_current_level()
# Reload physics after updating collision assets.
assert levels.load_level('/Game/Maps/Lobby')
assert levels.load_level(LEVEL)
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert helper.build_navigation(world), 'Navigation could not be built'
for a in actors.get_all_level_actors():
    if isinstance(a,unreal.RecastNavMesh): a.set_editor_property('runtime_generation',unreal.RuntimeGenerationType.DYNAMIC)

def xyz(v): return [round(v.x,3),round(v.y,3),round(v.z,3)]
door_locations=[a.get_actor_location() for a in actors.get_all_level_actors() if isinstance(a,unreal.ExtractionDoor)]
def ground(x,y,z=300):
    p=helper.probe_ground(world,unreal.Vector(x,y,z),True)
    if p.z < -1e8: return None
    # Keep containers and patrol parking clear of both doorway approaches and the full door swing.
    if any(math.hypot(p.x-d.x,p.y-d.y)<350 and abs(p.z-d.z)<200 for d in door_locations): return None
    return p
anchor=None
def connected(p):
    if anchor is None or (anchor-p).length()<1: return True
    path=unreal.NavigationSystemV1.find_path_to_location_synchronously(world,anchor,p)
    return bool(path and path.is_valid() and not path.is_partial())

def nearest(x,y,z=300,avoid=(),spacing=220):
    for radius in range(0,2001,100):
        n=1 if radius==0 else max(8,int(radius/60))
        for i in range(n):
            a=2*math.pi*i/n
            p=ground(x+radius*math.cos(a),y+radius*math.sin(a),z)
            if p and all(math.hypot(p.x-q.x,p.y-q.y)>spacing for q in avoid) and connected(p): return p
    raise RuntimeError('No standable navigable ground near %s'%((x,y,z),))

definition=data(DEFINITION,unreal.ExtractionMapDefinition)
definition.set_editor_property('map_id','SovietMilitary')
definition.set_editor_property('display_name','苏联军事基地')
definition.set_editor_property('level',unreal.load_asset(LEVEL))
definition.set_editor_property('guard_count',6)
definition.set_editor_property('reference_build','Project layout / Soviet Military UVB80 / UE 5.8.3')
definition.set_editor_property('source_to_world',unreal.Transform())
definition.set_editor_property('overview_min',unreal.Vector2D(-15000,-15000))
definition.set_editor_property('overview_max',unreal.Vector2D(15000,15000))

groups=[]; used=[]
for i,(x,y) in enumerate([(-10500,-8500),(-10500,8500),(10500,8500),(10500,-8500)]):
    center=nearest(x,y,900)
    if anchor is None: anchor=center
    positions=[]
    for dx,dy in [(0,0),(180,0),(0,180)]:
        p=nearest(center.x+dx,center.y+dy,center.z+120,used,130)
        used.append(p);positions.append(unreal.Transform(location=p,rotation=unreal.Rotator(yaw=math.degrees(math.atan2(-p.y,-p.x)))))
    g=unreal.ExtractionSpawnGroup()
    g.set_editor_property('id','SovietSpawn%d'%i)
    g.set_editor_property('positions',positions)
    g.set_editor_property('reference','Project-authored; ground, capsule and navigation probes passed')
    groups.append(g)
definition.set_editor_property('spawn_groups',groups)

exits=[]; exit_points=[]
for i,(x,y,label) in enumerate([(0,12000,'东侧林道'),(-12500,0,'南侧公路'),(11500,-3500,'北侧山路')]):
    p=nearest(x,y,900,used,1500);exit_points.append(p)
    e=unreal.ExtractionExitDefinition()
    e.set_editor_property('id','SovietExit%d'%i);e.set_editor_property('label',label)
    e.set_editor_property('transform',unreal.Transform(location=p))
    e.set_editor_property('half_extent',unreal.Vector(300,300,140))
    e.set_editor_property('hold_seconds',10)
    e.set_editor_property('reference','Project fixed extraction, available throughout the raid')
    exits.append(e)
definition.set_editor_property('exits',exits)

recipes=[]
for name,items in [('Supply',[('Ammo',60),('Bandage',1)]),('Medical',[('Medkit',1),('Bandage',1),('Splint',1)]),('Gear',[('Helmet2',1),('AmmoAP',30)]),('Valuable',[('Electronics',1),('Tools',1)])]:
    recipe=data('/Game/Extraction/Loot/DA_Soviet'+name,unreal.ExtractionLootRecipe)
    stacks=[]
    for item,count in items:
        stack=unreal.ExtractionLootStack();stack.set_editor_property('item',item);stack.set_editor_property('quantity',count);stacks.append(stack)
    recipe.set_editor_property('items',stacks);save(recipe);recipes.append(recipe)

layout=data('/Game/Extraction/Loot/DA_SovietMilitaryLoot',unreal.ExtractionLootLayout)
layout.set_editor_property('variant','SovietMilitary')
layout.set_editor_property('loose_spawn_count',10)
points=[]; loot_positions=[]
# Surveyed building/yard locations. A floor-height hint prevents placing loot on roofs.
areas=[(-4700,1400,250),(-1700,5300,250),(500,-6000,250),(5000,2000,200),(6100,4900,200),(-4500,4200,200)]
box=unreal.load_asset('/Game/Extraction/Loot/Models/props_collect_box_546_3P/props_collect_box_546_3P/SkeletalMeshes/props_collect_box_546_3P')
assert box
for area,(x,y,z) in enumerate(areas):
    for j in range(4):
        p=nearest(x+(j%2)*350,y+(j//2)*350,z,loot_positions,250);loot_positions.append(p)
        point=unreal.ExtractionLootPoint();point.set_editor_property('id','SovietBox%02d'%(area*4+j))
        point.set_editor_property('transform',unreal.Transform(location=p+unreal.Vector(0,0,5)))
        point.set_editor_property('label',['补给箱','医疗箱','装备箱','物资箱'][j])
        point.set_editor_property('kind','SovietContainer');point.set_editor_property('appearance',box)
        point.set_editor_property('recipe',recipes[j]);point.set_editor_property('reference','Project authored / surveyed building floor')
        points.append(point)
for j in range(20):
    x,y,z=areas[j%len(areas)]
    p=nearest(x-450+(j//6)*250,y-350,z,loot_positions,170);loot_positions.append(p)
    point=unreal.ExtractionLootPoint();point.set_editor_property('id','SovietLoose%02d'%j)
    point.set_editor_property('transform',unreal.Transform(location=p+unreal.Vector(0,0,10)))
    point.set_editor_property('kind','Loose');point.set_editor_property('recipe',recipes[j%4]);points.append(point)
layout.set_editor_property('points',points);save(layout)
definition.set_editor_property('loot_layout',layout);save(definition)

guard_positions=[]
for i,(x,y,z) in enumerate(areas):
    p=nearest(x+600,y+700,z,loot_positions,250);guard_positions.append(p)
    route=actors.spawn_actor_from_class(unreal.ExtractionPatrolRoute,p)
    route.set_actor_label('SMX_Patrol%02d'%i)
    route.set_editor_property('points',[p,nearest(p.x+300,p.y,p.z+80),nearest(p.x,p.y+300,p.z+80)])

space=unreal.load_asset('/Game/Extraction/Maps/DA_SpaceCenterGameplay')
space.set_editor_property('display_name','航天基地')
space.set_editor_property('level',unreal.load_asset('/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'))
save(space)
assert helper.build_navigation(world)
assert levels.save_current_level()
report=dict(map=LEVEL,collision_assets=fixed,new_doors=door_count,total_doors=sum(isinstance(a,unreal.ExtractionDoor) for a in actors.get_all_level_actors()),
    spawns=[xyz(p) for p in used],exits=[xyz(p) for p in exit_points],loot=[xyz(p) for p in loot_positions],guards=[xyz(p) for p in guard_positions])
(OUT/'Preparation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('SOVIET_PREPARED '+json.dumps({k:len(v) for k,v in report.items() if isinstance(v,list)}))
