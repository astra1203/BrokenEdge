# BrokenEdge 问题审计与整改清单

> 初稿：2026-11-06；源码复核：2026-11-07。源码基线：`bd56081333bafc3f9d1ae4bd3910b148041547e9`，当前分支 `master`。
>
> 本文记录用户提出的 9 项体验问题，以及本次检查发现的功能、代码／蓝图架构、资产组织和验证问题。它是整改依据，不是修复完成报告。
>
> 检查方式：11 月 6 日初稿保留源码与编辑器采集证据。11 月 7 日先复核源码，随后使用 UE 5.8.3 命令行重新读取关键资产、检查 Editor 构建状态并执行自动化测试；独立客户端场景继续验证中。**本夜新增证据与测试边界见第 10 节**。前文引用的 11 月 6 日缓存日志保留其历史身份，不等同于本夜复现。
>
> 证据等级：**已确认**指源码或当前资产直接证明某项限制／配置；**用户报告**指用户已观察到，但本次未复现；**待验证**指候选原因，不能当作根因。优先级 P1＝核心操作／物品安全，P2＝功能完整性／表现／维护成本，P3＝清理与规范。无实测依据的性能问题不写成性能结论。

## 1. 九项反馈总表

| 编号 | 问题 | 本次结论 | 优先级 |
| --- | --- | --- | --- |
| U01 | 首次打开背包，枪图标很窄 | 已实测并通过调试器定位：冷纹理占位尺寸导致首次 Brush 比例为 1；第二次恢复为 2.89，详见 10.7 | P2 |
| U02 | 背包与手持／背负槽不能互换武器 | 已确认且主槽双向拖拽已实测被拒绝：装备槽投放不生成交换请求；底层网格交换拒绝装备槽物品 | P1 |
| U03 | 初始子弹应进口袋；仅口袋／胸挂支持 R 换弹与 HUD 备弹统计 | 已实测：初始 5.56 备弹在背包，移到安全箱仍计入备弹，两处分区均被 R 换弹消耗 | P1 |
| U04 | 左右歪头未实现 | 检查范围内未找到主动探头链路；现有 Lean 是转向动画倾斜 | P2 |
| U05 | G 扔雷同时丢枪 | 输入冲突已实测：投雷后原 M4A1 GUID、弹匣与三个配件进入新掉落容器，详见 10.5 | P1 |
| U06 | 注水开关／拉闸点模型粗糙 | 已确认通用开关原生类使用 Cube＋Sphere，占位模型由多种机关直接生成 | P2 |
| U07 | 安全箱与背包拖动不自动交换 | 大厅 UI 和持久化入口存在明确缺口；局内网格已有交换，用户局内现象若存在仍待复现 | P1 |
| U08 | M4A1 偶尔不显示红点 | 已复现拔药枪时枪本体仍显示、红点附件被隐藏；模型归属与可见性管理不一致，其他触发仍待覆盖 | P1 |
| U09 | 激素枪打药仍不正确 | 已复现药枪与步枪重叠；正常落地自用可完成 40→100，射击可中断治疗；整体仍未解决 | P1 |

## 2. 逐项分析与验收要求

### U01：首次背包枪图标比例异常

**证据。** [LootingMainView.cpp](../Source/BrokenEdge/Extraction/UI/LootingMainView.cpp) 的 `ULootingWidgetPart::SetItem`（约 130 行）同步加载图标，以纹理尺寸和占格面积计算等比 Brush 尺寸。`Refresh`（459 行）在首次打开或容器版本变化时重建卡片。[InventoryWidgetBuilder.cpp](../Source/StingerTools/Private/InventoryWidgetBuilder.cpp) 的生成模板使用 `ScaleToFit`。

编辑器实读 `/Game/UI/Looting/WBP_LootingSlotView`：`WeaponIconFit` 确为 `ScaleToFit`，外层 Fill，内部 Image 为 Center；M4A1 定义占格为 4×2，引用 `T_ItemIcon_W_M4A1`。因此不能再把“缺少 ScaleBox”写成已确认原因，也不能据此否定首次显示异常。

**本夜定位。** 第一、第二次背包截图与调试器变量已经捕获。M4A1 的导入尺寸都是 578×200，首次平台数据仍为 0×0，UI 读取默认纹理比例而生成 115.2×115.2 Brush；再次打开生成 230.4×79.72 Brush。详见 10.5、10.7。剩余验证包括拖拽图、旋转卡片、不同 DPI 以及 Cook 后客户端。

**整改与验收。** 对比首次打开、关闭再打开、换枪后打开和旋转卡片四组尺寸；以纹理宽高比验证最终显示比例，不能只检查控件非零尺寸。分别验收背包卡片、装备卡片、拖拽图。冷启动且没有预打开资产时复测，覆盖至少两种窗口比例。修复需同时覆盖生成模板与最终 Widget，避免下次生成覆盖修改。

### U02：武器与装备槽双向交换

**已确认限制。** `ULootingMainView::ResolveDrop`（842 行起）命中装备槽后设置 `Slot` 就返回，不检查已有武器，也不设置 `SwapWith`。[ExtractionItems.cpp](../Source/BrokenEdge/Extraction/Inventory/ExtractionItems.cpp) 的 `SwapGridItems`（518 行起）明确拒绝 `A->Slot >= 0 || B->Slot >= 0`；普通 `Fits` 又要求目标装备槽为空。两个方向均有阻断点。

**目标规则。** 背包武器拖到已有枪的手持／背负槽，应把被替换的枪放回来源背包分区；装备槽武器拖到背包另一把枪上，应原子交换。回放位置可以优先原格，再在来源分区寻找合法空位；放不下则整体失败，并解释原因，不丢枪、不复制枪、不悄悄移动第三件物品。

**实现边界。** 扩展权威事务时必须保留 GUID、附件树、弹匣、耐久和来源批次，并通过 `SyncWeapons` 更新场景 Actor；不能仅交换 Widget，也不能拆成客户端先卸下再装备的两个独立操作。换弹、治疗、旧 Revision、重复请求及武器 Actor 生成失败都要保持回滚语义。

**验收。** 两个武器槽各自双向交换；来源背包满／碎片空间／旋转后可放；弹匣非空、带配件武器；重试与双客户端观察。成功后库存、第一人称、背负模型、弹药和保存结果一致。

### U03：口袋、胸挂与可换弹备弹

**已确认。** [ExtractionInventoryComponent.cpp](../Source/BrokenEdge/Extraction/Inventory/ExtractionInventoryComponent.cpp)：

- `GrantStartingSupply`（110 行起）补充四类备弹，并在约 261 行明确 `AutoPlace(..., BagGrid)`。
- `SyncReserve`（334 行起）直接使用 `Contents.Count(AmmoType)`。
- `Reload`（476 行起）使用同一全库存计数，再遍历全部同类物品扣弹，没有分区过滤。
- `FExtractionContainer::Count` 只判断物品定义，不判断位置。因此目前背包、安全箱里的同弹种子弹都会进入该统计和消耗路径。

**目标规则。** 初始备弹优先放口袋；R 键只取 `PocketGrid` 与 `RigGrid`；HUD 的备弹数只显示这两个分区内的当前兼容弹种数量。背包和安全箱的弹药仍应在各自库存卡片中显示数量，只是不计入战斗 HUD 备弹。弹匣内已装子弹单独显示。

**实现建议。** 增加明确的“可用于换弹的弹药”查询／消耗入口，使 HUD、客户端是否允许发起换弹、服务器开始与完成换弹使用同一分区规则；不要改动供交易、守恒检查使用的通用 `Count`。口袋已有物品或容量不够时明确溢出策略，建议转胸挂并提示，不覆盖旧物品。

**验收。** 全部备弹在背包时 HUD=0 且不能 R 换弹；挪到口袋／胸挂后立即可用；安全箱同样不计；不同弹种不混算；换弹期间移动弹药以完成时的权威状态为准；首次出战与已有存档出战都检查初始位置。

### U04：左右歪头／侧身探头

**当前证据。** [BrokenEdgeAnimInstance.cpp](../Source/BrokenEdge/Character/BrokenEdgeAnimInstance.cpp) 的 `Lean`（71 行起）根据角色转向计算身体倾斜，是动画表现。源码搜索及两个当前输入映射中未见主动左右探头 Action、相应 RPC 或探头状态链。不能把该变量算作探头功能已实现。

**所需完整功能。** 输入与回中、摄像机侧移／倾斜、手臂与枪械姿态、贴墙限制、远端第三人称表现，以及服务器认可的射击起点。只让镜头歪一下无法证明墙后射击与受击判定正确。

**验收。** 左右、按住／松开、瞄准、蹲／趴、切枪、贴墙、翻越和倒地均有明确行为；远端看得到姿态；不产生镜头穿墙、枪口穿墙伤害或左右状态卡住。当前 E 已承担交互／装备操作，键位方案需要一起整理，不能直接叠加 Q/E 而忽略冲突。

