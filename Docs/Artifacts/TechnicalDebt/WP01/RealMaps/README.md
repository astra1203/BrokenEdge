# WP-01 / WP-18 旧模式真实地图重生

基线 `e358632bd` 加本批代码；UE 5.8.4 / CL 58902052。

## 变更与结果

- `ExtractionPIELobbyTests` 的 FreeForAll / Teams 分支从大厅双客户端真实按钮准备、会话保留和无缝切图，扩展至环境伤害死亡、正常三秒计时重生、拥有者重新接管和观察者复制。
- 检查旧 Pawn 销毁、PlayerState 保留、角色恢复蓝图配置的初始 HP/Shield、重新允许玩法、位于实际 PlayerStart 附近、会话仍存在。使用 NullRHI；这是功能验证，不包含视觉验收，也没有手工操作冒充证据。
- `TeamsGameMode::CalculateDamage` 原先直接解引用 Attacker/Victim。现在缺失控制器按原有“无法取得队伍信息”规则保留基础伤害，避免环境伤害崩溃；Teams 实际死亡重生路径覆盖空攻击者，另断言空受害者的计算结果。
- `run_pie_lobby.py` 对旧模式额外要求重生成功标记，不能仅凭切图通过结束。`run_build.py` 补 running/passed/failed 和结束时间，源文件指纹扩展至编辑器模块及构建规则。

最终构建 `td_wp01b_build_20261114_03` 通过（35.13 s）；FreeForAll、Teams 两次真实网络 PIE 任务及空出生点边界测试均通过。两模式均为同进程专服 + 两客户端；未声称独立进程或打包结果。

## 失败诊断

先运行旧脚本的大厅切图基线，两模式通过，记录为 `td_wp18c_lobby_*_01`。新增重生检查的 FreeForAll 第一次等待满血 100 超时；第二次临时诊断确认三秒回调正常，服务器和两个客户端新 Pawn 均为 80/100。该蓝图的初始 HP 为 80，测试应检查类默认配置而非上限。修正夹具后复跑通过；角色数值和重生生产流程未改变。生产角色中的临时诊断已移除，测试侧保留等待状态日志。

本目录保留各次 Run.json、Automation.json 和 Events.txt；最终源码见 SourceSHA256.json。D 盘缓存与独立运行目录在报告内记录，用户的 BrokenEdge.uproject 改动不在提交中。
