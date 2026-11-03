# 当前搜打撤能力核对（2026-11-03）

本页核对当前工作区源码、物品白名单、现有资产文件及生成脚本，纠正 [2026-10-27 盘点](../航天基地复刻现状盘点.md) 已过时的缺项。**这是源码与配置盘点，不是整图、联网、Cook 或完整包验收。** 本次没有启动编辑器重读二进制资产，也没有核对官方线上最新版本；文中的官方参数来源限于仓库已有解包快照。运行、资源中止和发布结论统一以 [本轮夜间实施记录](../Overnight-2026-11-03.md) 及其链接证据为准。

## 已有能力：不应再按旧盘点重复建设

| 模块 | 当前事实与边界 | 源码 / 定义依据 |
| --- | --- | --- |
| 胸挂、背包、安全箱 | 已有独立装备槽和网格分区：胸挂 `4/-2`、背包 `5/-1`、安全箱 `6/-3`。网格容量来自装备定义，已不是统一固定背包。默认自动收纳先胸挂再背包，安全箱需明确指定。 | [ExtractionGear 槽位常量](../../../Source/BrokenEdge/Extraction/ExtractionItems.h)、[GridOf / Grids / Fits / AutoPlace](../../../Source/BrokenEdge/Extraction/ExtractionItems.cpp) |
| 携行装备内容 | 4 种胸挂：`Rig_Pouch/Ammo/Assault/Hurricane`，容量 6/10/16/22 格；4 种背包：`Bag_Climb/Hike/ALS/GTO`，容量 16/24/28/45 格；3 种安全箱：`Safe_Basic/Advanced/Premium`，容量 4/6/8 格。对应 `DA_Item_*` 资产均存在。 | [configure_gear.py 的 GEAR 表及来源 ID](../../../Scripts/Extraction/configure_gear.py)、[物品资产目录](../../../Content/Extraction/Items) |
| 安全箱保留 | `Protected()` 识别安全箱装备槽与安全箱分区；`DropAll()` 排除受保护物品，持久化 `Settle()` 在阵亡等结局的结算中仍保留受保护物品。已有兼容旧档案的装备补齐逻辑；这不等于所有异常退出/迁移场景本轮均已验收。 | [Protected](../../../Source/BrokenEdge/Extraction/ExtractionItems.cpp)、[DropAll](../../../Source/BrokenEdge/Extraction/ExtractionInventoryComponent.cpp)、[Settle 及旧档案处理](../../../Source/BrokenEdge/Extraction/ExtractionPersistence.cpp) |
| 第三把枪 M4A1 | `M4A1`、`Ammo55645` 已进入定义白名单和商店；枪械蓝图、表现资产、模型及动作资源存在。生成规格为 30 发弹匣、0.075 秒射击间隔；伤害按项目 AK 数值乘 31/40，不能写成官方绝对伤害完全一致。 | [Ids 白名单](../../../Source/BrokenEdge/Extraction/ExtractionItems.cpp)、[商店条目](../../../Source/BrokenEdge/Extraction/ExtractionProgression.cpp)、[weapon_specs.py](../../../Scripts/Extraction/weapon_specs.py)、[build_weapon.py](../../../Scripts/Extraction/build_weapon.py)、[BP_M4A1](../../../Content/Extraction/Weapons/M4A1/BP_M4A1.uasset) |
| 子弹飞行与重力 | 正式 Extraction 模式中，有机械参数的枪经 `AHitScanWeapon::Fire()` 生成 `AExtractionBullet`，以 1/120 秒步长推进并扫掠碰撞，包含初速、重力豁免距离、重力下坠和距离衰减。当前 AK/M4A1/SCARH 三份参数均有 `GravityParam=9.8`，代码转换为 980 cm/s²，前 2500 cm 不施加重力。参数缺失或旧模式仍可走原射线回退。 | [Fire](../../../Source/BrokenEdge/Weapon/HitScanWeapon.cpp)、[Initialize / Tick](../../../Source/BrokenEdge/Weapon/ExtractionBullet.cpp)、[WeaponMechanics::Find](../../../Source/BrokenEdge/Weapon/WeaponMechanics.cpp)、[WeaponMechanics.json 的 profiles](../../../Content/Extraction/Data/WeaponMechanics.json) |
| 物品范围 | 当前 `UExtractionItemDefinition::Ids()` 有 38 个可识别定义，包括上述新武器、弹种和携行装备；不能继续沿用旧文档“25 个物品、无第三把枪、无安全箱”的结论。定义存在不代表每项都已完成掉落分布、图标或实机验收。 | [完整定义白名单](../../../Source/BrokenEdge/Extraction/ExtractionItems.cpp) |

