"""Reuse the validated ActorX decoder; write only isolated SCAR animation assets."""
import json
import sys
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir())
sys.path.insert(0, str(root/'Scripts/Stinger'))
import repair_import as decoder

decoder.SOURCE = root/'Saved/Extraction/SpaceCenterExpansionSources01'
decoder.DEST = '/Game/Extraction/Weapons/SCARH'
names = ['AR_M_1P_Scar-H_idle', 'AR_M_1P_Scar-H_AimIdle',
         'AR_M_1P_Scar-H_Fire', 'AR_M_1P_Scar-H_ChangclipFull',
         'AR_M_1P_Scar-H_GetWeapon']
manifest = json.loads((decoder.SOURCE/'Manifest.json').read_text(encoding='utf-8'))
report = {}
for name in names:
    matches = [decoder.SOURCE/p for p in manifest['animations'] if Path(p).stem == name]
    assert len(matches) == 1, name
    result = decoder.import_file(matches[0])
    assert result['coverage'] >= .9, (name, result['coverage'])
    assert all('Mapped' in s['assets'] for s in result['sequences']), name
    report[name] = result
    print('SCAR_ANIMATION_IMPORTED', name, result['coverage'])
(root/'Saved/Extraction/SpaceCenterExpansion01/ScarAnimations.json').write_text(
    json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
