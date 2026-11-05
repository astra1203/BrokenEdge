"""Preserve the original opaque frame's Nanite settings after separating transparent panes."""
import json
from pathlib import Path
import unreal
root=Path(unreal.Paths.project_dir())
report=json.loads((root/'Saved/Extraction/Fix5767Glass/Report.json').read_text(encoding='utf8'))
rows=[]
for key,part in report['meshes'].items():
    if not part['frame']:continue
    source=unreal.load_asset(key.split('[')[0])
    frame=unreal.load_asset(part['frame'])
    settings=source.get_editor_property('nanite_settings')
    was=frame.get_editor_property('nanite_settings').enabled
    if was!=settings.enabled:
        frame.set_editor_property('nanite_settings',settings)
        assert unreal.EditorAssetLibrary.save_loaded_asset(frame)
    rows.append(dict(source=source.get_path_name(),frame=frame.get_path_name(),enabled=settings.enabled,was_enabled=was))
unreal.StingerAssetLibrary.finish_validation_asset_compilation()
(root/'Saved/Extraction/Fix5767Glass/FrameRendering.json').write_text(json.dumps(rows,indent=2),encoding='utf8')
print('GLASS_FRAME_RENDERING_PRESERVED',len(rows),sum(r['enabled'] for r in rows))
