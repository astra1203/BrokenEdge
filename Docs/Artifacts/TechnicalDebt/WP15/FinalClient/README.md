# 最新 Client 包与双客户端验收

2026-11-14，UE 5.8.4 / CL 58902052。构建基线 `bd75ea4e0` 加本批 ClientContext 测试标记；完整工作区、命令及源码 SHA-256 见 [Build.json](Build.json)。包含本次生命恢复、救援请求序号、Teams 环境伤害、旧模式换弹/切枪校验以及仓库布局修复。此前 `Packaged` 目录中的证据仍保留其原始较早基线。

- 新目录 `Packaged/TechnicalDebt-20261114-Final/Client/Windows` 完成构建、Cook、IoStore 和 Stage；BuildCookRun 213.32 秒。实际 Cook 总数 19,739，其中 19,681 Cook、58 因平台跳过、0 增量跳过。
- [实际 Cook 清单](CookCoverage.json) 的 381 个必需包及 [其他 18 包](OtherLiteralCook.json) 全部存在，后者含 17 个其他原生引用与训练场地图。包内可执行文件和容器指纹见 [PackageFiles.json](PackageFiles.json)。
- 独立 Development Game 的 [资产测试 1 项](assets/Automation.json) 通过：367 个物品定义、当前角色表现配置、输入、7 个携带物品图标、HUD/背包与 Revision 不变。[实际截图](assets/Inventory.png) 已查看。
- 同一包的 [CombatMode 4 项](combat/Automation.json) 全部通过，覆盖模式派发、预测节奏隔离、旧模式生命周期及服务端请求校验。测试在包内临时权威 World 执行，不代表远端旧模式武器矩阵。
- [双打包客户端大厅流程](HybridLobby/ReportSummary.json) 37 项通过：一个渲染客户端（1280×720、Medium）及一个 NullRHI 客户端，编辑器 `-server` 提供专用权威。三进程 ProcessDriver 都 Success、收到 finish ack、退出码 0。包括队伍/准备复制、真实鼠标仓库往返拖放、预览、入局、地图/背包关闭输入、正常撤离读条、持久化结算及返回；原携入 AK 的 ID 保持且仅返还一次。

双客户端流程明确记录撤离时间、背包条件、容量和传送位置夹具，保留正式读条与结算逻辑；不是路线、开放时刻、丢包撤规则或容量验收。完整状态快照无损压缩在 [FullReport.json.gz](HybridLobby/FullReport.json.gz)，摘要含原报告 SHA-256；[仓库截图](HybridLobby/A-stash.png) 已查看。

入口新增 `run_packaged_assets.py --suite combat` 和 `run_network.py --packaged --editor-server`。后者明确记录混合运行身份，不将编辑器服务器计为打包 Server Target。共用完成判定拒绝成功/失败混合、重复成功、缺失完成、Fatal 和 Python traceback；7 项 Python 测试通过（含 30 个无效引擎 CLI 用例），见 [测试记录](CommonTests.json)。网络流程现在记录失败清理时每个自有进程的退出码。

安装版引擎不支持 Server Target 的限制仍然存在。完整任务/战斗/输入双端矩阵、美术子 Widget 拆分与生产生成器重建基线未完成；M4 右手握点 9.22 cm 的旧诊断仍未验收。本次未修改内容资产。
