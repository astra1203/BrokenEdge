# 蜂医第三人称 AK 握持修复

2026-10-02 阶段 0 复核：本文件下方的“已保存”记录与首次冷启动读取的资产不一致，且手腕旋转混用了局部与组件空间。当前验收和最终握点以 [Baseline.md](Baseline.md) 为准；该报告同时保留数值、画面和重新加载证据。

## 历史记录（阶段 0 之前）

以下“本轮”“现在”和检查数字均指之前的修复快照，保留用于追溯，不作为当前资产验收结论。

2026-10-02 画面复核：上一轮距离验收不足以证明握姿正确。FABRIK 保留原动画手腕旋转，且第三人称待机手指与 AK 不匹配。本轮仅修正四个资产：世界 AK 左握点、蜂医第三人称 AnimBP，以及站立腰射／瞄准两个待机动画。左握点组件位置改为 `(4,20,9)`，旋转使用蜂医第一人称握姿基准；FABRIK 改为 `Copy From Target`；两个待机动画的 17 个左手指骨旋转使用现有握持姿态，保留原平移、缩放和其他骨骼轨道。四个资产已保存，保存前文件备份在 `Saved/Stinger/SupportHandBackup`。

本轮已在现有双客户端 PIE 中复核近景，左手能包住护木。配置脚本 `configure_third_person_grip.py` 已同步握点与旋转模式；`calibrate_support_hand.py` 仅处理这两个待机动画。尚未重新完成移动、换弹、切枪、俯仰全流程回归；下文配置、数字和视频属于上一轮，不能作为本轮手指接触验收。编辑器工具源码同步了旋转枚举，本轮未重新构建 C++。

入口仍是 `/Game/Characters/Stinger/Maps/L_StingerTest`。本次修复左手与 AK 护木脱离的问题，保留现有第三人称重定向动画及第一人称适配资产。工作分支为 `ue5.8-20260930`。

实际问题有三层。世界 AK 网格原本没有 `Hand_R`、`LeftHandSocket`，读取时回落到组件变换；补齐插槽后，原持枪姿态又把左手目标放在手臂够不到的位置；旧 FABRIK 位于缓存姿态中，后续开火蒙太奇还会覆盖求解结果。

Rider 调试器在 `AnimationCore::SolveFabrik` 中确认左臂最大长度为 **54.6915 cm**，目标超过可达范围。仅补插槽的稳定瞄准目标最远达 **73.5950 cm**。断点、调用链和实测值见 [ThirdPersonGripDiagnosis.json](ThirdPersonGripDiagnosis.json)。调试结束后已删除本轮断点，用户原有异常断点保持原状态。

世界 AK 的网格插槽现在使用组件空间握点 `Hand_R=(0,-8,0)`、`LeftHandSocket=(0,25,8)`；左手位置经过护木近景校准。蜂医身体的 `RightHandSocket` 平移补偿右手握点，原插槽旋转保留。配置脚本会转换为武器根骨骼空间后保存，不修改共享 Skeleton 插槽。

`AnimNode_WeaponGripReach` 在当前帧最终动画姿势上校正持枪右臂位置，使左握点进入双臂可达范围，保持右手及枪的朝向，并禁止骨骼拉伸。只有蜂医第三人称 AnimBP 显式配置的 AK 网格启用此校正。随后由 `LeftArm → LeftForeArm → LeftHand` 的 FABRIK 贴合握点，目标使用 `RightHand` 骨骼空间，保留动画的手腕组件空间旋转，精度为 0.1 cm。

两项校正在开火、受击、倒地等动画混合之后求解，沿用战斗状态门控。换弹、切枪、丢枪、角色失效或插槽缺失时释放 IK；每次动画更新先清空旧目标与开关，缺失目标按网格去重报告。换弹和切枪的现有结算通知不变。

验收数据分别记录数值与画面，不能仅用插槽配置代替运行验证：

| 证据 | 检查范围 |
| --- | --- |
| [修复前](ThirdPersonGripBefore.json) | 两人场景、缺失插槽、原始偏差 |
| [单人数值](ThirdPersonGrip1p.json) | 897 个连续模拟帧；真实游戏相机；抬头 55°、低头 65° |
| [双人数值](ThirdPersonGrip2pGameplayCamera.json) | 897 个连续模拟帧；监听服务器和客户端的本地、远端角色 |
| [双人录制数据](ThirdPersonGrip2p.json) | 逐帧骨骼距离、动作门控、双武器显隐、近景录制 |
| [画面审查](ThirdPersonGripVisualReview.json) | 本地及远端护木接触、动作过渡、第一人称切换 |
| [构建](ThirdPersonGripBuild.txt) | UE 5.8.3 BrokenEdgeEditor Development 构建 |

单人和双人真实游戏相机数值检查中，稳定持枪阶段左手距目标最大约 **0.1000 cm**，右手约 **0.000012 cm**，双臂长度未增加。稳定阶段定义为动作切换后的 0.4 秒起；换弹、切枪和丢枪阶段另行验证 IK 释放，不把故意离枪的手纳入握点误差。

画面入口：[待机](ThirdPersonGripCapture/idle_left.png)、[瞄准](ThirdPersonGripCapture/aim.png)、[本地握持](ThirdPersonGripCapture/local_idle_left.png)、[第一人称](ThirdPersonGripCapture/first_person.png)、[30 fps 动作视频](ThirdPersonGripCapture/third_person_ak_client.mp4)。汇总与视频帧数见 [ThirdPersonGripSummary.json](ThirdPersonGripSummary.json)。

视频保留实际生成的连续 512 帧，时长约 17.07 秒。原始报告记录了 513 次截图请求，最后一次未在退出前写完；该尾帧已明确记为遗漏，没有补帧或复制帧。验证脚本现已增加截图写完后的退出等待，[补充回归](ThirdPersonGripCaptureTail.json) 已通过。

复现使用 `Scripts/Stinger/configure_third_person_grip.py` 独立 PythonScript commandlet 配置资产，使用 `validate_third_person_grip.py` 编辑器启动脚本运行 PIE。`STINGER_GRIP_PLAYERS=1/2` 选择人数；`STINGER_GRIP_CAPTURE=0/stills/video` 选择数值、截图或连续帧。数值检查保留真实游戏相机，避免外部拍摄相机改变 `GetBaseAimRotation()`。归档脚本 `record_third_person_grip_reports.py --ffmpeg <路径>` 会要求运行报告和人工画面审查均通过。

验证使用同进程 PIE，尚未覆盖独立服务器、高延迟和丢包。视频由所选动作阶段的连续固定模拟帧以 30 fps 编码，不代表实时渲染性能；拍摄临时增加补光，数值测试强制屏幕外角色刷新骨骼，均不保存到地图。原第三人称动画素材质量、瞄准动作表现及其他武器适配不在本次验收范围内。项目既有 `GameFeatureData` 扫描配置错误仍会让 commandlet 返回 1，本轮通过脚本成功标记、蓝图编译、资产重新加载和 PIE 数据分别核对。