## 撤离点与守卫的当前配置

地图定义为 [DA_SpaceCenterGameplay](../../../Content/Extraction/Maps/DA_SpaceCenterGameplay.uasset)。[写入脚本](../../../Scripts/Extraction/place_official_spawn_exits.py) 默认使用 `OPEN_TIME_SCALE=1.0`、停留 10 秒；[原始点位快照](../RestorationEvidence/SpawnExit/OfficialSpawnExitAnchors.json) 保存来源参数。当前配置启用广场与试验场两处，德穆兰明确排除。

| 定义 ID | 当前配置 | 不能扩大的结论 |
| --- | --- | --- |
| `OfficialExitSquare` / 308 | 2026-11-03 起改为电闸式：4 个官方开关位置每局随机启用 2 个，全部拉下后开放 120 秒；容量不限；无空包条件。 | 开关为项目占位模型（闸箱+指示灯），拉闸为即时交互，无官方动画/音效。 |
| `OfficialExitTestrange` / 307 | 开局后 540 秒开放，无单独关闭截止；容量 3；启用 `bRequireEmptyPack`。 | 当前 `UpdateExtraction()` 仍把 `Slot < 0` 的有数量物品计入空包检查，因此胸挂/安全箱分区也落在这个条件内。官方 `Bag=Empty` 对这些分区的语义尚未确认，本轮保持原逻辑，不宣称已精确对齐。 |
| `OfficialExitDemoulin` / 309 | `EXCLUDED_EXITS` 明确保留禁用，不生成有效撤离点。 | 不因点位源数据存在就宣布支付物品、阵营条件或撤离玩法已完成，也不在本轮擅自启用。 |

以上时间是正常地图配置；[GameMode](../../../Source/BrokenEdge/Extraction/ExtractionGameMode.h) 默认一局 1800 秒，测试 runner 可用服务器参数缩短对局，`exit_config` 可在独立验收进程调整时间。这些测试覆盖不等于实等 900 秒或验证官方开关流程。中央反馈复用服务端状态，相关个人字段采用 `COND_OwnerOnly`，见 [UpdateExtraction](../../../Source/BrokenEdge/Extraction/ExtractionGameMode.cpp)、[HUD Refresh](../../../Source/BrokenEdge/Extraction/ExtractionWidget.cpp) 与 [复制声明](../../../Source/BrokenEdge/Extraction/ExtractionState.cpp)。

守卫布置链包含旧区 4 条路线 `ExtractionPatrol_0..3`，以及 7F 的 `WestDorm/WestGate/EastIndustry/TestRange` 四条，共 8 条 authored 路线；见 [初始两条](../../../Scripts/Extraction/prepare_phase5.py)、[北区新增两条](../../../Scripts/Extraction/prepare_phase7a.py)、[7F 路线计划](../RestorationEvidence/Phase7FGameplay/Plan.json) 和 [写入脚本](../../../Scripts/Extraction/place_7f_loot_guards.py)。这说明布置数据及生成链，不替代当前 Cook 地图的 8 条路线通行证明。

