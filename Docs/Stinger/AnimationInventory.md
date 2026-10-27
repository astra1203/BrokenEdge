# 蜂医动画资产盘点

由 `Scripts/Stinger/audit_animation_matrix.py` 生成，依据 `Content/Characters/Stinger/Animations` 磁盘清单
与当前 `DA_RifleLocomotion` / `ABP_StingerFirstPerson_Repaired` 实际配置。

> `Mapped` = 已按视点/第三人称骨架重映射（`Corrected/MappedV4`、`DeltaForceLocomotion/MappedV4`），
> 可直接在对应骨骼上播放；`Source`/`Imported` = 原始 PSA 导入版本，骨骼不同，直接播放会肢体错位。

## 总量

| 分类 | 1P | 3P | 通用 |
|---|---:|---:|---:|
| 技能·医疗枪 | 31 | 24 | 0 |
| 技能·蜂巢烟雾 | 14 | 12 | 0 |
| 技能·烟幕弹 | 29 | 12 | 0 |
| 展示姿态 | 0 | 0 | 46 |
| 跳跃/落地 | 11 | 0 | 4 |
| 开镜/瞄准 | 19 | 0 | 12 |
| 蹲/趴 | 24 | 12 | 14 |
| 翻越/攀爬 | 10 | 0 | 0 |
| 换弹/开枪 | 15 | 0 | 4 |
| 移动 | 28 | 21 | 41 |
| 死亡/受击 | 22 | 0 | 6 |
| 交互/装备 | 37 | 0 | 0 |
| 其他 | 12 | 0 | 29 |
| **合计** | 252 | 81 | 156 |

## 能力矩阵

### 持枪移动(Walk/Run/Sprint 循环)（已接入 3 / 可接入 0 / 需映射 0 / 源缺失 0）

- `AR_M_1P_Walk_Loop` — 已接入(ViewWalk)
- `AR_M_1P_Run_Loop` — 已接入(ViewRun)
- `AR_M_1P_Sprint_Loop` — 已接入(ViewSprint)

### 移动过渡(源内存在的 5 条)（已接入 5 / 可接入 0 / 需映射 0 / 源缺失 0）

- `AR_M_1P_WalkToRun` — 已接入(WalkToRun)
- `AR_M_1P_RunToWalk` — 已接入(RunToWalk)
- `AR_M_1P_WalkToSprint` — 已接入(WalkToSprint)
- `AR_M_1P_RunToSprint` — 已接入(RunToSprint)
- `AR_M_1P_SprintToWalk` — 已接入(SprintToWalk)

### 移动过渡 Sprint→Run(源缺失)（已接入 0 / 可接入 0 / 需映射 0 / 源缺失 1）

- `AR_M_1P_SprintToRun` — 源缺失

### 跳跃(腰射)（已接入 0 / 可接入 0 / 需映射 3 / 源缺失 0）

- `AR_M_1P_JumpStart` — 需重新映射(仅源/导入)
- `AR_M_1P_Jumploop` — 需重新映射(仅源/导入)
- `AR_M_1P_JumpLand` — 需重新映射(仅源/导入)

### 跳跃(开镜)（已接入 0 / 可接入 3 / 需映射 0 / 源缺失 0）

- `AR_M_1P_AimJumpStart` — 可接入(已映射)
- `AR_M_1P_AimJumpLoop` — 可接入(已映射)
- `AR_M_1P_AimJumpLand` — 可接入(已映射)

### 开镜过渡（已接入 0 / 可接入 2 / 需映射 0 / 源缺失 0）

- `AR_M_1P_AimOn` — 可接入(已映射)
- `AR_M_1P_AimOff` — 可接入(已映射)

### 蹲伏过渡（已接入 0 / 可接入 0 / 需映射 2 / 源缺失 0）

- `AR_M_1P_Idle2Crouch` — 需重新映射(仅源/导入)
- `AR_M_1P_Crouch2Idle` — 需重新映射(仅源/导入)

### 趴下（已接入 0 / 可接入 8 / 需映射 0 / 源缺失 0）

- `AR_M_1P_Idle2Prone` — 可接入(已映射)
- `AR_M_1P_Prone2Idle` — 可接入(已映射)
- `AR_M_1P_Prone2Crouch` — 可接入(已映射)
- `AR_M_1P_ProneMove_Start` — 可接入(已映射)
- `AR_M_1P_Prone_Movef` — 可接入(已映射)
- `AR_M_1P_Prone_Moveb` — 可接入(已映射)
- `AR_M_1P_Prone_MoveL` — 可接入(已映射)
- `AR_M_1P_Prone_MoveR` — 可接入(已映射)

### 翻越/攀爬（已接入 0 / 可接入 8 / 需映射 0 / 源缺失 0）

- `AR_M_1P_Vault60` — 可接入(已映射)
- `AR_M_1P_Vault90` — 可接入(已映射)
- `AR_M_1P_Vault120` — 可接入(已映射)
- `AR_M_1P_Valut180Stand` — 可接入(已映射)
- `AR_M_1P_Climb60` — 可接入(已映射)
- `AR_M_1P_Climb90` — 可接入(已映射)
- `AR_M_1P_Climb120` — 可接入(已映射)
- `AR_M_1P_Climb180Stand` — 可接入(已映射)

### 医疗枪（自身治疗）（已接入 0 / 可接入 3 / 需映射 0 / 源缺失 0）

- `Support_C101_MedicalGun_M_1P_UseForMe` — 可接入(已映射)
- `Support_C101_MedicalGun_M_1P_UseForMePutDown` — 可接入(已映射)
- `Support_C101_MedicalGun_M_3P_UesForMe` — 可接入(已映射)

### 医疗枪（救援队友）（已接入 0 / 可接入 6 / 需映射 0 / 源缺失 0）

- `Support_C101_MedicalGun_M_1P_Rescue_Start` — 可接入(已映射)
- `Support_C101_MedicalGun_M_1P_Rescue_Loop` — 可接入(已映射)
- `Support_C101_MedicalGun_M_1P_Rescue_End` — 可接入(已映射)
- `Support_C101_MedicalGun_M_3P_Rescue_Start` — 可接入(已映射)
- `Support_C101_MedicalGun_M_3P_Rescue_Loop` — 可接入(已映射)
- `Support_C101_MedicalGun_M_3P_Rescue_End` — 可接入(已映射)

### 蜂巢烟雾（已接入 0 / 可接入 2 / 需映射 0 / 源缺失 0）

- `Support_C101_HoneycombSmoke_M_1P_Fire` — 可接入(已映射)
- `Support_C101_HoneycombSmoke_M_3P_Fire_LHand` — 可接入(已映射)

### 烟幕弹（已接入 0 / 可接入 2 / 需映射 0 / 源缺失 0）

- `Support_C101_SmokeScreen_M_1P_Fire` — 可接入(已映射)
- `Support_C101_SmokeScreen_M_3P_Fire` — 可接入(已映射)
