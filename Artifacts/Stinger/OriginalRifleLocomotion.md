# Stinger 原版步枪移动动画接入

2026-10-26。范围为 Stinger 持 AK 的第一、第三人称移动表现；通过武器 Presentation 的 Locomotion 字段显式启用。未配置该字段的武器继续走原路径。没有修改移动速度、输入、伤害、弹药或网络规则。

## 来源与转换

来源为 `D:\司晨\源代码和素材\三角洲解包资产\原始资源ZIP\02_武器_011.zip`，包内 `DeltaForce/Content/Models/Weapons/Guns/AR/Anims/Male`。限定选择 43 个动画，连同依赖共 50 个包，不修改原始 ZIP。逐项路径、哈希和转换结果见 [来源清单](LocomotionEvidence/Sources.json)及 [转换结果](LocomotionEvidence/Conversion/ExportResults.json)。

- 第三人称：站立、蹲姿待机，走、跑、蹲走各八方向，冲刺，站立和蹲姿俯仰瞄准姿势。
- 第一人称：Idle、Walk、Run、Sprint，以及 Walk/Run/Sprint 间源资源实际存在的五个过渡。没有找到独立 SprintToRun，使用混合回到 Run。
- 第一人称源动画包含 additive，转换时显式补齐 Idle_Pose_2 基准并解码成完整姿势。第三人称俯仰姿势重建为相对各自中心姿势的 mesh-space additive。

保留 Source 与适配后的 MappedV4 两份序列；目录名沿用导入器，实际映射版本为 5。第三人称骨骼覆盖率 100%，第一人称 142/144（98.61%）。适配版本将前臂、手的局部位移固定到目标参考骨架长度，保留源旋转，避免新移动与旧动作交替时肢体长度变化。源序列不受此修正影响。新移动序列不带游戏逻辑通知，也不驱动 root motion。

## 接入方式

新增 `RifleLocomotionSet`、原生 `AnimNode_RifleLocomotion` 和复制的 `ABP_StingerRifleLocomotion`。节点位于 WeaponSlot 前，保留后续握枪 IK、动作蒙太奇及攀爬覆盖；跳跃、攀爬、持刀、死亡时回到既有路径。站蹲切换和节点启停采用 0.15 秒混合。原瞄准叠加仅在新节点实际启用时关闭，避免重复叠加。

第一人称在持枪、落地、非瞄准且没有动作覆盖时播放新移动循环和过渡，保留既有手部校准及武器跟随。速度判断带滞回。开镜继续使用现有原版 AimIdle_Pose 与瞄具对齐；本轮没有添加新的 ADS 移动晃动。

资源集中在 `/Game/Characters/Stinger/Animations/DeltaForceLocomotion`。仅新增 AK 专用 Presentation 并更改 AK 绑定；两个 Stinger 角色蓝图使用复制的动画蓝图，其他武器通过空配置回退。

| 标定 | 原动画自然速度（cm/s） | 用途 |
|---|---:|---|
| 走 | 131.86 | BlendSpace 132 |
| 跑 | 390.56 | BlendSpace 391 |
| 蹲走 | 128.21 | BlendSpace 128 |
| 冲刺 | 561.18（落脚段估计） | 播放速率基准 561 |

前三项来自源根骨位移，冲刺是原地动画，使用落脚段估计。实际角色原有速度不变，超过自然速度时调整第三人称播放速率。这里恢复的是原版动画素材及项目内适配，并非原游戏完整动画图或完全相同的步态系统。

## 验收与证据

`BrokenEdgeEditor Win64 Development` 编译成功。最终验证结果如下：

| 检查 | 结果 |
|---|---|
| 冷启动资产审计 | 140/140 |
| 单人移动 | 96/96 |
| 同进程双人移动 | 96/96 |
| 双人握枪与手臂长度 | 21/21 |
| 既有单人、双人动作回归 | 29/29 |
| Phase1 单人路线 | 16/16 |
| 既有 WeaponPIE.InputAndAssembly | 1/1 自动化测试 |

最后一项涵盖武器输入与装配、持刀、反复切换视角、刀攻击及切回枪械。截图抽查覆盖第三人称侧跑、蹲走及第一人称走路、冲刺；没有声称人工逐帧检查全部视频。

最终结果见 [Summary.json](LocomotionEvidence/Summary.json)，包含冷启动资产审计、单人和同进程双人移动、双人握枪、动作及 Phase1 路线报告。每套测试同时要求断言通过和编辑器正常退出。移动测试通过实际输入覆盖八方向走、跑、蹲走，以及第一人称走跑冲刺、瞄准、开火、换弹、跳跃落地；同时检查实际动画来源和握枪距离。

[证据目录](LocomotionEvidence)保留压缩日志、报告、截图及六段每秒 6 帧采样的移动视频。视频用于复核姿势，不代表性能测试。资产哈希绑定本次交付版本。编译与 PIE 是不同证据；本轮没有打包版、独立专服或两个独立客户端验证，也不据此宣称全部攀爬动作通过。

## 复现和回滚

在项目根目录执行。需要本机 UE 5.8.3、已有 SpaceCenter 工具引导及源资源目录；每个测试 tag 必须唯一，PIE 测试依次运行。

```powershell
./Scripts/Stinger/convert_locomotion.ps1
$engine = 'C:/UnrealProjects/UnrealEngine-5.8.3-release'
& "$engine/Engine/Build/BatchFiles/Build.bat" BrokenEdgeEditor Win64 Development "$PWD/BrokenEdge.uproject" -WaitMutex
$editor = "$engine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
& $editor "$PWD/BrokenEdge.uproject" -run=pythonscript "-script=$PWD/Scripts/Stinger/setup_locomotion.py" -NullRHI -unattended -nosound
& $editor "$PWD/BrokenEdge.uproject" -run=pythonscript "-script=$PWD/Scripts/Stinger/audit_locomotion.py" -NullRHI -unattended -nosound
$uePython = "$engine/Engine/Binaries/ThirdParty/Python3/Win64/python.exe"
& $uePython Scripts/Stinger/run_locomotion.py --players 1 --tag review-movement1
& $uePython Scripts/Stinger/run_locomotion.py --players 2 --tag review-movement2 --video
& $uePython Scripts/Stinger/run_baseline.py grip --players 2 --tag review-grip
& $uePython Scripts/Stinger/run_baseline.py actions --tag review-actions
& $uePython Scripts/SpaceCenter/run_editor.py traverse --players 1 --tag review-route
```

关闭测试编辑器后，使用同样的 Python commandlet 入口运行 `Scripts/Stinger/rollback_locomotion.py`，恢复两个角色蓝图和 AK 的原绑定。脚本优先读取 `Saved/Stinger/Locomotion/BindingsBefore.json`，不存在时使用本目录归档副本；不会删除新增资源。恢复后重新运行 setup 可再次接入。`Configuration.json` 记录最后一次配置运行，最初绑定以 `BindingsBefore.json` 为准。
