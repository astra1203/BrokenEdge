# 原版梯子来源修正

本次按用户确认的**当前解包版本、当前地图还原区域**修正。此前从模型名含 `ladder` 的 39 个家具道具里筛出 3 个可爬点，不能作为原版爬梯还原的证据。旧报告和录像保留其机制测试价值，不将它们重新标成原版点位验收。

## 来源与实现

- 扫描 `Maps/SpaceCenter_Ma1/InterActors` 的 118 个包头（包括 HD 伴随包），发现 7 个 Gameplay 包包含实际爬梯 Actor，共 10 个实例。按当前 7D 区域过滤后为 9 个；另 1 个位于区域外，未搬进地图。
- 身份由 `BP_Interactor_ClimbLadder_TemplateNew_C` 等原版交互类确认。每个实例保留包路径、对象名、导出序号、源文件 SHA-256、源坐标及减去 `[664000,-451000,-22400]` 后的工程坐标。
- 按原数据组合 `SM_Industrial_Tools_Stepladder_02A/B/C`，解析模板继承、`Ladder_System` 等模型插槽和 `PerInstanceSMData`，不以附近道具替代缺失数据。9 个实例共 125 个模型段。
- 材质使用原 `MI_GEN_Props_stepladder_01` 的 CA/NRM 贴图，并接入现有 NRMAC 重建材质。当前包解码得到的贴图为 256×256；不宣称恢复了原厂完整材质图。
- 横档位置从原 GLB 三角面测量，单段间距约 38 cm；世界空间接触点包含源缩放和实例变换。手部边缘留量为项目参数，不标成原版数值。
- 旧的 3 个家具梯只移除已知 SourceId 对应的交互 Actor，家具外观保留。新生成对象带 `TraversalSourceV2` 标记，清理只处理本工具拥有的对象。

源入口、出口的原始变换保留在清单中。项目角色胶囊为半径 34 cm、半高 92 cm。当前地图已应用原 `TopBox` 适配：取其内侧边界并内缩一个胶囊半径加 3 cm，在固定 XY 探测支撑面。2026-10-11 的 `LadderDemo20261011Bind02` 已重新保存上下入口体积，取代 Bind10 基线。

`TopClearance` 根据原模型最高点计算上下过沿所需的额外抬升，默认 0 保留原测试梯子的行为。该属性由服务器复制，路径仍执行原有胶囊 sweep。脚本移除了与运行时重叠的 3 cm 余量；Bind02 已完成九个实例的编辑器碰撞检查，逐实例运行时验收仍未全部完成。

## 当前验证状态

### 2026-10-11 自动入口修复与 PIE 演示

旧地图交互 Actor 的 `BottomEntry`／`TopEntry` 体积为零，无法触发自动附着。重新绑定时还确认 Unreal Python 的一元 `-Vector` 会修改原向量，导致上下入口共用的朝向被反转；改用 `facing * -1.` 并增加源入口方向断言。

新增 `TRAVERSAL_BINDINGS_ONLY=1` 路径，在已加载的目标地图中检查模型资产哈希、几何变换和碰撞配置，然后仅重建交互绑定。它拒绝脏地图或几何不匹配，不重新导入模型、贴图或修改场景碰撞。Bind02 的六处路径通过碰撞预检，三处不安全实例保留外观并禁用交互；其中两处长梯顶部的外侧接近采样未找到地面，不能据此宣称所有入口均已实测。

演示目标为 `9cf77452749b621c`，位于工程坐标约 `(25, -7102, -110)` 的黄色竖梯。使用隔离存档、大厅正式开局、同进程专服与两个客户端。`demo_ladder.py` 仅在开始前设置位置，之后通过 Enhanced Input 执行走近、暂停、上下往返和空格蹬离，记录客户端／服务器状态；不强制设置攀爬状态。录像和状态证据见 `SourceCorrectionEvidence/LadderDemo20261011.json`。

本次不修改 C++ 公共接口，不提交或推送 Git。此结果不替代新双端打包、弱网、占用、武器恢复、完整步行路线及全部九处实例的运行时验收。以下恢复记录和 Bind10 结果是历史证据。

### 2026-10-11 恢复工作

