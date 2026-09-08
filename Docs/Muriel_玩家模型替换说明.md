# Muriel 玩家模型替换说明

## 当前配置

`BP_BrokenEdgeCharacter` 的 Mesh 已改为 `/Game/ParagonMuriel/Characters/Heroes/Muriel/Meshes/Muriel_GDC`，继续使用原有 BrokenEdge 角色控制和战斗逻辑。

`BP_BrokenEdgeGameMode`、`BP_TeamsGameMode`、`BP_LobbyGameMode` 当前默认 Pawn 都是该玩家蓝图，因此无需修改 `Menu.cpp` 或 `LobbyGameMode.cpp`。本次没有修改 C++ 源码。

用户已有的 `BP_BrokenEdgeCharacter1` 保持原状。

## 重定向资源

新增资源集中放在 `/Game/Blueprints/Character/Muriel`：

| 资源 | 用途 |
|---|---|
| `IK_EpicCharacter` | 项目原角色的源 IK Rig |
| `IK_Muriel` | Muriel 的目标 IK Rig |
| `RTG_EpicCharacter_To_Muriel` | 源角色到 Muriel 的重定向器 |
| `Animations/ABP_BrokenEdgeAnimInstance_Muriel` | 玩家当前使用的动画蓝图 |
| `Animations/AM_*_Muriel` | 开火、换弹、换枪、受击、淘汰蒙太奇 |

两套 IK Rig 均以 `pelvis` 为重定向根，共匹配 20 条身体和手指链。Muriel 的颈部链包含 `neck_01` 到 `neck_02`；目标姿势已执行自动对齐。

共生成 63 个动画相关资源（包含动画蓝图和其依赖），保留原动画资源。全部输出使用 `Muriel_Skeleton`。

## 插槽和命中盒

在 `Muriel_GDC` 上添加了三个 Mesh Socket：

| 插槽 | 所属骨骼 |
|---|---|
| `RightHandSocket` | `hand_r` |
| `FlagSocket` | `hand_l` |
| `BackpackSocket` | `spine_03` |

手部插槽采用原角色的局部偏移。背部插槽将原背包挂点换算到脊柱空间，避免依赖 Muriel 不具备的 `backpack` 骨骼。

玩家蓝图中的四肢和躯干命中盒按目标骨骼段长度调整；头部盒参考 Muriel 物理资产的头部球体设置。原 `blanket`、`backpack` 两个组件改挂 `spine_03`，盒体尺寸设为零，不再保留旧模型背包和毯子的可命中体积。它们的组件和原生追踪条目仍保留。

这些命中盒属于初步适配，尚未完成各姿势下的精细校准和多人服务器回溯命中验证。Muriel 的翅膀保留模型自身骨骼的基础姿势，没有新增独立的飞行或翅膀动画逻辑。

## 已完成的验证

2026-09-08，在 UE 5.5.4 的 `MainMap` 中进行了单人 PIE：

- 实际玩家是 `BP_BrokenEdgeCharacter`，Mesh 为 `Muriel_GDC`，动画实例为 `ABP_BrokenEdgeAnimInstance_Muriel_C`。
- 移动输入生效，采样到的最大移动速度为 600 Unreal 单位/秒。
- 通过装备输入拾取 AK47，武器附着到玩家的 `RightHandSocket`，HUD 显示弹药。
- 五个蒙太奇的 `Montage_Play` 均返回正时长，说明运行中的动画实例接受了播放请求；这不等同于逐个验证所有 Notify、弹药结算和最终混合效果。
- 三个新增插槽均可在运行时找到。
- 测试结束后停止 PIE，并从磁盘重载玩家蓝图和目标模型，确认模型、动画类、插槽配置已保存；检查了全部 63 个动画资源的目标骨骼。

本机验证数据位于 `Saved/Codex/MurielReplacementBackup/pie_validation.json`，改动前的玩家蓝图、Muriel 模型及骨骼备份也在该目录。`Saved` 为本机生成目录，不随 Git 分发。

本次没有验证独立服务器/客户端联机，也没有重新打包。现有 `Packaged` 目录中的程序不会自动使用这些新资源，需要重新构建打包产物后测试。
