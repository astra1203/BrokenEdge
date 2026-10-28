"""Weapon specs for the generic extraction weapon pipeline.

Shared by the offline stage (``prepare_weapon.py``, run with the engine's bundled
Python) and the editor stages (``import_weapon.py`` / ``configure_weapon.py``).

Each spec names the official receiver (``rec``) and default preset (``preset``).
Parts are not listed by hand: ``prepare_weapon.py`` resolves the preset through
``GunDescTable`` -> ``CommonItemAssets_Weapon`` (mesh names) -> ``PartSocketsTable``
(mount sockets), so the assembly is the official default configuration.

Animations are archive-relative stems under ``Content/Models/Weapons/``; the AR
family shares one generic 1P set (idle / aim pose) with per-gun fire, reload and draw.
"""
from pathlib import Path

DEFAULT_SOURCE = Path('E:/MyFiles/三角洲解包资产/网盘上传压缩包')
GENERIC_AR = 'Guns/AR/Anims/Male/1P/'

WEAPONS = {
    'M4A1': dict(
        label='M4A1突击步枪',
        rec=18010000001,
        preset=10010000122,
        receiver='Rec_AR_M4A1-Std_001',
        socket_row='Base_AR_M4A1_None',
        anims={
            'idle': GENERIC_AR + 'AR_M_1P_Idle01',
            'aim': GENERIC_AR + 'AR_M_1P_AimIdle_Pose',
            # 机匣目录 Parts/Rec/.../Anims/1P 下的动画驱动的是枪体（枪栓/弹匣），只覆盖 20% 手臂骨骼；
            # 手臂动作全部来自通用 AR 目录，其中包含 M4A1 专用的首次抽枪。
            'fire': GENERIC_AR + 'AR_M_1P_Fire',
            'reload': GENERIC_AR + 'AR_M_1P_ChangeClipFull',
            'swap': GENERIC_AR + 'AR_M_M4A1_1P_GetWeaponInitial',
        },
        ammo='Ammo55645',
        ammo_label='5.56×45 mm 弹药',
        magazine=30,
        # WeaponAttributeTable RecId 18010000001：FireInterval 0.075 s（800 发/分）。
        fire_interval=0.075,
        # DamagePointValue 110001 / 110006：M4A1 31、AKM 40。
        damage_vs_ak=31 / 40,
        description='5.56×45 mm，30 发弹匣。官方默认配装（预设 10010000122）。',
    ),
}

# 第一人称持枪位置以已实机校准的 SCAR-H 表现资产为基准（相机空间，X 前 Y 右 Z 上，厘米），
# 叠加官方 WeaponSocketTransformTable 中两把枪 WeaponPoint 相对 FPPCamera 的差值。
REFERENCE_FIT = dict(socket_row='Base_AR_Scar-H_None',
                     presentation='/Game/Extraction/Weapons/SCARH/DA_SCARH_Presentation')
FIT_POSES = {'hip': 'Idle', 'aim': 'AimingIdleStaticSeq'}

# Part category (item id prefix) -> mount socket on its parent, from PartSocketsTable.
CATEGORY_SOCKETS = {
    '1302': 'Bar_point', '1303': 'PisG_point', '1304': 'Sto_point', '1305': 'Han_point',
    '1312': 'Mag_point',
}


def output_dir(project_root, weapon):
    return Path(project_root) / 'Saved/Extraction/Weapons' / weapon
