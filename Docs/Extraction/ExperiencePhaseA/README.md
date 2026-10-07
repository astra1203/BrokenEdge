# 航天基地体验建设：阶段 A

本轮依据[体验差距文档](../../航天基地搜打撤体验差距与实现方案.md)和用户确认的方案实施。基线为 `fac5dbcb`，UE 5.8.3。阶段 A
包含参考资料、原始资源审计和可重复完整打包；阶段 B 的内容制作必须等首期全部参考齐备后开始。

## 已锁定的目标

| 阶段 | 交付                                                                                            | 门槛                                                     |
|------|-------------------------------------------------------------------------------------------------|----------------------------------------------------------|
| A    | 同一国服 PC 版本参考、资源依赖清单、Client/Server 完整新包                                      | 首期资料全部齐备；新包能完成基础循环                     |
| B    | AKM、M4A1、MP5；枪口/开火动作/后坐力；HUD、背包、搜索、仓库、地图基础信息、观战和结算；四类容器 | 三把枪逐把验收，正式地图代表路线完成配装到回仓和再次出发 |
| C    | 三维改枪、兼容关系、属性比较、方案保存、精校；增加 K416、SR-25、AWM、M870、M249                 | 八把枪各自的动作、弹药、改装、联机和存档通过             |
| D    | 航天基地全区域、设施、物资、撤离、环境音、AI 行为和性能                                         | 区域覆盖表关闭；全路线、完整对局、冷/热性能分别通过      |

用户特别指出的枪口错位、看不到开火动画、只有散布没有后坐力，属于 B
的必交项。第一人称可见枪口、第三人称世界枪口和服务器命中需要分别校准；不能用“存在蒙太奇调用”或“伤害测试通过”替代连续画面验收。

默认保持轻松人机：2 秒反应、0.8 秒射击间隔、0.3 伤害倍率、四名守卫。首期沿用即时射线命中及现有装备容器规则；弹速/下坠、完整改枪和新增容器规则后续实施。旧
AK 和弹药定义 ID、物品 GUID、来源和存档必须兼容。局内任务不增加为内容门槛。本轮不提交、不推送。

## 参考资料门槛

[ReferenceChecklist.json](ReferenceChecklist.json) 包含 16 组、103 个观察项：9 组界面、3 把枪、4 类容器。版本号、赛季、采样日期、分辨率、FOV
和输入设置必须明确。每组至少保留一段连续操作录像；各观察项需关联实际媒体文件、SHA-256、同一客户端版本、检视结论。

目前没有取得该批同版本实机资料。原始素材包内的 Bink 过场/展示视频不充当实机操作录像，国际版活动网页和来源未明的数据表也不作为当前国服精确数值。资料缺失时只推进
A 的独立工作，B 继续等待。

填写证据时，在对应组的 `evidence` 数组中加入如下记录；`path` 相对清单所在目录，也可为现有本地文件的绝对路径。`covers` 必须使用该组
`required_observations` 中的名称。检视实际媒体之后才可设置 `reviewed=true`。

```json
{
  "path": "References/实际录屏.mp4",
  "kind": "video",
  "client_build": "实际客户端版本号",
  "sha256": "实际文件SHA-256",
  "covers": ["default", "interaction"],
  "reviewed": true,
  "review_note": "对应时间段、观察结果及保留差异"
}
```

校验器检查完整性、版本和文件哈希；它不能自动证明媒体内容和检视结论真实。遗漏整组、遗漏观察项、不同版本、无连续录像、未检视、文件缺失或哈希变化均不允许进入
B。

```powershell
$python = 'C:/UnrealProjects/UnrealEngine-5.8.3-release/Engine/Binaries/ThirdParty/Python3/Win64/python.exe'
& $python Scripts/Extraction/experience_reference.py --check Docs/Extraction/ExperiencePhaseA/ReferenceChecklist.json --report Saved/Extraction/<新标签>/ReferenceGate.json
```

退出码 0 表示资料清单通过；退出码 2 表示门槛未满足。报告文件必须使用新路径。

