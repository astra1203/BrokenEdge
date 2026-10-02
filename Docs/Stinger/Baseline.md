# 蜂医 + AK：阶段 0 基线验收

日期：2026-10-02。状态：**阶段 0 通过**。UE 5.8.3 编译、230 项角色与战斗检查、7 个独立进程 LAN 用例及关键画面复核均已完成，可进入阶段 1 的地图转换样本验证。

本阶段验证已有角色、双视角、AK 操作、伤害和现有联机入口，为后续航天基地小区域验证建立基线。未开始地图转换，也未实现撤离模式。工作分支为
`ue5.8-20260930`，起点提交为 `09030f3b1e14098294be9dbd56e09710fe517f86`；本轮未提交、未推送。

## 1. 实际运行的配置

| 项目            | 当前值                                                                                       |
|-----------------|----------------------------------------------------------------------------------------------|
| 引擎            | `C:\UnrealProjects\UnrealEngine-5.8.3-release`                                               |
| 构建目标        | `BrokenEdgeEditor Win64 Development`                                                         |
| 测试地图        | `/Game/Characters/Stinger/Maps/L_StingerTest`                                                |
| GameMode        | `/Game/Characters/Stinger/Blueprints/BP_StingerTestGameMode`                                 |
| 默认玩家蓝图    | `/Game/Characters/Stinger/Blueprints/BP_StingerCharacter`                                    |
| 蓝图直接父类    | `/Script/BrokenEdge.BrokenEdgeCharacter`                                                     |
| 第三人称身体    | `Meshes/Gameplay/Stinger_3P_Textured/SkeletalMeshes/Stinger_3P_Textured`                     |
| 第三人称 AnimBP | `Animations/ThirdPerson/ABP_BrokenEdgeAnimInstance_Stinger`                                  |
| 第一人称身体    | `Meshes/FirstPerson/CappedView/Stinger_CappedViewArms/SkeletalMeshes/Stinger_CappedViewArms` |
| 武器            | `/Game/Blueprints/Weapon/BP_AK47`，世界网格 `SK_KA47`                                        |
| 生命值          | 蓝图出生生命值 80，最大生命值 100；重生验证遵循当前蓝图配置                                  |

表中身体和动画的相对路径均以 `/Game/Characters/Stinger/` 为根。`BP_StingerCharacter_Repaired`
是此前修复过程保留的蓝图，已验证的配置曾被提升到原始角色蓝图；本轮未为了迎合测试名称切换 GameMode。

第三人称继续使用项目现有重定向动作及 `AM_FireWeapon_Stinger`、`AM_Reload_Stinger`、`AM_Swap_Stinger`、`AM_Defeated_Stinger`
。第一人称使用此前修复的导入动画。该基线不意味着所有第三人称动作已经替换成三角洲原版动作。

## 2. 本轮发现和修复

1. **握姿修改没有体现在磁盘资产中。** 初次独立进程审计读到 FABRIK 的 `rotation_source=0`，两个待机动画的 17
   个左手指骨旋转与预期基准最大相差约 94.44°。重新配置并保存世界 AK、第三人称 AnimBP 和持枪动画，再用新进程加载核对。
2. **手腕使用了错误的坐标空间。** 原脚本将手腕相对前臂的局部旋转直接用作武器组件空间旋转；数值 IK 能对准插槽，但近景中手掌仍偏后。改用
   `AnimPoseSpaces.WORLD` 得到完整姿态的组件空间旋转，再换算到武器根骨骼空间。最终世界 AK 左握点为组件空间 `(4,25,6)`
   厘米，旋转基准与已存在的第一人称拟合资产一致；位置经过第三人称护木接触校准。FABRIK 使用 `Copy From Target`。
3. **测试继承了本机额外服务器设置。** 请求单人 PIE 时，运行现场有两个世界：额外服务器没有玩家，另一个世界有一名玩家；本机
   `bLaunchSeparateServer=True`。`BeginValidationPIE` 现在显式关闭该选项，并继续按请求创建单人或双人测试。未更改用户保存的编辑器多人偏好。