已通过 `git stash apply 3816c6ee53d0270aa6370759179022416dbdb87a` 恢复整份搁置改动，stash 备份和工作区的引擎关联 GUID 修改均保留。65 个文件与 stash 核对一致（文本按 Git 换行规则比较），其中 10 个 LFS 资产已核对 SHA-256 和大小；无合并冲突。`BrokenEdgePlayerController.h` 不在该 stash 的改动范围内。

本次 4 项源变换回归、相关 Python 语法检查和三个源模型的横档测量通过。Sources04 清单、3 个 GLB 和 2 张贴图缓存哈希正确，9 个区域内实例均有 BottomBox／TopBox 数据。地图仍为 Bind10，不能视为已应用自动入口体积或新的 TopBox 适配；必须重新构建 Editor 后再绑定。

`LadderResume20261011Editor01` 在启动前被资源守卫拒绝：可用内存 7.10 GiB，低于 8 GiB 门槛；C／D 盘分别剩余 400.10／104.16 GiB。Editor 编译未启动，重新绑定、新双端包和本轮运行时验收均未执行。没有启动等待资源的后台任务。恢复与资源证据见 [Resume20261011.json](SourceCorrectionEvidence/Resume20261011.json)。

下一步从 Editor 构建开始，随后重新绑定、复核幂等性和逐点碰撞、构建新双端包并执行联机验收。全部 9 个点位保持原位置、外观和场景碰撞；入口与胶囊适配后仍不安全的实例保留模型、禁用交互并记录原因。原有资源门槛继续生效，不关闭用户应用或原游玩专服腾资源。

### 2026-10-09 交接基线

截至 2026-10-09，**来源和地图修改已落地，完整验收未完成**。不能把“编辑器检查脚本执行成功”解释为所有源梯子均可通行。

- 源包解析、插槽变换、几何导出和横档测量已执行；4 项变换回归测试通过。
- `LadderSourceEditor01` 的 Editor 编译通过。它不代表新客户端、专服包或联机测试已通过。
- 当前保存的 `LadderSourceBind10` 包含 125 个原模型段、4 个通过编辑器碰撞预检的交互、5 个未开放交互的源实例。原 3 个误绑交互已移除，家具道具保留。地图哈希与 `Scripts/Traversal/source_bindings.json` 一致。
- Bind07／Bind08 重复运行的模型变换、SourceId、分类与交互对象身份一致，没有重复创建或额外删除对象；这是初版适配的幂等性结果，最新 TopBox 适配还需重复验证。
- Bind10 已核对导入后模型顶点边界与源包边界，三个模型误差均小于 1 cm；`LadderSourceAudit02` 留存逐点碰撞诊断。
- 新增逐实例实际输入测试 `--traversal-source-only`，检查上下往返、暂停、双端武器恢复，以及原 3 个家具梯没有交互提示。脚本存在不等于已通过。
- `formal_route.json` 已停用旧家具梯路线，目标改为原交互 `9cf77452749b621c` 的上入口。`source-route` 需先找到胶囊可通过的步行连接，随后才能执行连续输入验收；碰撞探路也不等于角色已走通。
- 本轮尚未完成新包、正式连续路线、正常／弱网、占用与生命周期回归及双视角录像验收。
- 原版实机版本、同位置画面对照及逐帧动作一致性仍未验证；当前基线仅为用户指定的解包资产。

`LadderSourceRoute01` 的密集路线扫描与 `LadderSourceServer01` 的独立构建分别触发了低内存保护，最低采样约为 1.13 GiB 和 0.40 GiB；均不能计为通过。路线扫描已改为有上限的按需搜索，后续构建准备使用现有 `--no-uba --jobs 1`。重试需先满足资源条件，不关闭共享应用腾资源。证据见 [清单](SourceCorrectionEvidence/Manifest.json)；其中当前地图对应 Sources03，含 TopBox 的待应用来源清单为 Sources04。

`LadderSourceValidation03` 等待资源 20 分钟后退出，可用内存未达到 8 GiB；Bind11、Server02、Client02 和 Normal02 均未启动。当前没有遗留的待运行验证进程。恢复工作时先重新绑定与复查，再创建新双端包；旧包不能用于验证本轮地图修改。

