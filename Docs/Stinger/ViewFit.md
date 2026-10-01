# 蜂医第一人称 AK 适配

当前工作分支为 `ue5.8-20260930`。角色仍直接继承项目的 `ABrokenEdgeCharacter`。

后续发现的双武器低头重影及补充验证见 [WeaponVisibility.md](WeaponVisibility.md)。本页原验证未覆盖“先装备副武器，再低头”的组合。

第一人称使用修正导入的 Idle01、AimIdle_Pose、RelaxIdle、Fire、ChangeClipFull 和
GetWeapon。第三人称按本轮确认的方案暂用项目原动作的蜂医骨架重定向版本。两套视觉动作共享原有游戏状态和第三人称通知，不能理解为已经补齐源素材中缺失的第三人称步枪动作。

## 改动

- 第一人称网格保留完整双臂、肩袖及蒙皮，裁切和源网格开放边界加封口。生成结果 4,981 个三角面，其中 373 个封口面；按位置焊接检查剩余开放边为
  0。原第三人称身体保持完整。
- AK 显示网格增加真实握持插槽，使用可编辑的 `FirstPersonWeaponFit` 配置。手臂 IK 不拉伸骨长；肩部参与前伸。枪的位置在右手最终骨骼姿势完成后更新。
- AK 瞄准配置为摄像机空间位置 `(20, 0, -13.95)`、俯仰 `0.75°`、朝向 `-90°`。按实际画面校准前准星与后照门，避免枪托遮挡；导入脚本不再重复覆盖这项配置。
- 弹匣通过独立视觉动画代理跟随左手，握持变换由配置计算。代理读取当前完成的手部姿势，避免上一帧数据导致漂移；切回第一人称时可恢复到换弹当前阶段。
- 武器基础骨骼姿势从实际游戏武器复制，避免共用武器骨架的参考姿势与该网格导入绑定姿势不同，造成部件或握持方向错误。
- 第一人称蒙太奇包含默认片段，使用短混合时间，保留真实导入动作。第一人称蒙太奇没有游戏结算通知。
- 换弹结束对齐第三人称 `ReloadFinished`；切枪分别对齐 `AttachWeapons` 和 `SwapFinished`。视角切换从当前第三人称动作进度恢复，不从第一帧重新播放。
- 本地手臂、枪使用 UE 第一人称渲染标记；原身体、原武器作为世界空间表示。切回第三人称、换武器和死亡时恢复原可见性及渲染标记。
- 命中调试球默认关闭，可用 `BrokenEdge.Debug.HitScan 1` 开启，显示 1 秒。

## 入口及复测

原入口为 `/Game/Characters/Stinger/Maps/L_StingerTest`、`BP_StingerCharacter`。适配先在 `L_StingerRepairTest`
录制、检查，验收记录通过后由 `promote_view_fit.py` 更新原角色和地图。

离线生成 `prepare_capped_view_arms.py`，独立命令行编辑器依次运行 `import_view_arms.py`、`configure_view_fit.py`。这些脚本不要通过
RiderLink 的游戏线程同步导入调用，以免重现 TaskGraph 队列重入。

`validate_view_fit.py` 以 30 Hz 固定模拟步长运行，按 5 fps 保存 1280×720 帧。通过世界时间防止高分辨率截图触发 Slate
回调重入而重复推进测试。覆盖待机、瞄准、开火、两次换弹、换弹中切视角、抬头低头、蹲起、跳跃、冲刺、行走、双武器、切枪、切枪中切视角、第三人称、返回第一人称、受击、死亡。

`validate_pie.py` 通过 Enhanced Input 检查单人、双人玩法和远端身体。远端可见性检查是运行状态检查，不能代替独立客户端逐帧画面验收。

`validate_multiplayer_view.py` 在两个本地 PIE 视窗分别面对另一个玩家，保存两端画面并检查本地专用手臂、远端完整身体。这是同进程
PIE 检查，不是独立进程或互联网联机验收。

## 验证证据和限制

最终结果见 `ViewFitSummary.json`、`ViewFitMotionValidation.json`、`ViewFitPIEValidation.json` 和
`ViewFitVisualReview.json`；完整动作视频及关键帧在 `ViewFitCapture`。旧 `RepairSummary.json` 是上一版的历史失败记录。

适配覆盖本测试地图中的 AK47。其他武器仍需要各自的握持、瞄准和弹匣配置。跳跃源动画含零缩放，继续隔离；空中保持基础持枪姿势并暴露缺失状态标记。冲刺暂用导入的
RelaxIdle，受击和死亡由原第三人称动作处理。本轮并未逐帧验收全部 307 个映射动画，也不代表完成三角洲行动全部系统的复刻。
