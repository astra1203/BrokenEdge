# 阶段 4：撤离与基础仓库

状态：2026-10-03 原版限定范围验收通过；2026-10-04 先修复 PIE 退出崩溃，随后补齐 PIE 大厅往返与遗留出战占用修复。最新版完成 Editor 构建、Development 服务端／客户端打包、真实 PIE 连续两局、旧模式切图、四次启停、持久化规则与原存档恢复，以及五组打包联机回归。各版本证据分别归档；联机范围为同一主机专服与 2～4 个独立客户端，不代表公网服务或更大容量已通过。

实施起点：`7e4afbc11000aa48248f86ef52cd7e3e0ee80225`。引擎：`C:/UnrealProjects/UnrealEngine-5.8.3-release`。本阶段按批准范围保留未提交工作区，不提交或推送 Git。

## 2026-10-04：PIE 大厅往返与遗留记录修复

用户复测发现，从大厅准备后仍提示“请从大厅准备后进入对局”，返回大厅又提示“出战档案无效或已在对局中”。现场日志显示大厅已成功登记出战，但 `net.AllowPIESeamlessTravel` 未启用，PIE 改为重新连接切图，丢失仅通过 `PlayerState::CopyProperties` 携带的出战 ID。存档保留一条未激活、未关闭、两名参与者均未结算的记录，因而阻止再次出战。这条真实大厅往返路径未被前一次直接地图启停测试覆盖。[修复前日志](Phase4LobbyFixEvidence/diagnostics/Phase4LobbyBackup-Before.log.gz)

项目配置现在启用 PIE 无缝切图；大厅在开始倒计时及异步开始完成后检查配置与仓库状态，设置关闭时在扣除装备前阻止开局并给出明确提示。对局激活前核对所有参与者的档案、同一出战 ID 和记录成员。`AbortRaid(ID)` 仅取消指定未激活记录，新增 `AbortPreparedRaids()` 在等待阶段失败及专服重新进入大厅时清理未激活记录，返还原带入物资并解除占用；运行中的清理不会取消已激活对局，启动加载仍使用既有崩溃恢复规则。返还失败保留已提交状态并阻止新开局，重复取消不重复返还。存档格式及客户端 RPC 未改变。

| 最新版验证 | 结果 | 报告 |
| --- | --- | --- |
| Editor／Development 专服包／客户端包 | 三项退出码均为 0 | [Editor](Phase4LobbyFixEvidence/compilation-editor/Run.json)、[Server](Phase4LobbyFixEvidence/package-server/Run.json)、[Client](Phase4LobbyFixEvidence/package-client/Run.json) |
| 真实 PIE 大厅连续两局 | 专服与两个客户端，真实准备按钮与配装 RPC；两局均携带原身份和武器进入对局、独立撤离并返回大厅，记录关闭且准备状态重置，最后完整停止 PIE | [运行记录](Phase4LobbyFixEvidence/pie-lobby/Run.json)、[自动化报告](Phase4LobbyFixEvidence/pie-lobby/Automation/index.json) |
| 禁用无缝切图 | 同一 PIE 测试先关闭设置并准备，等待超过倒计时；提示明确、未登记出战、存档序号不变、装备仍在配装中，恢复设置后正常开局 | [报告](Phase4LobbyFixEvidence/pie-lobby/Run.json) |
| 旧模式 PIE 切图 | Teams／FreeForAll 均保留专服、两个客户端和在线会话，正常结束 PIE | [Teams](Phase4LobbyFixEvidence/pie-legacy/teams/Run.json)、[FreeForAll](Phase4LobbyFixEvidence/pie-legacy/free-for-all/Run.json) |
| 原退出崩溃回归 | 监听双人和专服双客户端各启停两次，四次均确认世界清理完成 | [报告](Phase4LobbyFixEvidence/pie-teardown/Run.json) |
| 持久化规则 | 四组通过，新增定向取消、重复取消、未激活记录清理、不误取消活动记录、写盘失败保留占用；保留事务、来源恢复和损坏日志测试 | [报告](Phase4LobbyFixEvidence/persistence/Run.json) |
| 当前用户存档恢复 | 副本恢复通过后恢复原档，再次加载字节不变；未关闭记录 1 → 0，两份身份和武器 ID 保持，物品及弹匣数量守恒 | [核对记录](Phase4LobbyFixEvidence/user-recovery/verification/Report.json)、[副本](Phase4LobbyFixEvidence/user-recovery/copy/Run.json)、[原档](Phase4LobbyFixEvidence/user-recovery/original/Run.json)、[重复加载](Phase4LobbyFixEvidence/user-recovery/repeat/Run.json) |
| 普通／延迟撤离与仓库 | 42／40 项通过 | [普通](Phase4LobbyFixEvidence/network/settlement/Report.json)、[延迟](Phase4LobbyFixEvidence/network/settlement-lag/Report.json) |
| 异常断线／四人不同结果 | 17／43 项通过，活动对局结果未被清理逻辑覆盖 | [断线](Phase4LobbyFixEvidence/network/disconnect/Report.json)、[四人](Phase4LobbyFixEvidence/network/four/Report.json) |
| 成功落盘未回执时崩溃 | 38 项通过，两次重启验证不重复入库及来源扣减 | [报告](Phase4LobbyFixEvidence/network/recovery-unacked/Report.json) |

