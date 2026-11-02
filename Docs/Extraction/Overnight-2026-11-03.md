# 航天基地夜间实施记录（2026-11-03）

所有时间为北京时间（UTC+08:00）。目标截止：2026-11-03 08:00；07:15 停止新增功能，07:30 进入最终发布窗口。

## 范围与基线

- 基线：`master` / `github/master`，`ba124b0e9ecdf09161c7d5a7a08b9c3a628084a4`。
- GitHub：`https://github.com/astra1203/BrokenEdge.git`。每个完成的功能独立验证、提交并推送；远端 SHA 核对成功才记为已发布。
- 本机已有 `BrokenEdge.uproject` 引擎关联修改，指向 UE 5.8.3；保留，不纳入功能提交。
- 路线：大厅配装 → `OfficialWestDorm` → `7F_WestDorm_A` 补给 → `7F_WestDorm_B` 医疗 → `7F_LivingArea_B` 贵重物资 / 宿舍守卫 → `OfficialExitSquare` → 回仓 → 第二局。
- 沿用现有 LAN、武器与蜂医。广场 900 秒开放、120 秒窗口是项目兼容规则，不宣称还原官方拉闸。德穆兰保持禁用；试验场 `Bag=Empty` 的官方语义未确认，本轮不扩改。
- 源点和历史站立审计不是当前连续步行验证；构建、自动化、联机、渲染、Cook、完整包分别记录。
- [当前源码能力核对](OvernightEvidence/CurrentCapabilities.md)：更正旧盘点中已经补齐的装备分区、安全箱、M4A1与子弹重力；区别现有能力、兼容规则和仍有差距。

## 时间线

