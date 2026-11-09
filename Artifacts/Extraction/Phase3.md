# 阶段 3：物品、交互与搜刮

状态：2026-10-03，阶段 3 限定范围已通过。最终 Editor 编译、Server／Client Development 打包、三组物品规则测试、九组独立进程打包联机测试和旧模式回归均已完成；已复核背包操作、医疗读条、死亡物资、落地模型及双视角截图。

实施起点为 `8f01a60095772ee0e967fe9949335eb6a9f77548`，引擎为 `C:/UnrealProjects/UnrealEngine-5.8.3-release`。本轮未提交或推送 Git；当前实现及最终包的 SHA-256 见 [Summary.json](Phase3Evidence/Summary.json)，归档文件校验见 [ArchiveIndex.json](Phase3Evidence/ArchiveIndex.json)。阶段 1 原始地图及阶段 2 历史验收文件未改动。

## 范围与入口

继续使用 UE 5.8.3、Extraction 大厅入口和 `L_SpaceCenterExtraction`。新增二维格子背包、独立逐件搜索、AK 物品化、真实备用弹药、基础医疗和死亡物资。FreeForAll／Teams 保留原有规则。

撤离点、成功带出和仓库存档属于阶段 4。武器附件、兼容关系、装配属性及改装界面已列入总路线阶段 6，本阶段仅保存 AK 身份和弹匣余弹。

## 局内规则

| 内容 | 当前测试默认值 |
| --- | --- |
| 背包 | 6 列 × 4 行，另有手持和背负两个武器槽 |
| AK | 4 × 2 格，不能堆叠，弹匣 30 发 |
| 步枪弹药 | 1 × 1 格，每堆最多 60 发 |
| 医疗包 | 2 × 1 格，不能堆叠，使用 3 秒后恢复最多 50 生命 |
| 电子元件 | 1 × 1 格，每堆最多 5 个 |
| 工具组件 | 2 × 1 格，不能堆叠 |
| 初始补给 | 每人 120 发备用弹药、两个医疗包，出生点附近放置 AK |
| 搜索 | 200 cm 范围且视线无遮挡；每件 1 秒；各玩家独立揭示 |

E 对当前目标拾取或搜索；无目标时切换两把已装备武器。Tab 打开背包；拖动时 R 旋转；Shift 点击快捷转移；右键菜单支持装备、卸下、使用、拆分和丢弃。拆分窗口支持数字输入、方向键调整及 Enter 开始拖放。H 使用医疗包，再按 H 或 Esc 取消。Esc 优先取消拖动、关闭物品界面，再打开原有退出菜单。

完整物品转移保留 GUID。拆分创建新的身份；合并保留目标身份并退役耗尽的来源。放置和堆叠先验证完整结果，容量不足时两侧保持原状，不自动丢弃占用武器槽中的枪。

容器内容在服务器生成时确定，打开界面不会重新生成。关闭、超距或遮挡中断当前件的搜索，已揭示内容保留；未完成的当前件重新计时。未揭示物品仅同步匿名占位，不发送定义、数量和实例身份。个人库存仅同步给拥有者。

备用弹药来自背包。换弹由服务器计时完成，在一次操作中扣除实际装填数量并增加枪内余弹；中断不扣弹。AK 装包、装备、丢弃和死亡转移保留身份与余弹，背包中的枪没有可交互的世界武器 Actor。

医疗包使用期间锁定，完成时消费和治疗一次。普通行走允许；主动取消、疾跑、跳跃、射击、换弹、切枪、有效伤害、死亡和退出中断使用。满血拒绝使用，未完成时不消费、不治疗。当前效果只恢复生命，护盾不代表护甲，未加入负面状态或蜂医技能。

死亡、主动退出和异常断线采用一次性物资掉落规则；背包及两个武器槽整体迁入死亡物资容器，角色销毁不销毁物资。局内物资不写仓库，不视为成功撤离；下一局重新生成测试补给。

## 实现与资产近似

物品定义使用 `/Game/Extraction/Items/DA_Item_*` DataAsset。局内库存由 Extraction PlayerState 持有；角色交互组件维护范围、搜索和医疗状态；拥有者的 PlayerController 负责请求入口。操作请求包含序号、物品身份、容器版本和目标位置，服务器验证后提交。世界容器保留完整服务器状态，各玩家收到按揭示进度过滤的视图。

