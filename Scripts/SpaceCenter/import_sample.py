"""Independent UE Python commandlet: import sample, build PBR materials, place actors.

Only generated SpaceCenter assets are created. Hand placed actors and explicit
overrides survive a repeat assembly. The original cooked files stay outside Content.
"""
import hashlib
import json
import os
import struct
import zlib
from pathlib import Path
import traceback
import unreal

BASE_WORK = Path(unreal.Paths.project_saved_dir()) / 'SpaceCenter'
WORK = Path(os.environ.get('SPACECENTER_WORK', str(BASE_WORK))).resolve()
if not WORK.is_relative_to(BASE_WORK.resolve()):
    raise ValueError('Import work directory must stay inside Saved/SpaceCenter')
ROOT = '/Game/Maps/SpaceCenter'
MAP = ROOT + '/L_SpaceCenterSample'
MANIFEST = json.loads((WORK / 'ImportManifest.json').read_text(encoding='utf-8'))
LIB = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EDIT = unreal.MaterialEditingLibrary
STATE_FILE = WORK / 'ImportedAssets.json'
state = json.loads(STATE_FILE.read_text(encoding='utf-8')) if STATE_FILE.exists() else (
    json.loads((BASE_WORK / 'ImportedAssets.json').read_text(encoding='utf-8')) if WORK != BASE_WORK else {})
report = {'assets': [], 'actors': [], 'errors': []}


def save_json(path, data):
    path.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')


def import_file(entry, kind):
    key = kind + ':' + entry['source']
    digest = hashlib.sha256(Path(entry['file']).read_bytes()).hexdigest()
    previous = state.get(key)
    if previous and previous['sha256'] == digest and LIB.does_asset_exist(previous['asset']):
        return LIB.load_asset(previous['asset'])
    if previous:
        raise RuntimeError('Changed source requires reviewed regeneration: ' + key)
    folder = ROOT + '/Assets/' + entry['id']
    task = unreal.AssetImportTask()
    task.filename = entry['file']
    task.destination_path = folder
    task.automated = True
    task.save = True
    task.replace_existing = False
    TOOLS.import_asset_tasks([task])
    cls = unreal.StaticMesh if kind == 'mesh' else unreal.Texture2D
    objects = [LIB.load_asset(p) for p in LIB.list_assets(folder, recursive=True)]
    matches = [o for o in objects if isinstance(o, cls)]
    if len(matches) != 1:
        raise RuntimeError('Expected one ' + kind + ' in ' + folder + ', got ' + str(len(matches)))
    asset = matches[0]
    if kind == 'mesh':
        body = asset.get_editor_property('body_setup')
        if body:
            body.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        LIB.save_loaded_asset(asset)
    state[key] = {'asset': asset.get_path_name(), 'sha256': digest}
    report['assets'].append(state[key])
    save_json(STATE_FILE, state)
    print('SPACE_IMPORTED', kind, entry['source'], flush=True)
    return asset


def expression(material, cls, x=0, y=0, **props):
    obj = EDIT.create_material_expression(material, cls, x, y)
    for key, val in props.items():
        obj.set_editor_property(key, val)
    return obj