### U05：G 扔雷同时丢枪

**已确认双重绑定。** 编辑器读取 `IMC_CharacterDefault` 和 `IMC_Stinger`，两者都是 `IA_Drop → G`；`BP_StingerCharacter` 默认使用后者。[ExtractionPlayerController.cpp](../Source/BrokenEdge/Extraction/Core/ExtractionPlayerController.cpp) 又在 `SetupInputComponent` 约 60 行绑定 G 投掷手雷，Tick 中松开 G 时提交投掷。

角色侧 `DropButtonPressed → Combat::Drop → DropEquippedItem` 在搜打撤模式仍然有效。[CombatComponent.cpp](../Source/BrokenEdge/Component/CombatComponent.cpp) 204 行证明搜打撤分支确实调用丢弃事务。11 月 7 日已通过实际 G 输入复现并核对原枪 GUID，详见 10.5。

**整改。** 搜打撤 G 只负责手雷；丢枪保留在明确的背包操作或独立键位中。统一模式输入上下文，并覆盖原始 IMC 与实际 Stinger IMC，检查 Alt+G、按下／松开、UI 获得焦点时的互斥。

**验收。** 有雷、无雷、冷却、背包开启、长按取消和死亡中断时，G 都不会改变枪的 GUID、槽位或弹匣；有效投掷只减少一颗雷。兼顾旧模式的既有丢枪操作。

### U06：注水开关与拉闸点模型

**已确认。** [ExtractionExitSwitch.cpp](../Source/BrokenEdge/Extraction/World/ExtractionExitSwitch.cpp) 构造函数直接使用 `/Engine/BasicShapes/Cube` 与 `Sphere`。撤离点、注水和吊桥分别在 `ExtractionExit.cpp`、`ExtractionWater.cpp`、`ExtractionDrawbridge.cpp` 直接生成 `AExtractionExitSwitch`。仅创建一个好看的子蓝图并不会自动替换这些生成路径。

**整改。** 把开关类型和外观配置化，分别提供电闸箱、注水阀／控制台、吊桥控制器；保留服务器权威交互，增加可配置 Mesh／Blueprint 类、把手或阀门运动、开关状态材质、音效与提示锚点。外观碰撞与交互查询分开设置。

**已有候选资产。** 文件检查发现 `/Game/SciFiWorld/Meshes/SM_ControlPanel01`，以及 `/Game/Maps/SpaceCenter/Restored/Assets/2ec25e02319b93f1/SM_M_Government_Electrical_Switch_02/StaticMeshes/SM_M_Government_Electrical_Switch_02`、`/Game/Maps/SpaceCenter/Restored/Assets/3d12b2d8b4a7b532/SM_H_FutureTech_Electronics_PowerSwitch_01_HD/StaticMeshes/SM_H_FutureTech_Electronics_PowerSwitch_01_HD`。可先核验这些现有资源的材质、比例、碰撞和可动部件，避免遗漏已经导入的候选；本夜没有把文件名等同于视觉质量验收，也没有替换场景资产。

**验收。** 航天基地实际点位正确生成新模型；未激活、可操作、操作中、已完成的外观一致；远端同步、视线检查、距离及旧机关规则不退化。验收应看场景中的比例、朝向和可识别性，而不只是资产成功导入。

### U07：安全箱与背包交换

**需要区分两个界面。**

| 界面 | 现有行为 | 结论 |
| --- | --- | --- |
| 局内 `ULootingMainView` | 遇到不同物品会生成 `Swap`；网格交换允许负数 Slot，包含背包 -1、安全箱 -3 | 不能认定完全没实现；需捕获失败物品、落点、分区与服务端返回 |
| 大厅 `SExtractionStash` | `OnMouseButtonUp`（约 369 行）命中已有物品就填 `MergeInto`，不生成 `Swap` | 已确认 UI 缺口：不同物品也会被当作堆叠候选 |
| 大厅持久化入口 | [ExtractionPersistence.cpp](../Source/BrokenEdge/Extraction/Persistence/ExtractionPersistence.cpp) 626 行只把 `Transfer` 交给 `ExtractionItems::Transfer`，其他操作走 `Configure` | 已确认后端分派缺口；仅改大厅前端也不够 |

**区分约束。** “安全箱内的物品”与“安全箱装备本身”不同，后者有保护／卸下限制。局内交换要求整件，且被换物品必须能放回来源分区；容积不足应合法拒绝。相同可堆叠物品优先合并，不能把合并和交换混为一谈。

**验收。** 局内和大厅分别覆盖安全箱↔背包、背包↔胸挂、同尺寸／不同尺寸、满格／旋转、可堆叠／不可堆叠；成功保留两件物品身份，失败完整保留原状态。大厅保存后重连仍一致。现有 `ExtractionSwapPocketTests.cpp` 主要覆盖一般网格、跨容器长枪和口袋约束，不能证明这两个 UI 入口均通过。

### U08：M4A1 偶发红点缺失

**已知。** 编辑器读取 M4A1 兼容附件包括 `RedDot`，`DA_Item_RedDot` 的世界模型和第一人称模型均非空。`DA_M4A1_Presentation.ScopeOverlay` 为空本身不是红点缺失证据：本项目红点通过附件 Mesh 表现，不能套用狙击镜 Overlay 的判定。

**需要分开定位。**

1. 库存实例是否实际带 RedDot。开局补件只在 `Attachments.IsEmpty()` 时执行；已有其他附件但没有瞄具的旧枪不会自动补红点。这是确定的配置策略，是否解释本次偶发待验证。
2. 库存有红点时，复制到武器 Actor 的 `ExtractionParts/ExtractionAssembly/ExtractionConfiguration` 是否一致。
3. [ExtractionWeaponVisuals.cpp](../Source/BrokenEdge/Extraction/Combat/ExtractionWeaponVisuals.cpp) 按配置、父组件和模型哈希重建两套附件；第一人称枪组件由角色复用。检查切枪时的实际父组件、可见性、机械瞄具材质隐藏及装配完成顺序。
4. 瞄具外壳存在但瞄点不见时，检查镜片／瞄点材质、瞄准偏移和遮挡，不应误判为附件丢失。

**验收。** 新枪、已有存档枪、切主副武器、背包交换、掉落捡回、装卸配件、死亡重生、重新进入战局；记录物品 ID 与装配版本，分别对比本地第一人称和另一客户端第三人称。至少捕获一次异常时的数据与画面后再选修复点。

### U09：激素枪打药仍不正确

**状态：未解决，不能以蒙太奇存在或服务器有回血函数结案。** 用户本次未细分是自用、射队友、枪模、动作、回血时机还是收枪异常，文档保留全部验证分支。

已核对的当前行为：

- V 轻点：切出／收起激素枪；按住至少 0.35 秒：请求自用。拿着激素枪左键：请求飞镖，即使没有瞄准队友也会射出，不会自动改为自用。
- `ServerBegin` 校验冷却与动作互斥，治疗按 `HealSeconds` 计时；`Complete` 自用时调用 `ApplyStim`，对外射击时生成飞镖，之后进入冷却并收枪。
- `ExtractionStimDart::Strike` 只对有效存活友方应用效果；自用药效也包含持续恢复与止痛，不能只看单帧血量判断。
- 编辑器实读角色 CDO：自用 3P／1P 蒙太奇长度约 1.401／1.334 秒；对外射击约 0.835／0.502 秒，绑定均存在。代码将表现映射到服务器时长，第一人称按第三人称进度同步。长度不同不直接证明 Bug，但需要验证实际出手帧。
- 枪模异步加载后才可创建；未就绪或正在编译时 `MakeStimGun` 返回空，Tick 后续会重试。首次使用的模型出现时机需要实测。

**下一轮应采集的同一次操作证据。** 输入按下／松开时间、`bStimDrawn`、`Action/bHealDart/Target`、服务器 `ActionEnd`、冷却截止时间、两套蒙太奇与当前进度、药枪模型及挂点、`EXTRACTION_STIM_APPLIED`／`EXTRACTION_STIM_DART` 日志，以及客户端／服务器生命值。RPC 多个拒绝分支直接返回，缺少明确的玩家拒绝原因，容易把被拒绝看成“没反应”。

**验收矩阵。** 自用、射存活队友、射空、被墙挡住、满血、倒地目标、移动中自用、受伤中断、快速重复按键、背包／菜单互斥、首次冷加载、用完恢复原枪。至少一名独立客户端观察远端动作。治疗结束／取消后模型、输入、移速和原枪都恢复；以录像和服务器状态一致为准，不再只改动画引用后宣称完成。

## 3. 额外功能与数据风险