五组打包回归共 180 项检查，计数包含实际拾取与重试步骤。恢复后的两个用户档案各保有三把 AK（弹匣合计 90 发）、备用弹药 360 发、医疗包六个；这是恢复前库存加未开始出战带入物资的守恒结果，没有重新发放初始补给。恢复物资进入仓库，空间不足进入待领取区，需要重新配装。原始日志和凭据备份位于本地 `Saved/Extraction/Phase4LobbyBackup`，未放入证据归档；已有 28 份客户端凭据字节保持不变。

正常测试入口是 `Content/Maps/Lobby`，模式选择 Extraction 后配装并准备；直接打开撤离地图不会凭空创建出战记录。新增 [PIE 大厅测试](../../Source/StingerTools/Private/ExtractionPIELobbyTests.cpp)、[运行脚本](../../Scripts/Extraction/run_pie_lobby.py) 和 [存档恢复验证脚本](../../Scripts/Extraction/recover_existing_store.py) 均保留在工作区。

最新 [Summary.json](Phase4LobbyFixEvidence/Summary.json) 记录 17 组构建／验证、当前源码与新包哈希，[ArchiveIndex.json](Phase4LobbyFixEvidence/ArchiveIndex.json) 校验证据原始字节。此前 `Phase4Evidence` 的 239 个文件及 `Phase4FixEvidence` 的 78 个文件均保持不变，[校验记录](Phase4LobbyFixEvidence/diagnostics/PreviousEvidenceVerification.json) 与 [用户断点记录](Phase4LobbyFixEvidence/diagnostics/Breakpoints.json) 一并归档。Editor 首两次构建遇到新增测试的类型和模块链接问题，修正后第三次通过；失败日志保留。两种包的 Zen 首次读取均曾自动重试后成功，既有编译与资产警告未扩展修复。

## 2026-10-04：PIE 退出崩溃修复（前一次修复记录）

Rider 现场停在 `UExtractionPersistence::EnsureLoaded()`，`this == nullptr`。调用链为 `UEditorEngine::EndPlayMap → ShutdownWorldNetDriver → Logout → LeavePlayer → SettlePlayer → Settle`。UE 5.8.3 在此路径先执行世界 `EndPlay` 和 GameInstance 子系统反初始化，再关闭网络驱动；旧代码随后处理控制器退出，仍然访问已经卸载的仓库子系统。直接打开撤离地图的 PIE 又没有大厅生成的有效出战记录，因此这是退出生命周期缺陷，原版验收未覆盖这一边界。[原始调试现场](Phase4FixEvidence/diagnostics/Debugger.json)

`ExtractionGameMode` 现在从 `EndPlay` 开始标记退出。世界清理期间跳过业务结算，`Logout` 仍调用父类清理；仓库访问允许返回空，结算在捕获库存前验证服务、库存、出战 ID 和档案 ID。直接地图的无出战记录玩家不写结算；等待开局时，取消出战失败也不会清空库存或标记退出。Tick 和会话结束回调同步检查退出状态。有效对局的成功、死亡、断线、超时与持久化恢复规则继续适用，存档格式与 RPC 接口未改变。

