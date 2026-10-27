"""Import verified safe models and audit source points against restored collision; never move a point."""
import json
from pathlib import Path
import unreal

root=Path(unreal.Paths.project_dir());work=root/'Saved/Extraction/LootSources'
manifest=json.loads((work/'Manifest.json').read_text(encoding='utf-8'))
lib=unreal.EditorAssetLibrary;tools=unreal.AssetToolsHelpers.get_asset_tools()
meshes={}
for name,relative in manifest['models'].items():
    folder='/Game/Extraction/Loot/Models/'+name
    if not lib.does_directory_exist(folder):
        task=unreal.AssetImportTask();task.filename=str(work/relative);task.destination_path=folder
        task.automated=True;task.save=True;tools.import_asset_tasks([task])
    candidates=[unreal.load_asset(p) for p in lib.list_assets(folder,recursive=True) if '/SkeletalMeshes/' in p]
    meshes[name]=next(m for m in candidates if isinstance(m,unreal.SkeletalMesh))

levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
validation={p['id']:[] for p in manifest['points']}
for level in ['/Game/Maps/SpaceCenter/L_SpaceCenterExtraction','/Game/Maps/SpaceCenter/L_SpaceCenterNorth','/Game/Maps/SpaceCenter/L_SpaceCenterEast']:
    if not lib.does_asset_exist(level):continue
    assert levels.load_level(level)
    unreal.StingerAssetLibrary.finish_validation_asset_compilation()
    world=editor.get_editor_world()
    for point in manifest['points']:
        p=unreal.Vector(*[point['world_position'][k] for k in ('X','Y','Z')])
        hit=unreal.StingerAssetLibrary.trace_traversal_surface(world,p+unreal.Vector(0,0,30),p-unreal.Vector(0,0,120))
        if len(hit)==2 and hit[1].z>.6:
            validation[point['id']].append({'level':level,'floor_z':hit[0].z,'distance':p.z-hit[0].z})

layouts={};report=[]
recipes={}
for kind,quantity in [('LargeSafe',3),('SmallSafe',1)]:
    name='DA_Project'+kind+'Recipe';path='/Game/Extraction/Loot/'+name
    recipe=unreal.load_asset(path) if lib.does_asset_exist(path) else None
    if not recipe:
        factory=unreal.DataAssetFactory();factory.set_editor_property('data_asset_class',unreal.ExtractionLootRecipe)
        recipe=tools.create_asset(name,'/Game/Extraction/Loot',unreal.ExtractionLootRecipe,factory)
    stacks=[]
    for item,count in [('Electronics',quantity),('Tools',1)]:
        stack=unreal.ExtractionLootStack();stack.set_editor_property('item',item);stack.set_editor_property('quantity',count);stacks.append(stack)
    recipe.set_editor_property('items',stacks);assert lib.save_loaded_asset(recipe);recipes[kind]=recipe
# 大小保险一律必定刷新：不再使用“N 选 1”兼容组，每个已启用点位都是固定点。
# 组语义仍保留在 UExtractionLootLayout 中（非空组按权重选一个），只有这里不赋值。
# 关掉分组的代价：原先 1/3、1/2 概率出现的总统室/H3 大保险柜，现在每局都在。
for variant in sorted({p['variant'] for p in manifest['points']}):
    folder='/Game/Extraction/Loot';name='DA_'+variant+'_Loot';path=folder+'/'+name
    layout=unreal.load_asset(path) if lib.does_asset_exist(path) else None
    if not layout:
        factory=unreal.DataAssetFactory();factory.set_editor_property('data_asset_class',unreal.ExtractionLootLayout)
        layout=tools.create_asset(name,folder,unreal.ExtractionLootLayout,factory)
    points=[]
    for row in manifest['points']:
        if row['variant']!=variant:continue
        p=unreal.ExtractionLootPoint();p.set_editor_property('id',row['id']);p.set_editor_property('kind',row['kind'])
        p.set_editor_property('label',row['label']);p.set_editor_property('appearance',meshes[row['model']])
        p.set_editor_property('recipe',recipes[row['kind']])
        rot=row['source_rotation'];scale=row['source_scale']
        p.set_editor_property('transform',unreal.Transform(
            location=unreal.Vector(*[row['world_position'][k] for k in ('X','Y','Z')]),
            rotation=unreal.Rotator(pitch=rot.get('Pitch',0),yaw=rot.get('Yaw',0),roll=rot.get('Roll',0)),
            scale=unreal.Vector(*[scale[k] for k in ('X','Y','Z')])))
        p.set_editor_property('enabled',bool(validation[row['id']]))
        # This authored actor is parked about 55m below the playable floor; collision alone is insufficient.
        if variant=='SpaceCenter_Ma1' and row['world_position']['Z'] < -1000:
            p.set_editor_property('enabled',False)
        p.set_editor_property('reference',row['package']+':'+row['actor']+'; fixed point, guaranteed spawn; compatibility drops')
        points.append(p)
        report.append({'id':row['id'],'variant':variant,'room':row['room'],'enabled':p.get_editor_property('enabled'),
                       'group':str(p.get_editor_property('group')),'group_status':'fixed (guaranteed spawn)', 'floor_checks':validation[row['id']]})
    layout.set_editor_property('variant',variant);layout.set_editor_property('points',points)
    layout.set_editor_property('group_policy','Ma1 source coordinates; every enabled safe is a fixed point and spawns in every raid (project tuning). Drops are project recipes.')
    assert lib.save_loaded_asset(layout)
    layouts[variant]=layout.get_path_name()
map_definition=unreal.load_asset('/Game/Extraction/Maps/DA_SpaceCenterGameplay')
assert map_definition
map_definition.set_editor_property('loot_layout',unreal.load_asset(layouts['SpaceCenter_Ma1']))
assert lib.save_loaded_asset(map_definition)
out=root/'Docs/Extraction/RestorationEvidence/Loot/PlacementAudit.json'
out.write_text(json.dumps({'layouts':layouts,'points':report,'runtime_enabled':True,'active_variant':'SpaceCenter_Ma1',
    'limits':'All enabled safes are fixed points and spawn every raid (project tuning, no one-of-N groups). Floor checks are not reachability or visual approval; unsupported and underground points stay disabled.'},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('LOOT_LAYOUTS_AUDITED',len(report),'floor-supported',sum(p['enabled'] for p in report))
