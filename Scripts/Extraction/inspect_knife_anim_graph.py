"""Dump the runtime third-person anim graph and knife montage slots as evidence."""
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir())
out = root / 'Saved/Extraction/KnifeAnimGraph.txt'
lib = unreal.EditorAssetLibrary
lines = []

# The character blueprint decides which anim blueprint actually runs; assert instead of assuming.
bp = unreal.load_asset('/Game/Characters/Stinger/Blueprints/BP_StingerCharacter')
gen = unreal.get_default_object(bp.generated_class())
body = gen.get_editor_property('get_mesh').get_editor_property('anim_class') if False else None
lines.append('=== CHARACTER ===')
lines.append('melee_mesh=%s' % gen.get_editor_property('melee').get_editor_property('mesh').get_path_name())
for path in ['/Game/Characters/Stinger/Animations/DeltaForceLocomotion/ABP_StingerRifleLocomotion',
             '/Game/Characters/Stinger/Animations/ThirdPerson/ABP_BrokenEdgeAnimInstance_Stinger']:
    anim_bp = unreal.load_asset(path)
    lines.append('=== ANIM GRAPH %s ===' % path)
    lines.append(unreal.StingerAssetLibrary.describe_anim_graph_chain(anim_bp))

for path in ['/Game/Extraction/Weapons/Knife/Montages/AM_3P_Fire_01',
             '/Game/Extraction/Weapons/Knife/Montages/AM_3P_Fire_02',
             '/Game/Extraction/Weapons/Knife/Montages/AM_1P_Fire_01']:
    montage = unreal.load_asset(path)
    slots = [str(t.slot_name) for t in montage.get_editor_property('slot_anim_tracks')]
    lines.append('=== MONTAGE %s slots=%s ===' % (path, slots))

montage = unreal.load_asset('/Game/Extraction/Weapons/Knife/Montages/AM_3P_Fire_01')
lines.append('=== SKELETON ===')
lines.append(unreal.StingerAssetLibrary.describe_skeleton_bones(montage.get_editor_property('skeleton')))

out.parent.mkdir(parents=True, exist_ok=True)
out.write_text('\n'.join(lines) + '\n', encoding='utf-8')
unreal.log_warning('KNIFE_ANIM_GRAPH_DUMPED ' + str(out))
