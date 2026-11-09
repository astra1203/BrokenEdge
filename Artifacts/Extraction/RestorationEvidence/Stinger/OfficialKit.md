# 蜂医（C101）官方技能数据

数据来源：本地解包资产（`D:/司晨/源代码和素材/三角洲解包资产/原始资源ZIP`），用项目自带的 `SpaceCenter.Converter`
只读解码（2026-10-28）。只记录解码出来的事实；查不到的写明“未找到”，不填猜测值。

## 激素枪（MedicalGun，技能 20010101）

| 项目 | 官方值 | 出处 |
| --- | --- | --- |
| 技能类型 | Support，双手技能，掏出专用武器 `10070000246`（GetAndPutWeaponFast 快速切换） | `DataTables/Skill/SkillDataConfig` 行 20010101 |
| 弹匣 | 1 发，不可补弹，无备弹（`WeaponClipAmmoCount=1`、`MaxCarriedAmmoCount=0`、`CanAddAmmo=false`） | `WeaponAttributeTable_CombinationTable` / `WeaponClipCapacityTable` 行 18080009206、18080009209 |
| 射击 | 单发，`FireInterval 0.66 s`，`FireCD 1.0 s` | 同上 |
| 自用 | 动画键 `SelfCure`，消耗 1 发（`AmmoNum=1`），动画通知 `AddBuff` 时上效果 | `BP_AbilityTemp_SelfCure` |
| 冷却 | 引用 `CDID=2`，**解包数据表里没有 CD 配置表，数值未找到** | `SkillDataConfig` 行 20010101 |
| 弹道 | `BulletFlyingId 2007`：初速 2900（cm/s）、`GravityParam 0`、`ZeroGravityDistance 0`、`MaxDistance 10000`、`Radius 1`、`FlyMode_PhysicSimulate`、`bEnableInstantHitInstead=false` | `WeaponBase/WeaponBulletTable` 行 2001 |

项目实现：对准队友时由服务器生成 `AExtractionStimDart`，按 120 Hz 子步扫掠飞行，命中存活队友才施加下表 Buff，
撞墙、命中敌人或飞满 100 m 都浪费这一发；自用不发射飞镖。

### 命中效果（SOL 撤离模式适用的 Buff）

激素弹 `BP_WeaponBulletSupport` 命中后按模式施加 Buff，作用对象 `TeammateAndMe`（队友和自己）：

| Buff | 类型 | 持续 | 周期 | 每跳 | 合计 |
| --- | --- | --- | --- | --- | --- |
| 300013 | AbilitySupport 持续回血 | 60 s | 0.1 s | Health +0.2（属性操作 100010007） | **+120 HP（2 HP/s）** |
| 300062 | AbilitySupportquick 快速回血 | 1 s | 0.5 s | Health +10（属性操作 100010050） | **+20 HP** |
| 300100 | TemporaryPainkiller 临时止痛 | 20 s | — | — | — |
| 300019 / 300020 / 300037 | 命中闪光 / 队友命中标记 / HUD 提示 | 1.2 / 0.6 / 1.5 s | — | 表现 | — |

开镜命中使用 `AddBuffIDs_Zoom`（300014 等），本表未展开。MP 模式用的是 300023（10 s、每 0.066 s +1），不适用于撤离。
属性 64 = `Default.GPAttributeSetHealth.Health`（`DataTables/AttributeName`）。

## 蜂巢烟雾（C101_Smoke）

| 项目 | 官方值 | 出处 |
| --- | --- | --- |
| 投掷物 | 弹性 0.2、摩擦 0.8，落停后起烟（`AbilityExplosionType=Stop`），预计 1 s 后触发 | `BP_AbilityBullet_C101_Smoke` |
| 烟雾 | 初始半径 300 cm、扩散 5 s（110 cm/s）、`SmokeSize 800`、`SmokeLifetime 15 s` | 同上 |
| 技能存续 | `AbilityExplosionDurationSeconds 20 s`，Buff `LeftTime 20 s` | 同上、`BP_AbilityBuff_Smoke` |
| 烟雾表 | 蜂医烟雾 SmokeID 3：半径 550 cm、高 400 cm、寿命 15 s | `DataTables/Ability/SmokeDataConfig` |
| 冷却 | **未找到**（同激素枪） | — |

## 模型与动作

`Content/Models/Weapons/MilitaryProps/Support_C101_MedicalGun_01`（13 个模型文件）与
`Support_C101_honeycombsmoke_01`；第一/第三人称动作见解包资产 `蜂医的人物模型和相关动画资产/03_蜂医技能动画`
（AimOn/AimFire、FireStart/Loop/End、ChangeClip、UseForMe、Rescue_Start/Loop/End、HoneycombSmoke Get/Fire/Put 等）。
