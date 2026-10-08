# 航天基地移动交互阶段

本阶段实现普通楼梯、竖梯、翻越和攀上平台。它是用户确认的独立玩法阶段；体验方案 A 的源游戏参考采集仍单独记账。本阶段工程验收已完成（2026-10-07，UE 5.8.3），最终候选包为 `Packaged/Traversal08`。

## 操作和边界

- WASD 走楼梯，保留 CharacterMovement 的行走与跨阶规则。
- 空格面对可用障碍时翻越或攀上；没有可用目标时按原逻辑跳跃。
- E 从梯子上下入口附着，W/S 上下爬，松开暂停；出口可用时自动离梯。
- 梯子中空格蹬离，E 松手。服务器按梯子限制一个占用者。
- 动作期间阻止射击、瞄准、换弹、换枪、医疗和背包操作。死亡、失去角色控制或失效路径释放占用。
- 不包含悬挂横移、自由攀岩、滑索、滑铲、趴下。

参数位于 `Config/DefaultGame.ini` 的 `ExtractionTraversalRules`：翻越上限 120 cm、攀上上限 180 cm、可翻越厚度 100 cm、梯子速度
150 cm/s。`LadderRoofReach` 控制手部到达顶端后的上沿过渡高度，默认 150 cm，短梯保留至少 30 cm 爬行段。这些是项目默认值，不是对源游戏参数的还原声明。

## 实现约束

`TraversalMovementComponent` 使用 CharacterMovement 自定义移动模式、SavedMove
输入与服务器位置纠正。客户端不提交目标终点；双方根据输入探测，服务器拥有最终动作和梯子占用权。纠正数据携带阶段、路径和时间，随后重放未确认输入。

路径预检覆盖前表面、顶面、完整胶囊路径及落脚足迹，移动时继续 sweep。仅允许静态障碍攀越；被阻挡时停在最后安全位置。动画是视觉表现，不驱动权威胶囊。

`ExtractionTraversalComponent` 同步远端动作并控制第一、第三人称蒙太奇；动作期间收起武器，禁用原持枪 IK。源动作通过已有 PSA
修复解码器导入，保存来源及哈希。第三人称翻越暂用对应高度的 ClimbRun，下降按梯子高度反向采样 UP；本阶段实机记录见下方双视角录像与接触测量；不声明源游逐帧一致。

`ExtractionLadder` 为源模型的隐形交互绑定，不替换原模型。`prepare_sources.py` 固定扫描重建清单中的 39 个候选，
`bind_ladders.py` 分类装饰、不可用和有安全进出口的梯子，并保存地图原件与坐标。

实际碰撞胶囊为半径 34 cm、半高 92 cm。进出口和路线探测使用 Pawn 碰撞通道。`measure_rungs.py` 从源模型横档三角面测出位置，绑定到交互
Actor，供第一、第三人称接触修正使用。最终骨骼坐标和双视角录像均已归档。

已定位两个预测问题：进入 CustomMovement 会清除普通跳跃位，因此翻越边沿使用独立 SavedMove
标记；进梯预测中立即转动视角会改变服务器收到的入口朝向，自动转向现已延后。两次原生调试记录位于
`Evidence/JumpRequestDebugger.json` 和 `Evidence/TopEntryDebugger.json`。

正式地图梯子的朝向问题也已通过原生调试定位：Python 的位置参数将水平朝向写进俯仰角，运行时胶囊随之倾倒，导致出口 sweep
与直立预检不一致。绑定改用显式 `pitch/yaw/roll` 参数并校验水平前向，移动组件只应用 yaw；证据见
`Evidence/SourceLadderOrientationDebugger.json`。

TraversalFormal04 的源梯子骨骼测量发现部分手腕离横档 23–38 cm。原生调试实测左臂长 55.89 cm、目标距肩 69.33 cm，超出不拉伸的可达范围。现按双臂实际距离调整视觉前移量，保留胶囊位置；见 `Evidence/SourceLadderContactDebugger.json`。Traversal08 已复验：源梯子附着采样的双手和双脚误差均小于 0.01 cm。

## 验收结果

