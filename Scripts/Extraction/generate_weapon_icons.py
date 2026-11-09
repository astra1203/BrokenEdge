"""33 号：每把枪用自己的 3D 枪模侧视图生成图标（原来所有枪共用一个通用步枪剪影）。

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript -script=<绝对路径> -NullRHI -unattended -nosound

光栅化在 UStingerAssetLibrary::RenderWeaponIconCanvas（项目自写），只用工程里的枪模；图标 T_ItemIcon_W_<物品名>。
generate_original_icons.py 重跑时会保留这些枪械图标。

画布固定 512×256（2 的幂），枪等比缩放居中、四周透明，贴图生成 mipmap：背包格子里缩到几十像素时
用低级 mip 采样，不会出现枪托、枪管的锯齿毛刺。界面按 GetImportedSize 的宽高比显示。"""
import unreal
from pathlib import Path
lib = unreal.EditorAssetLibrary
ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
PNG = ROOT / 'Saved/Extraction/WeaponIcons'
FOLDER = '/Game/Extraction/Items/Icons'
PNG.mkdir(parents=True, exist_ok=True)
L = lambda s: unreal.log('WEAPONICON ' + str(s))
done = []
for path in lib.list_assets('/Game/Extraction/Items', recursive=False):
    d = unreal.load_asset(path)
    if not isinstance(d, unreal.ExtractionItemDefinition) or d.get_editor_property('kind').name.upper() != 'WEAPON':
        continue
    cls = d.get_editor_property('weapon_class')
    if not cls:
        L('skip (no class) ' + d.get_name()); continue
    cdo = unreal.get_default_object(cls)
    mesh = cdo.get_editor_property('weapon_mesh').get_editor_property('skeletal_mesh_asset')
    if not mesh:
        L('skip (no mesh) ' + d.get_name()); continue
    name = d.get_name().replace('DA_Item_', '')
    file = PNG / (name + '.png')
    width = unreal.StingerAssetLibrary.render_weapon_icon_canvas(mesh, str(file), 512, 256, .05)
    if not width:
        L('render failed ' + name); continue
    task = unreal.AssetImportTask()
    task.filename = str(file); task.destination_path = FOLDER; task.destination_name = 'T_ItemIcon_W_' + name
    task.automated = True; task.replace_existing = True; task.save = False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = unreal.load_asset('%s/T_ItemIcon_W_%s' % (FOLDER, name))
    tex.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
    tex.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)
    tex.set_editor_property('never_stream', True)
    tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    assert lib.save_loaded_asset(tex, only_if_is_dirty=False)
    d.set_editor_property('icon', tex)
    assert lib.save_loaded_asset(d, only_if_is_dirty=False)
    done.append('%s(%s,%dpx)' % (name, mesh.get_name(), width))
L('done ' + ' '.join(done))