| 编号 | 已观察证据与影响 | 整改方向 | 等级／优先级 |
| --- | --- | --- | --- |
| F01 | 搜打撤三类 SSR 请求直接返回，射击请求没有客户端开枪时间；实体弹道不自动解决网络时延 | 设计适配弹道的有界延迟补偿；先做延迟／丢包测试，不能简单放开旧 SSR | 源码确认边界，体验待测／P1 |
| F02 | 旧 `HitScanConfirmHit` 的“什么也没命中”分支仍返回 `{true,false}`（约 478 行），调用方根据 `bHitConfirmed` 伤害结算 | 修正返回值并测完全未命中；当前搜打撤禁用该入口，不应描述成当前搜打撤必中 | 源码逻辑缺陷／P1，启用旧 SSR 前必修 |
| F03 | `InitializeLoadout` 无测试开关地调用 `GrantStartingSupply`，会置测试背包、保证 M4A1／SCARH 与两把测试枪、补雷补弹 | 测试配装和正式出战规则分开；定义正式经济规则后再切换，保留已有物品与存档兼容 | 源码确认，是否保留为产品决策／P1 |
| F04 | 初始配装把装不下的原物品移入 Overflow，补给箱生成失败时只写日志，未见回存或回滚 | 为失败路径保留可领取物品或恢复原库存，加入生成失败验证 | 确认失败路径缺口，未实测丢失／P1 |
| F05 | UI 把固定槽 0、1 命名为“手持／背负”，实际 `HeldSlot` 还由 `bHoldingSecondary` 决定 | 区分主／副武器槽与当前手持状态，切枪后更新标签／高亮 | 源码命名不一致／P2 |
| F06 | 经典 GameMode 未见完整计时状态推进，PlayerController 的比赛状态回调仍有空实现 | 标明旧模式完成度；若继续提供 FFA/Teams，单独验收比赛开始／结束／重生 | 源码确认／P2 |
| F07 | `DefaultPlatformService=Null`，当前持久化为本地文件状态／日志系统 | 明确局域网与单服务器边界；公网匹配、多服务器账号共享是独立建设项 | 配置／架构边界，不直接判为 Bug／P2 |
| F08 | 重生 `RequestRespawn` 直接对 PlayerStarts 随机并索引，未处理零出生点 | 缺失出生点时给出可诊断失败或安全回退 | 源码边界缺口／P2 |

F01 对照 [LagCompensationComponent.cpp](../Source/BrokenEdge/Component/LagCompensationComponent.cpp)、[ExtractionCombatComponent.cpp](../Source/BrokenEdge/Extraction/Combat/ExtractionCombatComponent.cpp)；F02 同前；F03–F04 对照 [ExtractionInventoryComponent.cpp](../Source/BrokenEdge/Extraction/Inventory/ExtractionInventoryComponent.cpp)；F05 对照 `RebuildEquipment` 与 `HeldSlot`；F06、F08 对照 [BrokenEdgeGameMode.cpp](../Source/BrokenEdge/GameMode/BrokenEdgeGameMode.cpp)。

## 4. 代码与蓝图架构问题

### 4.1 已有边界应保留

库存以服务器物品实例为准、拥有者专属复制；物品变更有 Sequence／Revision 校验、快照与失败回滚；战利品完整 `Contents` 不广播给所有客户端，搜索视图由交互组件提供。这些设计应继续保留。不能为修 UI 而改成客户端直接移动权威物品，也不能让蓝图外观成为第二份库存。

用户指定的 [ExtractionLootActor.h](../Source/BrokenEdge/Extraction/Inventory/ExtractionLootActor.h) 已分离交互盒、阻挡盒、可见模型，完整库存留在服务器。其主要维护问题是下述内容耦合，而不是“Actor 不该持有容器”。

### 4.2 需要整改的结构

| 编号 | 结构问题与证据 | 建议边界 |
| --- | --- | --- |
| A01 | 同一键位由角色 Enhanced Input、搜打撤控制器 BindKey、UMG／Slate 分别处理；U05 是实际冲突 | 按模式／UI 状态统一输入上下文，业务入口统一校验；整理键位表和输入消费规则 |
| A02 | 局内 UMG、大厅 Slate 分别构造物品请求，U07 证明行为已分叉 | 共用移动／合并／交换的请求规划和校验语义，各 UI 只负责布局与反馈 |
| A03 | `BrokenEdgeCharacter.cpp` 约 2500 行，混合第一人称组件、动画同步、输入、姿态、战斗入口；`LootingMainView.cpp` 约 1074 行；`CombatComponent.cpp` 约 1058 行 | 按视角表现、动作协调、输入、容器展示逐步拆分；文件长度是维护指标，不是单独的缺陷证明 |
| A04 | 经典战斗与搜打撤战斗大量依赖运行时分支，共享 `CombatState/CarriedAmmo` 等字段 | 明确模式适配层和唯一权威来源，限制旧路径绕过库存事务；先修行为再做大规模拆分 |
| A05 | 武器表现同时依赖库存实例、Weapon 复制字段、角色复用的 1P Mesh、每帧附件重建检查 | 明确装配版本与表现重建入口，添加可观测快照；评估事件驱动，保留必要的就绪重试 |
| A06 | 能力状态、治疗物品、换弹、近战、翻越分别检查动作互斥；技能拒绝常直接返回 | 集中定义动作允许／中断关系，返回明确结果；不必为此立刻重写为 GAS |
| A07 | Widget 既有 C++ 构建器又有保存的 Blueprint；角色、动画也有多个配置脚本 | 写清“生成源／手工可编辑区／最终资产”的归属，验证生成后控件树及引用，避免两边各改一份 |
| A08 | `UExtractionItemDefinition::Get` 用白名单和 `/Game/Extraction/Items/DA_Item_%s` 拼接加载；Loot Actor 的 `DoorBones` 默认值绑定具体模型与骨名，BeginPlay 内含物品配方 | 把内容配方、容器开门配置、模型和挂点放入版本化 DataAsset／注册表，Actor 保留交互与复制职责 |
| A09 | UI `SetItem` 同步加载图标；物品查找调用 `LoadObject`；首次打开存在资源／布局时序 | 记录首次打开耗时和加载路径，再决定预载与缓存。已有对象可命中缓存，不能把每次调用都当作磁盘 IO |

### 4.3 蓝图与骨架的可见问题

11 月 6 日的编辑器缓存日志记录了以下警告，见附带 JSON。它们不是本轮新启动 PIE 的结果，尚未确定发生地图与当前复现率。

- `Shotgun_A` 缺少 `LeftHandSocket` 和 `Hand_R`，日志提示 `Weapon grip target unavailable`：至少有一类武器不满足握持接口契约，应逐枪检查表现配置和骨架映射。
- `ABP_BrokenEdgeAnimInstance_Stinger` 的 RightHand `Transform (Modify) Bone` 提示未选择有效修改项：该节点可能没有实际作用，需打开动画图核对用途，不能只看节点存在。
- 创建 CrowdManager 时未找到 RecastNavMesh：需要结合实际加载地图检查导航，不能直接推断所有守卫都无法导航。
- 未找到 `/Script/ApexDestruction.DestructibleMesh`：存在旧类依赖警告，需要定位资产引用；不可未查引用就批量删除资源。

上述段落保留 11 月 6 日日志的历史身份。11 月 7 日已重新读取关键资产，并对资源注册表中的 99 个项目蓝图执行编译，详见 10.2、10.8；没有逐节点人工审阅全部图表。Rider 离线属性读取返回空不能当作“资产没有属性”。

## 5. 资产组织与生成流程

### 5.1 当前具体问题

| 编号 | 现状 | 影响与处理 |
| --- | --- | --- |
| R01 | 搜打撤功能资产跨 `/Game/Extraction`、`/Game/Blueprints/Extraction`、`/Game/UI/Looting`、`/Game/Characters/Stinger`、`/Game/Maps/SpaceCenter` | 功能归属与通用资源边界不清；先建立索引，再按依赖迁移，不能直接在文件系统移动 uasset |
| R02 | 地图资源在 `Restored/Assets/<哈希>/...`，物品世界模型也有哈希名和 SharedMeshes | 可追溯导入但人工查找困难；保留稳定来源 ID，增加“用途／显示名→资源路径→来源”的索引，不因难读就全部改名 |
| R03 | 医疗枪等目录存在多层来源名；人物同时有 `BP_StingerCharacter`、`BP_StingerCharacter_Repaired` | 标明正式入口与修复／历史产物。当前默认 Pawn 使用前者；后者是否仍被引用需查引用图，不能按名字判冗余 |
| R04 | 项目根并存多套素材包与 `Content/test`、`Developers` 等目录 | 需按引用、Cook 清单和用途分类；目录名并不能证明资源无用或已打包，当前未做全量引用／Cook 体积审计 |
| R05 | 构建脚本固定引擎 `C:/UnrealProjects/UnrealEngine-5.8.3-release`；导入脚本依赖工作区外的来源目录 | 换机器难复现；统一可配置引擎／原料根目录，建立输入清单、版本与哈希、产物路径和生成步骤 |
| R06 | 两个根目录打包 bat 的地图清单缺少 `L_SovietMilitaryExtraction`，`Scripts/Extraction/run_build.py` 包含它 | 入口选择可能改变产物，统一唯一地图／目标清单；不能仅以编辑器能打开地图证明打包包含 |