[证据清单与 SHA-256](Evidence/Manifest.json) 收录 12 轮通过的测试、165 个归档文件、16 段视频。共 750 项检查，包含重复轮次；每轮可执行文件哈希均与最终双端包一致。归档前还核对了当前玩法 C++ 与地图文件和打包输入的哈希。

| 项目 | 结果与证据 |
| --- | --- |
| Editor 与完整双端构建 | [Server08](Evidence/Builds/TraversalServer08/Run.json)、[Client08](Evidence/Builds/TraversalClient08/Run.json) Build/Cook/Stage 通过；PE/AMD64/文件边界校验通过 |
| 源动作 | [25 个源片段导入](Evidence/Animations-Report.json)，动画集装配通过 |
| 39 个源梯子候选 | [Ladders11](Evidence/LaddersReport.json)：3 个可用并绑定，25 个出口/路径不安全，11 个装饰；全部分类，不等于 39 个都可爬 |
| 两渲染客户端 + 专服，正常网络连续三轮 | [Normal02](Evidence/Runs/TraversalNormal02/Report.json)、[Normal03](Evidence/Runs/TraversalNormal03/Report.json)、[Normal04](Evidence/Runs/TraversalNormal04/Report.json)，各 82 项通过 |
| 边界及拒绝路径 | 三轮均覆盖 60/90/120 cm 翻越、180/121 cm 攀上、181/220 cm 拒绝、窄顶、低顶、占位、重复输入、普通跳跃、抢梯与出口受阻 |
| 双向各 50 ms、2% 丢包 | [Weak01](Evidence/Runs/TraversalWeak01/Report.json)，83 项通过 |
| 非致命伤、倒地、救援、死亡、断线、撤离 | [Lifecycle01](Evidence/Runs/TraversalLifecycle01/Report.json)，三人场景 100 项通过；两个渲染客户端，第三人使用 NullRHI |
| 正式地图连续路线 | [Formal06](Evidence/Runs/TraversalFormal06/Report.json)，27 项通过；一次起点布置，楼梯上下、源栏杆、源平台和源梯子之间无传送 |
| 双视角与武器恢复 | [Visual02](Evidence/Runs/TraversalVisual02/Report.json)，82 项通过；16 段视频编码、完整解码和哈希校验通过 |
| 战斗 | [Combat01](Evidence/Runs/TraversalCombat01/Report.json)，62 项通过 |
| 背包、医疗、物品唯一性、下一局配装 | [Inventory01](Evidence/Runs/TraversalInventory01/Report.json)，93 项通过 |
| 换枪 | [Swap01](Evidence/Runs/TraversalSwap01/Report.json)，22 项通过 |
| 超时结算与同房间第二局 | [Timeout01](Evidence/Runs/TraversalTimeout01/Report.json)，28 项通过 |
| FreeForAll / Teams 联机回归 | [Legacy02](Evidence/Runs/TraversalLegacy02/Report.json)，7 个用例通过；NullRHI 功能回归 |

三轮无录像测试的采样中位帧率为 30 / 60 / 117.49–120.00 FPS，四档障碍跨帧率上限的落点差异均小于 3 cm。[Normal04 时长与帧间隔报告](Evidence/Runs/TraversalNormal04/MotionAnalysis.json) 同时保留其余轮次的测量。时长来自状态快照，约有 0.15 s 的采样与移动时间戳误差，不应解释为逐帧延迟上限。

[源梯子接触测量](Evidence/Runs/TraversalFormal06/FormalContactAnalysis.json) 的附着采样中，第一人称双手及双方第三人称双手/双脚均贴合对应目标，误差小于 0.01 cm。[测试梯子暂停采样](Evidence/Runs/TraversalVisual02/ContactAnalysis.json) 中双手及支撑脚贴合，摆动脚处于换档过程。测量对象是腕/踝骨骼目标，不是逐帧指尖或鞋底碰撞。

## 双视角录像

