"""盘点蜂医动画资产：按用途分类、区分已映射版本与仅源版本，并对照当前接入配置。

纯本地脚本（不需要 UE 进程）：
  python Scripts/Stinger/audit_animation_matrix.py
输出：
  Docs/Stinger/AnimationInventory.json  机读清单
  Docs/Stinger/AnimationInventory.md    人读矩阵
"""
import json
import re
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ANIM = ROOT / 'Content/Characters/Stinger/Animations'
DOCS = ROOT / 'Docs/Stinger'

# 当前 DA_RifleLocomotion / ABP_StingerFirstPerson_Repaired 实际引用的序列（2026-10-27 读取资产确认）
WIRED_1P = {
    'AR_M_1P_Idle_Pose_2': 'ViewIdle',
    'AR_M_1P_Walk_Loop': 'ViewWalk',
    'AR_M_1P_Run_Loop': 'ViewRun',
    'AR_M_1P_Sprint_Loop': 'ViewSprint',
    'AR_M_1P_WalkToRun': 'WalkToRun',
    'AR_M_1P_RunToWalk': 'RunToWalk',
    'AR_M_1P_WalkToSprint': 'WalkToSprint',
    'AR_M_1P_RunToSprint': 'RunToSprint',
    'AR_M_1P_SprintToWalk': 'SprintToWalk',
    'AR_M_1P_Idle01': 'ABP.IdleAnimation',
    'AR_M_1P_AimIdle_Pose': 'ABP.AimAnimation',
    'AR_M_1P_RelaxIdle': 'ABP.SprintAnimation(填错)',
}
WIRED_3P = {
    'BS_RifleStanding': 'Standing',
    'BS_RifleCrouching': 'Crouching',
    'AO_Rifle': 'AimOffset',
    'AO_RifleCrouching': 'CrouchAimOffset',
}

CATEGORIES = [
    ('技能·医疗枪', r'^Support_C101_MedicalGun'),
    ('技能·蜂巢烟雾', r'^Support_C101_HoneycombSmoke'),
    ('技能·烟幕弹', r'^Support_C101_SmokeScreen'),
    ('展示姿态', r'^M_Roy|^Emote_'),
    ('跳跃/落地', r'Jump|jump|Land|land|Fall|Pounce'),
    ('开镜/瞄准', r'Aim|aim'),
    ('蹲/趴', r'[Cc]rouch|Prone|prone|Slid'),
    ('翻越/攀爬', r'Vault|Valut|Climb|Zipline|ZipLine'),
    ('换弹/开枪', r'Clip|ChangeClip|Fire|Reload'),
    ('移动', r'Sprint|Walk|Run|Idle|Loop|idle'),
    ('死亡/受击', r'Death|Dying|Hit|Dragged|Drag'),
    ('交互/装备', r'Deploy|GetWeapon|PutWeapon|Open|Wear|Inspect|PickUp|Door|Graffiti|Gesture|Helmet'),
]


def collect():
    files = defaultdict(list)
    for path in ANIM.rglob('*.uasset'):
        rel = path.relative_to(ANIM).as_posix()
        files[path.stem].append(rel)
    return files


def layer_of(rel):
    if rel.startswith('Imported/'):
        return 'Imported'
    if rel.startswith('Corrected/Mapped'):
        return 'Mapped'
    if rel.startswith('Corrected/Source'):
        return 'Source'
    if rel.startswith('Corrected/Montages'):
        return 'Montage'
    if rel.startswith('Corrected/'):
        return 'Other'
    if rel.startswith('DeltaForceLocomotion/MappedV4'):
        return 'Mapped'
    if rel.startswith('DeltaForceLocomotion/Source'):
        return 'Source'
    if rel.startswith('DeltaForceLocomotion/'):
        return 'Other'
    if rel.startswith('FirstPerson/'):
        return 'Montage'
    if rel.startswith('Retarget/'):
        return 'Retarget'
    return 'Other'