4. **HUD 准星没有使用当前画布。** 1280×720 截图中的准星落在约 `(320,240)`，代码从全局视口读取尺寸。现在使用当前 HUD 的
   `Canvas->SizeX/SizeY`，复测准星位于 `(640,360)`，与画面中心一致。
5. **GameFeatureData 缺少资产管理规则。** 按 UE 5.8.3 `GameFeaturesEditorModule::AddDefaultGameDataRule` 的规则补入
   `DefaultGame.ini`，消除了此前让 commandlet 返回 1 的配置错误；最终审计同时要求报告通过和进程退出码为 0。
6. **拾取目标被提前清空。** `pickup-runtime` 的逐帧记录显示，重新拾取第 0.3667 秒时，感应球仍重叠角色胶囊体，但
   `overlapping_weapon`
   已为空。身体网格的结束重叠事件清掉了整个角色的拾取目标。现在只有感应球完全不再重叠该角色时才清空；回归测试主动关闭身体网格的重叠事件，验证胶囊体仍在范围内时目标保留。原测试还把带有残留速度的掉落枪瞬移进角色身体，枪随后被弹出范围；测试改为等待落地、移动角色靠近，并在按拾取键前确认真实拾取目标。

站立修正后，画面检查又发现蹲伏和移动时拇指仍会竖起。手指校准因此覆盖了 20 段持枪待机、蹲姿、行走和慢跑动画，每段只替换 17
个左手指骨的旋转；保留平移、缩放和其他骨骼轨道。空手、开火、换弹、切枪及死亡动作未按此脚本重写。冷启动审计检查每段动画的起点、中点和终点，画面复核另外确认蹲伏、前移和侧移时不再竖拇指。

原文件备份位于 `Saved/Stinger/Baseline/asset-backup` 和 `asset-backup-locomotion`
。第三人称身体网格的插槽配置原本已经正确，配置过程产生的重复保存已还原；最终不修改该身体资产。原先“最新握姿已保存”的历史文字不能替代本轮磁盘审计。

调试工具曾附加到本轮自建测试进程，但测试辅助函数和角色装备函数的源码断点均报告没有关联的可执行代码；未把未命中的断点当作根因证据。PIE
启动问题依据运行中读取的世界、玩家数量和设置值确认；拾取问题依据原生只读状态输出和逐帧重叠记录确认。调试会话与本轮断点已清理，用户原有异常断点保持原状态。

## 3. 验证结果

| 检查 | 结果 | 证据 |
| --- | --- | --- |
| UE 5.8.3 Editor Development 构建 | 通过，80.14 秒；仍有既有警告 | [构建日志](BaselineEvidence/build-pickup.txt) |
| 冷启动磁盘资产审计 | 34 / 34 | [Audit.json](BaselineEvidence/audit/Audit.json) |
| 单人、双人握持数值 | 各 21 / 21，各 927 帧 | [单人](BaselineEvidence/grip-1p/third_person_grip_p0_1p.json)、[双人](BaselineEvidence/grip-2p/third_person_grip_p0_2p.json) |
| 单人、双人握持拍摄 | 各 20 / 20，各 927 帧；另逐张复核关键截图 | [单人](BaselineEvidence/grip-1p-visual/third_person_grip_p0_1p.json)、[双人](BaselineEvidence/grip-2p-visual/third_person_grip_p0_2p.json) |
| 单人、双人武器显隐与拾取 | 各 32 / 32，各 870 帧无显隐违规 | [单人](BaselineEvidence/visibility-1p/weapon_visibility_1p_stills.json)、[双人](BaselineEvidence/visibility-2p/weapon_visibility_2p_stills.json) |
| 走跑跳蹲、ADS、开火、换弹、视角 | 29 / 29 | [动作报告](BaselineEvidence/actions/pie_validation.json) |
| 双向实弹伤害、死亡掉枪、重生 | 21 / 21 | [Combat.json](BaselineEvidence/combat/Combat.json) |
| 独立进程 LAN 大厅与旅行 | 7 / 7 | [汇总与事件](BaselineEvidence/Summary.json)、[完整 LAN 日志](BaselineEvidence/LAN) |

