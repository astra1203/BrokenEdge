# 枪械兼容实现：运行验收与复现

本报告验证 BrokenEdge 的工程实现。来源未确认的后座角度换算、评分组合、伤害分段及握把调校仍遵循 [兼容策略](Compatibility.md)
，不代表官方游戏公式或手感已还原。

## 专用靶场

在内容浏览器打开 `/Game/Extraction/Training/L_WeaponRange`，使用单人 Standalone PIE。出生时手持 AKM、背负
SCAR-H，并提供两种备用弹药、垂直握把、红点和补偿器。使用右键开镜、左 Alt 屏息、左键开火、R 换弹、1/2 切枪、Tab
背包。右键枪械选择“改装与参数对比…”，预览并应用装配或调校。

场景包含 10、25、40、50、70、100 米目标、近距离枪口遮挡块和薄墙；控制台 `BrokenEdge.Weapon.DebugFlight 1` 显示扫掠轨迹及保留 30
秒的服务器命中红点，`BrokenEdge.Weapon.DebugStats 1` 显示机械参数。训练模式不启动正式 raid 票据或结算；存档隔离至
`Saved/Extraction/Training/<GUID>`，离开训练地图后重新加载正常存档。

地图由 `Scripts/Extraction/prepare_weapon_range.py` 生成。它只重建独立训练地图中的测试几何、标尺、灯光和
PlayerStart；需要保留个人布局时先另存地图，不对正式航天基地运行此生成器。

## 测试命令

以下命令使用 UE 自带 Python。每次 `--tag` 必须是尚不存在的目录名；运行输出位于 `Saved/Extraction/<tag>`。

```text
python Scripts/Extraction/run_build.py --tag mechanics-build
python Scripts/Extraction/run_editor_task.py weapon-range-assets --tag mechanics-range
python Scripts/Extraction/run_editor_task.py weapon-pie --tag mechanics-pie
python Scripts/Extraction/run_editor_task.py weapon-mechanics --tag mechanics-unit
python Scripts/Extraction/run_editor_task.py equipment --tag mechanics-equipment
python Scripts/Extraction/run_editor_task.py persistence --tag mechanics-persistence
python Scripts/Extraction/run_network.py --scenario weapon-mechanics --players 2 --rendered-clients 2 --lag --packaged --package-root Packaged/WeaponMechanics01 --tag mechanics-pair
python Scripts/Extraction/run_network.py --scenario weapon-mechanics --players 6 --rendered-clients 1 --lag --packaged --package-root Packaged/WeaponMechanics01 --tag mechanics-six
python Scripts/Extraction/plot_weapon_patterns.py
```

上述联机命令使用本地构建的 `Packaged/WeaponMechanics01`，打包输出不纳入 Git。重新构建时给 `--package-root`、`--cook-root` 和
`--tag` 选择新目录，客户端与服务器顺序执行：

```text
python Scripts/Extraction/run_build.py --target Client --package --package-root Packaged/WeaponMechanicsNew --file-cook --cook-root C:/BECookWeaponClientNew --direct-stage --no-uba --jobs 4 --cook-workers 2 --tag mechanics-package-client-new
python Scripts/Extraction/run_build.py --target Server --package --package-root Packaged/WeaponMechanicsNew --file-cook --cook-root C:/BECookWeaponServerNew --direct-stage --no-uba --jobs 4 --cook-workers 2 --tag mechanics-package-server-new
```

PIE 检查明确断言世界类型为 PIE，通过输入完成开镜、屏息耗尽冷却、松开恢复、开火消耗、换弹和切换 SCAR；背包安装与调校调用生产事务接口。测试临时指定单人
PIE，结束后恢复原有 PIE 设置。

联机测试通过正常仓库转移 AK 出战，不依赖地面拾取；每名玩家经输入发射子弹，检查权威接受次数、弹药收敛、配置版本、唯一伤害序列和正常撤离后保存的枪械
GUID、实际弹匣与装配。位置和生命值由测试夹具布置，伤害不由夹具注入。延迟配置为每方向 50 ms、2% 丢包。NullRHI
客户端仅提供联机功能证据，不提供视觉证据。

## 采样图的范围

`WeaponMechanics.PatternSamples` 导出 50 米平面上两把枪的站立、蹲伏、移动、腰射、开镜、单发和连发组合，共 720 个采样点，固定种子
12345。`plot_weapon_patterns.py` 输出 SVG，颜色区分姿态；各面板独立缩放，并标注厘米坐标范围。