def master(glass=False):
    name = 'M_SC_Glass' if glass else 'M_SC_Surface'
    path = ROOT + '/Materials/' + name
    if LIB.does_asset_exist(path) and state.get('master_version:' + name) == 2:
        mat = LIB.load_asset(path)
        if not glass and not mat.get_editor_property('used_with_nanite'):
            mat.set_editor_property('used_with_nanite', True)
            EDIT.recompile_material(mat)
            LIB.save_loaded_asset(mat)
        return mat
    mat = LIB.load_asset(path) if LIB.does_asset_exist(path) else TOOLS.create_asset(name, ROOT + '/Materials', unreal.Material, unreal.MaterialFactoryNew())
    EDIT.delete_all_material_expressions(mat)
    mat.set_editor_property('two_sided', True)
    mat.set_editor_property('used_with_nanite', not glass)
    if glass:
        mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    uv = expression(mat, unreal.MaterialExpressionTextureCoordinate, -800, -150)
    tiling = expression(mat, unreal.MaterialExpressionScalarParameter, -800, 0, parameter_name='UVScale', default_value=1.0)
    multiply_uv = expression(mat, unreal.MaterialExpressionMultiply, -600, -150)
    EDIT.connect_material_expressions(uv, '', multiply_uv, 'A')
    EDIT.connect_material_expressions(tiling, '', multiply_uv, 'B')
    base = expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -400, -200,
                      parameter_name='BaseTex', texture=LIB.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
    EDIT.connect_material_expressions(multiply_uv, '', base, 'UVs')
    tint = expression(mat, unreal.MaterialExpressionVectorParameter, -400, 0,
                      parameter_name='Tint', default_value=unreal.LinearColor(1, 1, 1, 1))
    multiply = expression(mat, unreal.MaterialExpressionMultiply, -150, -100)
    EDIT.connect_material_expressions(base, 'RGB', multiply, 'A')
    EDIT.connect_material_expressions(tint, 'RGB', multiply, 'B')
    EDIT.connect_material_property(multiply, '', unreal.MaterialProperty.MP_BASE_COLOR)
    normal = expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -400, 220,
                        parameter_name='NormalTex', texture=constant_texture('NeutralNormal', (128, 128, 255), True),
                        sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    EDIT.connect_material_expressions(multiply_uv, '', normal, 'UVs')
    EDIT.connect_material_property(normal, 'RGB', unreal.MaterialProperty.MP_NORMAL)
    emission = expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -400, 500,
                          parameter_name='EmissionTex', texture=constant_texture('Black', (0, 0, 0)))
    EDIT.connect_material_expressions(multiply_uv, '', emission, 'UVs')
    EDIT.connect_material_property(emission, 'RGB', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    for parameter, default, prop in [('Roughness', .65, unreal.MaterialProperty.MP_ROUGHNESS),
                                    ('Metallic', 0., unreal.MaterialProperty.MP_METALLIC)]:
        e = expression(mat, unreal.MaterialExpressionScalarParameter, -100, 400,
                       parameter_name=parameter, default_value=default)
        EDIT.connect_material_property(e, '', prop)
    if glass:
        opacity = expression(mat, unreal.MaterialExpressionConstant, -100, 600, r=.22)
        EDIT.connect_material_property(opacity, '', unreal.MaterialProperty.MP_OPACITY)
    EDIT.recompile_material(mat)
    LIB.save_loaded_asset(mat)
    state['master_version:' + name] = 2
    save_json(STATE_FILE, state)
    return mat


def constant_texture(name, rgb, normal=False):
    # Numeric calibration textures, not substituted source artwork.
    path = WORK / (name + '.png')
    if not path.exists():
        def chunk(kind, data):
            return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
        path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 4, 4, 8, 2, 0, 0, 0)) +
                         chunk(b'IDAT', zlib.compress((b'\0' + bytes(rgb) * 4) * 4)) + chunk(b'IEND', b''))
    texture = import_file({'source': 'generated:' + name, 'file': str(path), 'id': name}, 'texture')
    if normal:
        texture.set_editor_property('srgb', False)
        texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        LIB.save_loaded_asset(texture)
    return texture


def material_matches(mi, info, textures):
    if mi.get_editor_property('parent') != master(info['glass']):
        return False
    for key, value in [('UVScale', info['uv_scale']), ('Roughness', info['roughness']), ('Metallic', info['metallic'])]:
        if abs(EDIT.get_material_instance_scalar_parameter_value(mi, key) - float(value)) > .00001:
            return False
    color = EDIT.get_material_instance_vector_parameter_value(mi, 'Tint')
    desired = (info['tint'] if info['channels']['base'] else [.55, .55, .55]) + [1]
    if any(abs(getattr(color, channel) - value) > .00001 for channel, value in zip('rgba', desired)):
        return False
    defaults = {'base': '/Engine/EngineResources/WhiteSquareTexture',
                'normal': state['texture:generated:NeutralNormal']['asset'],
                'emissive': state['texture:generated:Black']['asset']}
    for channel, parameter in [('base', 'BaseTex'), ('normal', 'NormalTex'), ('emissive', 'EmissionTex')]:
        choice = info['channels'][channel]
        wanted = textures[choice['texture']] if choice else LIB.load_asset(defaults[channel])
        if EDIT.get_material_instance_texture_parameter_value(mi, parameter) != wanted:
            return False
    return True


