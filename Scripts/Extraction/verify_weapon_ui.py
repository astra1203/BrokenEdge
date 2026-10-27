"""Run in the full editor: repair saved templates and render real weapon cards without entering PIE."""
import unreal

result = unreal.InventoryWidgetBuilder.create_inventory_widgets()
print('WEAPON_UI_LAYOUT', result)
assert result.startswith('PASSED'), result
result = unreal.InventoryWidgetBuilder.render_weapon_layout_preview()
print('WEAPON_UI_RENDER', result)
assert result.startswith('PASSED'), result
