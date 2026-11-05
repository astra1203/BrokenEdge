"""Inspect or migrate the four legacy APEX window blueprints; keep source backups."""
import unreal, os, shutil
from pathlib import Path
root = Path(unreal.Paths.project_dir())
out = root/'Saved/Extraction/SovietMilitary'
out.mkdir(parents=True,exist_ok=True)
apply = '-SovietRepairWindows' in unreal.SystemLibrary.get_command_line()
for name in ['BP_WindowBase','BP_WindowA','BP_WindowB','BP_WindowC']:
    source=root/f'Content/UVB80/Assets/UVB80/Buildings/Blueprints/{name}.uasset'
    backup=out/'OriginalWindows'/source.name
    if apply and not backup.exists():
        backup.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,backup)
report=unreal.SovietMilitaryBuilder.repair_windows(apply)
(out/('WindowRepair.txt' if apply else 'WindowInspection.txt')).write_text(report,encoding='utf-8')
print(report)
