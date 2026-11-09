# 阶段 5：PVPVE、小队与蜂医能力

状态：2026-10-04 通过下述限定范围验收。实施基线为 `4f30da14041fdbf1d0c929c08e3ad5d14b1149da`，使用
`C:\UnrealProjects\UnrealEngine-5.8.3-release` 的 UE 5.8.3。阶段 5 已提交为 `9c5d035`，文档整理提交为 `c2ea495`；2026-10-04 重新核对远端 `ue5.8-20260930` 指向 `c2ea4952c6d5ba6f1c0f67862ebeb74cfa12fd25`。以下历史验收证据保持原样。

## 本阶段范围

- 两支三人小队，最多六名玩家；大厅自动平衡分队，未准备者可以选队，准备后锁定。队友免伤，小队身份跨大厅往返保留。
- 有存活且未撤离队友时，致命伤进入倒地；30 秒流血倒计时、100 点独立倒地生命、缓慢移动。全灭、补枪、退出及超时进入最终结算。
- 两米内无遮挡，按住 E 五秒救援，恢复 30 生命、零护盾。倒地期间不掉物品、不结算；最终阵亡才执行一次掉落与结算。
- 四名普通守卫，两条巡逻路线；服务器决定感知、交战、搜索和返回，使用已有 AK、弹药和物品掉落流程。每名守卫初始 100 生命、零护盾、30
  发装填弹药、60 发备用弹药和一个医疗包。
- Z 治疗自己或瞄准的队友：十米、两秒引导、恢复最多 50 生命，成功后冷却二十秒。
- X 投放烟雾：最远十五米，障碍物截断投放位置；半秒施放，半径四米、持续十五秒，生成后冷却三十秒。烟雾阻断视野和 AI 视觉，子弹可以穿过。
- 治疗、救援、医疗包与技能施放互斥；中断不产生效果或冷却。死亡及撤离后只可观战存活队友。

这是可调原型规则和基础表现，不声明与原作技能数值、动画或特效一致。技能、倒地和救援参数集中在 [UExtractionRules](../../Source/BrokenEdge/Extraction/ExtractionAbilities.h)
，可由 `[/Script/BrokenEdge.ExtractionRules]` 配置。守卫当前使用 30 米、120 度视野、0.7
秒反应和五秒搜索，见 [ExtractionGuard.cpp](../../Source/BrokenEdge/Extraction/ExtractionGuard.cpp)。

倒地使用蹲姿慢移和 HUD 倒计时；烟雾使用不透明噪声球体，视点进入体积后叠加遮罩。它们是功能原型，尚无专门救援动画或原作烟雾特效。烟雾不阻挡移动和子弹，守卫走出遮挡后可以重新发现玩家。

## 数据边界

AI 使用角色所属背包，不创建玩家档案、出战票据或大厅席位。AI 掉落保留物品实例和来源批次，带出后沿用现有个人结算。仓库存档格式保持兼容。测试使用每次运行独立的
Store，不读写用户仓库。

## 验证记录

完整证据见 [Summary.json](Phase5Evidence/Summary.json) 和 [ArchiveIndex.json](Phase5Evidence/ArchiveIndex.json)。本阶段归档
25 组通过的构建、准备及测试运行，保留命令、进程号、状态快照、压缩日志、截图，以及实现文件和 Development 包的 SHA-256。阶段 2～4
的历史证据未修改。

以下 11 组使用打包后的独立专服和客户端，共 744 项通过断言；观战报告另保留两条散布重试诊断，未计入断言数。

| 场景                                   | 人数     | 通过断言 | 证据                                                      |
|----------------------------------------|----------|----------|-----------------------------------------------------------|
| 小队、治疗、救援、烟雾、全灭、大厅往返 | 6        | 72       | [六人小队](Phase5Evidence/six-squad/Report.json)          |
| 同上，延迟和丢包                       | 6        | 73       | [六人小队延迟](Phase5Evidence/six-squad-lag/Report.json)  |
| 导航、巡逻、感知、换弹、AI 掉落与带出  | 6 + 4 AI | 57       | [PVPVE](Phase5Evidence/six-ai/Report.json)                |
| 同上，延迟和丢包                       | 6 + 4 AI | 58       | [PVPVE 延迟](Phase5Evidence/six-ai-lag/Report.json)       |
| 三轮倒地生命周期与结算边界             | 6        | 108      | [生命周期](Phase5Evidence/six-lifecycle/Report.json)      |
| 背包、搜索、医疗、死亡物资与下一局     | 2        | 92       | [背包回归](Phase5Evidence/inventory-network/Report.json)  |
| AK、伤害、换弹、握持与局中加入拒绝     | 2        | 66       | [战斗回归](Phase5Evidence/combat-network/Report.json)     |
| 补枪、队友观战、退出和下一局           | 3        | 103      | [观战回归](Phase5Evidence/spectator-network/Report.json)  |
| 撤离、阵亡、断线和超时独立结算         | 4        | 38       | [四人结算](Phase5Evidence/four-outcomes/Report.json)      |
| 撤离计时、满仓溢出、领取、拆分与准备锁 | 2        | 39       | [仓库结算](Phase5Evidence/settlement-network/Report.json) |
| 成功落盘未回执时崩溃，两次重启恢复     | 2        | 38       | [联机恢复](Phase5Evidence/recovery-network/Report.json)   |