这是直接调用运行时解析器和采样函数得到的算法图：单发重置瞄准，连发累计后座并按射速扣除散布恢复；不含鼠标压枪、重力、碰撞、漂移或镜头回正。不能把它当成真人压枪轨迹或完整
PIE 弹着采集。固定步进弹道、薄墙和伤害节点由原生测试覆盖，实际操作由 PIE 与联机测试另行覆盖。靶场可使用命中红点观察完整运行弹着。

## 加载问题的调查边界

历史 `weapon-mechanics-network06` 的渲染客户端地图旅行达到约 77／119 秒，超过 60 秒连接阈值。阶段四首轮六人单渲染全部进入地图，随后失败于脚本要求第六人拾取地面
AK；已改为正常仓库出战流程。

`mechanics-stage4-six02` 的六人双渲染组合再次出现加载期
ConnectionTimeout，未进入武器断言。保留失败结果，不将其表述为武器网络逻辑通过或六窗口渲染通过。后续分别验收双人双渲染与六人单渲染，并记录
Windows 可用内存和提交内存。加载资源竞争是需要结合采样分析的因素，不能仅由一次暖缓存成功断言冷启动问题已根治。

`mechanics-stage4-six03` 六人全部进入地图并通过 53 项检查，逐人开火时发现 NullRHI 客户端被服务器拒绝。源码原因是
`TraceUnderCrosshairs` 在视口尺寸为零时直接返回，留下旧的目标点。已增加玩家视点／旋转回退路径，保留正常反投影和服务器方向校验；修复后的
`mechanics-stage4-pair01` 通过 44 项，包含无渲染客户端真实输入开火和双方正常撤离保存。`six04` 再次受到同机多编辑器地图加载超时影响，转向独立
Development 包进行六人验收。

资源采样：`six03` 系统提交内存峰值 73.81 GiB，`six04` 为 72.11 GiB；两轮可用物理内存最低均接近零。机器物理内存约 32
GiB，日志同时出现数十秒长帧。因此单机运行七个编辑器进程有明确的换页／资源压力证据；这不是扩大网络等待时间就能证明已解决的问题。报告保留每轮资源摘要，Development
包的结果独立记录。

## 最终验收记录

2026-10-24，批准的兼容实现范围完成验收。所有下列运行均使用阶段四最终 C++；最后一轮只调整观察者截图角度。完整检查名称、失败记录、程序哈希与最终源码摘要见 [Acceptance.json](Runtime/Acceptance.json)，原始进程日志保留在本地 `Saved/Extraction`。

| 检查 | 结果 | 运行标签（前缀均为 `mechanics-stage4-`） |
| --- | --- | --- |
| 编辑器编译 | 通过 | `build07` |
| 枪械／采样／装配／弹道 | 4 项通过，含 30／60／120 FPS 固定步进、薄墙与衰减节点 | `unit02` |
| 装备回归 | 4 项通过 | `equipment01` |
| 持久化回归 | 4 项通过 | `persistence01` |
| 真实 PIE 输入与装配 | 1 项流程测试通过 | `pie05` |
| 正式仓库存档隔离 | PIE 前后日志 SHA-256 一致 | [StoreIsolation.json](Runtime/StoreIsolation.json) |
| Development 客户端／专用服务器 | 编译、Cook、IoStore、可执行文件验证均通过 | `package-client01`、`package-server01` |
| 双客户端（一个无渲染） | 44 项通过，覆盖无视口射线回退 | `pair01` |
| 打包双人双渲染 | 44 项通过；补拍侧面观察者截图后再次通过 | `dual-package02` |
| 打包六人，1 个渲染＋5 个 NullRHI 客户端 | 80 项通过，六人全部开火并正常撤离保存 | `six-package01` |

六人打包测试的系统提交内存峰值为 39.20 GiB，可用物理内存最低为 7.14 GiB，未发生此前的加载超时。此结果证明该部署方式下的六人枪械流程；不宣称六个编辑器窗口同时渲染、所有机器冷启动或官方游戏手感已经通过验证。

随仓库保存：[PIE 靶场截图](Runtime/WeaponRangePIE.png)、[改枪应用截图](Runtime/GunsmithApplied.png)、[观察者侧面截图](Runtime/Observer.png)、[姿态算法图](Runtime/Patterns.svg)、[720 点原始采样](Runtime/Patterns.csv)。截图已查看，确认靶场距离文字、SCAR 观察者模型和改枪面板可见。

仍保留的边界：官方公式缺失与来源版本未知；SCAR 标准件固定展示，未验证的独立附件不开放；六编辑器同机加载有资源压力；算法采样图不等同于完整 PIE 弹着采集。它们没有被改写为已证明的官方还原结果。
