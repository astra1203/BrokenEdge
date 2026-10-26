"""UE commandlet entry point for the bounded import and opt-in configuration."""
import sys
from pathlib import Path
import unreal
sys.path.insert(0,str(Path(unreal.Paths.project_dir())/'Scripts/Stinger'))
import import_locomotion
import configure_locomotion