本次 `rg --files Content` 分组看到 Maps 约 21048、Extraction 2795、Characters 1858 个可见文件，说明应优先整理地图和导入索引。这是文件数，不是已加载资源数、Cook 资源数或磁盘占用，不能据此得出性能／包体结论。

### 5.2 建议的目标归属（规划，不在本次移动）

```text
Content/
  Extraction/
    Core/                 # 通用玩法蓝图入口、全局定义
    Items/                # 物品定义、图标、实例无关数据
    Inventory/UI/         # 局内背包与大厅仓库共用展示资产
    Weapons/              # 每枪表现配置、配件、挂点约定
    Abilities/MedicalGun/ # 药枪模型和道具表现
    World/Interactables/  # 容器、电闸、注水控制器及外观定义
    Maps/Definitions/     # 战局布局与地图规则 DataAsset
  Characters/Stinger/     # 人物骨架、动画、角色专属表现
  Maps/SpaceCenter/       # 关卡和场景专属资产
  Shared/                # 明确被多个功能共享的自有资源
```

导入原料／来源包保持独立边界，不为统一外观目录而把第三方包拆散。迁移前先导出引用关系，尤其检查源码字符串、ini、Python 脚本中的软路径；通过编辑器移动／修复引用并完成加载与 Cook 验证。先迁移一小类可控资源，再推广。版本库中的 `Docs/*Evidence/files/Source` 是历史证据，搜索当前实现时应排除，避免误改旧快照。

## 6. 验证与文档问题

| 编号 | 问题 | 需要补齐的证明 |
| --- | --- | --- |
| V01 | 数据层测试和 UI 操作不是同一层证据 | 测 `SwapGridItems` 之外，分别从大厅和局内实际拖放，再检查服务器与持久化结果 |
| V02 | 图标验证检查 ScaleBox、非零布局和纹理存在，但未见最终宽高比断言 | 首次冷开背包的最终比例与画面证据；预热后的独立预览不能替代 |
| V03 | 偶发红点与激素枪反馈缺少同一次操作的状态、模型、动作和结果关联 | 使用物品 ID／动作序号串联请求、权威结果、客户端表现和录像 |
| V04 | 历史 PASS 不能代表当前成功；本夜新执行的测试也存在断言和测试数量漂移 | 当前结果见第 10 节；每次验收记录提交、构建时间、地图、模式、人数、延迟、资产版本及运行日志 |
| V05 | 既有“功能全景”的“已实现”偏向源码链路，与体验验收并不等价 | 把存在实现、资产已配、运行通过分列；本清单中的用户问题在实际验证前保持开放 |
| V06 | 注释有漂移：例如治疗“缺血”描述与满血可上止痛规则、对空射击与旧的自动自用说法不一致 | 注释与按键提示统一到当前确认的产品规则，避免维护者按旧注释再次改错 |

现有 `ExtractionSwapPocketTests.cpp`、`ExtractionStartingSupplyTests.cpp` 和 `InventoryWidgetBuilder` 的验证函数值得复用，但应增补行为要求，不能只增加重复实现逻辑的测试。本夜运行的数据层与资产验证结果见第 10 节，不能替代 UI 行为验收。

## 7. 建议整改顺序与关闭条件

1. **先修物品安全与误操作：L01、U05、U03。** 修复空容器回填后销毁、清除 G 冲突，统一可换弹弹药范围，初始物品放置不损失旧物品。
2. **再统一物品事务：U02、U07、F04。** 局内／大厅双向交换、失败回滚、Overflow 保全一起验证。
3. **修复已捕获的表现根因：U01、U08、U09。** 按本夜异常帧、调试变量与请求状态修正纹理比例时序、武器可见性归属和能力表现；激素枪保持 P1，不能用换动画文件代替完整验证。
4. **补完整玩法与模型：U04、U06。** 探头需要网络与碰撞边界；机关需要生成路径接入和实际场景验收。
5. **渐进整理架构与资产：A01–A09、R01–R06。** 每次迁移范围有限，保留可追溯路径与生成来源，避免在功能定位期间整仓改名。

每个问题关闭时至少记录：复现步骤、修改的源码／资产、构建版本、验证环境、预期与实测、仍未覆盖的边界。物品问题必须核对 GUID／数量／附件／弹匣和保存；视觉问题必须有实际视口证据；网络问题必须有独立客户端，不能用单机静态检查替代。

## 8. 证据与工作区说明

- [11 月 6 日编辑器只读资产及日志记录](../Artifacts/Extraction/Audit_2026-11-06_Assets.json)：输入映射、物品定义、Widget 模板、蒙太奇绑定与长度、编辑器状态及缓存警告；11 月 7 日保留原件，未重新采集。
- [既有功能与实现全景](BrokenEdge_功能与实现全景.md)：用于定位模块；本清单的关键结论重新核对了当前源码，未把其中的疑点直接当作事实。
- 本轮开始时本 Markdown、上述 JSON 和既有全景文档已在工作区，均为未跟踪文件。本轮完善本 Markdown，并新增截图证据；临时诊断脚本、独立测试档案与原始日志位于 `Saved/Extraction/Audit20261107*`，独立 Cook 输出位于 `D:/BEAuditCook1107`。未修改玩法 C++、项目配置、蓝图、地图、原 JSON、既有全景文档或正式玩家档案，未提交或推送。
- 本文不是全项目无遗漏证明。已做当前蓝图编译、五图 Cook、独立客户端与进程崩溃恢复检查；未做全部蓝图连线人工审阅、全部资产授权审查、完整打包客户端验收、跨机器网络和长期容量／性能测试。具体通过与失败范围见第 10 节。

## 9. 11 月 7 日复核与补充

### 9.1 九项反馈仍保持开放

本节是夜间审查开始时的源码复核记录。当前 HEAD 与初稿基线一致；相关 C++ 和配置没有本地修改。U02、U03、U05 的调用链、U06、U07 的分派缺口仍存在；U04 未找到主动探头实现。此时 U01、U08、U09 尚缺异常现场，后续已补充独立客户端与调试器证据，最终结论见第 1、10 节。G 扔雷同时丢枪**不是本需求下的正确行为**。

需要进一步明确的实现边界：

- **“手持／背负”必须对应实际持枪状态。** `SelectWeaponSlot` 的 1／2 键固定选择槽 0／1，切枪不会交换库存槽位；UI 若继续使用“手持／背负”名称，落点应解析到当时的 `HeldSlot()`，不能永远把槽 0 当作手持。U02 应先解决这一语义，否则交换成功也可能换错枪。
- **口袋不能当成一个大矩形。** `GridOf` 和 `ExtractionSwapPocketTests.cpp` 定义的是五个独立口袋，相邻口袋不能合放一个两格物品。初始四类备弹应逐栈验证尺寸和堆叠上限；放不下时保留原物品并按明确规则转胸挂／保留溢出，不能仅把 `BagGrid` 文本替换为 `PocketGrid` 后忽略返回值。未在本轮重新读取各弹药 DataAsset，故不声称四栈必定能全部装下。
- **换弹要覆盖守卫路径。** `InitializeGuard` 当前把备弹 `AutoPlace` 到默认网格，守卫也使用库存组件。实施“仅口袋／胸挂供弹”时，应同步核对守卫的配装，避免玩家修正后 AI 只能打完首个弹匣。产品规则是否同样约束守卫，需要在实现中明确，不能偷偷把 AI 作为例外。
- **弹药来源需要统一查询。** `TryReload` 和 `BeginServerReload` 使用 `Combat->CarriedAmmo`，`FinishServerReload` 再调用库存扣弹；HUD、客户端开始条件、服务器开始条件、完成时实际消耗必须一致。通用 `Count` 保留作总库存统计，另建战斗可用备弹查询。库存卡片上的背包弹药数量仍应可见。
- **M4A1 的“有红点”应以实例为准。** 兼容红点不等于这把枪装有红点；当前开局只对完全没有附件的枪补默认附件。但用户已说明异常枪实际带红点，应优先检查“实例有附件、表现没显示”的链路，不以默认配装策略替代用户报告。

### 9.2 指定文件 `ExtractionLootActor.cpp` 的专项审查

文件：[ExtractionLootActor.cpp](../Source/BrokenEdge/Extraction/Inventory/ExtractionLootActor.cpp)。它负责世界中的战利品外观、打开状态和容器内容入口；背包图标比例、拖放事务、R 换弹及技能输入分别由 UI、库存和能力模块处理，不能把九项问题都修在这个 Actor 里。

