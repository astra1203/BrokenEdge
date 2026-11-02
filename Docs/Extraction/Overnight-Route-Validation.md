# 航天基地连续路线验证

对应 [2026-11-03 夜间实施记录](Overnight-2026-11-03.md)。本文件说明复验方式和通过标准；实际通过与失败以该记录及关联证据为准。

## 验证流程

正常大厅配装 → `OfficialWestDorm` 原出生组 → 补给、医疗、贵重物资三个箱组 → 自然遭遇宿舍守卫 → 沿原场景步行和普通跳跃到广场 → 等待本局第 900 秒开放 → 连续停留 10 秒 → 回仓 → 出售本局取得的 Tools → 使用原枪再次配装并进入新一局。

本轮固定复现种子 `17848`，实际箱点为 `7F_WestDorm_A3`、`7F_WestDorm_B2`、`7F_LivingArea_B2`。第二局只验证准备和出生，不计第二次完整撤离。广场 `900/1020/10` 是项目现有兼容规则；未实现官方拉闸机制。

## 本机独立包复验

以下命令使用本机已构建的 `Packaged/Overnight20261103_05`。该目录不随 Git 发布；其原生客户端与服务器来源、Cook 复用边界见 [HUD 暂存证据](OvernightEvidence/HUD-ExitStatus-Stage.json) 和 [弹道诊断暂存证据](OvernightEvidence/FlightDiagnostics-Stage.json)。更改生产 C++ 或资源后，应先重新构建对应包，不能用旧 EXE 证明新代码通过。

在仓库根目录 PowerShell 执行，每轮使用新的 tag 和空闲端口：

```powershell
$env:UE_ENGINE_ROOT = 'C:\UnrealProjects\UnrealEngine-5.8.3-release'
$routePython = Join-Path $env:UE_ENGINE_ROOT 'Engine\Binaries\ThirdParty\Python3\Win64\python.exe'
$routeTag = 'manual-route-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
& $routePython -B Scripts/Extraction/resource_guard.py --tag $routeTag --start-d-gib 32 --stop-d-gib 25 --stop-ram-gib 2 --critical-ram-gib 1 -- $routePython -B Scripts/Extraction/run_network.py --packaged --package-root Packaged/Overnight20261103_05 --scenario overnight-route --players 2 --rendered-role A --bounded-rendering --resolution 1280x720 --isolated-room --flight-diagnostics --loot-seed 17848 --port 27910 --tag $routeTag
```

32/25 GiB 为这组已暂存包的 D 盘启动/停止阈值，不是通用打包空间预算。保护器默认阈值保持原值。一次只运行一组 UE 验证；本轮 A 使用受限中等画质渲染，B 使用 NullRHI，不能据此评价默认画质性能。

输出在 `Saved/Extraction/<tag>/`，资源保护报告在同名 `-resources` 目录。重点查看 `Report.json`、`OvernightMotion.jsonl`、三个进程日志和截图。查看 JSON 时显式使用 UTF-8。

## 必须同时满足的通过条件

- 大厅使用正常配装与 Ready 流程，服务器从指定的正式出生组创建角色。指定组仅由开发测试参数启用；普通游戏仍随机选组。
- 三类搜刮物品记录原始 GUID 与数量，服务器所有玩家、仓库、装备、箱体和守卫容器中只存在一个归属。
- 守卫通过实际感知产生交战，右键开镜和正常射击产生真实伤害、死亡与击杀记分。保留弹药、换弹及治疗规则。
- 路段由 Enhanced Input 完成，保留原始路径索引、45 cm 行走阈值、65 cm 服务器端点阈值及落地检查。仅实测桥面允许有上限的普通 Jump，起跳、落地、前进和必要回走均记录。
- 广场在第 900 秒前保持关闭，原生产 10 秒停留截止时间在同一服务器快照中检查。停留期间不发可能阻塞采样的同步截图命令。名义轮询为 0.15 秒，包含快照 I/O 和调度延迟，不宣称逐帧连续观测。
- 回仓后原枪和战利品各保留一次；Tools 的正常出售删除完整物品身份并精确增加服务器报价对应的货币；新一局不恢复已售物品，双方取得新的共享 raid ID。
- 场景报告通过后，Server/A/B 的自动化 Success、正常退出和 finish 回执也必须通过。场景局部检查、离线测试或单张截图不能替代整轮结果。

## 范围限制

`overnight_route.json` 包含多种候选组合；本轮固定种子的成功不能证明全部 82 个 pair 均能实走。当前正式 LootLayout 为 45 个候选，历史规划为 54 个，详见 [箱体资产审计](OvernightEvidence/ContainerBounds-AssetAudit.json)。离线 [胶囊路径几何审计](OvernightEvidence/ContainerRoute-Geometry.json) 使用资产推导 OBB 和理想折线，不是 Unreal 运行时 Sweep。

本场景不传送途中角色、不注入物品、不修改血量或 AI 参数、不跳过 900 秒时钟。独立守卫与瞄准矩阵测试使用明确记录的摆位 fixture，它们与本连续路线的证据分开。

路线再生成需要原始测量探针；已提交 JSON 的执行不需要重新生成。`flight-matrix` 则依赖本机保留的历史失败快照，用于本次诊断，不能当作新克隆仓库即可运行的通用测试。
