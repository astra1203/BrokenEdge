# 航天基地：阶段 1 可玩转换样本

2026-10-26 后续接入：[Stinger 原版步枪移动动画](../Stinger/OriginalRifleLocomotion.md)。该记录包含 AK 动画替换及本样本路线回归，下文保留阶段 1 的原始验收结果。

日期：2026-10-02。 **阶段 1 小样通过，包含下文明确列出的重建与近似。** 一个真实室内区域及相邻室外已完成转换、UE
冷加载、单人和双人通行验证。完整地图、撤离模式、交互门和原版专用渲染仍待后续阶段实现。

## 打开与运行

在 UE 5.8.3 内容浏览器打开 `/Game/Maps/SpaceCenter/L_SpaceCenterSample`，点击 Play。地图已指定 `BP_StingerTestGameMode`
，默认角色为 `BP_StingerCharacter`；两个出生点附近各放一把 `BP_AK47`，沿用当前项目拾取与移动按键。默认项目启动地图未切换。

源码工具：[Scripts/SpaceCenter](../../Scripts/SpaceCenter/README.md)
。最终资源：[Content/Maps/SpaceCenter](../../Content/Maps/SpaceCenter)。引擎路径为
`C:\UnrealProjects\UnrealEngine-5.8.3-release`；工作分支 `ue5.8-20260930`，起点提交
`f9eff23d672687fd45448b707c8be02866122b02`。

## 已恢复的内容

| 内容       | 实际范围                                                                             |
|------------|--------------------------------------------------------------------------------------|
| ZIP 索引   | 58 个 ZIP、1,605,676 个资产条目；完整包路径查找，没有同路径重复项                    |
| 原始布局   | `RoomBuilder_SpaceCenterH1`、四个相邻 `BigMeshGroup`、`Landmark_26_-18`，共 6 个分块 |
| 选区       | 源组件原点 X=659000～671000、Y=-458000～-445000 cm；跨边界的大网格保留完整几何       |
| 原点平移   | 全部场景对象减去 `(664000,-451000,-22400)` cm，原比例与朝向保留                      |
| 几何与摆放 | 255 个网格导入；选中布局排除一个专用雾体后，摆放 1,177 个源组件，实际使用 254 种网格 |
| 材质       | 523 个重建 PBR 实例、658 张选用纹理；保留组件的材质槽覆盖关系                        |
| 运行设置   | 蜂医 GameMode、两个出生点、两把 AK、基础天空和室内照明、地面与复杂碰撞               |

转换采用固定的 CUE4Parse 提交 `34f1a223e942b67e381329983fb7713f62f9ef9d`。源资源 ZIP 只读；缓存、GLB、PNG 和解析结果均在
`Saved/SpaceCenter`。最终 Content 为 UE 原生可编辑资产，共 2,181 个文件、563,011,488 字节（536.93 MiB）。完整路径、大小与
SHA-256 见 [GeneratedAssets.json](Phase1Evidence/GeneratedAssets.json)。

三个可辨识对照对象为圆形实验室及其门框、连接上下层的楼梯与栏杆、北侧门口和相邻建筑道路。源对象路径、变换、网格与目标资源映射保留在证据中；俯视及近景图用于检查摆放、比例和接缝。没有原版游戏同机位截图，本阶段不作原版外观一致的结论。

## 近似、缺失与处理

详细记录见 [ConversionIssues.md](ConversionIssues.md)，包括原始失败、处理方式、替代范围和仍未恢复的部分。

- 5 个网格无法解析距离场尾部，只对白名单保留此前已读取的原顶点、索引和材质槽；没有声称恢复原距离场或 PhysX 数据。
- 原 `LandscapeCDLOD` 未恢复。按门槛和道路标高补建底面及北侧平台，几何尺寸、材料和高度在 SC-006 中明确列出。
- 原专用材质改为普通 PBR；分层混合、天气、雾、动态标牌、部分遮罩和原烘焙照明存在差异。完整重放中 11 个纹理请求未成功，影响 9
  个实际使用的材质实例；逐材质绑定与降级见 [MaterialDecisions.json](Phase1Evidence/MaterialDecisions.json)。
