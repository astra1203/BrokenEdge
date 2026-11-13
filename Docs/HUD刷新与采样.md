# HUD 刷新与采样

WP-17 在 `ExtractionWidget::Refresh` 加入 CPU trace scope、cycle stat 和可选计数器。正常游戏不保留逐帧测量数组；自动化仅在固定的 60 秒区间采集耗时及文本写入次数。

## 刷新契约

- 撤离点在 BeginPlay 注册到当前 World 的 `UExtractionExitRegistry`，EndPlay 注销。注册表持有弱引用，HUD 文本和罗盘复用；权威撤离规则保持原入口。流送 Actor 的生命周期也走这两个入口。
- 所有 HUD 文本写入先比较 `FText::EqualTo`，文本不变时跳过 `SetText`。相机方向、距离、倒计时、撤离进度和命中反馈仍逐帧计算。
- 库存 `OnContentsChanged` 在权威提交完成和拥有者 RepNotify 后广播。拒绝或回滚的候选事务不触发此事件。HUD 收到事件后使生命/装备/武器显示缓存失效。
- 生命变化订阅 `OnVitalsChanged`。该事件目前只在权威端应用生命增量时广播，因此同时比较生命、弹药、创伤、近战、武器身份与库存版本，覆盖客户端复制及本地弹药预测。
- 任务文本以档案 ID、Revision 和本局守卫击杀数为缓存键；任务提交必须推进档案版本。固定按键提示以生命阶段为键，能力冷却和交互提示继续实时追加。
- Pawn/库存对象切换时解绑旧事件、绑定新事件；NativeDestruct 解绑并清缓存。失去 Pawn 时清空武器、生命显示，避免留着上一角色的值。

## 可复现采样

```powershell
python Scripts/Extraction/run_editor_task.py hud-profile --engine <UE根目录> --tag <唯一标签>
python Scripts/Extraction/export_hud_trace.py Saved/Extraction/<唯一标签> --engine <UE根目录>
```

场景为真实渲染的 WeaponRange 单玩家 PIE，六个动态启用的撤离点、持续旋转视角、60 FPS 上限，预热 5 秒后采集 60 秒。它用于相同条件下比较刷新实现，不能代替 SpaceCenter 全地图、双客户端或 GPU 性能验收。

`HUDProfile.json` 只计算 60 秒区间；`HUD.utrace` 保存 CPU/frame/bookmark/counters，Insights 导出 `HUDTimingEvents.csv` 包含预热及后续断言期间的 Refresh，行数会更多。原始 trace 留在 Saved，归档保留其 SHA-256、大小、导出命令和 CSV。该导出入口使用无界面 Insights，不经过 shell；当前要求导出目录不含空白字符。

采样后的同一 PIE 检查动态撤离点增删、死亡/恢复显示、生命与库存提交、失去/重新占有 Pawn、Widget 移除重挂、任务版本变化。完整断线重连、远端观战切换和真实子关卡流送尚待验收。实际数据与执行结果见 [验证索引](验证索引.md)。