| 动作 | 第一人称 A | 第三人称观察者 B |
| --- | --- | --- |
| 60 cm 翻越 | [视频](Evidence/Videos/TraversalVisual02/A-60.mp4) | [视频](Evidence/Videos/TraversalVisual02/B-60.mp4) |
| 90 cm 翻越 | [视频](Evidence/Videos/TraversalVisual02/A-90.mp4) | [视频](Evidence/Videos/TraversalVisual02/B-90.mp4) |
| 120 cm 翻越 | [视频](Evidence/Videos/TraversalVisual02/A-120.mp4) | [视频](Evidence/Videos/TraversalVisual02/B-120.mp4) |
| 180 cm 攀上 | [视频](Evidence/Videos/TraversalVisual02/A-180.mp4) | [视频](Evidence/Videos/TraversalVisual02/B-180.mp4) |
| 梯子附着、上/下爬、暂停、离梯 | [视频](Evidence/Videos/TraversalVisual02/A-ladder.mp4) | [视频](Evidence/Videos/TraversalVisual02/B-ladder.mp4) |
| 正式地图源栏杆 | [视频](Evidence/Videos/TraversalFormal06/A-formal-vault.mp4) | [视频](Evidence/Videos/TraversalFormal06/B-formal-vault.mp4) |
| 正式地图源平台 | [视频](Evidence/Videos/TraversalFormal06/A-formal-mantle.mp4) | [视频](Evidence/Videos/TraversalFormal06/B-formal-mantle.mp4) |
| 正式地图源梯子 | [视频](Evidence/Videos/TraversalFormal06/A-formal-ladder.mp4) | [视频](Evidence/Videos/TraversalFormal06/B-formal-ladder.mp4) |

录像为 1280×720、H.264、无音频，关闭运动模糊以便检查姿态。视口 PNG 读回使实际采集频率约为 7.84–8.74 FPS；编码使用原始时间间隔，没有补帧。这些录像用于姿态、动作阶段和收枪/恢复检查，性能结果使用上面的无录像轮次。各视频旁的 JSON 和 CSV 记录时长、采集频率、帧名、编码命令与哈希。

## 包与复现

新包位于 `Packaged/Traversal08/Client/Windows` 和 `Packaged/Traversal08/Server/WindowsServer`。从项目根目录可分别启动：

```powershell
& '.\Packaged\Traversal08\Server\WindowsServer\StartServer.bat' 28777 Extraction 2
& '.\Packaged\Traversal08\Client\Windows\StartClient.bat' '127.0.0.1:28777' Extraction
```

原游玩专服 PID 69528、旧 `Packaged/Client` 和 `Packaged/Server` 保留。测试使用 28777 端口、独立房间及存档目录；旧模式回归用独立 `LANSession.LanGameUniqueId` 隔离广播，因此无需停止原专服。本阶段改动保留在工作区，未提交或推送。

通过 `Scripts/Extraction/resource_guard.py --tag <新标签> --timeout 1000 -- <Python> Scripts/Extraction/run_network.py --tag <新标签> --packaged --package-root Packaged/Traversal08 --scenario traversal --rendered-clients 2 --port 28777 --isolated-room` 可复跑基础场景。额外参数：`--lag` 弱网、`--traversal-route-only` 正式路线、`--traversal-record` 录像、`--traversal-lifecycle --players 3` 生命周期。

动作/绑定工具为 `Scripts/Traversal/prepare_sources.py`、`run_editor.py animations|ladders`、`measure_rungs.py`。录像使用 `encode_recording.py`，分析使用 `analyze_motion.py` / `analyze_contacts.py`，归档使用 `archive_evidence.py`。原始运行记录保留在 `Saved/Extraction/<标签>`。

## 资源与证据边界

重任务串行运行，资源守卫的启动阈值保持 C 盘 40 GiB、D 盘 100 GiB、可用内存 8 GiB；内存低于 3 GiB 持续 15 秒即停止本任务树。编译/链接分开运行，并保留中断的 EXE，最终候选包来自成功的完整构建。

早期任务产物未删除，采用 NTFS/LZX 无损压缩并验证 EXE/PAK 哈希；最终打包使用独立 cook 目录与 `--direct-stage`，减少重复归档。`pe_validation.py` 防止中断链接残留的损坏 EXE 被当作成功产物。

源动画的复用方式见“实现约束”；本阶段通过不替代体验方案 A 的源游戏参考采集。历史双渲染守卫测试 `guard acquires player` 超时也继续单独记账，没有通过降低守卫参数或关闭碰撞绕过。