六人场景验证每队三人、第七名玩家被拒绝、准备后禁止换队、队友免伤、实际 E/Z/X 按键、救援竞争和中断、倒地不提前掉落、救起 30
生命／零护盾、烟雾位置和到期时间复制、烟雾阻挡 AI 感知而允许子弹通过。生命周期场景覆盖流血死亡、倒地超时、主动退出、断线、救援者受伤，以及最后存活队友撤离。

其他验证均通过：

- [Editor](Phase5Evidence/compilation-editor/Run.json)、[Server](Phase5Evidence/package-server/Run.json)、[Client](Phase5Evidence/package-client/Run.json)
  构建和两种 Development 包。
- [导航准备](Phase5Evidence/navigation-assets/Run.json)：先结束异步资产编译并解除导航初始化锁，再验证两条路线共六段完整路径。打包
  PVPVE 中四名守卫均实际到达巡逻点，包含楼梯路线。
- [真实三客户端 PIE](Phase5Evidence/pie-lobby/Run.json)
  连续两局，保留物品实例、小队和出战票据；[四次启停](Phase5Evidence/pie-teardown/Run.json) 后无残留 PIE 世界。Teams、FreeForAll
  的 PIE 切图和 [7 个打包 LAN 用例](Phase5Evidence/legacy-lan/Report.json) 通过。
- 三组物品规则、四组持久化规则，以及 [四种落盘边界各两次恢复](Phase5Evidence/crash-boundaries/Report.json)。AI
  武器带出后在 [第一次](Phase5Evidence/ai-recovery/first/Run.json)
  和 [第二次](Phase5Evidence/ai-recovery/repeat/Run.json) 独立进程读取中均保持同一物品仅一份。
-
检查了 [救援画面](Phase5Evidence/six-squad/A-being-rescued.png)、[烟雾外部](Phase5Evidence/six-squad/A-smoke-outside.png)、[烟雾内部](Phase5Evidence/six-squad/C-smoke-inside.png)
和 [AI 物资撤离结果](Phase5Evidence/six-ai/A-ai-loot-extracted.png)。Stinger 第一人称背包隐藏、第一／第三人称持枪及双手握持回归通过。

## 验收边界与复跑

全部运行位于同一台 Windows 主机：Ryzen 5 7500F、约 32 GB 内存、RTX 4060 Ti。六客户端独立进程，以 1280 × 720 离屏渲染、30 FPS
上限、阴影等级 0、特效等级 1 运行；上限不代表测得帧率。延迟场景对专服和所有客户端设置 `PktLag=50`、`PktLoss=2`
。硬件和一次七进程内存采样见 [环境](Phase5Evidence/diagnostics/Phase5Host.json)、[采样](Phase5Evidence/diagnostics/Phase5ActiveResources.json)
，不构成容量或性能基准。

测试夹具只布置位置、生命、弹药和临时墙体；伤害、技能、物品操作、撤离及持久化走实际游戏路径。倒地断线用例仅在该测试专服把连接超时设为十秒，以便在三十秒流血结束前验证断线结算；项目默认连接超时未改。四人结算回归使用默认网络超时。重入大厅不等于支持局中断线重连。

测试使用独立 Store，未改用户仓库。客户端打包时 Zen 暂存读取曾自动重试，最终打包退出码为零。调试失败运行保留在
`Saved/Extraction/Phase5*`，没有列为通过证据。存档、物品定义和旧模式接口保持兼容。

本阶段不包含公网、正式账号、局中重连、Shipping、容量压力或原作画面一致性验收。原地图转换中的地形与材质近似边界继续适用。

在项目根目录可以复跑；每次使用一个不存在的 `--tag`，避免覆盖证据：

```powershell
$phase5Python = 'C:\UnrealProjects\UnrealEngine-5.8.3-release\Engine\Binaries\ThirdParty\Python3\Win64\python.exe'
& $phase5Python Scripts/Extraction/run_network.py --packaged --scenario squad --players 6 --tag Phase5LocalSquad
& $phase5Python Scripts/Extraction/run_network.py --packaged --scenario pvpve --players 6 --lag --tag Phase5LocalPVPVE
& $phase5Python Scripts/Extraction/run_network.py --packaged --scenario lifecycle --players 6 --tag Phase5LocalLifecycle
```