- [SpawnGuards](../../../Source/BrokenEdge/Extraction/ExtractionGuard.cpp) 默认数量及上限均为 8，允许专服测试参数覆盖；每个出生点仍需 NavMesh 投影成功，不能仅凭默认值声称实际生成了 8 名。
- [当前规则](../../../Config/DefaultGame.ini) 为反应时间 **2 秒**、额外射击间隔 **0.15 秒**、伤害倍率 **15%**。实际射击间隔取 `max(Weapon.FireDelay, GuardShotInterval)`，并受换弹、动作和视线约束，见 [EngageTarget](../../../Source/BrokenEdge/Character/BTTask_EngageTarget.cpp)。倍率按射手身份应用，玩家拾取守卫武器不继续降伤，见 [ExtractionHealth::Bullet](../../../Source/BrokenEdge/Extraction/ExtractionHealth.cpp)。
- 当前默认随机巡逻，固定路线同时承担出生布置依据；并非 8 名守卫一定按固定路线顺序循环。已有视觉感知、墙壁/烟雾复核、真实弹药与换弹、最后可见速度预测和 12 秒搜索返回，见 [控制器](../../../Source/BrokenEdge/Extraction/ExtractionGuard.h) 与 [上下文 Service](../../../Source/BrokenEdge/Character/BTService_UpdateGuardContext.cpp)。

## 仍待补齐或证明的差距

| 差距 | 本次可确认的边界 |
| --- | --- |
| 撤离规则深度 | 广场开关机制、德穆兰支付/阵营条件未接入当前有效撤离配置；试验场分区语义待确认，依据见上表。 |
| 武器与弹道深度 | 2026-11-03 起：M4A1 已开放红点/垂直握把/补偿器（官方 socket 挂点，见 [patch_m4a1_attachments.py](../../../Scripts/Extraction/patch_m4a1_attachments.py)）；子弹可穿透薄的静态物体，上限为 `BrokenEdge.Weapon.PenetrationCmPerLevel`（默认 4 cm）× 弹药穿透等级，穿透后伤害按厚度保留 75%–35%，人物不穿透、每发最多一次，见 [TryPenetrate](../../../Source/BrokenEdge/Weapon/ExtractionBullet.cpp)。项目没有表面材质类型，按厚度而非材质判定，不等同官方材质穿透表。 |
| 遭遇与敌人类型 | 当前 [SpawnGuards](../../../Source/BrokenEdge/Extraction/ExtractionGuard.cpp) 与 [InitializeGuard](../../../Source/BrokenEdge/Extraction/ExtractionInventoryComponent.cpp) 使用同类守卫和 AK 装备；[交战 Task](../../../Source/BrokenEdge/Character/BTTask_EngageTarget.cpp) 站定射击，所查链路未提供掩体选择、包抄或 Boss 专属行为。 |
| 干员与物资内容 | 当前 [EExtractionAction](../../../Source/BrokenEdge/Extraction/ExtractionAbilities.h) 的实际能力入口为救援、治疗、烟雾；不足以表示全干员体系。7F 的 [RECIPES](../../../Scripts/Extraction/place_7f_loot_guards.py) 明确为项目配方与项目布点，不能声称使用了官方完整掉落表及概率。 |
| 完整体验和正式交付 | 一条样板路线的连续行走、联网结算、第二局、全图导航、多人容量和打包稳定性需要各自证据；源码、站立探针、传送规则测试不能互相替代。今晚具体已完成/失败/资源中止项目只引用 [主记录](../Overnight-2026-11-03.md)，本页不预先宣布通过，也不由 LAN 测试推出公网账号、匹配或反作弊服务完成。 |

本页不修改旧盘点；旧盘点保留为历史快照。后续实现应从上述当前接口和资产继续，优先补证据与可玩闭环，避免重复开发已经存在的安全箱、携行分区、M4A1 或子弹重力。
2026-11-03 06:15 只读资产补查：[箱体边界审计](ContainerBounds-AssetAudit.json) 从正式 MapDefinition 读取 Ma1 LootLayout，确认当前为 **45 个启用候选、18 组**。历史 Phase7F Plan 的 54 个提案中有 9 个已在旧落地/胶囊检查中筛除，不能当作当前出生点。审计提供实际网格 Bounds、VisualOffset、旋转及推导 OBB；这是资产/CDO数据，尚不是实机碰撞扫描或全部路径通过证明。
