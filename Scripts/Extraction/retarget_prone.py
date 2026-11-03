"""Retarget the Epic AnimStarterPack prone set onto the Stinger body (2026-11-04 user request #17).

Source: /Game/AnimStarterPack (Epic sample content, UE4 mannequin skeleton). The existing
RTG_EpicCharacter_To_Stinger retargeter (Scripts/Stinger/setup_character.py) maps the UE4 bone
names (pelvis / spine_01 / clavicle_l ...) onto Stinger, so the mannequin mesh works as source.
Output: /Game/Characters/Stinger/Animations/Prone/<name>_Stinger.

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript \
        -script="Scripts/Extraction/retarget_prone.py" -NullRHI -unattended -nosound
"""
import json

import unreal

LIB = unreal.EditorAssetLibrary
SOURCE_MESH = '/Game/AnimStarterPack/UE4_Mannequin/Mesh/SK_Mannequin'
TARGET_MESH = '/Game/Characters/Stinger/Meshes/Gameplay/Stinger_3P_Textured/SkeletalMeshes/Stinger_3P_Textured'
RETARGETER = '/Game/Characters/Stinger/Animations/Retarget/RTG_EpicCharacter_To_Stinger'
DEST = '/Game/Characters/Stinger/Animations/Prone'
ANIMS = ['Stand_To_Prone', 'Prone_Idle', 'Prone_To_Stand', 'Prone_Fire_1', 'Prone_Reload_Rifle']

source = unreal.load_asset(SOURCE_MESH)
target = unreal.load_asset(TARGET_MESH)
rtg = unreal.load_asset(RETARGETER)
assert source and target and rtg, (SOURCE_MESH, TARGET_MESH, RETARGETER)
for name in ANIMS:
    out = '%s/%s_Stinger' % (DEST, name)
    if LIB.does_asset_exist(out):
        LIB.delete_asset(out)
assets = [LIB.find_asset_data('/Game/AnimStarterPack/' + name) for name in ANIMS]
result = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
    assets, source, target, rtg, suffix='_Stinger', target_path=DEST, include_referenced_assets=False)
made = [str(a.get_editor_property('package_name')) if hasattr(a, 'get_editor_property') else str(a) for a in result]
LIB.save_directory(DEST, only_if_is_dirty=False, recursive=True)
report = {}
for name in ANIMS:
    anim = unreal.load_asset('%s/%s_Stinger' % (DEST, name))
    report[name] = anim.get_play_length() if anim else None
print('PRONE_RETARGETED', json.dumps({'made': made, 'lengths': report}))