| 编号 | 证据与结论 | 整改及验收 |
| --- | --- | --- |
| L01／P1 | `Changed()` 仅在 `Loose && Empty` 时设置 0.5 秒生命周期，没有在重新非空时撤销定时销毁。`SpawnContainer()` 只撤销新建时的定时器。交互组件允许已打开的整包掉落物继续处理转入请求，`Execute()` 未把 `Empty` 当作关闭会话条件。**双客户端两次复现成功回填后销毁丢枪，第二轮捕获了销毁前武器确实位于容器的连续帧，见 10.12。** | 选择并固定一种语义：空后立即禁止再存入并关闭所有会话，或重新非空时取消销毁；真正销毁前复查权威内容。用两个操作在 0.5 秒内清空／回填，检查超过 0.5 秒后物品仍存在且 GUID、数量守恒，再做双客户端验证。 |
| L02／P2 | `ApplyDoorPose()` 从 `GetRefBonePose()[Index]` 取参考姿态的局部旋转，却用 `EBoneSpaces::ComponentSpace` 设置结果。父骨骼变换没有在此显式累积。**坐标空间混用是源码可见风险；当前两种保险箱父骨骼是否恰好为单位变换未核验。** | 核验实际骨架层级；按统一坐标空间计算铰链。覆盖非单位父旋转、关卡旋转、初次打开、晚加入和重新显示；不能只测试默认朝向。 |
| L03／P2 | 散落枪械外观读取 `LooseDefinition` 对应 WeaponClass 的默认骨骼模型；该 Actor 没有按物品实例 `Attachments` 创建瞄具等组件，也未复制公开的装配摘要。**地面枪表现不能据此完整反映实例改装**，但不能据此解释已装备 M4A1 的偶发红点缺失。 | 若需求要求地面也显示改装，单独复制最小外观摘要并复用装配表现；不广播完整库存。验证装备→丢弃→拾取的模型和实际配件一致，保持未搜索内容保密边界。 |
| L04／P2 | `BeginPlay()` 每个启用 `SeedContents` 的 Actor 为落地射线遍历所有 Pawn、Weapon 和 Loot Actor；`UpdateFocus()` 又按 Tick 扫描多个交互 Actor 类型。**这是随场景规模增长的遍历成本，不是已经证明的卡顿根因。** | 在航天基地记录实际 Actor 数、初始化与交互查询耗时，再决定邻域查询、注册表或更新频率；避免未测量就大规模重写。 |
| L05／P2 | `DoorBones` 的键绑定模型名和骨名；种子配方、外观切换、骨骼动画与权威内容生命周期共处一个类。部分种子 `AutoPlace` 不检查结果。 | 把配方、模型、铰链和碰撞描述转入容器定义；为配方无法装入给出明确错误。保留 Actor 的服务器内容与复制责任，先迁移一个容器类型验证。 |

以上是新增源码审查项。L01 随后在双客户端中捕获了成功转入后容器销毁、原武器 GUID 消失，见 10.12；其余项尚未运行复现。L02 不能仅凭阅读就认定现有保险箱开门已经坏了；L04 不能代替性能采样。

### 9.3 激素枪另一个可确认的失败处理缺口

`UExtractionAbilities::Complete()` 的对外治疗分支只有在 `SpawnActor<AExtractionStimDart>` 成功时才 `Launch`，但随后无论生成是否成功都设置 `HealReadyAt`、收起激素枪并结束动作。**弹体生成失败时仍会进入冷却，且该分支未返回明确失败原因。** 这是可从代码确认的失败处理问题，不代表已经定位用户所说的“打药不对”。

建议新增 F09／P2：区分“成功发射后打空”与“根本未能生成弹体”；后者应有错误记录和明确的状态恢复策略。验收时注入生成失败，确认不会表现为静默消耗一次技能。正常自用、队友命中、打空、取消仍需完成 U09 的整个验收矩阵。

### 9.4 可直接拆分的整改任务

下表是待执行任务，**不是本轮已交付的修复**。优先解决操作正确性，再处理架构重整。

| 顺序 | 任务与最小修改面 | 关闭条件 |
| --- | --- | --- |
| 1 | U05：搜打撤输入上下文和 `DropButtonPressed` 模式分流，保留显式库存丢弃入口 | G／Alt+G、有雷／无雷、UI 打开和取消均不丢枪；一次有效投掷只消耗一颗雷 |
| 2 | U03：可用备弹查询／扣除、初始配装、守卫配装、HUD | 背包／安全箱弹药不能 R 换弹且 HUD 不计入；口袋／胸挂正确；初始物品不丢失 |
| 3 | U02＋U07：共用请求规划、原子交换、局内／大厅入口与持久化分派 | 两种 UI 双向交换可用；装备槽跟随真实手持状态；空间不足、旧版本请求和武器生成失败完整回滚 |
| 4 | L01＋F04：掉落容器延迟销毁与出生溢出物资保全 | 清空后回填、生成失败、并发操作均不丢失或复制物品 |
| 5 | U01＋U08＋U09：先增加最小可观测状态并捕获异常，再作定点修复 | 冷启动图标正常；实例、红点模型和瞄点一致；药枪输入、动画、效果与收枪一致，有实机证据 |
| 6 | U04：探头输入、摄像机、第三人称与服务器射击边界 | 左右、回中、贴墙、瞄准、倒地和远端姿态全部通过；与 E 交互不冲突 |
| 7 | U06：机关外观定义和可配置生成类，接入注水／撤离／吊桥生成路径 | 航天基地实际点位显示合适的电闸箱／注水控制台，比例、交互和网络状态正确 |
| 8 | A01–A09、R01–R06、L02–L05：逐模块整理结构与资产 | 明确源文件与生成物归属；引用校验、地图加载和 Cook 通过后才关闭迁移项 |

本节记录早期源码复核阶段；后续构建、自动化与运行检查见第 10 节。九项用户反馈均未标记为修复完成。

## 10. 11 月 7 日夜间持续审计记录

### 10.1 构建与数据层测试（04:00—04:30）

环境：Windows、UE `C:/UnrealProjects/UnrealEngine-5.8.3-release`，源码基线未改变。测试使用 `Saved/Extraction/Audit20261107*` 内的独立输出／存储目录；没有修改玩法源码、资产或现有玩家档案。以下是本夜执行结果，不沿用历史 PASS。

| 检查 | 当前结果 | 证明边界 |
| --- | --- | --- |
| Editor 构建检查 | 成功；UBT 报告 `Target is up to date`，本次编译动作 0 | 当前 Editor 目标可用，不是全量重编译，也不是 Client／Server 打包证明 |
| Inventory | 5 项通过、0 失败 | 原包装脚本期待 3 项，因数量不符返回失败；引擎测试结果实际通过 |
| Persistence／Equipment／Progression／WeaponMechanics | 分别 4／4／5／5 项通过 | 数据与规则断言；不代表拖拽、动画或最终瞄具画面通过 |
| GuardBT／SpawnLifecycle／ExitFeedback | 分别 12／3／3 项通过 | 部分测试是临时世界；临时世界缺导航警告不能直接归于正式地图 |
| LootLayout.Selection | 1 项失败，24,000 次同一断言错误 | 断言仅接受骨骼 `Appearance`；当前容器存在合法 `StaticAppearance`，见下文 |
| Abilities.MovementRules | 1 项通过 | 只覆盖该规则测试，不覆盖激素枪输入、表现、打药全过程 |
| Expansion | 3 项通过，`WeaponDefinitionTransactions` 失败 | 临时修改弹匣定义后，工厂与校验／详情使用不同容量来源 |
| Maps／World | 各 1 项通过 | 断言范围内的地图定义与世界规则，不是完整地图游玩验收 |

合计 **49 项：47 通过、2 失败**。异常断言数量不能当作独立缺陷数量。原始证据：`Saved/Extraction/Audit20261107Build01`、`Saved/Extraction/Audit20261107Inventory01/Automation/index.json`、`Saved/Extraction/Audit20261107/Rules/*/Automation/index.json`。

**V07／P2：测试入口的通过条件已漂移。** `Scripts/Extraction/run_editor_task.py` 写死 Inventory 数量 3、Progression 数量 4，当前实际分别 5、5。应验证预期测试名称与每项结果，避免新增测试后整组误报失败，也避免缺测试仍被数量巧合掩盖。

**V08／P2：容器外观测试未覆盖静态模型。** `ExtractionLootLayoutTests.cpp` 对每个非散落物只断言 `Appearance != nullptr`。本夜读取布局，启用的非散落物候选中有 48 个仅使用 `StaticAppearance`；未发现同时缺失两类外观的此类候选。每轮选中 24 个、运行 1000 个种子，产生 24,000 条重复失败。现有证据支持“测试契约过时”，不能据此报告“24,000 个箱子没有模型”。应断言实际支持的外观组合，并另验收静态箱盖和交互碰撞。