-
原门控制逻辑未恢复。北侧两片门叶及室内一片楼梯门固定在开启位置，偏移统一记录在 [overrides.json](../../Scripts/SpaceCenter/overrides.json)
；其他源布局对象保留原变换。
- 碰撞重建为双面复杂三角形。保留源组件明确的无碰撞设置，未恢复全部原游戏自定义通道。该方案用于小样，扩区前应评估简化碰撞、实例合批和流送。

## 验收路线与证据

固定路线为 **圆形房间 → 实验室门口 → 上楼 → 下楼 → 北侧出口 → 室外平台 → 原房间**，规划路径约 222.14 米。角色由 Enhanced
Input 驱动；不传送、不临时关闭碰撞。双人 PIE 按顺序走同一条路线，并用 PlayerState ID 在另一世界核对远端位置和持枪弹量；12
次远端检查的最大位置差为 0.080 cm，弹量一致。

转换验证、UE 冷加载、单人运行与双人复制是分开的证据。Commandlet 不提供本测试所需的 PIE
物理场景，因此冷审计不冒充碰撞或通行检查。初次单面墙返程失败和测试脚本身份匹配问题另存于 [diagnostics](Phase1Evidence/diagnostics)
，标记为非验收记录；最终验收只采用通过的运行。

| 验证层级         | 结果                                                                                | 证据                                                                                                                  |
|------------------|-------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------|
| 完整转换重放     | `replay-01` 通过；纹理失败按逐项记录降级，选中几何全部可用                          | [转换运行记录](Phase1Evidence/conversion/Run.json.gz)                                                                 |
| 几何校验         | 255/255 网格顶点、索引与源包围盒检查通过                                            | [GeometryAudit.json](Phase1Evidence/GeometryAudit.json)                                                               |
| 重复执行         | 1,177 个对象 ID 不变；布局、网格、材质引用无变化；913 个网格／纹理输入 SHA-256 不变 | [Repeatability.json](Phase1Evidence/Repeatability.json)                                                               |
| UE 5.8.3 构建    | `BrokenEdgeEditor Win64 Development` 退出码 0                                       | [构建日志](Phase1Evidence/build/Build.txt)                                                                            |
| 地图导入与冷加载 | 最终导入成功；冷审计 6,397/6,397 通过，UE 与 GLB 包围盒最大差 0.000489 cm           | [导入报告](Phase1Evidence/import/Report.json)、[冷审计](Phase1Evidence/audit/Report.json)                             |
| 最新构建单人通行 | `phase1-final-1p`：16/16 通过，含拾枪、蹲跳、视角切换和 6 个路线检查点              | [单人报告](Phase1Evidence/single/Report.json)                                                                         |
| 最新构建双人通行 | `phase1-final-2p`：34/34 通过，含两名角色路线和远端同步                             | [双人报告](Phase1Evidence/multi/Report.json)                                                                          |
| 蜂医基线回归     | 资产审计 34/34，最新构建动作回归 29/29 通过                                         | [资产审计](Phase1Evidence/baseline-audit/Audit.json)、[动作回归](Phase1Evidence/baseline-actions/pie_validation.json) |

本轮未改动角色、武器 C++、原蜂医资产或项目默认配置。构建仍有既有 `C4263`／`C4264` 和 `C4701` 警告；编辑器启动日志也保留基线已有的
`LogAutomationTest` 条件失败信息。上述通过结论来自具体测试报告与正常退出，不表示日志无警告或已复测阶段 0 的全部 230 项检查、7
个 LAN 用例。

证据入口为 [Summary.json](Phase1Evidence/Summary.json)，包含归档文件的字节数和
SHA-256。源对象及原变换见 [SceneManifest.json](Phase1Evidence/SceneManifest.json)
，目标资产映射见 [ImportedAssets.json](Phase1Evidence/ImportedAssets.json)
，工具版本见 [Implementation.json](Phase1Evidence/Implementation.json)。原始转换与编辑器日志、碰撞采样和 CSV 以 gzip 归档。