物资箱和死亡物资复用已导入的工具箱网格；医疗包复用项目现有拾取模型；步枪弹药复用弹药模型并放大以便辨识；电子元件暂用小型几何体表示。界面用中文名称和数量显示物品，没有宣称恢复原版物品图标。基础医疗以 HUD 读条表达，未添加未经适配验收的医疗手部动画，也没有将医疗枪技能动画冒充普通医疗包动作。

`Scripts/Extraction/prepare_items.py` 可重新生成定义及带有阶段 3 标签的物资点；不修改阶段 1 原始地图与导入网格。两个物资箱分别安排在室内与室外路线，出生点保留地面 AK。

## 重复验证

在项目根目录运行，标签必须唯一。构建、打包和联机用例依次执行，避免共享构建锁、端口或测试进程。

```powershell
$uePython = 'C:/UnrealProjects/UnrealEngine-5.8.3-release/Engine/Binaries/ThirdParty/Python3/Win64/python.exe'
& $uePython Scripts/Extraction/run_build.py --target Editor --jobs 8 --tag InventoryEditorCheck
& $uePython Scripts/Extraction/run_editor_task.py inventory --tag InventoryRulesCheck
& $uePython Scripts/Extraction/run_build.py --target Server --package --jobs 8 --tag InventoryServerCheck
& $uePython Scripts/Extraction/run_build.py --target Client --package --jobs 8 --tag InventoryClientCheck
& $uePython Scripts/Extraction/run_network.py --scenario inventory --packaged --tag InventoryPackageCheck
& $uePython Scripts/Extraction/run_network.py --scenario inventory --packaged --lag --tag InventoryLagCheck
& $uePython Scripts/Extraction/run_network.py --scenario swap --packaged --lag --tag InventorySwapCheck
& $uePython Scripts/Extraction/run_network.py --scenario disconnect --packaged --tag InventoryDisconnectCheck
```

开发版本驱动使用真实 Slate 鼠标／键盘事件、Enhanced Input 及正式 RPC。服务端夹具只安排物品、位置、遮挡物和目标生命；争抢、搜索、转移、医疗与死亡仍经过正式规则。报告中的检查数量包含初始化、同步和散布重试，不代表独立功能数。`--lag` 在每个进程验证 50 ms 单向延迟及 2% 丢包实际生效。Shipping 不编译这些测试命令。

## 验证记录

| 检查 | 当前状态 |
| --- | --- |
| 最终 Editor 编译 | `Phase3Editor12` [退出码 0](Phase3Evidence/compilation-editor/Run.json) |
| 最终 Server／Client Development 打包 | `Phase3PackageServer02`／`Phase3PackageClient02` [Server](Phase3Evidence/package-server/Run.json)、[Client](Phase3Evidence/package-client/Run.json) 均退出码 0 |
| 物品定义与地图生成 | `Phase3Assets01` [通过](Phase3Evidence/assets/Run.json) |
| 占格、旋转、身份、装备限制、拆分合并、满包原子回退 | `Phase3Rules03` [三组自动化测试通过](Phase3Evidence/rules/Run.json) |
| 双人搜刮、库存竞争、UI、医疗与死亡物资 | 最终包普通 [88 项](Phase3Evidence/network/inventory/Report.json)、延迟／丢包 [89 项](Phase3Evidence/network/inventory-lag/Report.json) 通过 |
| 连续切枪及动作中途退出 | 最终包延迟／丢包 [21 项](Phase3Evidence/network/swap-lag/Report.json) 通过 |
| 阶段 2 战斗与弹药同步回归 | 最终包普通 [66 项](Phase3Evidence/network/combat/Report.json)、延迟／丢包 [73 项](Phase3Evidence/network/combat-lag/Report.json) 通过 |
| 三人观战、对局期限、加载超时、异常断线 | 最终包分别 [93 项](Phase3Evidence/network/spectator/Report.json)、[24 项](Phase3Evidence/network/timeout/Report.json)、[4 项](Phase3Evidence/network/load-timeout/Report.json)、[15 项](Phase3Evidence/network/disconnect/Report.json) 通过；加载超时实际等待 120 秒 |
| 蜂医动作、旧战斗、双人武器显隐回归 | 分别 [29 项](Phase3Evidence/regression/actions/pie_validation.json)、[21 项](Phase3Evidence/regression/combat/Combat.json)、[32 项](Phase3Evidence/regression/visibility/weapon_visibility_2p_stills.json) 通过，各 Editor 进程退出码 0 |
| FreeForAll／Teams 打包 LAN 大厅回归 | [7 项通过](Phase3Evidence/regression/legacy-lan/Report.json)，进程退出码 0 |
| 最终日志、截图及实现／包哈希归档 | [Phase3Evidence](Phase3Evidence) 收录 18 组运行记录、诊断记录和截图；索引列出 156 个文件的 SHA-256 |

