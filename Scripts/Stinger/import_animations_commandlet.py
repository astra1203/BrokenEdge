import sys, os
import unreal
sys.path.insert(0, unreal.Paths.project_dir() + "Scripts/Stinger")
import import_assets
exec(open(unreal.Paths.project_dir() + "Scripts/Stinger/inspect_commandlet.py",encoding="utf-8").read())
import_assets.batch(int(os.environ.get("STINGER_IMPORT_START", "0")),
                    int(os.environ.get("STINGER_IMPORT_COUNT", "443")))
