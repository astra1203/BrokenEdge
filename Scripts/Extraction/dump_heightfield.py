"""导出可玩区原始高度图（1024²，float32，北在上）到 Saved/Extraction/Overview/SpaceCenter.r32，供河道等分析。"""
import unreal
from pathlib import Path
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/SpaceCenter/L_SpaceCenterExtraction')
world = unreal.EditorLevelLibrary.get_editor_world()
da = unreal.load_asset('/Game/Extraction/Maps/DA_SpaceCenterGameplay')
mn, mx = da.get_editor_property('overview_texture_min'), da.get_editor_property('overview_texture_max')
out = Path(unreal.Paths.project_saved_dir()) / 'Extraction' / 'Overview' / 'SpaceCenter.r32'
n = unreal.StingerAssetLibrary.render_level_overview(world, mn, mx, 1024, str(out))
unreal.log('HEIGHTFIELD %d %s %s' % (n, mn, mx))