打包验收使用同一台 Windows 主机上的专用服务器及两个或三个独立客户端，会话后端为 `OnlineSubsystemNull`。延迟测试在各进程核对 50 ms 发送延迟及 2% 丢包配置。结果覆盖上述 Development 包与测试人数，不代表跨机器、公网、8 人容量或 Shipping 验收。异常断线用例有意终止一个客户端，该退出在报告中单独标记。

已复核的关键画面包括：[旋转拖放](Phase3Evidence/network/inventory/A-inventory-drag-rotated.png)、[拆分数量](Phase3Evidence/network/inventory/A-inventory-split-quantity.png)、[右键菜单](Phase3Evidence/network/inventory-lag/A-inventory-context-menu.png)、[医疗读条与物资落地](Phase3Evidence/network/inventory-lag/A-medical-progress.png)、[死亡物资](Phase3Evidence/network/inventory/B-death-container.png)、[场景第三人称](Phase3Evidence/network/spectator/A-third-person.png) 和 [双人第一人称武器显隐](Phase3Evidence/regression/visibility/WeaponVisibilityFrames/20261003-165649-2p/first_person.png)。

规则测试首轮的失败来自测试夹具向 TArray 直接添加其自身元素，触发引擎自引用检查；夹具改为先复制元素，再构造重复身份，重测通过。Editor 启动期的 13 条 `LogAutomationTest: Error: Condition failed` 在阶段 2 的既有 Editor 基线也存在；阶段 3 三组正式测试的结果独立记录为 Success，不以启动期输出替代测试结果。

阶段 2 曾出现一次根因未确定的远端弹量停滞，历史说明仍保留在 [Phase2.md](Phase2.md)。阶段 3 修改武器与库存衔接后，最终包的普通／延迟战斗回归分别通过 66／73 项；这提供了当前版本的弹药同步证据，历史异常的根因仍未确定。

连续切枪诊断中，`Phase3SwapTrace01` 的服务端第二次请求在 `State=0` 时进入，但只有结束通知，没有挂接通知；客户端 A 同轮收到了挂接通知。Extraction 因而改为按蒙太奇通知时间设置服务端提交与结束定时器，武器 GUID、槽位和余弹由一次提交更新，死亡／退出时取消。旧模式仍保留原通知路径。Rider 能附加 Editor 服务端，但项目模块源码断点未能绑定，暂停栈中的项目帧没有符号；此次结论来自独立进程状态和动作日志，不宣称取得了该函数的源码断点证据。代理断点已清理，原有 8 个用户异常断点及其启用状态未改动。

- 旧路径从动画挂接通知进入 `FinishSwapAttachWeapons()`，该通知缺失时，世界武器及库存槽位都没有交换。
- [ExtractionCombatComponent.cpp](../../Source/BrokenEdge/Extraction/ExtractionCombatComponent.cpp) 现在由服务器在 0.754 秒提交、1.333 秒结束；时刻读取自当前蒙太奇，提交前重新验证存活、动作状态和两把武器。
- [CombatComponent.cpp](../../Source/BrokenEdge/Component/CombatComponent.cpp) 的 Extraction 动画通知不再重复修改装备；退出、死亡和世界销毁取消定时器，避免后续回调恢复旧装备。

画面复核还修正了物资悬浮：可见模型底部对齐地面，落地射线忽略玩家、武器和物资交互框。地面 AK 使用固定姿态模型，没有物理抛掷效果。中间包的普通／延迟搜刮及连续切枪曾分别通过 88／89／21 项；为纳入落地修正而停止的 `Phase3PackagedCombat01` 属于代理主动终止，不能当作玩法失败或最终通过报告。最终验收以重打包后的记录为准。
