# 动画导入修复记录

这是 `6e4b0b7` 导入修复阶段的历史记录。问题现场提交为 `9b0525f`；当时使用的 `fix/stinger-animation-import` 已合入并移除，当前工作统一在 `ue5.8-20260930`。后续第一人称 AK 适配、原测试入口更新及验收见 [ViewFit.md](ViewFit.md)。旧导入资产和旧验证报告保留，不能把旧报告解释为动画画面验收通过。

## 已确认的原因

1. 旧导入器对所有骨骼使用了根骨骼的旋转处理，非根骨骼旋转被取逆。修正版按导出器的实际规则恢复：位置反转 Y；旋转反转 Y，只有根骨骼另外反转 W。依据：[CUE4Parse ActorXAnim 导出源码](https://github.com/FabianFG/CUE4Parse/blob/master/CUE4Parse-Conversion/Writers/ActorX/ActorXAnim.cs)。
2. 旧实现仅按名称写入目标骨架，并且用骨骼数量选择面部骨架。现在按完整参考记录建立源骨架，保存全部轨道；根据不区分大小写的骨骼名称和父子关系选择预览骨架，并把运动相对源参考姿势的变化映射到目标参考姿势。
3. 若源轨道存在零或负缩放，仅保留源资产并标记需要叠加动画或隐藏骨骼元数据。不直接把缩放改成 1，也不把它作为普通动画接入角色。PSA 本身缺少可靠的原始叠加基准声明。
4. 项目 AK 的骨架没有 `Hand_R` 和 `LeftHandSocket`。读取不存在的插槽会回落到组件原点，旧第一人称 IK 因此拉错位置。修正版使用独立武器显示网格，增加实际存在的握枪插槽；没有有效插槽时禁用该 IK。

## 修正版入口

- 地图：`/Game/Characters/Stinger/Maps/L_StingerRepairTest`。
- 角色：`/Game/Characters/Stinger/Blueprints/BP_StingerCharacter_Repaired`，直接继承 `ABrokenEdgeCharacter`。
- 第一人称动画蓝图：`/Game/Characters/Stinger/Animations/Corrected/ABP_StingerFirstPerson_Repaired`。
- `Corrected/Source` 保存完整源骨架动画；`Corrected/MappedV4` 保存可以匹配模型、且没有异常缩放的预览动画。

第一人称待机、瞄准、放松持枪、开火、完整换弹和取枪使用修正版导入动作。空手时隐藏持枪手臂；持枪及瞄准时均显示手臂。基础姿势切换混合时间为 0.15 秒。动画实例公开 `CurrentVisualState`、`CurrentAnimationName` 和 `bMissingStateAnimation`，可以检查运行状态和实际资产来源。

导入修复阶段为避免当时编辑器的文件锁，采用独立角色、网格、动画蓝图和地图。该阶段未替换原入口；后续适配已使用独立进程检查，并通过专用脚本更新原测试角色和地图。

## 仍未解决的素材缺口

- 源目录的 186 个通用步枪动作都是第一人称动作。未找到普通第三人称步枪行走、蹲行、开火和换弹；已有第三人称片段主要属于角色展示、医疗枪、烟雾等专用动作。
- 修复测试角色目前仍继承初版的第三人称动画配置，用于保持游戏结算可运行；这是未完成项，不能称为蜂医第三人称动作替换完成。需要补充对应素材，或明确选择保留项目原动作作为临时方案后，再接入新状态机和蒙太奇通知。
- 第一人称跳跃循环包含零缩放轨道，未接入。空中维持基础持枪姿势，并公开缺失状态标记；这不是已修复的跳跃动画。
- 道具或特殊骨架缺少对应网格时保留源动画，不能宣称已经在蜂医身体上验证。
- **此历史阶段第一人称画面未通过验收。** 当时的瞄准截图可见裁切手臂开口，枪托与瞄具需要重新适配。后续封口手臂、握持、弹匣和动作同步修复，以及本轮验证范围，见 `ViewFit.md`；不能据此宣称全部动画或三角洲行动复刻已经完成。

## 验证方法

`test_psa_rotation.py` 验证导出/导入旋转往返及无效四元数拒绝。`repair_pose_check.py` 在五个时间点检查已映射动画的关键骨骼数值；数值通过与画面通过分开记录。

自动 PIE 设置 `STINGER_ISOLATE_INPUT=1`，清除真实键鼠映射并通过 Enhanced Input 注入输入；设置 `STINGER_CHARACTER_NAME=BP_StingerCharacter_Repaired_C`。图形验证使用 `-RenderOffscreen`，减少对当前编辑器操作的干扰。功能检查通过不等于动画画面通过。完整验证结果和未解决项由修复清单分别记录。

## 本次批量结果

443 个文件保存成功，403 个通过导入键值有效性检查，40 个因零或负缩放标记为需要补充元数据。307 个建立目标骨架映射，并通过五个时间点的关键骨骼数值检查；另有 96 个数值有效的动画仅保留源骨架，未接入蜂医模型。数值检查不证明全部骨骼、蒙太奇过渡或画面已经合格。

旋转往返测试 2 项通过，Editor C++ 编译通过。单人图形功能检查 14 项通过；双人功能检查另行记录在 `RepairPIEValidation.json`。`RepairVisualValidation.json` 内的 passed 字段仅代表自动功能检查；人工画面验收以 `RepairSummary.json` 的 `visual_acceptance` 为准，当前为 `not_completed`。截图见 `RepairScreenshots`。

重新生成报告使用 `Scripts/Stinger/record_repair_reports.py`，不会覆盖初版证据。源导入使用 `repair_import.py`，不要继续使用旧 `import_assets.py` 的动画转换入口。修复资产使用独立目录保留比较版本。修改后的 C++ 需要重新编译并重启编辑器加载；本次没有重启用户当前编辑器。