| 修复版验证 | 结果 | 报告 |
| --- | --- | --- |
| Editor／Development 专服包／客户端包 | 三项退出码均为 0 | [Editor](Phase4FixEvidence/compilation-editor/Run.json)、[Server](Phase4FixEvidence/package-server/Run.json)、[Client](Phase4FixEvidence/package-client/Run.json) |
| 真实 PIE 连续启停 | 双人监听服务器两次、专服与两个客户端两次；每次确认 PIE 世界数归零，再等待后续编辑器 Tick，之后重启或结束测试；无有效出战记录的结算请求均被拒绝 | [运行记录](Phase4FixEvidence/pie-teardown/Run.json)、[自动化报告](Phase4FixEvidence/pie-teardown/Automation/index.json) |
| 普通／延迟撤离与仓库 | 39／43 项通过；延迟组每方向 50 ms、2% 丢包 | [普通](Phase4FixEvidence/network/settlement/Report.json)、[延迟](Phase4FixEvidence/network/settlement-lag/Report.json) |
| 异常断线 | 17 项通过，幸存者继续、掉落唯一、数量和弹匣一致 | [报告](Phase4FixEvidence/network/disconnect/Report.json) |
| 四人不同结果 | 37 项通过，成功／死亡／断线／超时结果独立 | [报告](Phase4FixEvidence/network/four/Report.json) |
| 成功已落盘但未回执时崩溃 | 38 项通过；真实终止专服后两次重启，验证身份、来源扣减与幂等恢复 | [报告](Phase4FixEvidence/network/recovery-unacked/Report.json) |

五组打包回归共 174 项检查；检查数包含运行中实际执行的拾取和重试步骤，因此与原版同名组的计数可不同。新增 [PIE 测试](../../Source/StingerTools/Private/ExtractionPIETeardownTests.cpp) 由 [运行脚本](../../Scripts/Extraction/run_pie_teardown.py) 使用独立存档和临时 PIE 设置执行，报告在世界完整清理后才判定通过。旧崩溃进程已停止，用户的 8 个异常断点及其中 5 个启用状态均保持一致。[断点核对](Phase4FixEvidence/diagnostics/BreakpointsAfter.json)

该次退出修复证据单独存于 [Phase4FixEvidence/Summary.json](Phase4FixEvidence/Summary.json) 与 [文件索引](Phase4FixEvidence/ArchiveIndex.json)。原 `Phase4Evidence` 的 239 个证据文件、Summary 与索引原始字节均保持不变，原哈希描述修复前版本；`Phase4FixEvidence` 的实现与包哈希描述大厅往返修复之前的版本。[原归档校验](Phase4FixEvidence/diagnostics/OriginalArchiveVerification.json) 该次重新执行上表范围，原版其他组的记录保留为历史证据。

PIE 用例的正式自动化报告为 1 成功、0 失败、0 警告、0 错误；启动阶段仍出现与原版相同的 13 条 `Condition failed` 日志。构建保留既有 `AMyCharacter::TakeDamage` 重载和资产警告，客户端打包的 Zen 首次连接未就绪后自动重试成功；不据此宣称完整日志无警告或已修复这些独立问题。

## 玩法与操作

| 项目 | 本阶段规则 |
| --- | --- |
| 入口 | Extraction 准备大厅 → 仓库／出战配装 → 准备 → 航天基地小区域 |
| 仓库 | 10 × 12 格；无法容纳的成功带出或恢复物资进入待领取区，不丢弃 |
| 出战装备 | 6 × 4 背包与手持、背负两个武器槽；允许空装出战 |
| 初始补给 | 新测试档案仅发放一次，共三套；每套 AK 弹匣 30 发、备用弹药 120 发、医疗包两个 |
| 配装锁定 | 准备后锁定，取消准备后解锁；开始跳图前，所有参与者的带入物资一次性扣除并记入同一出战记录 |
| 固定撤离点 | 北侧出口，中心 XY=(1400, 2900) cm，范围 5 × 5 m，地面绿色标记；全程开放，无费用与名额限制 |
| 撤离计时 | 每人独立 10 秒；离开重置，受击继续，死亡失败；对局期限优先于同刻撤离完成 |
| 成功 | 保存结果与物资后离场，可继续观战或主动退出；其他存活玩家继续本轮 |
| 失败 | 死亡、主动退出、异常断线和对局超时均损失带入及局内持有物资，不算撤离成功 |
| 回大厅 | 无活动玩家或达到对局期限后结束，保留 10 秒状态提示，再返回准备大厅 |

仓库支持拖放、R 旋转、Shift 点击快捷转移、右键装备／卸下／拆分。拆分支持输入数字、方向键和 Enter 拖放；Esc 取消当前操作或返回大厅。待领取区点击领取、滚轮翻页；只有仓库确实容纳后才扣除待领取物资。

