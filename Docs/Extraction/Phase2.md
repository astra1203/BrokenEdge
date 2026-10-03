# 阶段 2：航天基地战斗与撤离模式骨架

实施基线：`d628b5dabc55a01269ce27524e523d007d993eb4`，UE 5.8.3。

**阶段 2 限定范围已通过验收（2026-10-03）。** 本阶段交付战斗与模式骨架；背包、搜刮、撤离点和仓库结算分别在阶段 3、4 推进。

## 模式规则

- 准备大厅新增 `Extraction · 航天行动`，进入 `/Game/Maps/SpaceCenter/L_SpaceCenterExtraction`。
- 地图独立于阶段 1 样本，复用已转换的网格和材质；出生区位于室内与北侧平台。
- 服务器等待本轮参与者加载，等待期间不能移动或战斗；120 秒未全部到齐则结束。
- 全员加载后开始 600 秒对局。最后一名存活玩家仍可继续行动；无人存活或时间耗尽后显示结果，10 秒后返回准备大厅。
- 玩家有存活、阵亡和退出状态。死亡掉枪，不自动重生；死亡表现结束后跟随存活玩家观战，可切换上一位／下一位，包含对手。
- 观战对象死亡或退出时自动切换；无人可观战时保持最后镜头。Esc 菜单可离开对局并清理本地会话，其他玩家继续。
- 开局后不接受中途加入，不提供断线重连。

## 战斗边界

阶段 2 仅验收现有 AK 命中扫描。伤害沿用项目配置，默认身体 20、头部 40、弹匣 30；角色初始生命为 80，最大生命为 100。现有护盾不代表原版护甲。

服务器检查射击序号、当前武器与所有权、玩家生存和对局状态、射速、弹药、换弹状态及目标坐标。拥有者预测表现与扣弹，服务器回执修正；其他客户端重放被接受的射击表现。命中采用服务器当前世界中的枪口射线和物理骨骼，不承诺还原原版倒带算法。Extraction
下旧开火及倒带计分 RPC 不参与伤害结算。

换弹由服务器定时完成一次，动画通知负责表现；丢弃或死亡会取消未完成的换弹。原 FreeForAll／Teams 使用原有战斗规则。

AK 蓝图启用了随机散布，验收保留该设置。拥有者先播放本地预测开火，服务器计算实际散布和伤害，因此预测弹道表现可能与最终服务器落点不同；拥有者不会再次播放服务器广播的开火。弹药回执和远端射击／换弹快照按可靠通道顺序处理。

## 本轮修复

- 专服刷新角色骨骼，射线按当前物理骨骼区分身体与头部；Extraction 下关闭头顶名称组件的碰撞，避免它挡住子弹。
- 准星反投影使用实际拥有者的控制器和视口。HUD 纹理引用初始化并参与 GC，关卡切换时检查资源，修复回大厅时的空纹理崩溃。
- 无本地玩家的旧控制器不再创建大厅或对局 UI，避免无缝切换期间 `CreateWidget` 报错。
- 自定义握枪动画图模块改为 `UncookedOnly`，让独立的 `-game`／`-server` 进程也能加载节点；烘焙版本使用 Runtime 中的动画节点实现。
- 第一人称手臂隐藏 `M_Roy_016_BP_LOD` 背包材质分段，第三人称保留完整装备。局部服装边缘仍属于当前手臂裁剪资产，未重做整套手臂。
- 丢枪或死亡清空当前武器后，第一人称武器动画先检查武器与网格有效性，避免视觉网格下一次刷新前访问空指针；此崩溃由打包用例发现。
- 不透明场景父材质启用 Nanite；Extraction 地图的透明玻璃组件禁用 Nanite，使用普通网格渲染。阶段 1 原始地图文件保持原哈希，共享父材质另做回归。

## 可重复运行

在项目根目录 PowerShell 中：

```powershell
$uePython = 'C:/UnrealProjects/UnrealEngine-5.8.3-release/Engine/Binaries/ThirdParty/Python3/Win64/python.exe'
& $uePython Scripts/Extraction/run_build.py --tag EditorCheck
& $uePython Scripts/Extraction/run_network.py --tag CombatCheck
& $uePython Scripts/Extraction/run_network.py --tag SpectatorCheck --scenario spectator
& $uePython Scripts/Extraction/run_network.py --tag TimeoutCheck --scenario timeout
& $uePython Scripts/Extraction/run_network.py --tag LagCheck --lag
& $uePython Scripts/Extraction/run_build.py --target Server --package --tag PackageServerCheck
& $uePython Scripts/Extraction/run_build.py --target Client --package --tag PackageClientCheck
& $uePython Scripts/Extraction/run_network.py --tag PackagedCombatCheck --packaged
& $uePython Scripts/Extraction/run_network.py --tag PackagedLagCheck --packaged --lag
& $uePython Scripts/Extraction/run_network.py --tag PackagedSpectatorCheck --packaged --scenario spectator
& $uePython Scripts/Extraction/run_network.py --tag PackagedTimeoutCheck --packaged --scenario timeout
& $uePython Scripts/Extraction/run_network.py --tag PackagedLoadTimeoutCheck --packaged --scenario load_timeout
& $uePython Scripts/Extraction/run_network.py --tag PackagedDisconnectCheck --packaged --scenario disconnect
```

每个标签必须唯一，结果写入 `Saved/Extraction/<标签>`，不会覆盖旧记录。打包前需要关闭该项目的编辑器进程。测试脚本只清理自己启动的进程。

