# AKM / SCAR-H 枪械数据取证报告

本报告由离线审计生成；只证明快照中的记录与引用，不证明正式服运行公式。

完整原始行、字段、成员路径及 SHA-256 见 [关系报告](audit.json)。`source_direct` 表示原始数据；`reference_derived` 表示按引用路径解析；运行语义未证实项不会用于修改游戏参数。

## 默认配装与功能引用

### AK：机匣 18010000006，默认预设 10010000012

装配表 10 行归并为 6 个节点、5 条连接；图检查问题：0。

| 节点 | 物品 ID | 父节点 / 槽位 | SOL 功能 ID | 功能行数 | 调校条数 |
| --- | --- | --- | --- | --- | --- |
| 1 | 18010000006 | 0 / 0 | 18010006 | 0 | 0 |
| 2 | 13030000138 | 1 / 1 | 120138 | 0 | 0 |
| 3 | 13040000143 | 1 / 3 | 130143 | 0 | 0 |
| 4 | 13050000201 | 1 / 4 | 140201 | 0 | 0 |
| 5 | 13120000302 | 1 / 5 | 150302 | 1 | 0 |
| 6 | 13020000353 | 1 / 2 | 110353 | 0 | 0 |

找到的 SOL 功能记录：

- 行 `3876`，功能 `150302`：`EWeaponPartFunctionType::StaticAttributeReplace`，参数 `GMagCapacity` / `Initial` / `30`。

原始基础评分：2=40.0, 3=39.0, 5=54.0, 6=55.0, 4=53.0；独立曲线开关：`False`。以下通用属性行是审计候选；函数与曲线的执行顺序仍未确认。

功能行数为 0 不代表已证明没有属性影响。所有函数参数保持原样，不推定运算优先级。

评分与曲线的关联：

| 属性 ID | 名称 | 记录 RecId | 曲线规则 |
| --- | --- | --- | --- |
| 0 | 后座力 | 0 | 无曲线；查看原始 FunctionItems |
| 1 | 射速 | 0 | 无曲线；查看原始 FunctionItems |
| 2 | 射程 | 0 | GBullet_Range: AbsoluteMappingThenScale |
| 3 | 后坐控制 | 0 | GRecoil_H: DeltaMappingThenScale, GRecoil_V: DeltaMappingThenScale, GGunkickSpring: DeltaMappingThenScale, GGunkickRandom: DeltaMappingThenScale |
| 4 | 腰射精准度 | 0 | GSpread_Hip: DeltaMappingThenScale, GSpread_Hip_Continuous: DeltaMappingThenScale |
| 5 | 操控 | 0 | GAiming_ADSTime: AbsoluteMapping, GSprintToFireTime: DeltaMappingThenScale, GMovement_ADSSpeed: DeltaMapping, GMovement_SilentWalkSpeed: DeltaMapping |
| 6 | 稳定 | 0 | GBreathScale: DeltaMappingThenScale, GBreathTime: DeltaMappingThenScale, GBreathRecoil: DeltaMappingThenScale, BehitGunSway: DeltaMappingThenScale, AimMoveGunSway: DeltaMappingThenScale, GRecoil_HShake: DeltaMappingThenScale, GRecoil_VShake: DeltaMappingThenScale |

曲线引用 18 条；原始关键帧、切线和外推配置保存在 JSON evidence 中。

伤害仅列候选分支，尚不选择 SOL 最终值：

| 行键 | 候选 ID | Damage | HealthDamage | ArmorDamage |
| --- | --- | --- | --- | --- |
| 突击步枪2-1 | 110006 | 40.0 | 40.0 | 42.0 |
| （攻防）突击步枪2-1 | 210006 | 26.0 | 26.0 | 0.0 |

未解析引用：`DamagePointValue.AttackerValueId` → `[310006]`。

### SCARH：机匣 18010000021，默认预设 10010000228

装配表 10 行归并为 6 个节点、5 条连接；图检查问题：0。

| 节点 | 物品 ID | 父节点 / 槽位 | SOL 功能 ID | 功能行数 | 调校条数 |
| --- | --- | --- | --- | --- | --- |
| 1 | 18010000021 | 0 / 0 | 18010021 | 0 | 0 |
| 2 | 13020000389 | 1 / 2 | 110389 | 0 | 0 |
| 3 | 13050000237 | 1 / 4 | 140237 | 0 | 0 |
| 4 | 13040000154 | 1 / 3 | 130154 | 0 | 0 |
| 5 | 13030000134 | 1 / 1 | 120134 | 0 | 0 |
| 6 | 13120000210 | 1 / 5 | 150210 | 0 | 0 |