## 构建修复与隔离

旧 `EasyGuardsServer01` 在 Cook 完成后暂存失败：Cook 元数据记录 `[::1]:8558`，暂存拉起服务后日志报告 `[::1]:8758`，UAT
仍等待原端口。该记录说明端点/生命周期不一致，尚不证明具体的端口变更根因。

本轮恢复方案使用 UE 5.8.3 `CookCommandlet` 原生支持的 `-SkipZenStore`：完整重新 Cook 到独立文件目录，再生成 IoStore 包。DDC
仍可使用 Zen，但 Stage 不再需要从结束或端点已变化的 Cook 服务读取物资。未修改引擎或全局 Zen 配置。

文件式 Cook 需要短路径，地图中的长资源名在深层 `Saved/Extraction/...` 路径会触发 UE 的 260 字符限制。Cook 目录必须以
`Windows` 或 `WindowsServer` 结束，使 CookCommandlet 的实际输出和 UAT Stage 的输入一致；脚本自动补齐平台目录。

`run_build.py` 新增 `--file-cook`、`--cook-root`、`--package-root`。新 Cook 根目录和对应归档目标必须不存在，避免覆盖旧产物。
`run_network.py` 新增独立包根目录、端口和房间筛选；Development 驱动只加入指定标签房间，继续通过生产房间浏览/加入流程完成联机。

```powershell
$python = 'C:/UnrealProjects/UnrealEngine-5.8.3-release/Engine/Binaries/ThirdParty/Python3/Win64/python.exe'
& $python Scripts/Extraction/resource_guard.py --tag <新Server标签> --timeout 7200 -- $python Scripts/Extraction/run_build.py --target Server --package --file-cook --cook-root D:/BEExperienceCook/<新短名称S> --package-root Packaged/<新批次> --no-uba --jobs 2 --cook-workers 2 --tag <新Server标签>
& $python Scripts/Extraction/resource_guard.py --tag <新Client标签> --timeout 7200 -- $python Scripts/Extraction/run_build.py --target Client --package --file-cook --cook-root D:/BEExperienceCook/<新短名称C> --package-root Packaged/<同批次> --no-uba --jobs 2 --cook-workers 2 --tag <新Client标签>
& $python Scripts/Extraction/resource_guard.py --tag <新验收标签> --timeout 1800 -- $python Scripts/Extraction/run_network.py --tag <新验收标签> --packaged --package-root Packaged/<同批次> --port 28777 --isolated-room --scenario settlement --rendered-clients 2
```

尖括号内容是说明性占位，执行前替换为新名称。重任务串行，继续使用已有磁盘/RAM保护。保留原来供用户游玩的服务器、包和存档；不终止共享进程。本轮新包位于
`Packaged/ExperiencePhaseA03`，验收使用 28777 端口和独立标签房间。

凡通过屏幕准星发射真实子弹的参与者必须使用渲染客户端。`TraceUnderCrosshairs` 依赖有效视口，NullRHI
射手不能作为正常瞄准的验收配置。首次结算运行的失败及两个渲染客户端的后续通过分别保留。

## 资源审计与后续实施入口

`audit_experience_sources.py` 只读 ZIP 目录和有限的模型数据，不改动原始资源。报告分别记录候选文件、GLB 结构、贴图引用、UE
导入、播放、视觉检视的状态。名称命中和结构通过不代表完整武器已适配。

```powershell
& $python Scripts/Extraction/audit_experience_sources.py --tag <新资源审计标签>
```

本轮只完成武器/UI 候选资源的首轮清点：27 个 ZIP，M4A1 与 MP5 的 1P、3P、UI 共六个枪体 GLB
结构通过，引用的贴图路径在压缩包索引中均可定位。八枪名称匹配的动画候选数量依次为 AKM 24、M4A1 120、MP5 112、K416 126、SR-25
68、AWM 69、M870 155、M249 179；这些计数包括不同动作、外观及共享资源，不能作为已适配动作数量。

