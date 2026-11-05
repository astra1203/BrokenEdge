"""42 号：霰弹枪第一人称握姿。
34 号按 AK 换算出的左手插槽在枪管前 26 cm，正好落在机匣前沿；这把短霰弹枪的泵动护木在 31–55 cm，
左手要放到护木上（38 cm，护木后半段，手臂够得着）。另外源模型自带的 MuzzleFlash 网格插槽在 107 cm（枪口实际约 60 cm），
覆盖了骨架上正确的那个，枪口火光/弹道起点悬空在枪口前，一并改到骨架插槽的位置。"""
import unreal
L = lambda s: unreal.log('SHOTGUN ' + str(s))
LEFT_X = 38.0
MUZZLE = unreal.Vector(59.603718, 0.0, 12.356133)

for path in ['/Game/Extraction/Weapons/PumpShotgun/SK_PumpShotgun_ViewFit', '/Game/MilitaryWeapSilver/Weapons/Shotgun_A']:
    mesh = unreal.load_asset(path)
    lib = unreal.StingerAssetLibrary
    lib.add_mesh_socket(mesh, 'MuzzleFlash', 'Grip_Bone', unreal.Transform(MUZZLE, unreal.Rotator(), unreal.Vector(1, 1, 1)))
    s = mesh.find_socket('LeftHandSocket')
    if s and 'ViewFit' in path:
        loc = s.relative_location
        lib.add_mesh_socket(mesh, 'LeftHandSocket', s.bone_name,
                            unreal.Transform(unreal.Vector(LEFT_X, loc.y, loc.z), s.relative_rotation, s.relative_scale))
    unreal.EditorAssetLibrary.save_loaded_asset(mesh, False)
    m = mesh.find_socket('MuzzleFlash'); h = mesh.find_socket('LeftHandSocket')
    L('%s muzzle=%s left=%s' % (path, m.relative_location, h.relative_location if h else None))
