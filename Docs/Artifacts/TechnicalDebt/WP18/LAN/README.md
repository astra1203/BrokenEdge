# 独立 LAN 会话实跑

2026-11-14；UE 5.8.4 / CL 58902052。执行 `Scripts/Test-LanSessions.ps1 -EngineRoot <UE> -LanGameUniqueId 2026111401`，默认 FreeForAll、Teams 两种模式均通过。

共 9 个独立 UE 进程：无房间搜索 1 个；每模式一个实际编辑器专服、另一个模式的房间广告、两个真实客户端。7 个测试进程各报告 1 项 Automation 成功并正常退出 0；2 个广告进程由脚本明确终止，报告标记 `intentional_stop=true`、退出 -1。进程号、完整命令、引擎版本、源码 SHA 和实际退出码均保留。

本批补齐广告进程及失败清理分支的退出码记录，清理仅限脚本自己启动的进程。没有修改系统执行策略，仅当前 PowerShell 进程使用 Bypass。此次为 NullRHI 功能验证，覆盖房间浏览、不同模式并列、加入、取消/准备和实际切图；不代表画面或打包专服验收。前一阶段的失败判定检查见 [PowerShell 检查](../Phase2/PowerShellFinalChecks.json)。