AKM 没有按名称命中的枪体 GLB。数据表 `WeaponPart/GunPresetTable.json` 的 `10010000012` 行名为“AKM标准预设”，指向
`RecId=18010000006`、`DescId=10010000012`。这是继续追踪共享机匣的入口，尚未完成最终模型映射；不能据此断言资源缺失。原始包的客户端版本、三枪完整装配、音效映射、动画重定向与
UE 播放仍未验收。

## 版本、区域与缺陷跟踪

| 比较对象         | 当前证据                                 | 下一步                                   |
|------------------|------------------------------------------|------------------------------------------|
| 国服 PC 参考版本 | 版本号、赛季、采样日期均待补             | 填写清单并审阅全部 16 组媒体             |
| 本地解包素材     | ZIP 目录和模型结构已清点；来源版本未证实 | 保留来源与差异，不能直接抄作当前官方参数 |
| BrokenEdge 基线  | `fac5dbcb` 加本轮构建/测试工具变更       | 以新包哈希和运行报告定位本轮产物         |

| 区域/路线          | 可复用证据与入口                                                       | 本轮边界                                   |
|--------------------|------------------------------------------------------------------------|--------------------------------------------|
| 旧室内、楼梯与室外 | [7D 历史验收](../Phase7D.md)、`Scripts/SpaceCenter/route.json`         | 新包进入和撤离已验证；没有重跑全部输入实走 |
| North              | `L_SpaceCenterNorth.umap`、`north_route.json`；历史保留 870 个流送实例 | 本轮未重跑区域加载/卸载和导航验收          |
| 东侧往返           | `east_route.json`、`east_traverse_route.json`；历史正式地图绕行路线    | B 集成继续使用正式地图路线；本轮未新增区域 |
| 完整官方航天基地   | 同版本全图边界与设施清单待采样                                         | D 逐区域建立对应关系，当前不声明完整覆盖   |

| 缺陷/差距                | 已定位入口                                                                   | 关闭条件                                               |
|--------------------------|------------------------------------------------------------------------------|--------------------------------------------------------|
| Cook 完成后 Zen 暂存失败 | `run_build.py` 文件式 Cook 与隔离输出                                        | 完整双端打包及新包基础运行；Zen 底层端口根因单列       |
| 枪口与可见枪体不一致     | `HitScanWeapon.cpp` 使用世界武器 `MuzzleFlash`；角色另有第一人称视觉网格     | B 三枪分别校准本地枪口、第三人称枪口、服务器命中和贴墙 |
| 开火动作不可见           | `PlayFireMontage` 受身体蒙太奇前置条件影响；世界武器另行播放 `FireAnimation` | B 连续录像证明手、枪体机械动作与扣弹时序一致           |
| 缺少独立后坐力           | 当前附件主要影响散布                                                         | B 镜头与枪体后坐力、回正、玩家反向输入及帧率对照       |
| UI、搜索仍为原型         | 当前库存面板、固定物资与统一搜索时长                                         | B 逐页/逐容器对照同版本资料，保持服务器事务            |
| 首访和偶发慢帧           | 7D 历史性能报告                                                              | 后续重新采集冷/热数据；本轮不宣称性能改善              |

B 的实施顺序固定为：共用物品/武器定义 → 三枪枪口、动作和后坐力 → 共用 UI → 四类搜索样板 →
正式地图旧室内/室外/东侧往返路线集成。规则继续通过服务器事务，未揭示物品的图标、品质、价值与音效不能提前泄露内容。纯展示数据不升级存档；新增实例结构时才备份并显式迁移当前
v3。

B 验收必须分别保留：四种分辨率的 UI 画面，三枪单发/连射/换弹/贴墙录像和录音，30/60/120 fps 后坐力对照，库存守恒/争抢/恢复报告，独立进程正常及每方向
50 ms/2% 丢包测试，以及冷/热性能。性能沿用 1080p Medium、热身路线 P95≤16.7 ms；新增操作窗口另查超过100 ms慢帧。

最终运行结果、失败尝试和证据索引由本轮结束前的验收记录补充；在资料和资源门槛全部通过前，阶段 A 不标记完成。
