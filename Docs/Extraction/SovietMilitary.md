# 苏联军事基地地图接入

军事基地作为搜打撤模式的第二张地图接入，默认地图仍为航天基地。大厅选择地图后所有成员重新准备；服务器根据注册表中的地图 ID
开局。两张地图共用账号、仓库、出战票据和撤离结算。

## 玩法配置

| 项目       | 军事基地配置                                                          |
|------------|-----------------------------------------------------------------------|
| 地图 ID    | `SovietMilitary`                                                      |
| 关卡       | `/Game/Maps/SovietMilitary/L_SovietMilitaryExtraction`                |
| 地图定义   | `/Game/Extraction/Maps/DA_SovietMilitaryGameplay`                     |
| 人数与时长 | 两队，每队最多三人，默认三十分钟                                      |
| 出生点     | 四组候选出生点，每组三个位置                                          |
| 撤离点     | 东侧林道、南侧公路、北侧山路；全程开放，连续停留十秒                  |
| 物资       | 二十四个容器，二十处散落候选位置，每局选择十处                        |
| 守卫       | 六名，使用现有行为树与战斗规则                                        |
| 门窗       | 十七扇 F 键普通门，服务器校验距离、视线和开关范围；窗户为不可破坏玻璃 |

地图 X 正方向对应战术地图北方。可玩范围限制在 X/Y ±15000 cm 内，外围阻挡位于地形边缘内侧。地图整图加载，不使用 World
Partition，也不增加水面、钥匙房或新的枪械规则。

出生朝向指向基地。原素材的 201 个远景树卡片设置了 30 米最小绘制距离，避免近处出现整面树贴图；该设置写在生成蓝图模板中，并经过独立进程重新加载验证。

## 代码与资源

`UExtractionMapRegistry` 的白名单来自 `Config/DefaultGame.ini`。`SelectedMap` 存在 GameInstance 子系统中，跨无缝切图保留。大厅
RPC 只接受注册 ID，切换会取消倒计时并清除准备状态；无效地图在扣除出战装备前被阻止。

`AExtractionGameMode` 根据实际 World 解析定义，再复制到 `AExtractionGameState::MapDefinition`
。HUD、小地图和战术地图使用这份定义，切图时刷新名称、底图和坐标范围。启动参数 `-ExtractionMap=SovietMilitary` 可设置专服初始地图。

`AExtractionDoor` 复制开关状态和服务器开始时间，客户端据此插值旋转。服务器检查整个摆动范围的 Pawn
碰撞；关门时有角色进入会反向打开。静态建筑使用三角形碰撞保留门窗洞口，门扇使用简单盒碰撞。

原资源位于 `Content/UVB80`。四个旧窗户蓝图移除了无法在当前引擎加载的 APEX 破碎逻辑，保留窗框并补静态玻璃。修复前原件备份在
`Saved/Extraction/SovietMilitary/OriginalWindows`。`ExtractionWater.h` 无需修改。

## 重建步骤

先编译 `BrokenEdgeEditor`。以下脚本均使用项目的 UnrealEditor-Cmd Python commandlet，建议传入
`-SCCProvider=None -NullRHI -unattended -nosound` 并为每次运行指定日志。

1. `Scripts/Extraction/repair_soviet_windows.py`：加 `-SovietRepairWindows` 执行修复，否则只检查。
2. `Scripts/Extraction/prepare_soviet_military.py`：复制源关卡（仅缺失时）、修复碰撞、转换门、重建生成器拥有的布点和边界。重复执行保留已转换的门与门框。
3. `Scripts/Extraction/validate_soviet_layout.py`：审计十二个出生位置、三个出口、四十四个物资候选位置和十八个巡逻节点；同时记录门关闭与开启时的导航结果。
4. `Scripts/Extraction/bake_soviet_overview.py`：生成含地形和建筑的 2048 像素俯视图，并绑定到地图定义。

生成器通过地面射线、胶囊空间、导航投影以及到入口的完整路径选择布点。命令行构建会先完成资源编译，再解除对应的异步加载导航锁；不能只检查导航
Actor 存在就认定构建成功。

客户端和服务器打包入口为 `Scripts/Extraction/run_build.py`，地图列表包含军事基地。新的打包验证应使用独立 `--package-root`
和新的 `--cook-root`，避免复用旧 Cook 结果。

## 验收记录

最终代码与资源提交为 `a87702d06`，新 Client/Server 包位于 `Packaged/SovietMilitary03`，构建均退出 0。完整日志、原始报告、截图、资源与包的 SHA-256 已归档至 [Phase7SovietEvidence](Phase7SovietEvidence/Summary.json)，文件索引见 [ArchiveIndex.json](Phase7SovietEvidence/ArchiveIndex.json)。安装包保留在本地，不纳入 Git。

