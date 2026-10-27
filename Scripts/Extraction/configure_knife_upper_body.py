"""Lay the third-person knife attack onto the upper body only.

The knife montages used to play into DefaultSlot, which sits after the grip IK and
replaces the whole body, so the legs froze while the capsule kept moving (foot slide).
The weapon layer cannot host them because bWeaponEquipped is forced false while the
knife is out, therefore a dedicated MeleeSlot + layered blend per bone is installed
right before the output pose, branching at Spine.

The anim blueprint that matters is the one the player blueprint's body mesh actually
runs, so it is derived from BP_StingerCharacter instead of hardcoded.
"""
from pathlib import Path
import json
import unreal

ROOT = Path(unreal.Paths.project_dir())
PLAYER_BLUEPRINTS = ['/Game/Characters/Stinger/Blueprints/BP_StingerCharacter',
                     '/Game/Characters/Stinger/Blueprints/BP_StingerCharacter_Repaired']
SLOT = 'MeleeSlot'
BRANCH = 'Spine'
BODY_MONTAGES = ['/Game/Extraction/Weapons/Knife/Montages/AM_3P_Fire_01',
                 '/Game/Extraction/Weapons/Knife/Montages/AM_3P_Fire_02']
VIEW_MONTAGES = ['/Game/Extraction/Weapons/Knife/Montages/AM_1P_Fire_01',
                 '/Game/Extraction/Weapons/Knife/Montages/AM_1P_Fire_02']

lib = unreal.EditorAssetLibrary


def body_anim_blueprint():
    """Resolve the third-person anim blueprint through the player blueprint's mesh."""
    resolved = set()
    for path in PLAYER_BLUEPRINTS:
        bp = lib.load_asset(path)
        assert bp, 'player blueprint missing: ' + path
        mesh = unreal.get_default_object(bp.generated_class()).get_editor_property('mesh')
        anim_class = mesh.get_editor_property('anim_class')
        assert anim_class, 'body mesh has no anim class: ' + path
        package = anim_class.get_path_name().split('.')[0]
        assert lib.does_asset_exist(package), 'anim blueprint asset missing: ' + package
        resolved.add(package)
    assert len(resolved) == 1, 'player blueprints disagree on the body anim blueprint: %s' % resolved
    return resolved.pop()


ANIM_BP = body_anim_blueprint()
changes = {}
bp = lib.load_asset(ANIM_BP)
assert unreal.StingerAssetLibrary.configure_melee_upper_body_slot(bp, SLOT, BRANCH), 'MELEE SLOT INSTALL FAILED'
chain = unreal.StingerAssetLibrary.describe_anim_graph_chain(bp)
assert 'SlotName=' + SLOT in chain, 'melee slot not in the runtime anim graph'
assert 'BlendMode=0 [%s:1 ]' % BRANCH in chain, 'layered blend filter missing'
assert lib.save_loaded_asset(bp, only_if_is_dirty=False), 'anim blueprint save failed'

for path in BODY_MONTAGES:
    montage = lib.load_asset(path)
    assert montage, 'montage missing: ' + path
    before = [str(t.slot_name) for t in montage.get_editor_property('slot_anim_tracks')]
    assert unreal.StingerAssetLibrary.set_montage_slot_name(montage, SLOT), 'slot retarget failed: ' + path
    assert lib.save_loaded_asset(montage), 'montage save failed: ' + path
    after = [str(t.slot_name) for t in montage.get_editor_property('slot_anim_tracks')]
    # A montage whose slot is absent still "plays" but no node consumes it, so assert the pairing.
    assert all(s == SLOT and 'SlotName=' + s in chain for s in after), 'montage slot not served: ' + path
    changes[path] = {'before': before, 'after': after}

# First person montages keep DefaultSlot: the first person proxy only implements that slot.
for path in VIEW_MONTAGES:
    montage = lib.load_asset(path)
    assert montage, 'montage missing: ' + path
    slots = [str(t.slot_name) for t in montage.get_editor_property('slot_anim_tracks')]
    assert slots == ['DefaultSlot'], 'first person montage slot changed: %s -> %s' % (path, slots)

evidence = ROOT / 'Saved/Extraction/KnifeUpperBodySlot.json'
evidence.write_text(json.dumps({'anim_blueprint': ANIM_BP, 'slot': SLOT, 'branch': BRANCH,
                                'montages': changes}, ensure_ascii=False, indent=2) + '\n',
                    encoding='utf-8')
unreal.log_warning('KNIFE_UPPER_BODY_SLOT_OK %s %s' % (ANIM_BP, json.dumps(changes)))
