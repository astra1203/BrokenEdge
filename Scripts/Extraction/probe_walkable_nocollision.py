"""43 号：全图找"看得见、像能踩、但不挡角色"的结构件：组件/实例没有碰撞（或不挡 Pawn），
且从它顶面往下打射线，最近的碰撞面比顶面低 60 cm 以上（踩上去会掉）。只看结构类网格，排除植被/贴花/杂物。"""
import unreal, re, json
L = lambda s: unreal.log('WALKNC ' + s)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/SpaceCenter/L_SpaceCenterExtraction')
unreal.StingerAssetLibrary.finish_validation_asset_compilation()
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
SKIP = re.compile(r'Decal|Grass|Leaves|Palm|Tree|Buxus|Daisy|Laurestine|Yucca|Zoysia|Juniper|Celosia|Litter|Paper|Cable|Bullet|Debris|DECOR|_BS_|_GR_|_TR_|Foliage|Weed|Moss|Ivy|Vine|Light|Lamp|Cloth|Wire|Rope|Glass|Window|Sign|Poster|Sky|Cloud|Water|Fog|Shadow|Plant|Flower(?!Bed)', re.I)
WANT = re.compile(r'Stair|Step|Bridge|Floor|Ground|Platform|Ramp|Deck|Walk|Corridor|Plank|Board|Slab|Road|Roof|Terrace|Balcony|Ladder|Stage|Base|Pier|Dock|Pavement|Tile|Concrete|Building|Struct', re.I)
found = []
def blocks(c):
    return c.get_collision_enabled() in (unreal.CollisionEnabled.QUERY_AND_PHYSICS, unreal.CollisionEnabled.QUERY_ONLY) and \
        c.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN) == unreal.CollisionResponseType.ECR_BLOCK
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        mesh = c.get_editor_property('static_mesh')
        if not mesh: continue
        name = mesh.get_name()
        if SKIP.search(name) or not WANT.search(name): continue
        desc = None
        if blocks(c):
            if not isinstance(c, unreal.InstancedStaticMeshComponent): continue
            desc = unreal.StingerAssetLibrary.describe_component_collision(c, unreal.Vector(), unreal.Vector(0, 0, 1))
            if 'physicsState=1' in desc and 'disableCollision=0' in desc: continue
        b = mesh.get_bounds()
        if b.box_extent.x * b.box_extent.y < 2500 and b.box_extent.y * b.box_extent.z < 2500: continue
        transforms = [c.get_instance_transform(i, True) for i in range(c.get_instance_count())] if isinstance(c, unreal.InstancedStaticMeshComponent) else [c.get_world_transform()]
        for i, t in enumerate(transforms):
            o = t.transform_location(b.origin)
            sc = t.scale3d
            ex, ey, ez = abs(b.box_extent.x * sc.x), abs(b.box_extent.y * sc.y), abs(b.box_extent.z * sc.z)
            top = o.z + max(ez, 1)
            if top > 6000 or top < -2000: continue
            # 顶面附近 3x3 点打射线：碰撞面普遍比顶面低很多就算"会掉"
            low = 0; tested = 0
            for fx in (-.5, 0, .5):
                for fy in (-.5, 0, .5):
                    p = unreal.Vector(o.x + fx * ex, o.y + fy * ey, top + 30)
                    r = unreal.SystemLibrary.line_trace_single(world, p, p - unreal.Vector(0, 0, 2000), unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [], unreal.DrawDebugTrace.NONE, True)
                    tested += 1
                    hz = r.to_tuple()[4].z if r else -1e9
                    if hz < top - 60: low += 1
            if low >= 5:
                found.append({'actor': a.get_actor_label(), 'comp': c.get_name(), 'mesh': name, 'path': mesh.get_path_name(), 'inst': i if len(transforms) > 1 else -1,
                              'at': [round(o.x), round(o.y), round(o.z)], 'ext': [round(ex), round(ey), round(ez)], 'low': low,
                              'coll': str(c.get_collision_enabled()), 'desc': desc or ''})
found.sort(key=lambda f: -f['ext'][0] * f['ext'][1])
json.dump(found, open(unreal.Paths.project_saved_dir() + 'Extraction/walkable_nocollision.json', 'w'), ensure_ascii=False, indent=0)
L('found %d' % len(found))
