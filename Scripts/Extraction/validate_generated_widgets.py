"""Reload and compile saved generated assets in an isolated editor process."""
import json
import os
from pathlib import Path
import unreal

names = ['WBP_LootingSlotView', 'WBP_LootingGridCell', 'WBP_LootEquipSlot', 'WBP_LootingMenuAction',
         'WBP_LootingBackpackV2_Pc', 'WBP_LootingContextMenu', 'WBP_LootingSplitDialog',
         'WBP_LootingInteractionPrompt', 'WBP_LootingPickupView_Pc']
paths = ['/Game/UI/Looting/' + name for name in names] + [
    '/Game/UI/Extraction/WBP_ExtractionHUD', '/Game/Blueprints/Extraction/BP_ExtractionLootActor']
results = []
for path in paths:
    bp = unreal.load_asset(path)
    assert bp, path
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    status = str(bp.get_editor_property('status'))
    assert 'BS_ERROR' not in status.upper(), path + ': ' + status
    results.append(dict(asset=path, status=status))
for result in [unreal.InventoryWidgetBuilder.validate_inventory_widgets(),
               unreal.ExtractionBlueprintBuilder.validate_extraction_blueprints()]:
    assert result.startswith('PASSED'), result
(Path(os.environ['EXTRACTION_RUN_DIR']) / 'WidgetValidation.json').write_text(
    json.dumps(results, indent=2) + '\n', encoding='utf-8')
print('GENERATED_WIDGETS_VALIDATION_COMPLETE')
