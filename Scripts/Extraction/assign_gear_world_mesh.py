# 给胸挂/背包定义配地面模型（丢在地上时显示）；用地图还原里已有的皮包模型，不导入新素材。
import unreal

BAG = '/Game/Maps/SpaceCenter/Restored/Assets/55144909b7760f87/SM_M_Commercial_Resort_LeatherBag_01/StaticMeshes/SM_M_Commercial_Resort_LeatherBag_01.SM_M_Commercial_Resort_LeatherBag_01'
RIG = '/Game/Maps/SpaceCenter/Restored/Assets/7dcef31aae93beb6/SM_M_Commercial_Resort_LeatherBag_02/StaticMeshes/SM_M_Commercial_Resort_LeatherBag_02.SM_M_Commercial_Resort_LeatherBag_02'

meshes = {}
for k, p in (('bag', BAG), ('rig', RIG)):
    m = unreal.load_asset(p)
    meshes[k] = m
    b = m.get_bounds() if m else None
    unreal.log('GEAR_MESH %s %s origin=%s extent=%s' % (k, p, b.origin if b else None, b.box_extent if b else None))

reg = unreal.AssetRegistryHelpers.get_asset_registry()
for a in reg.get_assets_by_path('/Game/Extraction/Items', recursive=False):
    n = str(a.asset_name)
    if not (n.startswith('DA_Item_Bag_') or n.startswith('DA_Item_Rig_')):
        continue
    da = unreal.load_asset(str(a.package_name) + '.' + n)
    m = meshes['bag' if 'Bag_' in n else 'rig']
    da.set_editor_property('world_mesh', m)
    unreal.EditorAssetLibrary.save_loaded_asset(da, False)
    unreal.log('GEAR_MESH_SET %s' % n)