找到的 SOL 功能记录：

- 无匹配行，不能推断所有默认件均不影响属性。

原始基础评分：2=40.0, 3=40.0, 5=58.0, 6=55.0, 4=45.0；独立曲线开关：`False`。以下通用属性行是审计候选；函数与曲线的执行顺序仍未确认。

功能行数为 0 不代表已证明没有属性影响。所有函数参数保持原样，不推定运算优先级。

评分与曲线的关联：

| 属性 ID | 名称 | 记录 RecId | 曲线规则 |
| --- | --- | --- | --- |
| 0 | 后座力 | 0 | 无曲线；查看原始 FunctionItems |
| 1 | 射速 | 0 | 无曲线；查看原始 FunctionItems |
| 2 | 射程 | 0 | GBullet_Range: AbsoluteMappingThenScale |
| 3 | 后坐控制 | 0 | GRecoil_H: DeltaMappingThenScale, GRecoil_V: DeltaMappingThenScale, GGunkickSpring: DeltaMappingThenScale, GGunkickRandom: DeltaMappingThenScale |
| 4 | 腰射精准度 | 0 | GSpread_Hip: DeltaMappingThenScale, GSpread_Hip_Continuous: DeltaMappingThenScale |
| 5 | 操控 | 0 | GAiming_ADSTime: AbsoluteMapping, GSprintToFireTime: DeltaMappingThenScale, GMovement_ADSSpeed: DeltaMapping, GMovement_SilentWalkSpeed: DeltaMapping |
| 6 | 稳定 | 0 | GBreathScale: DeltaMappingThenScale, GBreathTime: DeltaMappingThenScale, GBreathRecoil: DeltaMappingThenScale, BehitGunSway: DeltaMappingThenScale, AimMoveGunSway: DeltaMappingThenScale, GRecoil_HShake: DeltaMappingThenScale, GRecoil_VShake: DeltaMappingThenScale |

曲线引用 18 条；原始关键帧、切线和外推配置保存在 JSON evidence 中。

伤害仅列候选分支，尚不选择 SOL 最终值：

| 行键 | 候选 ID | Damage | HealthDamage | ArmorDamage |
| --- | --- | --- | --- | --- |
| 战斗步枪3-2 | 110021 | 40.0 | 40.0 | 41.0 |
| （攻防）战斗步枪3-2 | 210021 | 25.0 | 25.0 | 0.0 |

未解析引用：`DamagePointValue.AttackerValueId` → `[310021]`。

## 未确认项与下一步

- **默认件功能空引用**：缺少 空功能行等同无操作的证明，或缺失的重定向／覆盖数据。当前策略：不凭空启用配件，也不将缺失功能默认为零修正。
- **评分组合**：缺少 线性函数、曲线及配件操作的运行时选择和计算顺序。当前策略：保留现有基础参数，不把曲线试算值作为官方最终性能。
- **DFM 后座**：缺少 角度单位与运行时组合实现。当前策略：保留明确标注的 0.1 兼容系数。
- **BlendMode_Dest**：缺少 运行时插值与区间边界语义。当前策略：保留目标阶梯兼容规则。
- **SOL 伤害**：缺少 GameMode01/02 的模式映射以及护甲／弹药应用顺序。当前策略：保留现有伤害流程，不把候选行直接认定为 SOL 数值。

已检查 DataTables 与 R13N/Base、R13N/PC 表；按明确对象路径在压缩包目录中查找曲线。逻辑检索覆盖独立文件与客户端静态逆向包中的限定文本／Lua 文件；命中只说明字节串存在，不视为已读到计算实现，未执行源包脚本或解包二进制。

审计问题 13 项，逻辑关键词命中文件 3 个。全部细节见 audit.json 的 issues、logic_search、unknowns。

下一步应先解决功能空引用与评分组合语义，再接入运行时；目前可可靠使用的是装配拓扑和显式引用，不能承诺默认装配后的官方最终性能。

## 复现

```powershell
python Scripts/Extraction/import_weapon_mechanics.py --audit
python Scripts/Extraction/import_weapon_mechanics.py --audit --check
python -m unittest discover -s Scripts/Extraction -p test_weapon_mechanics_audit.py
```

`--audit` 仅写 Docs/Extraction/WeaponMechanicsEvidence；`--audit --check` 重新读取源包并比对报告，不写文件。