**A10／P1：弹匣容量存在双重来源。** `ExtractionItems::Make` 使用 ItemDefinition 的 `MagazineCapacity`，校验与详情则使用 `ExtractionItems::MagazineCapacity(Item)`，后者优先读取解析后的 WeaponMechanics 参数。失败测试把 AK 的定义容量临时改为 20，创建弹匣 20，但填入 21 后校验仍通过，详情也不是 `/20`。当前实际 AK 定义与机制容量同为 30，因此此测试不证明现有 AK 已超装；它证明配置变化时契约会分裂。应明确基础值与改装后容量，创建、UI、校验、换弹共享最终容量规则，并调整测试构造。

### 10.2 关键资产重新读取

本夜通过 Editor Python 命令行读取 367 个物品定义、角色默认对象、布局点及资源注册表；运行 Widget 和 Extraction Blueprint 校验器，均报告通过。编译校验没有保存资产。源码／配置中本次提取的完整 `/Game` 字面量路径均能解析为已有资产或目录；此范围不含动态拼接、所有蓝图内部引用，也不证明 Cook 完整。

两个人物蓝图当前均绑定了自用／对外、第一／第三人称四个治疗蒙太奇。不能再以“没有绑定动画”概括 U09。IMC 第一版采集访问 UE 已废弃的 `Mappings`，空值已排除；随后按本地引擎接口读取 `DefaultKeyMappings.Mappings`：通用上下文 12 条、Stinger 上下文 13 条，**两者当前均包含 G→IA_Drop，且没有附加触发器／修饰器**。新证据为 `Saved/Extraction/Audit20261107/Assets/Mappings.json`。

### 10.3 激素枪与动作互斥的新增源码问题

**F10／P1：第一人称步枪副本不受激素枪隐藏逻辑管理。** `ExtractionAbilities::RefreshStimGun` 隐藏的是 Weapon Actor，并生成药枪组件；`BrokenEdgeCharacter::UpdateFirstPersonWeapon` 管理角色上的独立 `FirstPersonWeaponMesh`，其可见条件仅包含武器存在、未死亡、未攀爬、未近战、未使用瞄准镜，没有药枪／能力动作或 Weapon 隐藏状态。两套组件的职责与条件不一致。随后独立客户端已复现步枪与药枪同时显示、红点附件消失，见 10.4；尚不能把它等同于用户反馈的全部根因。

**F11 修正为 A11／P2：动作中断依赖隐含副作用，射击并行推测不成立。** 第二轮运行显示开始自用治疗后开枪，治疗取消、不写入新冷却，射击成功。进一步核对发现 `ServerRequestShotVersioned`、换弹和近战选择都会调用 `CancelMedical()`，该函数同时取消能力动作，而不只是医疗物品。不能仅因入口没有检查 `Action` 就认定可以同时施法射击。实际维护问题是跨模块副作用与函数命名不一致，应统一并记录“拒绝／中断／允许并行”的规则；手雷与拔枪状态组合仍单独待查。

**F12／P2：手雷出手点缺少贴墙校验。** `ServerThrowGrenade` 从眼睛沿视线前移 40 厘米，用 `AlwaysSpawn` 生成，没有检测眼睛到出手点的遮挡。相邻激素飞镖路径已经有墙面射线修正。薄墙附近可能从墙后生成手雷，需贴墙实测；不是当前已复现的穿墙投掷结论。

**V09／P2：激素枪旧网络脚本测试了旧规则。** `phase5_scenarios.py` 仍按 Z 启动治疗并声称它是“实际 Z 绑定”，当前 Z 为趴下，药枪在 V；部分直接对友方治疗请求也没有先拔出药枪，而新服务端要求飞镖动作处于拔枪状态。直接运行此脚本出现失败不能据此判断药效未实现。夜间使用临时诊断场景调用当前生产输入／RPC，并保留原脚本不变。

### 10.4 第一轮独立客户端复现：药枪、步枪与红点

执行 `Saved/Extraction/Audit20261107/run_stim_probe.py`，使用专服＋两客户端，A 以 1280×720 离屏渲染，B 使用 NullRHI；中等画质与 768 MiB 纹理池仅作用于该测试进程。场景使用当前生产输入和能力请求，独立档案 `Audit20261107Stim01`。诊断脚本完成并正常退出，**脚本的 `passed` 表示完成了记录，不表示这些玩法问题通过验收**。

1. 初始 M4A1 上可以看到红点外观。
2. 轻按 V 拔出药枪，主人客户端记录 `weapon_hidden=true`、`fp_weapon_visible=true`，稳定等待后仍如此。
3. 截图显示药枪与第一人称 M4A1 叠在一起；此时 M4A1 本体仍可见，红点附件消失。
4. 选择主武器后，Actor 隐藏状态恢复。更多切换与药效验证继续进行。

因此 **F10 从源码问题升级为已实测复现**。同时定位到 U08 的一个具体触发路径：`ExtractionWeaponVisuals` 用 `NewObject<UStaticMeshComponent>(W)` 创建附件，附件归属于 Weapon Actor；第一人称枪本体却归属于角色。能力隐藏 Weapon Actor 后，红点等附件受隐藏影响，本体副本仍显示。这个模型归属不一致能解释本次捕获到的“实例仍有附件、枪本体在、红点不显示”，但不能覆盖用户所有偶发场景。

原始截图与状态：`Saved/Extraction/Audit20261107Stim01/A-audit-rifle-baseline.png`、`A-audit-stim-drawn.png`、`StimObservations.json`。本轮后续自用治疗被取消且没有药效，但夹具移动与输入时序存在干扰，**暂不把它作为药效故障结论**；下一轮保留原出生位置、等待落地，分别记录单独治疗与治疗期间射击。

### 10.5 第二轮独立客户端：首次图标、G 键与治疗中断

场景 `Audit20261107Player02` 使用新进程、新档案和相同渲染限制。没有预先打开局内背包。正常落地后通过当前输入开启背包、关闭再开，随后在原地将测试生命设为 40，记录实际 V 输入与 G 输入。

- **U01 已复现。** `A-audit-first-bag.png` 中 M4A1、SCAR-H、栓动步枪、泵动霰弹枪明显横向压窄；`A-audit-second-bag.png` 在相同窗口、相同物品和卡片布局下恢复正常比例。两张截图可以直接对照，不再只是用户报告。首次纹理尺寸与 Slate 赋值时序继续定位；此证据本身不能区分纹理初始化与控件布局根因。
- **U05 已复现。** G 前手持 M4A1，手雷 2，世界掉落容器 139；G 后手持变为 SCAR-H，手雷 1，掉落容器 140。服务器物品快照用于核对 M4A1 原实例进入掉落容器，而非凭武器名称变化推断。G 同时执行投雷与丢枪不是合理行为。
- **U09 自用药效链能够完成。** 保持落地，长按 V 后出现治疗动作，生命从 40 上升，服务端写入治疗冷却；后续持续恢复至 100。药枪模型叠枪仍存在，因此不能将 U09 整体标成通过。
- **治疗中开枪会中断。** 第二次治疗开始时 `action=2`，随后实际开火使 `action=0`、弹匣 30→27、有效射击计数 0→3；冷却保持上一次数值，没有完成第二次治疗。源码补查明确由 `CancelMedical()` 中的能力取消触发，已修正早期并行推测。
- **切回枪械后红点可恢复。** 六轮主副武器／药枪切换中捕获了药枪叠枪状态与切回后的红点；不宣称已穷尽红点偶发问题。

本轮诊断完成并正常退出；完整原始记录为 `Saved/Extraction/Audit20261107Player02/PlayerObservations.json`。这些运行证据支持定位，不表示已有代码修复。

可直接查看的脱敏画面：[第一次背包](../Artifacts/Extraction/Audit_2026-11-07_Evidence/first-bag.png)、[第二次背包](../Artifacts/Extraction/Audit_2026-11-07_Evidence/second-bag.png)、[正常步枪与红点](../Artifacts/Extraction/Audit_2026-11-07_Evidence/rifle-baseline.png)、[药枪叠枪且红点消失](../Artifacts/Extraction/Audit_2026-11-07_Evidence/stim-overlap.png)。

### 10.6 存档与长期运行结构风险