## 点位与当前开放状态

下表为源 Actor 在工程中的坐标，单位 cm，方便在编辑器定位；不是另设的传送点或新摆放位置。在 `L_SpaceCenterExtraction` 的 World Outliner 搜索 `Traversal_SourceMesh_<SourceId>` 可定位模型，已开放交互的对象名为 `Traversal_Ladder_<SourceId>`。这里“预检通过”仅指编辑器胶囊检查。

| SourceId | 原对象后缀 | 工程坐标 XYZ | Bind02 状态 |
| --- | --- | --- | --- |
| `a2a2a227017b5672` | TemplateNew4 | -11778.6, 11081.0, -799.4 | 下入口未找到支撑面，未开放 |
| `ae7a89bd071bffda` | TemplateNew6 | -10142.4, 9615.0, -279.7 | 上入口胶囊与码头边缘相交，未开放 |
| `bfd9aa0f4d20e855` | TemplateNew10 | -432.6, 13258.9, 25.9 | 碰撞预检通过，实机待验收 |
| `670af423419e21d8` | TemplateNew11 | 3465.3, -2546.7, 2551.5 | 上入口站立空间不足，未开放 |
| `370921801e3e15fd` | TemplateNew9 | -4559.3, 9802.7, 526.0 | 碰撞预检通过，实机待验收 |
| `577f28d856f3f159` | TemplateNew3 | 7919.4, -954.3, 1031.0 | 核心路径预检通过；顶部外侧接近采样无地面，实机待验收 |
| `14fd6a616d4f8738` | TemplateNew2_2 | 1751.3, -5929.9, 1031.4 | 核心路径预检通过；顶部外侧接近采样无地面，实机待验收 |
| `9cf77452749b621c` | TemplateNew7 | 25.2, -7101.6, -109.8 | PIE 上下往返、暂停、蹬离通过；打包待验收 |
| `a3fd9163e28c7e90` | TemplateNew8 | 59.6, -6197.4, -708.5 | 预检通过，实机待验收 |

## 复现入口

使用 UE 自带 Python：`C:/UnrealProjects/UnrealEngine-5.8.3-release/Engine/Binaries/ThirdParty/Python3/Win64/python.exe`。

1. `Scripts/Traversal/prepare_sources.py --tag <新标签>`：只读源 ZIP，生成可追溯实例清单。
2. `Scripts/Traversal/measure_rungs.py` 和 `Scripts/Traversal/test_source_ladders.py`：源横档测量和变换回归。
3. 通过 `Scripts/Extraction/resource_guard.py` 包装 `Scripts/Traversal/run_editor.py ladders --tag <新标签>`；`ladder-audit` 为只读碰撞诊断，`geometry` 核对导入后顶点与源包边界。
4. 在资源守卫下用 `run_editor.py source-route --tag <新标签>` 探测步行连接；成功时更新正式路线，失败时保持待验证状态。
5. 用 `run_build.py --target Server|Client --package --package-root Packaged/<新目录> --file-cook --cook-root D:/BELadderCook/<各端新目录> --direct-stage --no-uba --jobs 1 --cook-workers 2 --tag <新标签>` 分别构建双端；每次用资源守卫包装，各端输出目录不能复用失败目录。
6. 双端包通过后，在资源守卫下用 `run_network.py --tag <新标签> --packaged --package-root Packaged/<新目录> --scenario traversal --traversal-source-only --rendered-clients 2 --port 28777 --isolated-room` 运行逐实例验证；另加 `--lag`／`--traversal-record` 运行弱网或录像轮次。
7. 单独运行基础机制、`--traversal-lifecycle --players 3` 与 `--traversal-route-only` 回归。逐实例接触分析使用 `analyze_contacts.py <运行目录> --source-id <SourceId>`；分析读取该轮保存的绑定快照，避免误用历史家具梯横档。

重任务继续使用既有资源守卫：启动需 C 盘 40 GiB、D 盘 100 GiB、可用内存 8 GiB。不得通过关闭用户正在使用的 Rider 或原游玩专服来腾资源。未提交或推送 Git。
