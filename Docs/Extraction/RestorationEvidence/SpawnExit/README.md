# 官方出生点 / 撤离点还原记录

## 数据出处

`OfficialSpawnExitAnchors.json` 由 `Scripts/Extraction/build_official_spawn_exits.py` 从
`Docs/Extraction/SpaceCenterExpansion/SourceGameplayAnchors.json` 生成。后者是解包
`DeltaForce/Content/Maps/SpaceCenter_Level{1,2}/GameplayConfig/...` 的导出，含解码文件 SHA-256、
原包路径、导出索引、原坐标与换算坐标。

换算约定与物资、保险箱同一套：`world = source + (-664000, +451000, +22400)`。

| 变体 | PlayerStart | PlayerExit | ExitTrigger |
| --- | --- | --- | --- |
| SpaceCenter_Level1 | 45 | 3 | 4 |
| SpaceCenter_Level2 | 45 | 3 | 4 |

两变体的出生点最大平面漂移 340 cm（Level2 对个别点做过手工挪动），三个撤离点完全同址。
默认取 Level1，生成时对 Level2 做交叉断言并把漂移写进报告。

## 45 个出生点 = 9 队

按 PlayerStart 编号分带；组内最大跨距 340–1176 cm，组间最小间距 5562 cm，是 9 个互相分开的出生区，
不是按邻近关系猜出来的分组。

| 组 | 中心（工程坐标 cm） | 点 |
| --- | --- | --- |
| OfficialWestGate | (-21788, -3320) | PlayerStart 2/3/4/5/_1 |
| OfficialWestDorm | (-22444, -21359) | PlayerStart 6_8/7/8/9/10 |
| OfficialControlNorth | (1348, -28526) | PlayerStart 21–25 |
| OfficialAssembly | (15000, -17770) | PlayerStart 31–35 |
| OfficialSoutheast | (25176, -29492) | PlayerStart 36–40 |
| OfficialEastGate | (33156, -13529) | PlayerStart 41–45 |
| OfficialNortheast | (24037, 3360) | PlayerStart 46–50 |
| OfficialLaunchSouth | (7856, -27550) | PlayerStart 51–55 |
| OfficialNorthRidge | (-6207, 33730) | PlayerStart 56–60 |

## 三个官方撤离点

| id | 源对象 | PlayerExitId | 世界坐标 | 官方开放 | 官方关闭 | 官方条件 |
| --- | --- | --- | --- | --- | --- | --- |
| OfficialExitSquare | BP_PlayerExit_Square | 308 | (8363, 9761, 31) | Switch，4 个 trigger 随机 2 个，ExpectedOpenTime 900 s | 开后 120 s | 无 |
| OfficialExitDemoulin | BP_PlayerExit_Demoulin_3 | 309 | (3828, 22731, 883) | Delay 900 s | 倒计时（无显式时长） | PayItem：5 个 151104000xx 或通行物 15200000303；最多 3 人 |
| OfficialExitTestrange | BP_PlayerExit_Testrange_3 | 307 | (23505, 23586, 228) | Delay 540 s | 撤离满 3 人（Level2 为 1） | Bag = Empty（背包清空） |

结论：原版的「丢包撤」是**试验场 307**，不是此前临时放在东区吊桥的那一个点。

## 写资产

`Scripts/Extraction/place_official_spawn_exits.py`（编辑器 Output Log 运行）

1. 逐点碰撞复核：authored Z ±300 → 放宽到 ±1200 → XY 八向外扩 300/600/900/1200/1500 cm。
   只保留能站立的点（法线 Z≥0.7 + 半径 34 / 半高 92 胶囊不重叠）；外扩过的点标 Inferred 并记录偏移。
2. 组内可站立点少于 3 个则整队丢弃；撤离点探不到地面则丢弃。
3. 门槛：出生组 ≥2、撤离点 ≥1（`AExtractionGameMode::bMapConfigurationValid` 的硬要求）。
   达不到就只写报告不动资产；`--fallback-legacy` 可把旧配置组/撤离点以 `Legacy_*`、Temporary 兜底。

时序默认不启用（换位置不改开放时间）；`--official-timing` 才按官方秒数 ×1/3 折算进 600 s 回合
（试验场 180 s、德穆兰 300 s、广场 300 s 开、420 s 关）。1/3 是项目折算，不是官方局时。

## 尚未解决

当前已实测可站立的区域（保险箱 12 个 + 源梯子 7 个 + 散落物资 36 个候选）只覆盖
X −11778…14000 / Y −30000…13258 一带。按此推断 9 队里可能只有 ControlNorth、LaunchSouth 能落，
Assembly 处于边缘；3 个撤离点大概只有 Square 落在北区范围内。
**必须先跑 `--audit` 看实际覆盖，再决定补地形还是接受小范围随机。**