地图已有地面武器和搜刮物资仍属于局内物资。进入新一轮不再自动补发背包弹药或医疗包，只有实际带出的物资进入仓库。原 FreeForAll／Teams 规则不依赖此仓库。

## 测试身份与存档

服务器默认存档为 `Saved/Extraction/Persistence/Store.journal`；`-ExtractionStore=<目录>` 可指定独立测试目录。客户端凭据位于 `Saved/Extraction/Clients/<标签>/<服务器存档ID>.credential`，通过 `-ExtractionProfile=<标签>` 区分本机多个客户端；PIE 按实例追加标签。同一服务器上同一档案不能同时在线。凭据丢失后不按显示名称找回旧档案，不能把新建档案当成旧仓库恢复。

客户端仅提交操作、物品 ID、版本和目标位置，服务器验证身份、准备锁、来源、数量、占格与版本。请求包含操作 ID，重复或过时请求不能重复转移。完整库存和待领取物资仅向拥有者复制；凭据哈希、出战来源记录留在服务器。

这是受控测试身份与单服务器文件存档，不包含正式账号、互联网鉴权、跨服并发、远程数据库、保险、保险箱、商店或断线重连。

## 崩溃恢复规则

带入、开局与个人结算分别有持久化边界。每个事务把相关档案、出战原始快照、已带出来源数量和结果一并追加到日志，校验并完成磁盘刷新后才更新内存与反馈成功。

| 崩溃位置 | 重启处理 |
| --- | --- |
| 带入已扣除、正式开局前 | 返还完整带入快照，标记开局取消 |
| 正式开局后，个人尚未结算 | 返还原始带入物资，扣除已被任何玩家成功带出的来源数量 |
| 个人成功结果已落盘，客户端尚未收到 | 保留成功及入库结果，不再次返还或入库 |
| 死亡／退出／断线／超时失败已落盘 | 保留已确认损失，不按未结算玩家返还 |
| 再次重启 | 已关闭的出战记录不重复恢复 |

物品数量与弹匣弹药分别记录来源批次。拆分、合并、换弹和开火维护来源数量；因此 A 带走 B 的枪后成功撤离，B 在服务器崩溃后不能再获得该枪。B 的弹药进入 A 的混合堆叠或枪内弹匣并被带出，也从 B 的返还量中扣除。未带出的消耗物仍按带入快照恢复。部分原始堆叠的 ID 已被带出时，返还部分使用新 ID。

日志记录包含版本、长度、序号和 CRC。未完成的尾部记录回退到最后完整事务；完整记录损坏、未知版本或序号异常停止加载，不静默清空仓库。磁盘写入失败停止依赖存档的操作，并冻结对局以等待维护后重启。本阶段采用完整状态追加日志，单记录上限 64 MiB，未提供长期日志压缩、备份服务或生产运维能力。

## 重复验证

标签必须唯一，构建、打包、联机测试依次执行，避免共用构建锁、端口及游戏进程。

```powershell
$uePython = 'C:/UnrealProjects/UnrealEngine-5.8.3-release/Engine/Binaries/ThirdParty/Python3/Win64/python.exe'
& $uePython Scripts/Extraction/run_editor_task.py exit --tag ExitMaterialCheck
& $uePython Scripts/Extraction/run_build.py --target Editor --jobs 8 --tag StorageEditorCheck
& $uePython Scripts/Extraction/run_editor_task.py persistence --tag StorageRulesCheck
& $uePython Scripts/Extraction/run_storage_restarts.py --tag StorageRestartCheck
& $uePython Scripts/Extraction/run_build.py --target Server --package --jobs 8 --tag StorageServerCheck
& $uePython Scripts/Extraction/run_build.py --target Client --package --jobs 8 --tag StorageClientCheck
& $uePython Scripts/Extraction/run_network.py --scenario settlement --packaged --tag SettlementCheck
& $uePython Scripts/Extraction/run_network.py --scenario settlement --packaged --lag --tag SettlementLagCheck
& $uePython Scripts/Extraction/run_network.py --scenario recovery --packaged --unacked --tag RecoveryCheck
& $uePython Scripts/Extraction/run_network.py --scenario four --packaged --tag FourPlayerCheck
```

Development 测试驱动使用实际 Slate 输入、Enhanced Input 和正式 RPC；服务端夹具安排位置、生命、满仓状态及期限边界。`--unacked` 仅供恢复场景，在服务器已经持久化成功、尚未更新角色或客户端结果时挂起，由测试脚本终止该自有进程。Shipping 不编译此挂起点或测试命令。重启报告保留原进程 PID、命令、预期强制终止和新进程记录；不归档明文凭据。