| 编号 | 当前源码证据 | 风险边界及建议 |
| --- | --- | --- |
| A12／P1 | `ExtractionPersistence::Commit` 每次复制、验证并序列化整个 Store，追加完整 JSON，执行同步 `Flush(true)`；`Load` 重启时逐条解析并验证全部历史记录。没有日志压缩入口 | 写盘量与启动耗时随档案／历史事务增长；目前未做长期负载测量，不声称已有卡顿。应测不同档案数和事务数，设计校验后的快照切换、日志截断与恢复协议 |
| A13／P1 | `Raids` 保留已关闭战局和 Original 库存，`EconomyReceipts` 持续追加，没有保留策略；单条完整快照上限 64 MiB，超限归入写入故障并锁定服务 | 这是明确的容量上限与增长方向，不代表当前已达阈值。按幂等／恢复所需期限压缩历史，超限前监测并给出具体错误，避免只显示“写入失败” |
| A14／P2 | Account 组件每 0.25 秒完整赋值 Profile 并 `ForceNetUpdate()`，即使 Revision 未改变 | 增加服务端复制对象与脏比较成本；UE 不必每次把相同快照全部发上网，不能据此直接推算带宽。可按 Revision／锁定变化更新，先记录实际 CPU／网络数据 |
| A15／P2 | 新凭据等待客户端确认期间 `Bound=false`，重复空身份 `ServerBind` 可再次走 `OpenProfile` 创建档案；没有 pending 身份关联 | 异常或重复请求可产生多份无用档案，并放大全库日志开销。应保存待确认身份，使重试返回同一份结果；本夜没有对正式档案执行此操作 |
| A16／P2 | 仓库 `Move` 仅记最后一个 Operation，命中相同 ID 直接成功；Economy 另有完整签名比较 | 两类事务重试语义不同。相同 ID／不同内容的移动被当作成功但不执行，UI 回执可能误导；没有证据表明物品因此复制。建议统一操作签名及回执策略 |

测试中的 CRC、断尾恢复、提交失败不发布候选状态等保护应保留；不要为了减小文件直接删除日志中的历史帧。这里指出的是扩展与维护边界，不否定已有持久化完整性测试的通过结果。

### 10.7 U01 调试器定位：冷纹理占位尺寸被写入 Brush

使用 Rider 原生 LLDB 附加到本夜独立诊断客户端，未更改运行配置或玩法源码。断点实际命中 `LootingMainView.cpp:159`，即 `Icon->SetBrush(Brush)`。调用链为 `AExtractionPlayerController::Tick` → `ULootingMainView::Refresh` → `RebuildEquipment` → `ULootingWidgetPart::SetItem`。

| 同一 M4A1 图标 | 第一次打开 | 第二次打开 |
| --- | --- | --- |
| Item.Size／Rotated | 4×2／false | 同一物品 |
| 纹理 ImportedSize | 578×200 | 578×200 |
| PrivatePlatformData.SizeX／SizeY | 0／0 | 578／200 |
| 即将赋值的 Brush.ImageSize | 115.199997×115.199997 | 230.399994×79.7231827 |
| Brush 宽高比 | 1 | 2.89，与图像相符 |

本地引擎 `Texture2D.cpp:349–377` 说明：在 `WITH_EDITOR` 下，`GetSizeX/Y` 对尚处于默认纹理状态的对象返回默认纹理尺寸。`LoadSynchronous()` 得到 UObject 不意味着纹理异步构建完成；第一次 `SetItem` 因而把占位纹理的方形比例固化进 Brush。后续纹理资源变为长条图，但代码没有在资源就绪后重算 Brush，直到重新打开背包触发重建。

这是 **已复现且有运行变量支撑的 U01 根因**。不能只改 ScaleBox。整改应使用可靠的实际图标尺寸／长宽比，在纹理就绪时更新 Brush 并使布局失效；可以用异步预加载或已保存的比例元数据，避免每帧同步等待纹理编译。编辑器冷启动与 Cook 后客户端应分别验证：本次精确定位发生在 Editor `-game`，不能据此断言 Shipping 也走相同占位分支。

调试过程中没有修改变量；仅调用尺寸 getter 的求值因优化符号不可用而失败，结论使用实际可读字段、Brush 值和本地引擎实现。代理断点已清理，用户原有异常断点未改动。结束后 Rider 状态为无调试会话，原有 8 个用户异常断点仍为 5 个启用、3 个禁用。

### 10.8 全项目蓝图与脚本测试（04:46）

按资源注册表生成 `/Game` 蓝图允许列表，调用引擎 `CompileAllBlueprints`，不保存资产。命令返回 **2**，结果为 **2 个编译错误、16 个编译警告、0 个蓝图加载失败**。启动阶段和最终汇总会重复输出同一错误，不应按日志行数累计缺陷。

**B01／P2：第三方示例角色蓝图没有完成当前引擎迁移。** `/Game/ParagonMuriel/Characters/Heroes/Muriel/MurielPlayerCharacter` 的 `Reset Orientation and Position` 节点残留不存在的引脚，并找不到 `ResetOrientationAndPosition` 函数；日志同时指出 `/Script/XRBase` 导入不可用。它导致全项目蓝图编译失败。当前正式角色是否引用该示例、以及 Cook 是否受影响仍单独核查；不据此认定当前 Stinger 角色无法运行。

**B02／P2：旧动画蓝图存在失效变量和空操作节点。** `ABP_BrokenEdgeAnimInstance_Muriel` 的 AO_X／AO_Y 属性引用及 AimOffset 路径失效；多个动画蓝图的 `Transform (Modify) Bone` 没有启用任何变换分量，包括当前 Stinger 动画图的 RightHand。空操作警告本身不能解释所有武器姿态错误，但应确认节点是否仍承担实际设计意图并清理生成器与资产的漂移。

**R07／P3：部分导入资源保留旧版元数据。** MilitaryWeapSilver 的若干 SoundCue 节点缺 NodeGuid，旧 `Orion_Proto_Retarget` 引用已不存在的 Rig 类。需要按引用范围逐项迁移、重新保存和 Cook 验证；本夜审计没有批量重存第三方资产。

原始记录：`Saved/Extraction/Audit20261107/Registry/BlueprintAllowList.txt`、`CompileBlueprints.log`。另运行 `python -m unittest discover -s Scripts/Extraction -p test_*.py`，**174 项通过，耗时 3.182 秒**；它证明 Python 测试断言通过，不证明蓝图或玩法体验通过。日志为 `Saved/Extraction/Audit20261107/PythonTests.log`。

### 10.9 源资产体积与注册表边界

重新枚举 `rg --files --hidden Content`：29,220 个文件，合计 16,042,626,232 字节，约 **14.94 GiB**。其中 `.uasset` 29,179 个、`.umap` 27 个、PNG 13 个、JSON 1 个。没有发现小文件形式的 Git LFS 指针资产，也没有绝对路径达到 240 字符的当前 Content 文件。这个结果只覆盖本次文件枚举，不能代替完整 LFS 对象校验或证明迁移后的路径仍安全。

| 源目录 | 文件数 | 源文件体积 |
| --- | ---: | ---: |
| Maps | 21,048 | 4.56 GiB |
| UVB80 | 536 | 4.17 GiB |
| Characters | 1,858 | 3.31 GiB |
| SciFiWorld | 455 | 0.89 GiB |
| ParagonMuriel | 1,369 | 0.77 GiB |
| Extraction | 2,795 | 0.64 GiB |

因此资产整理不应只按文件数量排序：UVB80 和人物来源资产也值得优先建立用途、引用和导入索引。最大的几个包包括约 96.5 MiB 的当前航天基地地图、约 96 MiB 的 SciFiWorld 光照 BuiltData，以及多份 70–90 MiB 的人物来源包。**这些是源包体积，不是 Cook 体积、显存或帧耗时；不能据此直接删除来源包或断言性能根因。** 明细为 `Saved/Extraction/Audit20261107/PhysicalAssets.json`。

资源注册表返回 29,209 条资产记录；资产记录与物理包文件不是同一个计数单位。5 条 Redirector 记录去重后位于 2 个包，不能写成 5 个独立残留文件。3 个旧 ApexDestructibleMesh 包未出现在先前查询的三张搜打撤／大厅地图的注册表依赖闭包中；该查询不含 C++／ini 动态路径，不能据此直接判为可删。

### 10.10 出生与工具初始化的失败处理

**F13／P2：初始化配装未处理武器 Actor 同步失败。** `InitializeLoadout()` 在设置 `bInitialized` 后调用 `SyncWeapons()`，忽略其 bool 返回；`StartWaitingPlayers()` 只按是否有 Pawn 计入 Ready。与普通物品事务中“同步失败就回滚”的处理不同，这里缺少明确的初始化失败状态与恢复策略。当前实际定义／运行场景能够生成武器，本项没有复现缺枪；应注入武器生成失败，验证不能静默进入“库存装备槽有枪、场景无枪”的状态。

**F14／P1：出生兜底绕过安全条件和分组唯一性。** `ChoosePlayerStart()` 的正常候选会排除 `ClaimedSpawnGroups` 并校验地面／胶囊；候选全部失败时，兜底仅按 `Positions.Num()` 选择最多点位的组，没有排除已占用组。随后单点探测失败仍使用 authored 坐标加胶囊半高，并以 `AlwaysSpawn` 创建起点。源码可以确认这些校验被绕过，尚未实测不同小队重叠／穿地。建议设置经过验证的独立备用组，或维持可恢复的等待／退出状态；覆盖无有效地面、碰撞占用、所有组已分配、地图流送未完成。已通过的 SpawnLifecycle 测试使用缓存起点与 DefaultPawn，刻意隔离了地面探测，不能覆盖本项。

