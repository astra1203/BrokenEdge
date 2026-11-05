"""Generate approved fixes with the rebuilt editor; each child validates its output."""
import runpy
from pathlib import Path
import unreal
root=Path(unreal.Paths.project_dir())
for script in ['configure_long_gun_viewfit.py','create_inventory_widgets.py','inspect_world_fixes.py']:
    runpy.run_path(str(root/'Scripts/Extraction'/script),run_name='__main__')
print('WORLD_FIXES_ASSETS_COMPLETE')
