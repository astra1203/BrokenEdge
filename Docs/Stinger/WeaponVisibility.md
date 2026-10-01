# 第一人称双武器重影修复

用户在双武器状态下低头，画面中同时出现第一人称显示枪和背包副武器。运行现场确认：本地玩家的主武器 `OwnerNoSee=true`，副武器却为 `false`；旧逻辑还会在切枪后恢复旧主武器的显示。

上一轮 `ViewFit` 的低头测试发生在装备副武器之前，未覆盖这个组合。旧报告保留为历史记录，不能作为双武器低头正确的证据。

## 修复

武器分别缓存自己的原始可见性和渲染类型。本地拥有者处于第一人称时，主、副武器的世界模型均对拥有者隐藏；第一人称显示枪只对应主武器。切枪后原主武器作为副武器继续隐藏。

装备、所有权变化、复制通知、视角和控制权变化都会刷新显示状态，现有更新流程做一致性检查。丢弃、死亡和退出时恢复原设置，弱引用不延长武器生命周期。复制消息先后到达时，已经丢弃、失去所有权或销毁的武器不再作为第一人称显示枪。

显示标记只在本机修改，不增加网络 RPC；其他玩家仍看见完整身体和两把世界武器。AK 握持、瞄准和动作时间配置沿用上一版。

## 验证

`validate_weapon_visibility.py` 在原 `L_StingerTest` 上分别运行单人和双人 PIE。先装备两把枪，再依次覆盖极限低头、蹲伏、瞄准、连续切枪、动作中切视角、丢弃与重新拾取、失去控制与重新控制、死亡重生、装备武器销毁。

以 30 Hz 固定模拟步长逐帧检查主、副武器的所属角色、武器状态、拥有者可见性、渲染类型和第一人称显示枪数量。单人使用 NullRHI 检查状态；双人运行包含实际渲染及主机和客户端的本地/远端检查。每轮 840 个连续模拟帧、27 项检查全部通过，没有漏采模拟帧或可见性违规。

关键低头和切枪片段按每个模拟帧保存，共 240 帧，合成为 1280×720、30 fps、8 秒视频；这是固定步长的逐帧回放，不是实际运行帧率测试。已通过联系表复核全部帧，并检查关键原尺寸画面。生命周期截图取自各阶段末尾，避免把拾取前的空手画面当作拾取结果。

此验证为同进程 PIE，未覆盖独立进程、互联网延迟和丢包。动作测试期间临时关闭角色受伤，防止另一个测试玩家的开火提前打断流程；到死亡测试时重新开启受伤。该设置只在测试脚本中使用，不修改角色资产或游戏默认值。

另有独立的画面问题：`repossess.png` 中重新接管后的手臂和显示枪位于画面上方。该阶段可见性数量正确，但不能据此宣称动作或摄像机适配正确；原因尚未诊断，本次保留原适配配置。高分辨率截图中的 HUD 准星偏移也不属于本次验收范围。

结果见 [WeaponVisibilitySummary.json](WeaponVisibilitySummary.json)、`WeaponVisibility1p.json`、`WeaponVisibility2p.json`、`WeaponVisibility2pStills.json` 和 `WeaponVisibilityVisualReview.json`。编译结果见 `WeaponVisibilityBuild.txt`。

- [客户端双武器低头及切枪视频](WeaponVisibilityCapture/dual_weapon_client.mp4)
- [低头画面](WeaponVisibilityCapture/dual_down.png)
- [第三人称画面](WeaponVisibilityCapture/third_person.png)
- [重新拾取后](WeaponVisibilityCapture/re_pickup.png)

## 复测

关闭当前项目的编辑器后，在项目目录运行下面的 PowerShell 命令。脚本使用独立编辑器实例，结束后自动关闭该实例；不通过 RiderLink 同步导入资源。

```powershell
$env:STINGER_VISIBILITY_PLAYERS = '2'
$env:STINGER_VISIBILITY_CAPTURE = '1'
& 'C:/UnrealProjects/UnrealEngine-5.8.3-release/Engine/Binaries/Win64/UnrealEditor.exe' `
  'D:/SourceFiles/UnrealProjects/BrokenEdge/BrokenEdge.uproject' `
  /Game/Characters/Stinger/Maps/L_StingerTest -unattended -RenderOffscreen -d3d11 -nosound -NoScreenMessages `
  '-ExecCmds=py D:/SourceFiles/UnrealProjects/BrokenEdge/Scripts/Stinger/validate_weapon_visibility.py,t.MaxFPS 60'
```

`PLAYERS=1`、`CAPTURE=0` 加 `-NullRHI` 可复测单人状态；`PLAYERS=2`、`CAPTURE=stills` 补录动作完成后的截图，单独输出 `weapon_visibility_2p_stills.json`。移除这两个环境变量后再启动日常编辑器。

完成目视检查并记录 `Saved/Stinger/weapon_visibility_visual_review.json` 后，通过 `record_weapon_visibility_reports.py --ffmpeg <ffmpeg.exe路径>` 归档。归档脚本校验报告、人工复核对应的帧目录及视频帧数，不自动生成目视通过结论。
