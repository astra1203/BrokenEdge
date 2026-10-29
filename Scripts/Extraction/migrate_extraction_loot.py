"""Migrate the production map's native Loot fixtures, verifying instance data before saving.

The source asset scan found native Loot references only in L_SpaceCenterExtraction.
Historical preview maps are not rewritten. Re-running this script is a no-op.
"""
import json
from pathlib import Path
import unreal

MAP = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
out = Path(unreal.Paths.project_saved_dir()) / 'Extraction/BlueprintRefactor'
out.mkdir(parents=True, exist_ok=True)
assert unreal.EditorLoadingAndSavingUtils.load_map(MAP)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
# Non-editable replicated/server fields are checked by the native migration helper.
FIELDS = ['loose', 'loose_definition', 'loose_quantity', 'label',
          'seed_contents', 'equipment_tier', 'tags']

def snapshot():
    result = {}
    for actor in actors.get_all_level_actors():
        if not isinstance(actor, unreal.ExtractionLootActor):
            continue
        key = actor.get_actor_label()
        assert key not in result, f'Duplicate fixture label: {key}'
        result[key] = {name: str(actor.get_editor_property(name)) for name in FIELDS}
        result[key]['transform'] = str(actor.get_actor_transform())
    return result

before = snapshot()
(out / 'MigrationBefore.json').write_text(json.dumps(before, ensure_ascii=False, indent=2), encoding='utf-8')
result = unreal.ExtractionBlueprintBuilder.migrate_loot_actors()
assert result.startswith('PASSED'), result
after = snapshot()
assert before == after, 'Instance data changed; map has NOT been saved'
assert not any(a.get_class() == unreal.ExtractionLootActor.static_class() for a in actors.get_all_level_actors())
if result != 'PASSED 0':
    assert unreal.EditorLoadingAndSavingUtils.save_current_level()
report = {'result': result, 'map': MAP, 'verified_instances': len(after), 'instance_data_preserved': before == after}
(out / 'Migration.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print('EXTRACTION_MIGRATION', report)
