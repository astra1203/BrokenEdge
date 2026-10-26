"""Import only the bounded source list; never touch historical corrected assets."""
import json
import os
import sys
from pathlib import Path
import unreal

project = Path(unreal.Paths.project_dir())
work = project/'Saved/Stinger/Locomotion'
results = json.loads((work/'export/ExportResults.json').read_text(encoding='utf-8'))
assert results and all(r['Success'] for r in results), 'Incomplete source conversion'
os.environ['STINGER_REPAIR_SOURCE'] = str(work/'export')
os.environ['STINGER_REPAIR_DEST'] = '/Game/Characters/Stinger/Animations/DeltaForceLocomotion'
os.environ['STINGER_REPAIR_REPORT'] = str(work/'Import.json')
os.environ['STINGER_REPAIR_NAMES'] = '*'
os.environ['STINGER_REPAIR_FIXED_LIMBS'] = '1'
sys.path.insert(0,str(project/'Scripts/Stinger'))
import repair_import
repair_import.main()
manifest = json.loads((work/'Import.json').read_text(encoding='utf-8'))
assert all(e['status']=='written' for e in manifest['entries'].values())
print('LOCOMOTION_IMPORTED',len(manifest['entries']))
