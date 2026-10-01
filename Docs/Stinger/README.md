# 蜂医可控角色

**本文记录存在动画适配错误的初版。当前修复入口、原因与未解决项见 [Repair.md](Repair.md)。旧的“检查通过”仅代表功能检查，不代表动画画面合格。**

在内容浏览器打开 `/Game/Characters/Stinger/Maps/L_StingerTest`，运行 PIE。地图使用独立的 `BP_StingerTestGameMode`，玩家角色为 `/Game/Characters/Stinger/Blueprints/BP_StingerCharacter`，直接继承项目 C++ 类 `ABrokenEdgeCharacter`。项目原有默认角色和默认地图没有替换。

| 操作 | 按键 |
| --- | --- |
| 移动 / 跳跃 | WASD / Space |
| 向前冲刺 / 蹲下 | Left Shift / Left Ctrl |
| 拾取、装备武器 | E |
| 瞄准 / 射击 / 换弹 | 鼠标右键 / 左键 / R |
| 切换第一、第三人称 | V |
| 切换武器 | G |

第一人称使用从完整人物中裁出的手臂网格、蜂医第一人称动画和本地武器显示组件；第三人称使用完整人物、重定向后的项目动画蓝图及原有战斗蒙太奇。第一人称蒙太奇只负责显示，装弹和切换武器仍由第三人称动画通知结算。行走、冲刺、蹲行、瞄准速度分别为 600、850、300、450 cm/s，第一人称视野为 90°，瞄准为 65°。

瞄准时暂时隐藏第一人称手臂，仅显示武器，避免未经完整姿势重定向的前臂遮挡瞄具；退出瞄准后恢复手臂显示。这是当前视觉适配的明确限制。

## 导入结果

源目录：`D:\司晨\源代码和素材\蜂医的人物模型和相关动画资产`。完整人物和展示人物从现成 GLB 导入。443 个完整 PSA 文件导入为动画资产，分组为角色动画 100、技能动画 157、通用男性动画 186；未完成的 `.downloading` 文件未导入。所有生成资产位于 `Content/Characters/Stinger`，当前专用手臂源文件位于 `RawContent/Stinger/Stinger_1P_Forearms.glb`，共 3,042 个三角面；先前的完整手臂版本保留供比较。

96 个 PSA 包含当前网格不存在的骨骼；对应轨道被跳过，清单记录了骨骼名称。导入帧率采用可精确表示的有理数，最大相对帧率偏差约 0.4444%；每个序列的源帧率、导入帧率和时长见 [ImportManifest.json](ImportManifest.json)，统计见 [ImportSummary.json](ImportSummary.json)。技能动画导入不代表治疗、烟雾、复活等技能已经实现。

## 验证与复现

[AssetValidation.json](AssetValidation.json) 检查蓝图父类、网格、动画引用、输入映射和全部 PSA 导入资产。[PIEValidation.json](PIEValidation.json) 保存单人及同进程双人监听服务器的真实运行快照，包括移动、冲刺、蹲下、跳跃、装备、瞄准、开火、无自身伤害、换弹、视角切换和远端完整人物。[LevelValidation.json](LevelValidation.json) 记录测试场景。

自动化脚本位于 `Scripts/Stinger`。使用独立 UE 进程执行，避免通过 RiderLink 游戏线程 AsyncTask 同步提交 Interchange 导入。`setup_commandlet.py` 接收环境变量 `STINGER_SETUP_STAGE=retarget/configure/level`；`validate_assets.py` 用 PythonScript commandlet 执行；`validate_pie.py` 用编辑器 `-ExecCmds="py <脚本绝对路径>,t.MaxFPS 60"` 执行。设置 `STINGER_VISUAL_CAPTURE=1` 可在使用图形 RHI 时输出第一人称、瞄准、换弹和第三人称截图到 `Saved/Stinger`。`record_reports.py` 使用普通 Python 归档验证结果。

编辑器 Development 构建成功，记录见 [BuildValidation.txt](BuildValidation.txt)。单人及同进程双人 PIE 共 25 项检查通过；资产重新加载检查通过。图形模式的单人回归与截图检查另见 [VisualValidation.json](VisualValidation.json)。本次使用 UE 5.8.3 独立进程验证，原来停在 `TaskGraph.cpp` 断言处的调试进程保持暂停；重新打开项目后加载新模块和资产。

## 当前范围

这是可运行的角色与基础射击接入，尚未达到《三角洲行动》的完整动作表现。未实现治疗、烟雾、救援、趴下、侧身、滑铲和翻越；技能与通用动画作为资产保留。第一人称瞄准和跳跃使用稳定持枪基础姿势，瞄准另外平滑移动手臂、缩小视野；原始瞄准、跳跃动画仍保留供后续修正。第一人称握枪针对项目 AK47 校准，其他武器需要单独检查；动作切换、瞄具细调、后坐力表现和命中盒精修仍可继续完善。双人验证不涵盖高延迟网络或独立服务器打包运行。

当前项目启用的 GameFeatures 工具插件存在缺少 `GameFeatureData` 扫描规则的启动报错，可能使成功执行 Python 的 commandlet 仍返回非零退出码；应同时检查脚本成功标记和验证 JSON。本次没有改动该既有配置。