9 组角色和战斗报告共 **230 项检查通过**，各独立 UE 测试进程退出码均为 0。两组真实相机握持测试中，稳定帧左手最大误差均约 **0.099 厘米**，右手约 **0.0000115 厘米**，左手腕最大旋转误差约 **0.0213°**。

LAN 在 `Dedicated-20261002-155500` 运行中通过空房间重试，以及两轮专服、客户端 1、客户端 2 测试；两轮分别从 FreeForAll 切换到 Teams、从 Teams 切换到 FreeForAll。日志确认切换后全员取消准备、取消倒计时后重新准备、点击指定房间加入和旅行后保留会话；两个专服均确认没有本地玩家、旅行后仍为专服且容量为 2。另有两个仅广播房间的辅助进程，不计作测试用例。

握持数值与画面分开运行：数值测试保留真实玩家相机，覆盖抬头 55°、低头 65°；近景拍摄相机会改变 `GetBaseAimRotation()`
，因此不拿拍摄结果证明真实相机俯仰。固定 30 Hz 的完整动作序列为 927 个模拟帧，检查左右手距离、左手旋转、双臂长度及换弹／切枪／丢枪期间的 IK 释放。

真实射击测试通过 Enhanced Input 拾枪和开火，分别验证客户端攻击主机、主机攻击客户端：单发减少一发弹药，目标受到 20
点伤害，护盾先扣除，两端数值一致；继续真实开火触发死亡、掉枪，等待现有模式重生。该测试未用直接 `ApplyDamage`
代替命中。显隐测试中的直接伤害仅用于覆盖死亡时的网格状态，与真实射击证据分列。

动作检查覆盖步行 600、冲刺 850、蹲伏 300、瞄准移动上限 450 cm/s，以及 ADS FOV
65°、跳跃、开火扣弹、换弹回补和视角切换。显隐检查覆盖两把已装备世界武器、低头、蹲伏、ADS、切枪及换弹中切视角、丢弃再拾取、失去控制、死亡、重生和销毁。

独立进程 LAN 流程与同进程 PIE 射击范围不同：专服加两个客户端验证房间发现、点击加入、准备／取消准备、模式切换和地图旅行。不能据此声称专服射击、高延迟战斗或打包版本已经验收。

## 4. 画面和证据

已归档 **34 张人工复核的原始 PNG**、9 组报告及启动记录、构建和 LAN 日志、3 份修复前诊断记录。文件清单与 SHA-256 见 [Summary.json](BaselineEvidence/Summary.json)，逐张观察记录见 [VisualReview.json](BaselineEvidence/VisualReview.json)。关键画面：

| 画面 | 检查点 |
| --- | --- |
| [第三人称待机](BaselineEvidence/Captures/2p-grip-idle_front.png)、[瞄准](BaselineEvidence/Captures/2p-grip-aim.png) | 左手包住护木，手掌不悬空 |
| [蹲伏](BaselineEvidence/Captures/2p-grip-crouch.png)、[侧移](BaselineEvidence/Captures/2p-grip-strafe.png) | 拇指弯曲，移动时保持接触 |
| [换弹后](BaselineEvidence/Captures/2p-grip-after_reload.png)、[切枪后](BaselineEvidence/Captures/2p-grip-after_swap.png) | 动作结束后恢复握持 |
| [单人低头](BaselineEvidence/Captures/1p-visibility-dual_down.png)、[客户端低头 ADS](BaselineEvidence/Captures/2p-visibility-aim_down.png) | 仅显示一把视图枪 |
| [实弹命中](BaselineEvidence/Captures/combat-0_damage.png)、[死亡掉枪](BaselineEvidence/Captures/combat-0_death.png) | 准星居中；伤害及复制结果以结构化报告为准 |
| [两种房间](BaselineEvidence/Captures/LAN-Join-FreeForAll-Rooms.png)、[切换模式](BaselineEvidence/Captures/LAN-Join-FreeForAll-ModeChanged.png)、[准备倒计时](BaselineEvidence/Captures/LAN-Cancel-FreeForAll-Countdown.png) | 房间列表、准备状态和倒计时可见 |