| 检查 | 结果与范围 | 证据 |
|------|------------|------|
| 最终注册表与导航 | 注册表自动化通过；开门、关门状态均为 77/77 点连通；201 个远景卡片近距隐藏设置在重新加载后仍有效 | [LayoutAndRegistry](Phase7SovietEvidence/LayoutAndRegistry/Run.json) |
| 六人打包联机 | 105 项通过：大厅切图重置准备、非法地图 ID 不扣装备、17 扇门复制、F 键交互、占用拒关与关门反向打开、24 个容器、搜刮、六人撤离返仓 | [PackagedSix](Phase7SovietEvidence/PackagedSix/Report.json.gz) |
| 两人连续实走 | 30 项通过：三段导航路线合计约 713 米，251 次位置采样，零位置传送；实际搜索与转移物资、十秒撤离、同一物品 GUID 返仓并回大厅 | [PackagedRoute](Phase7SovietEvidence/PackagedRoute/Report.json.gz)、[路线](Phase7SovietEvidence/PackagedRoute/ContinuousRoute.json) |
| 守卫战斗 | 30 项通过：六名守卫、十八段巡逻导航、实际巡逻、受击锁定、还击、脱战返回、击杀归属、唯一死亡掉落和撤离 | [PackagedGuards](Phase7SovietEvidence/PackagedGuards/Report.json.gz) |
| 异常结算 | 41 项通过：三人死亡、突然断线、真实 120 秒测试回合超时；非保护物品进入唯一尸体箱，数量和弹匣保留，安全箱不掉落且返还，回大厅后准备状态重置 | [PackagedLifecycle](Phase7SovietEvidence/PackagedLifecycle/Report.json.gz) |
| 入口台阶回归 | 17 项通过，同一入口从正面和两侧正常步行进入；使用明确的起点定位夹具 | [PackagedStairs](Phase7SovietEvidence/PackagedStairs/Report.json.gz)、[位置记录](Phase7SovietEvidence/PackagedStairs/Stairs.json) |
| 六人跨地图 PIE | 连续完成军事基地 → 航天基地 → 军事基地三局结算与返大厅，保留原枪械 GUID；六客户端 NullRHI | [SixPlayerCrossMapPIE](Phase7SovietEvidence/SixPlayerCrossMapPIE/Run.json) |

打包功能测试使用一名 1920×1080、Medium、768 MiB 纹理池、关闭光追、30 FPS 上限的渲染客户端，其余客户端使用 NullRHI。主联机场景、守卫和异常结算场景使用位置夹具，连续实走场景不使用位置夹具。航天基地跨地图 PIE 使用显式撤离时钟夹具，未等待其九百秒门槛。这些结果不等同于六客户端同时渲染、全地图随机路线、匹配服务或性能验收。

实走发现并修复了两类碰撞问题，原失败报告和截图保存在归档的 `diagnostics` 下：

- 导入资源的 NavCollision 缓存没有随碰撞策略更新，导航经过实际阻挡的岩石。`RefreshCollision` 同时重建 Chaos 物理网格和 NavCollision，再构建关卡导航；修复后的编辑器路线与最终打包路线均通过。
- 入口矮台阶的细节三角形会挡住侧向进入的角色胶囊。`ConfigureStairCollision` 只对 `SM_SmallStairSmall`、`SM_SmallStairBig` 生成凸包角色碰撞，精确射线保留原模型。原打包失败记录中的 `SmallStairSmall2_9`、正常输入与零速度，以及修复后三方向实走结果分别保留。

失败诊断见 [CollisionDiagnostics.json](Phase7SovietEvidence/diagnostics/CollisionDiagnostics.json)。Rider 附加时引擎跨台阶函数的源码断点未能绑定，因此此处结论基于碰撞日志和修改前后的实走对照，不声称已证明引擎内部的具体失败分支。

异常结算测试曾将目标固定放在随机出生点的 X 正方向，恰好被植被实例中的岩石遮挡。只读射线确认相机和枪口都命中该岩石；测试现先选择导航可达、射界无遮挡的站位，再使用正常射击验证死亡，保留遮挡诊断与失败记录。此项调整没有修改子弹或死亡逻辑。

复跑示例（每次使用新的 `--tag`）：

```powershell
python Scripts/Extraction/run_soviet_audit.py --tag SovietAuditNew
python Scripts/Extraction/run_network.py --scenario soviet --players 6 --guards 0 --packaged --package-root Packaged/SovietMilitary03 --rendered-clients 1 --bounded-rendering --resolution 1920x1080 --isolated-room --loot-seed 17848 --tag SovietSixNew
```

连续路线使用 `--players 2 --guards 0 --soviet-route`；守卫验收使用 `--players 2 --guards 6`；异常结算使用 `--players 3 --guards 0 --soviet-lifecycle`。入口回归使用同样的双人参数，入口脚本换成 `Scripts/Extraction/run_soviet_stairs.py`。常规功能场景将回合上限设为六百秒，默认正式规则仍为三十分钟；异常结算单独覆盖真实一百二十秒测试截止时间。

当前项目的 `GrantStartingSupply` 会提供 M4A1 与 SCAR-H，并把携入 AK 移入背包。地图验收按这份现有规则检查枪械保留；旧 AK 握枪距离断言仍保留在原有枪械场景中。