def run():
    meshes = {p: import_file(e, 'mesh') for p, e in MANIFEST['meshes'].items()}
    for mesh in meshes.values():
        body = mesh.get_editor_property('body_setup')
        if body and not body.get_editor_property('double_sided_geometry'):
            body.set_editor_property('double_sided_geometry', True)
            LIB.save_loaded_asset(mesh)
    textures = {p: import_file(e, 'texture') for p, e in MANIFEST['textures'].items()}
    master(False)
    master(True)
    mats = {}
    shared_materials = json.loads((BASE_WORK / 'ImportManifest.json').read_text(encoding='utf-8'))['materials'] if WORK != BASE_WORK else {}
    for p, info in MANIFEST['materials'].items():
        path = ROOT + '/Materials/MI_' + info['id']
        if p in shared_materials and LIB.does_asset_exist(path):
            mats[p] = LIB.load_asset(path)
            continue
        if LIB.does_asset_exist(path):
            mats[p] = LIB.load_asset(path)
            mi = mats[p]
            if WORK != BASE_WORK and material_matches(mi, info, textures):
                continue
        else:
            mi = TOOLS.create_asset('MI_' + info['id'], ROOT + '/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        EDIT.set_material_instance_parent(mi, master(info['glass']))
        for channel, parameter in [('base', 'BaseTex'), ('normal', 'NormalTex'), ('emissive', 'EmissionTex')]:
            choice = info['channels'][channel]
            if choice:
                texture = textures[choice['texture']]
                if channel == 'normal':
                    if texture.get_editor_property('srgb') or texture.get_editor_property('compression_settings') != unreal.TextureCompressionSettings.TC_NORMALMAP:
                        texture.set_editor_property('srgb', False)
                        texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
                        LIB.save_loaded_asset(texture)
                EDIT.set_material_instance_texture_parameter_value(mi, parameter, texture)
        color = info['tint'] if info['channels']['base'] else [.55, .55, .55]
        EDIT.set_material_instance_vector_parameter_value(mi, 'Tint', unreal.LinearColor(*color, 1))
        for key, value in [('UVScale', info['uv_scale']), ('Roughness', info['roughness']), ('Metallic', info['metallic'])]:
            EDIT.set_material_instance_scalar_parameter_value(mi, key, float(value))
        EDIT.update_material_instance(mi)
        LIB.save_loaded_asset(mi)
        mats[p] = mi
    if os.environ.get('SPACECENTER_ASSETS_ONLY') == '1':
        report['result'] = 'passed'
        report['mode'] = 'assets_only'
        save_json(STATE_FILE, state)
        save_json(WORK / 'ImportReport.json', report)
        print('SPACE_REGION_ASSETS_COMPLETE', len(meshes), flush=True)
        return
    editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if LIB.does_asset_exist(MAP):
        assert editor.load_level(MAP)
    else:
        assert editor.new_level(MAP)
    existing = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
    origin = MANIFEST['origin']
    overrides_path = Path(unreal.Paths.project_dir()) / 'Scripts/SpaceCenter/overrides.json'
    overrides = json.loads(overrides_path.read_text(encoding='utf-8')) if overrides_path.exists() else {}
    for source in MANIFEST['actors']:
        label = 'SC_' + source['id']
        actor = existing.get(label)
        if actor is None:
            actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector())
            actor.set_actor_label(label)
            actor.set_folder_path('SpaceCenter/Generated')
        if source['id'] in overrides.get('preserve_actors', []):
            continue
        transform = source['transform']
        t, q, s = transform['Translation'], transform['Rotation'], transform['Scale3D']
        translation = [t[k] - origin[i] for i, k in enumerate(['X', 'Y', 'Z'])]
        offset = overrides.get('translation_offsets_cm', {}).get(source['id'], [0, 0, 0])
        translation = [a + b for a, b in zip(translation, offset)]
        actor.set_actor_location(unreal.Vector(*translation), False, True)
        actor.set_actor_rotation(unreal.Quat(q['X'], q['Y'], q['Z'], q['W']).rotator(), False)
        actor.set_actor_scale3d(unreal.Vector(s['X'], s['Y'], s['Z']))
        component = actor.static_mesh_component
        component.set_static_mesh(meshes[source['mesh']])
        component.set_mobility(unreal.ComponentMobility.STATIC)
        body = source.get('properties', {}).get('BodyInstance', {})
        collision = body.get('CollisionProfileName') != 'NoCollision' and body.get('CollisionEnabled') != 'ECollisionEnabled::NoCollision'
        component.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
        for i, mat in enumerate(source['materials']):
            if mat in mats:
                component.set_material(i, mats[mat])
        report['actors'].append({'id': source['id'], 'label': label, 'source': source['source_object'],
                                 'location': translation, 'mesh': meshes[source['mesh']].get_path_name()})
    def spawn_once(label, cls, location, rotation=None):
        obj = existing.get(label)
        if obj is None:
            obj = actors.spawn_actor_from_class(cls, unreal.Vector(*location), rotation or unreal.Rotator())
            obj.set_actor_label(label)
            obj.set_folder_path('SpaceCenter/Setup')
        return obj
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property('default_game_mode', LIB.load_blueprint_class('/Game/Characters/Stinger/Blueprints/BP_StingerTestGameMode'))
    ground = spawn_once('SC_RebuiltGround', unreal.StaticMeshActor, [1000, -500, 29])
    ground.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    ground.set_actor_scale3d(unreal.Vector(120, 130, .5))
    ground.static_mesh_component.set_collision_profile_name('BlockAll')
    ground_path = ROOT + '/Materials/MI_SC_RebuiltGround'
    if LIB.does_asset_exist(ground_path):
        ground_material = LIB.load_asset(ground_path)
    else:
        ground_material = TOOLS.create_asset('MI_SC_RebuiltGround', ROOT + '/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    EDIT.set_material_instance_parent(ground_material, master())
    texture_key = '/Game/Environment/Structures/HAAVK/00_Textures/SpaceCenter/HD/T_GEN_Concrete_Base_02_C_HD'
    EDIT.set_material_instance_texture_parameter_value(ground_material, 'BaseTex', textures[texture_key])
    EDIT.set_material_instance_scalar_parameter_value(ground_material, 'UVScale', 60.)
    EDIT.set_material_instance_scalar_parameter_value(ground_material, 'Roughness', .9)
    EDIT.update_material_instance(ground_material)
    LIB.save_loaded_asset(ground_material)
    ground.static_mesh_component.set_material(0, ground_material)
    forecourt = spawn_once('SC_RebuiltForecourt', unreal.StaticMeshActor, [2500, 4050, 208])
    forecourt.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    forecourt.set_actor_scale3d(unreal.Vector(50, 29, 3.08))
    forecourt.static_mesh_component.set_collision_profile_name('BlockAll')
    forecourt.static_mesh_component.set_material(0, ground_material)
    post = spawn_once('SC_Exposure', unreal.PostProcessVolume, [0, 0, 0])
    post.set_editor_property('unbound', True)
    settings = post.get_editor_property('settings')
    settings.set_editor_property('override_auto_exposure_bias', True)
    settings.set_editor_property('auto_exposure_bias', -1.)
    settings.set_editor_property('override_bloom_intensity', True)
    settings.set_editor_property('bloom_intensity', .15)
    post.set_editor_property('settings', settings)
    spawn_once('SC_PlayerStart_1', unreal.PlayerStart, [-1500, -300, 600])
    spawn_once('SC_PlayerStart_2', unreal.PlayerStart, [-1500, 0, 600])
    weapon_class = LIB.load_blueprint_class('/Game/Blueprints/Weapon/BP_AK47')
    spawn_once('SC_AK_1', weapon_class, [-1480, -300, 406])
    spawn_once('SC_AK_2', weapon_class, [-1480, 0, 406])
    sun = spawn_once('SC_Sun', unreal.DirectionalLight, [0, 0, 6000], unreal.Rotator(pitch=-48, yaw=-35))
    sun_component = sun.get_component_by_class(unreal.DirectionalLightComponent)
    sun_component.set_editor_property('intensity', 5.)
    sun_component.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
    sky = spawn_once('SC_Sky', unreal.SkyLight, [0, 0, 4000])
    sky.light_component.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property('intensity', 1.)
    sky.light_component.set_editor_property('real_time_capture', True)
    spawn_once('SC_Atmosphere', unreal.SkyAtmosphere, [0, 0, 0])
    for i, xy in enumerate([[-1800, -100], [0, -100], [1700, -1500]]):
        light = spawn_once('SC_InteriorLight_' + str(i), unreal.PointLight, [*xy, 700])
        comp = light.get_component_by_class(unreal.PointLightComponent)
        comp.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
        comp.set_editor_property('intensity', 2000.)
        comp.set_editor_property('attenuation_radius', 1800.)
    assert editor.save_current_level()
    report['result'] = 'passed'
    save_json(WORK / 'ImportReport.json', report)
    print('SPACE_IMPORT_COMPLETE', len(report['actors']), flush=True)


try:
    run()
except Exception:
    report['result'] = 'failed'
    report['errors'].append(traceback.format_exc())
    save_json(WORK / 'ImportReport.json', report)
    raise
