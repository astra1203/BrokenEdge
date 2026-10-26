"""Give SCAR-H the same attachment set as the AK, without touching any other property.

Run inside the Unreal Editor (Output Log command line):

    py "D:/Users/SCSJ/BrokenEdge2.0/Scripts/Extraction/patch_scar_attachments.py"

`configure_scar.py` originally authored `compatible_attachments=[]`, which is why the
gunsmith panel shows no install actions for SCAR-H. This patch only rewrites that one
array on the existing asset, so it is safe to re-run.
"""
import unreal

ITEM = '/Game/Extraction/Items/DA_Item_SCARH'
PARTS = ['RedDot', 'VerticalGrip', 'Compensator']
lib = unreal.EditorAssetLibrary

gun = unreal.load_asset(ITEM)
assert gun, 'missing ' + ITEM

before = [str(n) for n in gun.get_editor_property('compatible_attachments')]
unreal.log('SCAR_ATTACH_BEFORE %s' % before)

for part in PARTS:
    definition = unreal.load_asset('/Game/Extraction/Items/DA_Item_' + part)
    assert definition, 'missing definition for ' + part
    slot = str(definition.get_editor_property('attachment_slot'))
    kind = str(definition.get_editor_property('kind'))
    print('%s -> slot=%s kind=%s' % (part, slot, kind))
    # A part without a slot can never mount: SupportsAttachment() rejects it.
    assert 'NONE' not in slot.upper(), part + ' has no attachment slot'

gun.set_editor_property('compatible_attachments', [unreal.Name(p) for p in PARTS])
after = [str(n) for n in gun.get_editor_property('compatible_attachments')]
assert after == PARTS, after

assert lib.save_loaded_asset(gun, only_if_is_dirty=False)
unreal.log('SCAR_ATTACH_AFTER %s' % after)
print('SCAR_ATTACH_PATCHED', ITEM, after)
