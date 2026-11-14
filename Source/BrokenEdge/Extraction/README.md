# Extraction 目录

目录表达现有职责，所有类型仍属于 BrokenEdge Runtime 模块。此次分区只移动文件并更新 include / 工具路径；不改变 UCLASS、USTRUCT、RPC、资源路径或存档格式，因此没有 CoreRedirects。

| 目录 | 职责 |
| --- | --- |
| Core | 模式、控制器、复制状态、账号桥接与交互入口、靶场 |
| Inventory | 物品数据与注册表、库存事务、装备规则、枪匠、掉落容器 |
| Combat | 射击与动作状态、近战、技能、投射物和武器表现 |
| Health | 伤害部位、护甲、医疗、出血与倒地 |
| AI | 守卫、瞄准计算、导航与巡逻区域 |
| World | 门、桥、水体、滑索、钥匙房、撤离点及场景拾取布局 |
| UI | HUD、背包、仓库、任务、战术地图及 UI 采样 |
| Persistence | 存档协议、事务日志、迁移与恢复 |
| Progression | 经济与任务数据和纯规则 |
| Map | 地图定义、注册表与区域流送 |

依赖方向以 UI → 玩法 → 数据/规则为准。UI 消费库存只读快照和玩法入口；库存、战斗、生命分别持有自己的权威状态；Persistence 依赖物品和任务数据，不依赖 Widget 或 UI。

Core 的控制器仍负责创建 UI，旧 Combat 与 Character 仍有兼容桥接；分目录并不等于已经消除这些现有调用关系。本次不做模块拆分或新的接口抽象。

跨目录 include 使用 `BrokenEdge/Extraction/<目录>/<头文件>`。新增文件按职责放置，生成的 `.generated.h` 名称保持与类型头一致。移动验证见 [WP-19 记录](../../../Docs/Artifacts/TechnicalDebt/WP19/实施记录.md)。