### 画面检查

归档 22 张 1280×720 截图，其中逐张查看了 4 张场景图、6 张单人路线图及双人运行中玩家 2 的 6 张路线图，共 16
张；清单与观察见 [VisualReview.json](Phase1Evidence/VisualReview.json)
。建筑群、圆形实验室、门框、上下层楼梯及北侧道路可辨认，尺寸和摆放另由源变换与包围盒检查支持。室内偏暗、平台偏亮及小样矩形边界仍可见；第一人称画面左下方也可见角色装备，后续视角表现需继续检查。

![航天基地小样总览](Phase1Evidence/capture/overview.png)

[俯视布局](Phase1Evidence/capture/top.png) · [室内近景](Phase1Evidence/capture/interior.png) · [室外近景](Phase1Evidence/capture/exterior.png) · [上楼](Phase1Evidence/single/p1_stairs_up.png) · [室外往返](Phase1Evidence/multi/p2_outside.png) · [返回房间](Phase1Evidence/multi/p2_room_return.png)

### 小样性能记录

设备为 Ryzen 5 7500F（6 核 12 线程）、31.61 GiB 物理内存、RTX 4060 Ti，驱动 616.92，DX12。采用有渲染的隐藏编辑器
PIE、High（2）、屏幕比例 100%、关闭 VSync。实际单人视口为 **1250×522**；双人视口为 **1250×522 和 640×482**。命令行请求和截图尺寸为
1280×720，不能替代实际视口尺寸。

| CSV 指标                  | 单人（8,150 帧）          | 双人（12,226 帧）         |
|---------------------------|---------------------------|---------------------------|
| 帧时间 P50 / P95 / 最大值 | 12.60 / 20.79 / 543.26 ms | 17.19 / 28.44 / 549.60 ms |
| 游戏线程 P95              | 20.60 ms                  | 28.34 ms                  |
| GPU 时间 P95              | 5.44 ms                   | 9.51 ms                   |
| 进程物理内存峰值          | 4,913.16 MiB              | 5,059.60 MiB              |
| GPU 本地内存使用峰值      | 4,970.70 MiB              | 6,067.82 MiB              |

原始计数及统计见 [Performance.json](Phase1Evidence/Performance.json)
，硬件记录见 [Hardware.json](Phase1Evidence/Hardware.json)。CSV 包含编辑器、Python
驱动、路线停顿和截图开销；帧时间尖峰未经归因，不能据此给出打包版帧率或专服容量承诺。`RenderThreadTime` 的 P95 仅为单人
0.0018 ms、双人 0.0004 ms，此计数不足以建立可信的渲染线程预算；后续扩区前应使用 Unreal Insights 或独立打包运行补测。

## 重跑方法

从项目根目录执行，每个标签使用新名称，上一条命令成功后再运行下一条：

```powershell
$uePython = 'C:/UnrealProjects/UnrealEngine-5.8.3-release/Engine/Binaries/ThirdParty/Python3/Win64/python.exe'
& $uePython Scripts/SpaceCenter/run_editor.py audit --tag review-audit-01
& $uePython Scripts/SpaceCenter/run_editor.py traverse --players 1 --tag review-walk-1p-01
& $uePython Scripts/SpaceCenter/run_editor.py traverse --players 2 --tag review-walk-2p-01
```

完整转换、导入、碰撞采样和路线生成命令见 [工具说明](../../Scripts/SpaceCenter/README.md)。运行元数据记录实际命令、进程、Git
基线和执行时脚本哈希。

## 后续边界

本阶段只证明限定区域可从源布局恢复并由现有角色通行。下一阶段仍需独立撤离模式和专服加两个独立客户端的实战验证；本轮同进程双人
PIE 不替代这些条件。物品、搜刮、撤离结算、AI、干员能力和整图流送继续按原路线后续阶段推进。