## 诊断记录

首个撤离用例未启动计时。Rider 原生调试器连接了自有专服进程，项目断点返回“没有可执行代码与此行相关联”；手动暂停取得引擎栈与帧值，但未取得项目判定帧。因此使用 Development 驱动的只读状态记录继续确认，未把引擎暂停位置作为业务原因。

原射线从 Z=1500 向下命中 `StaticMeshActor_910` 的 Z=1051.5955，区域中心因此为 Z=1054.5955；实际角色中心 Z=456.1500，相差 -598.4455，超出 `Contains` 的 [-15, 250] 高度区间。将射线起点改为可通行空间内的 Z=650 后，撤离进入／离开和结算检查通过。标识网格与文字同时改用独立根组件，避免薄网格缩放压低文字。调试器会话与代理断点已清理，保留原有八个用户异常断点、其中五个启用。

恢复测试初轮在拾取处停止。只读射线记录确认，玩家距新掉落枪约 102 cm，但射线先命中原有的 `ExtractionLootActor_4`。阶段 3 用例先捡走出生点武器；阶段 4 从仓库带枪，因此原地面武器仍在。将测试掉落点移至 XY=(1900, 4600)，并等待位置及库存同步后，拾取和来源转移通过；未放宽正式交互的遮挡判定。

## 最终验收

所有下表记录均来自本轮工作区。检查数包含初始化、同步和散布重试，不等于相互独立的功能数量。前三项构建完成后未再修改实现或测试驱动，最终打包用例使用同一组产物。

| 验证 | 结果与范围 | 报告 |
| --- | --- | --- |
| Editor／专服包／客户端包 | 三项 exit code 均为 0，UE 5.8.3、Win64 Development | [Editor](Phase4Evidence/compilation-editor/Run.json)、[Server](Phase4Evidence/package-server/Run.json)、[Client](Phase4Evidence/package-client/Run.json) |
| 持久化规则 | 三组通过：事务与重复请求、来源数量恢复、损坏日志及写入失败 | [报告](Phase4Evidence/persistence-rules/Run.json) |
| 原物品规则 | 三组通过 | [报告](Phase4Evidence/inventory-rules/Run.json) |
| 四个崩溃边界 | 扣除后未开局、已开局、成功已保存、死亡已保存；每个边界终止一次进程，再用两个新进程验证，共 12 个进程 | [报告](Phase4Evidence/storage-restarts/Report.json) |
| 普通／延迟撤离入库 | 39／40 项；进入、离开重置、受击继续、独立成功、满仓待领取、实际仓库 UI、准备锁与取消解锁 | [普通](Phase4Evidence/network/settlement/Report.json)、[延迟](Phase4Evidence/network/settlement-lag/Report.json) |
| 成功落盘未回执时崩溃 | 38 项；正式专服强制终止，两次启动新专服／客户端，验证跨玩家枪械、混合弹药、弹匣来源和消耗医疗包的恢复规则 | [报告](Phase4Evidence/network/recovery-unacked/Report.json) |
| 四人不同结果 | 40 项；A 成功、B 阵亡、C 异常断线、D 超时；期限与撤离完成同刻时按超时结算 | [报告](Phase4Evidence/network/four/Report.json) |
| 重复在线身份 | 9 项；同一凭据第二客户端不能绑定、准备或操作仓库，也不重新领取初始补给 | [报告](Phase4Evidence/network/identity/Report.json) |
| 普通／延迟搜刮回归 | 92／93 项；保留阶段 3 物品、搜索、医疗和死亡物资路径 | [普通](Phase4Evidence/network/inventory/Report.json)、[延迟](Phase4Evidence/network/inventory-lag/Report.json) |
| 延迟连续切枪 | 23 项 | [报告](Phase4Evidence/network/swap-lag/Report.json) |
| 普通／延迟战斗 | 65／69 项；扣弹、预测纠正、伤害、死亡不重生和拒绝中途加入 | [普通](Phase4Evidence/network/combat/Report.json)、[延迟](Phase4Evidence/network/combat-lag/Report.json) |
| 三人观战 | 102 项 | [报告](Phase4Evidence/network/spectator/Report.json) |
| 对局期限与再次开局 | 28 项 | [报告](Phase4Evidence/network/timeout/Report.json) |
| 加载超时 | 4 项；实际等待 120 秒，缺少第三参与者时冻结操作并返回大厅 | [报告](Phase4Evidence/network/load-timeout/Report.json) |
| 异常断线 | 17 项；幸存者继续，完整持有物只转移到一个容器 | [报告](Phase4Evidence/network/disconnect/Report.json) |
| 蜂医动作／旧战斗／双人显隐 | 29／21／32 项，退出码均为 0 | [动作](Phase4Evidence/regression/actions/pie_validation.json)、[战斗](Phase4Evidence/regression/combat/Combat.json)、[显隐](Phase4Evidence/regression/visibility/weapon_visibility_2p_stills.json) |
| 原大厅与 FreeForAll／Teams LAN | 最终打包程序 7 项通过，退出码 0 | [报告](Phase4Evidence/regression/legacy-lan/Report.json) |