def classify(name):
    for label, pattern in CATEGORIES:
        if re.search(pattern, name):
            return label
    return '其他'


def view_of(name):
    if re.search(r'_1P_|_1P$|^AR_M_1P|^AR_1P|1P_', name):
        return '1P'
    if re.search(r'_3P_|_3P$|^AR_M_3P|3P_', name):
        return '3P'
    return '通用'


def main():
    files = collect()
    entries = []
    for name, rels in sorted(files.items()):
        layers = sorted({layer_of(r) for r in rels})
        entries.append({
            'name': name,
            'view': view_of(name),
            'category': classify(name),
            'layers': layers,
            'mapped': any(l == 'Mapped' for l in layers),
            'montage': any(l == 'Montage' for l in layers),
            'wired': WIRED_1P.get(name) or WIRED_3P.get(name),
            'paths': sorted(rels),
        })

    # 只保留动画序列（排除骨架、蓝图、混合空间、数据资产）
    sequences = [e for e in entries if not re.match(
        r'^(ABP_|BS_|AO_|DA_|SK_|IK_|RTG_|AM_)', e['name'])]

    summary = defaultdict(lambda: defaultdict(int))
    for e in sequences:
        summary[e['category']][e['view']] += 1

    # 关键能力矩阵：某能力需要哪些序列、当前是否可接入
    capability = {
        '持枪移动(Walk/Run/Sprint 循环)': ['AR_M_1P_Walk_Loop', 'AR_M_1P_Run_Loop', 'AR_M_1P_Sprint_Loop'],
        '移动过渡(源内存在的 5 条)': ['AR_M_1P_WalkToRun', 'AR_M_1P_RunToWalk',
                                     'AR_M_1P_WalkToSprint', 'AR_M_1P_RunToSprint', 'AR_M_1P_SprintToWalk'],
        '移动过渡 Sprint→Run(源缺失)': ['AR_M_1P_SprintToRun'],
        '跳跃(腰射)': ['AR_M_1P_JumpStart', 'AR_M_1P_Jumploop', 'AR_M_1P_JumpLand'],
        '跳跃(开镜)': ['AR_M_1P_AimJumpStart', 'AR_M_1P_AimJumpLoop', 'AR_M_1P_AimJumpLand'],
        '开镜过渡': ['AR_M_1P_AimOn', 'AR_M_1P_AimOff'],
        '蹲伏过渡': ['AR_M_1P_Idle2Crouch', 'AR_M_1P_Crouch2Idle'],
        '趴下': ['AR_M_1P_Idle2Prone', 'AR_M_1P_Prone2Idle', 'AR_M_1P_Prone2Crouch',
                 'AR_M_1P_ProneMove_Start', 'AR_M_1P_Prone_Movef', 'AR_M_1P_Prone_Moveb',
                 'AR_M_1P_Prone_MoveL', 'AR_M_1P_Prone_MoveR'],
        '翻越/攀爬': ['AR_M_1P_Vault60', 'AR_M_1P_Vault90', 'AR_M_1P_Vault120',
                      'AR_M_1P_Valut180Stand', 'AR_M_1P_Climb60', 'AR_M_1P_Climb90',
                      'AR_M_1P_Climb120', 'AR_M_1P_Climb180Stand'],
        '医疗枪（自身治疗）': ['Support_C101_MedicalGun_M_1P_UseForMe',
                              'Support_C101_MedicalGun_M_1P_UseForMePutDown',
                              'Support_C101_MedicalGun_M_3P_UesForMe'],
        '医疗枪（救援队友）': ['Support_C101_MedicalGun_M_1P_Rescue_Start',
                              'Support_C101_MedicalGun_M_1P_Rescue_Loop',
                              'Support_C101_MedicalGun_M_1P_Rescue_End',
                              'Support_C101_MedicalGun_M_3P_Rescue_Start',
                              'Support_C101_MedicalGun_M_3P_Rescue_Loop',
                              'Support_C101_MedicalGun_M_3P_Rescue_End'],
        '蜂巢烟雾': ['Support_C101_HoneycombSmoke_M_1P_Fire',
                     'Support_C101_HoneycombSmoke_M_3P_Fire_LHand'],
        '烟幕弹': ['Support_C101_SmokeScreen_M_1P_Fire', 'Support_C101_SmokeScreen_M_3P_Fire'],
    }
    by_name = {e['name']: e for e in sequences}
    matrix = {}
    for label, names in capability.items():
        rows = []
        for n in names:
            e = by_name.get(n)
            if e is None:
                rows.append({'name': n, 'state': '源缺失'})
            elif e['wired']:
                rows.append({'name': n, 'state': '已接入(%s)' % e['wired'], 'mapped': e['mapped']})
            elif e['mapped']:
                rows.append({'name': n, 'state': '可接入(已映射)', 'mapped': True})
            else:
                rows.append({'name': n, 'state': '需重新映射(仅源/导入)', 'mapped': False})
        matrix[label] = rows

    DOCS.mkdir(parents=True, exist_ok=True)
    (DOCS / 'AnimationInventory.json').write_text(json.dumps({
        'total_sequences': len(sequences),
        'summary': {k: dict(v) for k, v in summary.items()},
        'capability': matrix,
        'entries': sequences,
    }, ensure_ascii=False, indent=2), encoding='utf-8')

    lines = ['# 蜂医动画资产盘点', '',
             '由 `Scripts/Stinger/audit_animation_matrix.py` 生成，依据 `Content/Characters/Stinger/Animations` 磁盘清单',
             '与当前 `DA_RifleLocomotion` / `ABP_StingerFirstPerson_Repaired` 实际配置。', '',
             '> `Mapped` = 已按视点/第三人称骨架重映射（`Corrected/MappedV4`、`DeltaForceLocomotion/MappedV4`），',
             '> 可直接在对应骨骼上播放；`Source`/`Imported` = 原始 PSA 导入版本，骨骼不同，直接播放会肢体错位。', '',
             '## 总量', '', '| 分类 | 1P | 3P | 通用 |', '|---|---:|---:|---:|']
    for cat in [c[0] for c in CATEGORIES] + ['其他']:
        if cat not in summary:
            continue
        row = summary[cat]
        lines.append('| %s | %d | %d | %d |' % (cat, row.get('1P', 0), row.get('3P', 0), row.get('通用', 0)))
    lines.append('| **合计** | %d | %d | %d |' % (
        sum(r.get('1P', 0) for r in summary.values()),
        sum(r.get('3P', 0) for r in summary.values()),
        sum(r.get('通用', 0) for r in summary.values())))
    lines += ['', '## 能力矩阵', '']
    for label, rows in matrix.items():
        ok = sum(1 for r in rows if r['state'].startswith('已接入'))
        ready = sum(1 for r in rows if r['state'].startswith('可接入'))
        missing = sum(1 for r in rows if r['state'].startswith('源缺失'))
        need = sum(1 for r in rows if r['state'].startswith('需重新映射'))
        lines.append('### %s（已接入 %d / 可接入 %d / 需映射 %d / 源缺失 %d）' % (
            label, ok, ready, need, missing))
        lines.append('')
        for r in rows:
            lines.append('- `%s` — %s' % (r['name'], r['state']))
        lines.append('')
    (DOCS / 'AnimationInventory.md').write_text('\n'.join(lines), encoding='utf-8')

    print('STINGER_INVENTORY sequences=%d categories=%d' % (len(sequences), len(summary)))
    for label, rows in matrix.items():
        ok = sum(1 for r in rows if r['state'].startswith('已接入'))
        ready = sum(1 for r in rows if r['state'].startswith('可接入'))
        need = sum(1 for r in rows if r['state'].startswith('需重新映射'))
        missing = sum(1 for r in rows if r['state'].startswith('源缺失'))
        print('  %-28s 已接入%d 可接入%d 需映射%d 源缺失%d' % (label, ok, ready, need, missing))


if __name__ == '__main__':
    main()