**V10／P3：Editor 游戏进程的工具插件启动有非致命 Python 错误。** 独立 `-game` 客户端日志显示 StateTreeToolset 等启动脚本访问不存在的 `unreal.ToolsetDefinition`，其他工具还涉及 AgentSkill／PythonTestRunner。项目显式启用 AllToolsets／AIAssistant；本地插件描述符为 EditorOnly，部分模块仅 Editor 目标。应统一 Editor、命令行、游戏进程的工具初始化条件，避免错误噪声掩盖玩法日志。当前客户端仍成功运行，**没有证据说明这些插件进入 Shipping，也不能把它们当作本次玩法失败的根因**。

### 10.11 五张地图 Windows Cook（05:08）

在独立新目录 `D:/BEAuditCook1107` 执行 Windows Cook，覆盖 Lobby、FreeForAll、Teams、SpaceCenter、SovietMilitary 五张配置地图，未复用既有 Cook 输出。进程退出码 **0**，汇总 **0 错误、152 警告**，最终 **19,707 个包完成、剩余 0**；外部计时约 20 分 35 秒。日志为 `Saved/Extraction/Audit20261107/Cook/Cook.log`，完整命令和退出信息为同目录 `Run.json`。

本次通过的是资源烘焙，不包含新建 Client／Server 二进制、Stage、安装包启动或 Shipping 验收。Cook 会复用已有 DDC，本次也不能称为清空缓存后的全量构建。Cook 成功不否定 10.8 的全项目蓝图编译错误，两种检查覆盖范围不同。

警告包括已记录的旧 SoundCue NodeGuid、Muriel 动画变量、Stinger RightHand 空操作、旧 Rig 类缺失。`MemoryMaxUsedPhysical` 弃用和资产编译内存上限警告与本次进程的内存限制参数有关，不列为项目默认配置缺陷。日志只在尾部展示前 50 条警告，152 是引擎汇总数，不能当作 152 个独立玩法问题。

### 10.12 武器交换、备弹来源与掉落容器丢枪（05:12）

独立场景 `Audit20261107InventoryProbe01`，专服＋两客户端、全新档案，使用实际 Slate 鼠标拖拽和当前生产输入／物品 RPC。诊断流程完成，完成状态不代表玩法通过。

- **U02：主槽双向交换失败已实测。** 背包栓动步枪拖到主槽 M4A1 返回“空间不足、位置占用或无法堆叠”；M4A1 拖到该背包武器返回“只能交换网格中的完整物品”。服务器库存未交换，拒绝路径与源码一致。背负槽仍以源码证据为主。
- **U03：背包与安全箱弹药均可换弹已实测。** 初始 Ammo55645 在背包，数量 60、备弹 60。开两枪后弹匣 28，R 后弹匣 30、背包弹药 58、备弹 58。用正常物品 RPC 将其移至安全箱，再开两枪并按 R，弹匣重新为 30、安全箱弹药 56、备弹 56。场景里没有口袋／胸挂的同种弹药；因此可以直接确认不符合目标规则。
- **L01：空散落容器回填导致武器消失已实测。** B 丢下整包，A 打开该包形成普通查看会话；B 取回整包使容器清空，A 收到空视图后立即用当前会话／版本转入主武器。服务端记录成功 `EXTRACTION_TRANSFER Sequence=4 ... ToLoot=1`，随后 A 切为副武器，目标容器从 140 个容器中消失，原 M4A1 GUID `E1D1EC4C4A2B78EEB73D7F8FA876A52A` 不在任何玩家库存或世界容器中。没有伪造 Revision；fixture 仅将两名测试玩家放到可交互范围。

L01 成功转移日志位于 `ExtractionInteractionComponent::Execute()` 完成库存事务、武器同步、`Loot->Changed()` 和 `Inv->Changed()` 之后；与 `Changed()` 只设置、不取消 0.5 秒生命周期的源码相吻合。建议把它与 G 键冲突、交换事务一起放在物品安全修复的首批。

第二轮 `Audit20261107Refill02/LifecycleFrames.json` 记录了完整中间态：世界时间 97.784 秒枪在 A 库存、容器为空；97.885／97.988／98.090 秒枪已在容器且不在玩家库存；98.196 秒开始容器和枪都不再存在。该轮使用新的 M4A1 GUID，独立复现相同问题。安全箱拖拽的初步尝试因目标卡片在滚动区域外、采到零坐标，没有形成物品请求，**不把这次无效尝试记为 U07 失败**。

原始状态：`Saved/Extraction/Audit20261107InventoryProbe01/InventoryObservations.json`、`Server.log`；精简分析：`Saved/Extraction/Audit20261107/InventorySummary.json`。截图 `A-audit-bag-to-equipped.png`、`A-audit-equipped-to-bag.png` 保留拒绝反馈。

### 10.13 引用范围补查

五张地图的注册表硬／软包依赖闭包均未发现不存在的 `/Game` 包；此范围不含 C++／ini 动态加载。旧 `MurielPlayerCharacter` 错误蓝图不在五张图的该闭包中，因此不能用其编译失败宣称正式地图启动失败。另一方面，旧 Muriel 动画蓝图**确实通过 Lobby → BP_LobbyGameMode → BP_BrokenEdgeCharacter 被引用**，其警告不能简单归为完全无关的孤立演示资产。

体积最大的六份 `Characters/Stinger/Animations/Corrected/Source` 包已确认属于 AnimSequence，当前注册表没有包引用者；但导入／生成脚本仍可能按路径使用，不能直接删除。UVB80 的 8K 天空立方纹理在军营地图依赖中；人物全景眼睛纹理在大厅依赖中。应根据实际用途制定压缩和归档策略，保留来源与生成关系。证据：`Saved/Extraction/Audit20261107/References/Report.json`。

另枚举全部 29,206 个 `.uasset/.umap`，对其中所有同尺寸候选计算 SHA-256：14,583 个候选、991,239,743 字节，未发现字节完全相同的重复包。不同尺寸不可能字节相同，因此没有漏掉此定义下的重复；但资产元数据不同可能掩盖相同纹理／网格内容，**本次不是语义去重审查**。证据：`Saved/Extraction/Audit20261107/ExactDuplicates.json`。

### 10.14 真实进程终止后的战局存档恢复

对 prepared、active、success、death 四种持久化边界，等待引擎测试明确写入 `Ready.txt` 后强制终止该测试进程；每个边界使用独立测试存档，并连续启动两个新进程验证。没有终止用户编辑器或读取正式玩家凭据。

| 边界 | 第二次重启后的结果 | AK／备用弹药 | 携行装备 |
| --- | --- | --- | --- |
| prepared | Aborted | 3／360 | 背包、安全箱、胸挂各 1 |
| active | Recovered | 3／360 | 背包、安全箱、胸挂各 1 |
| success | Extracted | 3／360 | 背包、安全箱、胸挂各 1 |
| death | Dead | 2／360 | 安全箱保留，默认背包／胸挂按当前规则补齐 |

**V11／P2：崩溃边界测试保留了过时的“配装必须为空”断言。** 八次重启测试均报告失败，每次唯一失败都是 `ExtractionPersistenceTests.cpp:340` 的 `Loadout.Items.Num()==0`，实际为 3。恢复实现通过 `ReturnGear` 装回三类携行装备，死亡结算也会 `EquipDefaultGear`；因此这条断言与当前规则冲突。枪数量、备用弹药、唯一档案与最终结果断言均通过，不能将此失败解释成恢复丢枪，也不能悄悄把测试标成全绿。

额外只读解析四份测试 journal，完整记录头与 CRC 均合法，最终战局都为 Closed。此场景只覆盖给定边界和测试物资，不证明所有并发交易、断电、磁盘损坏或长期容量都安全。应将旧空配装断言改成明确的装备归属、数量和 GUID 守恒要求。证据：`Saved/Extraction/Audit20261107/CrashBoundaries/Summary.json`、`JournalSummary.json` 及各次 `Automation/index.json`。

### 10.15 经济事务崩溃边界（05:33）

运行当前 `BrokenEdge.Extraction.EconomyCrashBoundary`：购买、出售、任务交付、领奖、撤离结算，各自在提交前与提交后终止进程，再独立重启两次。共 **10 个边界、10 次强制终止、20 次恢复测试，全部通过**，测试脚本最终退出 0。

验证涵盖金钱、经验、步枪与钥匙数量、任务文件消费／资格／奖励状态、未确认请求重试、同请求再次提交，以及第二次重启完整档案快照一致。每个边界使用自己的 Store；未复用前一边界的档案。证据：`Saved/Extraction/Audit20261107/EconomyCrashBoundaries/Summary.json` 和各次 `Automation/index.json`。这些通过项支持现有事务持久化保护，但不消除 A12／A13 的全量快照增长和容量风险，也不覆盖 L01 的战局内世界容器销毁。