四个持久化边界测试使用正式持久化子系统的独立 UE commandlet，验证磁盘记录跨进程恢复；完整打包联机崩溃由 `recovery-unacked` 单独覆盖，两类证据不混称为四组完整网络对局。延迟组在服务器及客户端各设置 50 ms 发送延迟与 2% 丢包，约为 100 ms 往返延迟。其余网络组不模拟丢包。

画面已逐张检查：[固定撤离点](Phase4Evidence/network/settlement/B-extraction-approach.png)、[满仓与待领取](Phase4Evidence/network/settlement/A-stash-overflow.png)、[拖放旋转](Phase4Evidence/network/settlement/A-stash-drag-rotation.png)、[数量拆分](Phase4Evidence/network/settlement/A-stash-split-quantity.png)、[个人成功与继续观战](Phase4Evidence/network/settlement-lag/A-extraction-result.png)、[重启后的仓库](Phase4Evidence/network/recovery-unacked/B-recovered-stash.png)、[双人武器显隐](Phase4Evidence/regression/visibility/WeaponVisibilityFrames/20261003-190207-2p/first_person.png)。当前采用绿色地面标记与原生文字界面，未复刻原版撤离美术和仓库图标。身份拒绝截图拍摄于后续伪造库存请求被拒后，初次重复身份拒绝的具体信息以报告中的较早状态为准，详见 [画面复核](Phase4Evidence/diagnostics/Phase4VisualReview.json)。

## 证据与边界

[原版 Summary.json](Phase4Evidence/Summary.json) 记录实施基线及 25 组构建／验证，[退出修复 Summary.json](Phase4FixEvidence/Summary.json) 记录此前九组构建／验证；两者描述各自历史版本。当前实现与包哈希及本轮 17 组构建／验证见 [大厅往返修复 Summary.json](Phase4LobbyFixEvidence/Summary.json)，每份归档有独立文件索引。日志采用 gzip 无损压缩，明文客户端凭据和服务器存档未归档。阶段 1～3 的历史证据及已有地图资产保持不变。

核心实现见 [ExtractionPersistence](../../Source/BrokenEdge/Extraction/ExtractionPersistence.cpp)、[ExtractionAccountComponent](../../Source/BrokenEdge/Extraction/ExtractionAccountComponent.cpp)、[ExtractionGameMode](../../Source/BrokenEdge/Extraction/ExtractionGameMode.cpp)、[ExtractionStashWidget](../../Source/BrokenEdge/Extraction/ExtractionStashWidget.cpp)。诊断原始记录见 [撤离高度](Phase4Evidence/diagnostics/Phase4ExitDiagnosis.json) 和 [拾取遮挡](Phase4Evidence/diagnostics/Phase4PickupDiagnosis.json)。

打包日志仍有既有的 Stinger RightHand Modify Bone 无活动分量、旧 Rifle_ImpactBody_Cue 缺少 NodeGuid 警告；本轮没有为消除警告重存无关资产。客户端 UAT 曾遇到一次 Zen 本机连接未就绪，工具自行重试后完成打包，未改动服务配置。当前通过结果不代表这些日志完全无警告。

测试使用 OnlineSubsystemNull、同机专服和独立客户端，没有执行跨主机／跨网卡、公网、Shipping、长时间运行、断电存储硬件、负载容量或真实账号服务验收。阶段 2 历史记录中的一次未解释远端弹药停滞仍保留原记录；本轮普通与延迟战斗用例通过，不据此宣称已经定位该历史问题。

阶段 4 的小区域搜集—战斗—撤离—仓库—再次带入循环已通过。下一阶段为路线中的阶段 5：普通 AI、小队与倒地救援、蜂医治疗和烟雾能力，尚未在本阶段实现。
