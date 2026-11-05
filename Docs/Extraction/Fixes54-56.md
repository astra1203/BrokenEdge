# 54–56：第一人称栓狙、基地门碰撞与测试配装

2026-11-06。修改项目代码与项目资源，未修改 Unreal Engine 源码。用户确认只处理第一人称，并在所有搜打撤地图的每次出发中提供测试装备。

## 实现

- **54 栓狙**：`Sniper_Rifle_A` 枪口朝 +X，原表现配置直接使用朝 +Y 的 AK 相机变换，造成枪身横在屏幕前。栓狙表现配置补上坐标转换，保留既有握点和动画。旧枪械没有新参数表时，
  `RefreshMechanics` 不再每帧清空状态；瞄准进度使用兼容的 0.3 秒过渡，射击、弹药和伤害仍走原路径。栓狙开镜复用现有
  `SniperScopeNoBG` 遮罩与 30 度视野，隐藏第一人称枪模，避免不透明镜片遮挡；退出瞄准、打开背包或开始其他动作后恢复枪模。其他枪的表现资源未修改，没有增加第三人称动画工作。
- **55 普通门**：`SM_Door` 原为 `SimpleAsComplex`
  ，但实际简单碰撞形状数量为零。编辑器原生辅助函数按模型本地包围盒生成一个带偏心的碰撞盒，刷新物理与导航缓存。资源脚本检查生成结果，重新保存碰撞与基地导航。沿用
  F 键与服务器开关门校验。
- **56 测试配装**：保留 M4A1 主武器与 SCAR-H 副武器；空缺或损坏的头盔、护甲补为满耐久 2 级装备，可用旧装备保留。装备新的连续
  8×8、64 格测试背包，装入栓狙、霰弹枪与对应 20/24 发备弹，保留步枪的 60/60 发备弹。新长枪弹匣分别装满 5/6 发；携入的同类枪保留
  GUID、附件、弹匣和来源批次。另留 6×2 换枪空间。旧物品放不下时进入出生旁的补给箱，安全箱内容不变。同一局重复初始化不增发物品。

## 编辑器验证

| 验证                 | 结果                                                                                                                                      |
|----------------------|-------------------------------------------------------------------------------------------------------------------------------------------|
| `Fix5456Editor05`    | Editor 编译成功                                                                                                                           |
| `Fix5456Unit03`      | 6 项自动化通过；其中物理子弹测试的临时 World 销毁产生 3 条既有上下文警告，0 失败                                                          |
| `Fix5456Audit01`     | 新进程重新加载碰撞资产通过；77 个玩法点开门导航连通审计通过                                                                               |
| `Fix5456Doors01`     | 151 项通过；全部 17 扇门、正反 34 次关门阻挡及开门步行穿越，核对服务器和拥有者位置                                                        |
| `Fix5456PoseAfter04` | 58 项通过；双人真实出生装备、重复初始化、库存 RPC 换枪、栓狙腰射/瞄准/蹲伏/行走/冲刺恢复、开镜遮挡与恢复、两把测试枪射击及装填、SCAR 切回 |

栓狙各采样姿态的左右手均距渲染握点小于 2 cm，并人工查看修复前、修复后腰射与开镜截图。握点距离不能单独证明视觉正确，因此同时保留截图和姿态变换。

## 打包验证

新的 Development Client/Server 包均构建成功，位于 `Packaged/Fix5456`，未覆盖旧包。五组打包验收共 **402 项通过**：

| 验证 | 结果与证据 |
|---|---|
| 苏联基地测试配装与第一人称 | 62 项通过，包括开镜中切到 SCAR、切回栓狙恢复；[报告](Phase7Fix5456Evidence/PackagedSovietLoadout/Report.json.gz) |
| 太空中心测试配装与第一人称 | 59 项通过；[报告](Phase7Fix5456Evidence/PackagedSpaceLoadout/Report.json.gz) |
| 17 扇门双向阻挡与穿越 | 151 项通过；[报告](Phase7Fix5456Evidence/PackagedDoors/Report.json.gz)、[34 次穿越位置](Phase7Fix5456Evidence/PackagedDoors/DoorCrossings.json) |
| 门交互、搜刮与撤离 | 89 项通过，覆盖远距离拒绝、F 键、占用拒关、移动中反向打开、24 个容器、真实物品转移与撤离返仓；[报告](Phase7Fix5456Evidence/PackagedInteraction/Report.json.gz) |
| 三人异常结算 | 41 项通过，覆盖死亡、主动终止客户端、真实 120 秒回合超时、唯一尸体箱、物品数量/弹匣与安全箱保留；[报告](Phase7Fix5456Evidence/PackagedLifecycle/Report.json.gz) |

最终资源重新加载检查也通过：64 格背包、栓狙向前的腰射/瞄准变换、现有瞄准镜纹理和 30 度视野均正确。实际调用 UE 结构体 `copy()` 并修改复制品的嵌套变换，确认 AK 源配置不受影响；生成脚本已采用此隔离方式。

日志、截图、姿态、自动化报告、代码/资源及新包的 SHA-256 见 [证据总表](Phase7Fix5456Evidence/Summary.json) 和 [文件索引](Phase7Fix5456Evidence/ArchiveIndex.json)。安装包留在本地，不纳入 Git。图像对照：[修复前横向持枪](Phase7Fix5456Evidence/diagnostics/SniperBefore-Hip.png)、[打包版腰射](Phase7Fix5456Evidence/PackagedSovietLoadout/A-BoltSniper-hip.png)、[打包版开镜](Phase7Fix5456Evidence/PackagedSovietLoadout/A-BoltSniper-ads.png)。

## 证据边界与诊断

联机测试采用真实库存与生产 RPC，没有注入测试枪或弹药；角色站位使用明确的位置夹具。一个客户端以 1920×1080、Medium、关闭光追、768
MiB 纹理池渲染，另一个客户端为 NullRHI。这是功能与第一人称画面验证，不是多人同时渲染或性能验收。

保留的前置失败包括：旧脚本仍要求 AK
出生；旧背包挤占换枪空间；栓狙横向持枪和旧参数兼容路径使瞄准进度始终为零；观察者在约两百米外导致射手不在网络可见范围。后者通过将观察者放到射手附近修正测试布置，没有放宽复制断言。

配装、门碰撞和栓狙实现已分别提交并推送为 `bf65f2a2b`、`17f9b6b7a`、`06f4288cb`；每次都核对了 HEAD、跟踪分支与远端 SHA。本地
`.uproject` 引擎关联修改保留且未提交。

复跑时使用全新的 `--tag`：

```powershell
python Scripts/Extraction/run_soviet_doors.py --scenario soviet --players 2 --guards 0 --packaged --package-root Packaged/Fix5456 --rendered-clients 1 --bounded-rendering --resolution 1920x1080 --isolated-room --loot-seed 17848 --tag DoorsNew
python Scripts/Extraction/run_test_loadout.py --scenario soviet --players 2 --guards 0 --packaged --package-root Packaged/Fix5456 --rendered-clients 1 --bounded-rendering --resolution 1920x1080 --isolated-room --tag LoadoutSovietNew
python Scripts/Extraction/run_test_loadout.py --scenario scar --players 2 --guards 0 --packaged --package-root Packaged/Fix5456 --rendered-clients 1 --bounded-rendering --resolution 1920x1080 --isolated-room --tag LoadoutSpaceNew
```
