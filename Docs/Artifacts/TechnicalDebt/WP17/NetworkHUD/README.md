# 双打包客户端撤离反馈与 HUD

2026-11-14，UE 5.8.4 / CL 58902052；使用 [最新 Client 包](../../WP15/FinalClient/README.md)。两个独立打包客户端（A 渲染 1280×720 Medium，B NullRHI），编辑器 `-server` 专用权威，每方向 50 ms 延迟、2% 丢包。26 项检查通过，三进程均 ProcessDriver Success、finish ack、退出码 0。

覆盖未开放、开始读条、两个玩家独立截止时刻、名额耗尽取消、离开区域清空、重新开始读条、达到关闭时刻取消。私有撤离字段只到权威与拥有者，观察者保持默认；真实 HUD 文本与进度可见性逐阶段核对。[读条](td_final_packaged_hud_20261114_01/A-exit-feedback-countdown.png) 和 [名额耗尽](td_final_packaged_hud_20261114_01/A-exit-feedback-capacity.png) 截图已查看。

入口新增 `--exit-feedback-id`，本次明确选择 `OfficialExitTestrange`；默认广场选择仍保留。记录原始定义、全部运行时配置覆盖及位置夹具，正式 HUD、复制与资格判断代码未改。测试覆盖时刻和容量反馈，显式关闭背包资格并延长读条，不计作真实开放时刻、丢包撤条件、广场电闸交互、路线或成功结算验收。

完整命令、二进制/脚本指纹与各进程状态在 [报告摘要](td_final_packaged_hud_20261114_01/ReportSummary.json)，全部断言时状态无损保存在同目录 `FullReport.json.gz`，摘要提供原报告 SHA-256。另有 [4 个非法 CLI 组合](CLI-Checks.json) 正确拒绝且没有生成运行目录。此项不代替重连、完整观战或真实流送子关卡的剩余验收。