完整过程输出位于 `Saved/Stinger/Baseline/`；最终采用 `phase0-*`
运行，其他标签的运行保留为中间诊断证据，不代表最终验收。

近景测试临时增加补光，并强制屏幕外角色刷新骨骼；这些测试设置没有保存到地图。截图用于检查接触和显隐，固定步长记录不能用于声称实时帧率或性能达标。

## 5. 边界和遗留项

- 本阶段验收的是蜂医 + AK 的当前基础操作，不是三角洲完整角色、动作库或射击系统的复刻。
- 蓝图保留出生 80 生命值和当前模式自动重生；后续撤离模式应单独定义生命、死亡、观战和结算规则。
- 尚未覆盖独立专服战斗、打包客户端、爆头倍率、全部武器、延迟／丢包、公网服务、完整动作过渡视频和性能基准。
- 构建仍有既有警告：测试类 `AMyCharacter::TakeDamage(float)` 隐藏父类虚函数，以及 `TraceUnderCrosshairs` 的 `ViewportSize`
  在无全局视口分支可能未初始化。它们未在本轮受测流程中造成失败；本报告不宣称编译零警告。
- 现有第三人称动作的整体姿态、瞄准肩托细节及更高标准的动画制作质量仍属于后续表现工作。本轮握姿结论限于列出的检查和截图，不承诺任意武器或任意动作都正确。

## 6. 复现

在项目根目录执行，下列标签必须使用新目录名，运行器会拒绝覆盖已有证据：

```powershell
$uePython = 'C:/UnrealProjects/UnrealEngine-5.8.3-release/Engine/Binaries/ThirdParty/Python3/Win64/python.exe'
& $uePython Scripts/Stinger/run_baseline.py audit --tag review-audit
& $uePython Scripts/Stinger/run_baseline.py grip --players 1 --tag review-grip-1p
& $uePython Scripts/Stinger/run_baseline.py grip --players 2 --tag review-grip-2p
& $uePython Scripts/Stinger/run_baseline.py grip --players 1 --capture stills --tag review-grip-1p-visual
& $uePython Scripts/Stinger/run_baseline.py grip --players 2 --capture stills --tag review-grip-2p-visual
& $uePython Scripts/Stinger/run_baseline.py actions --tag review-actions
& $uePython Scripts/Stinger/run_baseline.py visibility --players 1 --capture stills --tag review-visibility-1p
& $uePython Scripts/Stinger/run_baseline.py visibility --players 2 --capture stills --tag review-visibility-2p
& $uePython Scripts/Stinger/run_baseline.py combat --tag review-combat
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Scripts/Test-LanSessions.ps1 -EngineRoot C:/UnrealProjects/UnrealEngine-5.8.3-release -Render -SwitchMode
```

运行器创建独立 UE 进程，每轮记录命令、PID、Git 起点、工作区状态、日志、报告和退出码。关闭其他使用相同测试端口的实例后按顺序执行，保留正在调试的共享实例现场。重新配置资产使用独立
PythonScript commandlet，避免通过 RiderLink 游戏线程同步执行批量导入。

后续入口：[三角洲功能还原评估与实施路线](../三角洲功能还原评估与实施路线.md) 的阶段 1——验证一个室内及实际相邻室外区域的地图转换样本。
