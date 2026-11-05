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

本节在验证结束后记录结果。导航检查与使用场景定位夹具的 RPC 检查分别记录，不作为无传送连续实走或全地图性能结论。

当前项目的 `GrantStartingSupply` 会提供 M4A1 与 SCAR-H，并把携入 AK 移入背包。地图验收按这份现有规则检查枪械保留；旧 AK
握枪距离断言仍保留在原有枪械场景中。