| 时间 | 工作 | 结果 / 证据 | 提交 / 推送 |
| --- | --- | --- | --- |
| 02:56 | 启动夜间执行，复核分支、工作区、引擎及磁盘；分派撤离反馈、守卫回归和路线三个工作包 | 当前只有预先存在的 `.uproject` 修改；无正在运行的 Unreal 进程。D 盘约 60.7 GiB 可用，完整包需控制空间 | `558bcaed1` 已推送，HEAD/跟踪引用/GitHub SHA一致 |
| 03:01 | 首次 Editor 构建 | overnight-editor01：Succeeded | 尚未发布功能 |
| 03:02–03:08 | 撤离反馈自动化及测试初始化修复 | 首跑3失败：临时World控制器未注册；修复fixture后 overnight-exit-feedback02 三项全部通过。中间driver局部变量重名编译错误已修复，overnight-editor04构建通过 | 正在做联机HUD验证 |
| 03:07 | 守卫逐枪采样单元测试 | 13项通过，覆盖旧参数、采样漏帧、burst与历史缺失拒绝；不等同实机AI通过 | 待运行回归 |
| 03:10 | 专服与两客户端首次联机尝试 | overnight-exit-network01进入正式地图；加载峰值可用内存0.64 GiB，主动停止自有测试，记为aborted_resource，无联机通过结论 | 不作为验收通过 |
| 03:12 | 发布撤离中央反馈和规则测试 | 3组规则测试通过；证据见OvernightEvidence/ExitFeedback-Automation.json。HUD渲染/复制仍待低内存方式验证 | 功能提交 `0b5bb56281499bd5cd610bc1ba715b58ef8a0553`；日志提交 `14ac1cac88613411b60c054a6c462cfa8f6df9fd`，均已推送且远端核对一致 |
| 03:13–03:20 | 修复集成地图只读补测的 PIE 启动，补齐西宿舍出生区 | 首次直接用正式模式进入 PIE 没有大厅 raid ticket，无法创建检测角色；改为与现有审计相同的临时测试模式，未保存地图。OvernightSpawnProbe02 通过：632422 探针、892632 边、5个出生点覆盖，地图哈希前后一致 | 随补测工具修复提交；只证明几何，不证明真实行走 |
| 03:22 | 统一编译守卫逐枪事件和新增联机测试驱动 | overnight-editor05 进行中；新增真实守卫RPC断言，测试仍为12组 | 待验证 |
| 03:23–03:24 | 编译与守卫行为树回归 | overnight-editor05通过；overnight-guard-bt01为12/12通过，真实射击/拒绝RPC/换弹的事件记录断言通过。补测提交2326eaf48已推送并核对三处SHA | 守卫完整联网回归待验 |
| 03:25–03:27 | 撤离反馈网络02 | 限制测试画质后正式地图可运行；脚本误要求OwnerOnly字段同步给旁观者，失败事实保留。已按生产复制契约修正，并新增旁观者不得泄露断言，5项离线测试通过 | 未计整轮通过 |
| 03:29 | 带守卫网络01资源中止 | 正式地图加载时可用RAM0.04GiB，停止本轮自有UE进程；Report记aborted_resource。改走新独立包验证，不关闭用户应用 | 未计守卫联网通过 |
| 03:30–03:34 | 撤离反馈网络03 | editor06通过；26项状态/复制/HUD检查通过并产出4张截图。但旧控制器切图期间被错误重试出生，Server自动化最终Fail，整轮仍记failed | 正在修复真实初始化时序；截图还发现三行文字挤压进度条，改回两行待重验 |
| 03:36 | 增强构建/验收资源保护 | 可配置本轮磁盘/RAM门槛，RAM低于1GiB立即中止自有任务树；保留默认规则，报告精确触发原因。10项阈值测试和真实短子进程smoke通过 | 随资源保护修复提交并推送 |
| 03:37 | 资源保护发布 | 21af3ac25d118f0bd354a9328f93568df3bfd5d7，HEAD/跟踪引用/GitHub一致 | 已推送 |
| 03:40–03:42 | 修复无缝切图旧控制器的出生资格 | editor07/08通过；SpawnLifecycle首轮2/3，fixture的ULocalPlayer Outer不符合UE ClassWithin，改为GEngine后overnight-spawn-lifecycle02为3/3。旧PC不重生/不计Ready，真实当前PC缺PS仍Error | 功能独立提交；联网仍待新包验证 |
| 03:43–03:45 | 撤离反馈网络04资源中止 | 新保护器检测RAM0.832GiB并停止自有任务树，随后空闲恢复19GiB。未进入完整验收；记录为aborted_resource，转新Server/Client包 | 不能用中止结果替代通过 |
| 03:46–03:47 | 出生时序修复推送，并启动全新专服包 | 1eb55cf46d99928e57a28507cab02a8279d42675三处SHA一致；Server独立file-cook到C:/BECookONServer01，归档到Packaged/Overnight20261103_01，不复用旧包 | 已推送；打包进行中 |
| 03:48 | 核对旧盘点与当前实现 | 新增CurrentCapabilities.md，38项定义及资产路径检查，记录已有装备分区/M4/重力、两处有效撤离点和守卫当前参数 | 源码盘点，不计运行验收 |
| 03:50 | 发布真实逐枪诊断基础 | 开发构建仅记录服务器接受的最近64次射击及时间，失败RPC不入列；GuardBT 12项回归通过。驱动补充真实视点、LOS/烟雾遮挡与弹药状态供后续联机采样 | 原生验证已完成；完整守卫网络场景待新包 |
| 03:51–03:53 | 完成全新独立专服包 | overnight-package-server01：Build/Cook/Stage 成功，PE 验证有效；后续用此包配对全新客户端 | 开发包，本轮功能和验证源已编译 |
| 03:54–04:07 | 客户端完整符号链接内存恢复 | client01/03 在 Full PDB 链接期间分别触发 RAM 0.890/0.630 GiB 保护而中止；client02误复用了中断产物，末尾 PE 校验拒绝，无有效包。损坏产物已保留并隔离 | 三次失败未计通过；未关闭用户应用 |
| 04:08–04:11 | 增加可选 FASTLINK 本机符号并完成新客户端 | overnight-client-fastlink01 重新链接成功，最低空闲 RAM 12.56 GiB；overnight-package-client04 Build/Cook/Stage/PE 全部通过。包 EXE 与原生 SHA 一致，FASTLINK 索引符号不纳入运行包 | 默认构建方式不变，待独立提交工具改进 |
| 04:15 | 启动新独立包撤离反馈联机验收 | overnight-packaged-exit01：专服 + 两客户端，A 渲染、B NullRHI；中等测试画质，仅作为功能/HUD覆盖 | 运行中 |
| 04:16–04:18 | 独立包撤离反馈首轮发现遗漏 Cook 资源 | 26项场景/复制/HUD检查全部通过，专服自动化Success、无旧PC出生错误；客户端因缺动态加载的拾取UI而Fail，另发现蜂医枪资源遗漏。整轮仍failed，修复正式Cook覆盖后重跑；两行中央提示已截图确认 | 不将场景通过记为整轮通过 |
| 04:20 | 发布可选低内存链接方式与独立包构建证据 | 3组CLI测试通过，Editor/compile-only误用提前拒绝；58452bc8f08b480a4e7b7471f7558cc4922bad10已推送，HEAD/跟踪/GitHub一致 | 已推送 |
| 04:21–04:23 | 新Cook配置首轮专服打包中止 | overnight-package-server02重新触发Full PDB链接，RAM0.691GiB时保护器停止；未到Cook，没有有效包。中断EXE/PDB及UBT日志保留 | failed/aborted_resource，改专服也用FASTLINK |
| 04:24 | 开始新配置专服03并修正世界文字遮挡 | UI/Looting、蜂医模型、物品定义和钥匙门默认模型4个窄Cook根已补齐；此次重建一并隐藏运行时世界Label，保留地面标记和HUD点名/规则，未改区域判定 | 编译/打包中，截图重验待完成 |
| 04:26–04:30 | 全新配对包通过并完成撤离反馈联网验收 | Server03/Client05均Build/Cook/Stage/PE通过，两端所有生产Source/Config哈希与当前文件一致。overnight-packaged-exit02共26项检查通过，Server/A/B均Automation Success、exit0、finish ack；无缺UI/蜂医包错误 | DevelopmentPackages-02.json、ExitFeedback-Packaged.json及4张真实HUD截图；A中等画质，B NullRHI |
| 04:30 | 核对近距离撤离HUD | 两行倒计时与进度条无重叠；世界Label缺字遮挡已消失；地面Marker和屏幕点名、条件保留 | 本轮fixtures验证，不宣称连续路线或900秒自然开放已通过 |
| 04:31–04:33 | 独立包守卫回归首轮 | 21项通过：巡逻、实际逐枪反应/射速/伤害、玩家伤害、墙体、烟雾三秒阻挡；随后等待守卫进入换弹超时。整轮failed，保留原始日志，正在定位 | 不计完整守卫回归通过 |
| 04:34–04:36 | 发布独立包资源与撤离显示修复 | f006745bdf9d10ebb4e2fd5a32a593be87d14172已推送，4张截图LFS上传成功；HEAD/跟踪/GitHub于04:35核对一致 | 已推送 |
| 04:35–04:37 | 双人完整路线首跑 | overnight-route01完成12项大厅/配装/出生/战术图检查，首段前往补给点时在实测路径全局48–52段第2局部点发生Walk stalled。两人存活、宿舍guard未发现目标；整轮failed，保留motion轨迹，未传送越障 | 几何连通不等于实走，正在核对阻挡与路线 |
| 04:38–04:41 | 修复12个导入材质实例的Nanite使用标记 | 新进程repair写入12项；独立verify全部通过且saved0。相对路径备份齐全，参数/父材质/5个原网格哈希未变；只改实例usage位。Client06重新Cook验证中，专服复用并逐字节核对Server03 | 实际独立包视觉和日志复验待完成 |
| 04:43–04:45 | 材质修复独立包复验通过 | Client06重新Cook/Stage/PE成功；复用同源码配置Server03，EXE SHA已核对。overnight-packaged-exit03全部26项和三进程Automation Success；A.log原12个Nanite缺usage材质警告降为0，真实截图已保存 | NaniteUsage-Repair/Verify/Packaged与Package-Run证据齐全，准备提交推送 |

## 工作包状态

| 工作包 | 状态 | 验收边界 |
| --- | --- | --- |
| 撤离区域反馈 | 规则/3项原生/26项独立包联机/HUD截图通过 | 中央原因、倒计时取消、服务器确认、玩家状态隔离 |
| 守卫回归修正 | 实施中 | 当前规则射速/伤害、快照漏采、掉落与拾枪 |
| 正式地图连续路线 | 准备中 | 正常行走与搜索，不使用途中传送证明路线 |
| 回仓与第二局 | 待实施验证 | 物品唯一归属、装备保留、出售与再次出战 |

## 验证记录

Editor构建：overnight-editor01、overnight-editor04通过；02/03因开发测试driver变量遮蔽失败，已修复。撤离规则：overnight-exit-feedback02为3/3通过；首跑失败记录保留。历史报告不作为本轮通过记录。

## 已知差异与阻塞

- 项目尚不等同完整官方游戏，本轮只打磨上述路线和关联流程。
- 发布过程不强推、不重写历史；失败项保留失败事实，不降低断言冒充通过。

## 最终交付

待收尾时记录实际完成项、剩余问题及最终远端提交。