构建与打包命令依次执行；UAT 的启动程序构建步骤不遵循目标的等待锁参数，同时启动另一项 UBT 构建会造成冲突。`--jobs 8`
可用于限制编译并发，不改变验收条件。

测试用例通过开发版本自动化驱动实际房间 UI、Enhanced Input 和产品
RPC。服务器测试夹具只布置位置、生命、护盾与弹药；它们不作为命中或死亡成功的证据。射击必须经过真实输入、服务器射线及伤害入口。刻意伪造的请求用于验证拒绝路径。Shipping
不编译测试驱动。

散布可能使指定头部用例命中身体或落空。报告逐次记录真实消耗和伤害，再恢复靶标状态重试；检查数量包括这些重试，不能当作互不重复的功能数。掉枪拾回用例先按物理落点布置角色，再验证实际拾取重叠与装备输入。

延迟用例在服务器和客户端分别设置 `NetEmulation.PktLag 50`、`NetEmulation.PktLoss 2`，即约 100 ms 往返延迟及每个发送方向 2%
丢包；报告保存网络驱动的实际生效值。

对局期限用例通过 `-ExtractionRoundSeconds=15` 缩短等待，仍走正式结束、10 秒结果显示和大厅重开路径；其余常规对局使用 600
秒。加载超时用例等待真实的 120 秒上限。

打包后可在 `Packaged/Server/WindowsServer` 执行 `StartServer.bat 7777 Extraction 2`，客户端从房间列表加入并准备。此启动方式仍使用
LAN 会话。

## 验收结果与证据

UE 5.8.3 Editor、Development 专服和客户端完整打包均成功，最新记录为 `BuildEditor15`、`PackageServer04`、`PackageClient02`。包位于 `Packaged/Server/WindowsServer` 和 `Packaged/Client/Windows`。

以下六组均使用实际打包可执行文件、独立专服及两个或三个渲染客户端：

| 用例 | 检查记录 | 结果与报告 |
| --- | --- | --- |
| 双人战斗、射击请求校验、换弹中断、死亡与退出 | 63 | [通过](Phase2Evidence/packaged-combat/Report.json) |
| 约 100 ms 往返延迟、每方向 2% 丢包 | 70 | [通过](Phase2Evidence/packaged-lag/Report.json) |
| 三人观战切换、目标死亡／退出、无人存活及下一轮 | 102 | [通过](Phase2Evidence/packaged-spectator/Report.json) |
| 对局期限、10 秒结果显示、准备重置与重开 | 24 | [通过](Phase2Evidence/packaged-timeout/Report.json) |
| 真实 120 秒加载超时、等待期间禁止移动／射击 | 4 | [通过](Phase2Evidence/packaged-load-timeout/Report.json) |
| 杀死一个客户端后，剩余玩家继续对局 | 13 | [通过](Phase2Evidence/packaged-disconnect/Report.json) |

正常参与的测试进程均通过 UE 自动化并以 0 退出。断线用例中的 B 客户端按测试要求被强制结束，退出码 1 属于预期；专服与存活客户端正常结束。

前置独立 Editor 进程验收还包括延迟战斗 64 条、三人观战 93 条与期限重开 20 条。原有角色资产审计 34 项、双人握枪 20 项、动作 29 项、战斗／重生 21 项及武器显隐 32 项通过；最后的空指针修复后再次运行了动作和战斗回归。FreeForAll／Teams 的大厅流程在 Editor 独立进程和打包版本中各通过 7 个用例。

阶段 1 地图冷加载 6,397 项、双人通行 34 项回归通过，原始小样地图哈希未变。这些前置回归与后台编译同时进行，不作为新的性能验收；阶段 0、1 的历史证据保持原样。

已核对打包版 [第一人称](Phase2Evidence/packaged-combat/A-spawn.png)、[ADS](Phase2Evidence/packaged-combat/A-ads.png)、[第三人称](Phase2Evidence/packaged-combat/A-third-person.png)、[观战](Phase2Evidence/packaged-spectator/A-spectator.png) 与 [无人存活](Phase2Evidence/packaged-spectator/A-no-survivors.png) 画面，分辨率为 1280×720。握点数值检查结合手掌接触和实际显隐画面复核。

[Summary.json](Phase2Evidence/Summary.json) 保存工作区实现、关键资产和打包产物哈希；[ArchiveIndex.json](Phase2Evidence/ArchiveIndex.json) 保存每个归档文件的大小与 SHA-256。完整 JSON 报告、压缩日志和截图位于 `Phase2Evidence`，失败的排查记录单列在 `diagnostics`，不计入通过用例。

## 保留的限制

地图的地面重建、材质近似与固定开启门仍遵循 [阶段 1 转换问题记录](../SpaceCenter/ConversionIssues.md)。本阶段不扩大原地图恢复范围，验收仅覆盖本机 LAN 的双人／三人 Development 进程；8 人负载、跨机网卡兼容性和互联网服务需另行验证。

首次打包战斗出现一次远端收包停滞：服务器与拥有者为 24 发，远端停在 25 发。其根因尚未定位；调试器复现确认远端广播及赋值正常，后续正式双人、延迟丢包和三人测试均通过。原始失败报告和调试证据保留在 [诊断记录](Phase2Evidence/diagnostics/Diagnostics.json)，不据此宣称所有网络环境都已稳定。

烘焙仍报告旧动画蓝图中无操作的 `Transform (Modify) Bone` 节点、旧 Muriel 动画资源和音频节点 `NodeGuid` 升级警告；完整构建日志已归档。首次客户端打包还发生过一次 Zen 读取重试，恢复后 UAT 退出码为 0；修复后的最终包同样成功。构建成功不表示历史资源警告已经全部清理。
