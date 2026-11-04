"""39 号：烘焙航天基地俯视地图，供小地图和 M 战术地图使用（项目自写光栅化，只用关卡里的几何）。

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript -script=<绝对路径> -NullRHI -unattended -nosound

范围取官方可玩区（源坐标经 SourceToWorld 换算）；结果导入 /Game/Extraction/Maps/T_SpaceCenterOverview，
并把纹理和范围写回 DA_SpaceCenterGameplay。"""
import os
import unreal
from pathlib import Path
L = lambda s: unreal.log('OVERVIEW ' + str(s))
LEVEL = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
DA = '/Game/Extraction/Maps/DA_SpaceCenterGameplay'
SIZE = int(os.environ.get('OVERVIEW_SIZE', '4096'))
lib = unreal.EditorAssetLibrary
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level(LEVEL)
world = unreal.EditorLevelLibrary.get_editor_world()
da = unreal.load_asset(DA)
# 官方可玩区（MapRegionData_SpaceCenter，见 7F 还原配置）的源坐标四角换算到世界，取外接正方形。
t = da.get_editor_property('source_to_world')
pts = [t.transform_location(unreal.Vector(x, y, 0)) for x in (635958, 703158) for y in (-486414, -411414)]
x0, x1 = min(p.x for p in pts), max(p.x for p in pts)
y0, y1 = min(p.y for p in pts), max(p.y for p in pts)
side = max(x1 - x0, y1 - y0)
cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
mn = unreal.Vector2D(cx - side / 2, cy - side / 2)
mx = unreal.Vector2D(cx + side / 2, cy + side / 2)
out = Path(unreal.Paths.project_saved_dir()) / 'Extraction' / 'Overview'
out.mkdir(parents=True, exist_ok=True)
png = out / 'SpaceCenterOverview.png'
n = unreal.StingerAssetLibrary.render_level_overview(world, mn, mx, SIZE, str(png))
L('components %d range %s %s' % (n, mn, mx))
assert n > 0
if os.environ.get('OVERVIEW_PREVIEW'):
    raise SystemExit
task = unreal.AssetImportTask()
task.filename = str(png); task.destination_path = '/Game/Extraction/Maps'; task.destination_name = 'T_SpaceCenterOverview'
task.automated = True; task.replace_existing = True; task.save = False
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
tex = unreal.load_asset('/Game/Extraction/Maps/T_SpaceCenterOverview')
tex.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
tex.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)
tex.set_editor_property('never_stream', True)
tex.set_editor_property('srgb', True)
assert lib.save_loaded_asset(tex, only_if_is_dirty=False)
da.set_editor_property('overview_texture', tex)
da.set_editor_property('overview_texture_min', mn)
da.set_editor_property('overview_texture_max', mx)
assert lib.save_loaded_asset(da, only_if_is_dirty=False)
L('saved')
