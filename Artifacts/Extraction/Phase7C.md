# 阶段 7C：航天基地建筑群与原始地形重建

本阶段依据原始资源版本重建当前建筑群及周边，范围约 290 × 330 米。材质采用原始贴图、保留参数及游戏画面进行视觉重建；7B
记录的原始材质图缺失仍然成立，不将重建结果称为原版着色器等价恢复。

**阶段 7C 已完成，验收与证据归档校验通过。** 独立预览、几何审计、地形碰撞、室内外实走、正式地图接入、重复生成、Development 打包、规则测试、完整多人回归、正式地图最终截图与六轮性能采样均已通过。

起点：`227b65b9d0ec76411dafa5a8839caa3fb3a5b358`；引擎：`C:\UnrealProjects\UnrealEngine-5.8.3-release`。本阶段不增加新撤离规则、事件、Boss
或在线服务。

## 原始数据与重建范围

| 项目         | 实际结果                                                                          |
|--------------|-----------------------------------------------------------------------------------|
| 源坐标范围   | X=652000～681000，Y=-466000～-433000 cm                                           |
| 项目原点     | `(664000,-451000,-22400)` cm                                                      |
| 场景实例     | 33,120：20,651 个静态场景组件、12,469 个植被实例                                  |
| 静态网格     | 1,899 个；原始地形另有 110 块                                                     |
| 源组件保留   | 既有 1,309 个实例；补入相交建筑、房间、装饰、植被层，排除 22 个完全重复的静态实例 |
| 材质配方     | 1,759 个，覆盖原始层、法线、粗糙度、透明度、玻璃和发光参数                        |
| 原始地形     | 113,313 个唯一采样点、225,280 个三角形；共享边界法线重算                          |
| 原生实例组件 | 7,568 个 `SpaceCenterInstanceGroup`，使用可持久化的 HISM 默认子对象               |
| 正式分区     | 常驻 32,250 个实例及全部地形；North 流送 870 个无碰撞装饰/植被实例                |

原始 ZIP 只读。源探测覆盖配置目录中的 58 份 ZIP；范围不代表整张航天基地地图。高度采用原始 CDLOD
数据，未以平板替代。植被紧凑变换的物理实例使用源物理记录校准，负缩放保留。

原始虚拟纹理按实际 Morton 瓦片、块偏移、压缩方式及瓦片边框恢复；不将虚构的矩形填充当作纹理数据。部分损坏的最高 mip
使用已记录的下一层源 mip。少量明确的原始 HD/同名素材替代见 `restoration_source_variants.json`，不会隐式搜索同名任意素材。

## 材质和视觉边界

材质槽按源渲染 section 和原生槽名映射，省略 2,610 个未使用的 cooked 槽；833 个网格的槽顺序已校正。普通植被使用源 `ColorAo`
，impostor 使用源 `BaseColorOpacity`，避免把继承的白色默认贴图用于整株植被。

地形 G16 权重按已记录的位域规律恢复为四层权重图；全部观察值通过范围与权重和校验。插值、缺失全局
tint、风动、屏幕动画及被剥离的材质函数属于视觉重建边界。原始 glitch 屏幕贴图保留为静态效果。

当前使用经过预览检查的 UE 光照基线：太阳 5 lux、天空光强度 1、曝光补偿
-1。源环境包的太阳、天空光和后处理参数已提取记录，但原作的动态时段控制与定制后处理链未恢复，因此这些画面不代表原作同一时段的精确光照。

已逐张检查正式地图的 14 个固定相机、原生 1920×1080、Medium 截图（`Phase7CIntegratedCapture01`）。东区相机调整为建筑群全景，原庭院视角准确命名为 `paved_yard`，新增 `terrain_seam_native` 检查四块原始地形交界处，未见裂缝。110 块地形的 770 个原生碰撞探针同时通过。检查前后两张地图 SHA-256 一致，临时本地 GameMode 与 North 显示设置未写回地图。

历史 2024 年离心机房画面只作为同房间候选参考，源版本及视角尚未严格匹配。源贴图中的 `BAD` 字样、紫色标记和 glitch 屏幕仍可见，保留为当前源资源重建的明确视觉边界，不能把这些固定截图称为与原作完整画面等价。外缘截断属于约定的局部重建范围；既有玩法箱子和钥匙房附属结构保留原外观。

## 已完成的原生验证

| 检查               | 证据及结果                                                                                                                                                        |
|--------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 预览几何审计       | `Phase7CNativeAudit05`：33,120 个实例、2,009 个几何资产；无几何、材质绑定或纹理色彩空间错误                                                                       |
| 原生仿射坐标       | 最大世界几何误差 0.0008834 cm；网格边界误差 0.0009156 cm，均小于 1 cm                                                                                             |
| 地形碰撞           | `Phase7CCapture05`：110 块地形、770 个探针，最大高度误差 0.0001193 cm                                                                                             |
| 预览实走           | `Phase7CTraverse03`：武器装备、蹲起、跳跃、门口、上下楼梯、北门与室外往返全部通过                                                                                 |
| 正式地图接入       | `Phase7CIntegrate01`：保留 38 个玩法对象，移除明确列出的历史占位地面、坡道和边界墙，巡逻路径及北区导航通过                                                        |
| 正式地图冷启动审计 | `Phase7CIntegratedAudit02`：重复生成后的全部实例、几何及材质绑定通过；历史占位地面不存在；两张地图前后哈希一致 |
| 正式地图最终截图 | `Phase7CIntegratedCapture01`：14 张原生 1080p Medium 截图逐张检查；110 块地形、770 个原生碰撞探针通过，最大误差 0.0001193 cm |
| 重复生成           | `Phase7CIntegrateRepeat02`：33,120 个源 ID/实例绑定完全一致，38 个玩法对象位置保持不变，巡逻及北区导航再次通过                                                    |
| 新 Development 包  | `Phase7CPackages04`：Server 与 Client 均完成编译、完整 Cook、暂存和归档；并非复用旧包进行运行验收                                                                 |
| 规则测试           | `Phase7CRules01`：背包 3 项、持久化 4 项、装备 4 项、进度 4 项通过                                                                                                |
| A 客户端分区实走   | `Phase7CStreaming01-two-rendered-a`：原生 1080p 渲染 A，完成 10 轮加载、实际往返和卸载；68 项检查通过，包括延迟加载阻挡及恢复、战利品身份、撤离入库、观战与第二局 |
| B 客户端分区实走   | `Phase7CStreaming02-two-rendered-b`：原生 1080p 渲染 B，完整重跑 10 轮及加载失败恢复、拾取、撤离、跨区观战和第二局；全部通过                                      |
| 六客户端普通网络流送 | `Phase7CStreaming02-six-streaming`：六客户端连接、两轮实际往返、延迟加载恢复、战利品身份、撤离持久化、观战及第二局全部通过；一个渲染客户端，其余使用 NullRHI |
| 六客户端延迟流送 | `Phase7CStreaming02-six-streaming-lag`：每方向 50 ms 延迟、2% 丢包下的相同流程通过 |
| 六玩家与四 AI | `Phase7CStreaming02-six-northcombat` 及 `-lag`：47/48 项检查通过；所有 AI 实际到达巡逻起点、目标及返回点，包含跨区；真实子弹、死亡掉落、拾取、撤离入库、观战及大厅清理通过 |
| 任务与钥匙房回归 | `Phase7CRegression01-progression`：六客户端、延迟丢包下三局任务、交易、防重放、钥匙房实际碰撞、文件资格、击杀归属、撤离持久化及奖励解锁通过 |
| 装备完整回归 | `Phase7CRegression03-equipment`：六客户端、两个渲染客户端；附件装卸/显示/回收、真实子弹伤害、护甲耐久、医疗与创伤、救援、撤离入库、回收价格及下一局状态通过；四 AI 在巡逻验证后按记录解除武装，隔离额外伤害 |
| 其余完整回归 | `Phase7CRegression03`：小队 72 项、延迟 PVPVE 58 项、生命周期 108 项、背包 93 项、结算 39 项、断线 17 项通过；进度 132 项复用 `Phase7CRegression01-progression`，装备 134 项使用当前完整重跑结果 |
| 红点专项 | `Phase7CRegression03-optic`：108 项通过；1920×1080 的站立/蹲姿、0°/±35° 俯仰及重新安装共 7 张测量图，最大中心偏差 0.0866 px，低于 2 px；随后完成伤害、医疗、撤离和第二局回归 |

HISM 对负缩放矩阵的分解可能产生不同的四元数及缩放符号，因此几何误差比较使用全部局部边界角点的世界仿射变换，不能用原始四元数差直接判断错位。

地形 Nanite 使用完整 fallback 几何作为碰撞，避免自动简化带来约 50 cm 的高度偏差。重复生成会刷新 HISM
导航条目；早期未刷新版本的失败证据保留。预览拾取测试显式走到武器近旁再按装备键，避免停在骨骼重叠边缘，生产拾取逻辑未因此改动。

## 生成、覆盖和回退

正式入口为 `Scripts/SpaceCenter/run_restoration_editor.py`。接入前校验同一 manifest
的审计、截图与实走结果；备份两张原地图，再在内存中组装并检查导航，最后统一保存。首次备份位于
`Saved/SpaceCenter/Phase7C/EditorRuns/Phase7CIntegrate01/OriginalMaps`。

`Scripts/SpaceCenter/restoration_active.json` 标记当前正式重建。旧 sample/7A 生成入口会拒绝覆盖重建地图；重建器仅删除自身标记且已失效的对象。
`restoration_overrides.json` 中的材质、实例变换、保留对象和原有门片平移覆盖优先。

当前权威输入：

```text
Saved/SpaceCenter/Phase7C/ManifestRuns/Phase7CManifest02/Manifest.json
SHA-256 05ce654893c5156adc26abec982303deb7091ab32e35e457eccaf7842afb9a0a
几何签名 0413371cc9b754b81f71368fbb08e02f5dc5c474ae171c836f314646df29ff4b
```

每次使用新的运行标签；UE 任务通过 `Scripts/Extraction/resource_guard.py` 包装。正式地图接入示例：

```text
python Scripts/Extraction/resource_guard.py --tag <新标签> --timeout 3600 -- python Scripts/SpaceCenter/run_restoration_editor.py integrate --tag <新标签> --manifest Saved/SpaceCenter/Phase7C/ManifestRuns/Phase7CManifest02/Manifest.json
```

分组验收入口：`Scripts/Extraction/run_phase7c_acceptance.py`，依次执行 `packages`、`rules`、`streaming`、`regression`、
`performance`，每组使用新的 `--tag`。双客户端流送测试会让实际渲染的角色执行往返，各 10 次；其余客户端是否 NullRHI 由运行记录明确标注。

## 性能结果与验证边界

`Phase7CPerformance01/Performance.json` 的 `target_passed=true`。硬件为 Ryzen 5 7500F、RTX 4060 Ti；使用 UE 5.8.3 Development、D3D12/SM6、原生 1920×1080、Medium，关闭 VSync 和帧率上限。单个渲染客户端与一个 NullRHI 客户端、专用服务器同机运行；环境路线没有 AI。

| 路线与轮次 | 有效采样时长（秒） | 帧耗时 P95（ms） | 最大帧耗时（ms） |
| --- | ---: | ---: | ---: |
| 老区 1 | 189.900 | 11.9687 | 37.2952 |
| 老区 2 | 190.234 | 12.2358 | 32.5525 |
| 老区 3 | 189.980 | 12.2142 | 106.2559 |
| 北区 1 | 185.843 | 12.2238 | 34.7262 |
| 北区 2 | 186.006 | 12.1759 | 50.1683 |
| 北区 3 | 185.723 | 12.0033 | 40.3252 |

六轮均达到不少于 180 秒、P95≤16.7 ms 的目标。老区第三轮在 44.783 秒和 164.794 秒处分别出现 106.256 ms、103.105 ms 的帧；这些尖峰均在 North 流送事件前后 2 秒窗口之外，不能概括为没有卡顿。六轮流送事件窗口内均没有超过 100 ms 的渲染帧。服务器 CSV 中的尖峰也原样保留，包括部分捕获末尾的 720 ms 记录。

首次往返单独记录，未清空操作系统缓存：老区 38.376 秒，P95 13.1525 ms、最大 29.0376 ms；北区 62.335 秒，P95 13.4552 ms、最大 44.3926 ms。上述首次路线未出现超过 100 ms 的渲染帧。

| 资源峰值 | 老区 | 北区 |
| --- | ---: | ---: |
| 渲染客户端物理内存（MiB） | 3335.61 | 3364.42 |
| NullRHI 客户端物理内存（MiB） | 729.07 | 728.92 |
| 服务器物理内存（MiB） | 677.59 | 678.02 |
| GPU LocalUsedMB 计数器峰值 | 3245.22 | 3311.72 |

性能采样期间系统可用 RAM 最低约 8.01 GiB，未触发资源保护。

六玩家、四 AI 普通及延迟战斗另有功能压力采样，渲染客户端限制为 30 fps；GPU 帧耗时 P95 分别为 11.8281 ms、11.8153 ms。两次采样最大渲染帧耗时分别为 297.4864 ms、535.7447 ms，均保留在报告中，不能作为六个渲染客户端稳定 60 fps 的证明。功能验证仅覆盖同机 Development/OnlineSubsystemNull 与注入延迟、丢包的测试条件，未验证 Shipping、互联网部署或容量上限。UE 字节速率计数器未启用，报告中的不可用值不代表零网络流量。

## 资源限制与失败记录

重任务串行，临时文件放 D 盘。启动要求 C≥40 GiB、D≥100 GiB、可用 RAM≥8 GiB；运行中 C<25 GiB、D<60 GiB，或可用 RAM<3 GiB 持续 15
秒即停止本任务进程树。没有关闭用户 Rider、调整系统分页或删除历史证据。

`Phase7CCapture04`、`Phase7CPackages01` 和 `Phase7CPackages03` 曾触发内存保护，属于失败记录。后续截图通过编译收尾和垃圾回收完成；C++
编译降为单任务，Cook 设置提前回收阈值。`Phase7CPackages02` 因中断留下的零字节构建清单失败；只移除此空清单后由 UBT
重新生成。最终完整打包结果为 `Phase7CPackages04`。

`Phase7CStreaming01-two-rendered-b` 在完成第 6 轮后中断，未产生最终成功结果，不能计入验收。A
轮次所记录的地图、测试驱动和路线输入哈希经核对保持一致，允许复用；B 的 10 轮及后续六客户端场景使用新的 `Phase7CStreaming02`
标签完整通过。最终归档逐个验证所选轮次，不能仅依据分组的最后退出状态。

`Phase7CRegression01-equipment` 暴露旧测试射击坐标仍依赖已移除的占位地面：射手从 Z=180 cm 下落至约 -3635 cm，头盔截图和枪口日志记录了下落中的向下射击，实际命中右肩。装备射击夹具已平移到通过实走的室内地面，保留双方间距与全部伤害、弹药、创伤断言，并在射击前等待双方落地。仅调整测试夹具，未修改地图、生产伤害或物品规则；该失败记录保留，装备流程完整重跑。

`Phase7CRegression02-equipment` 的伤害与医疗断言已通过，但远处测试 AI 沿真实导航返回，在撤离点击杀了护甲携带者。装备对比现在先证明四 AI 存活和巡逻可达，再通过已有测试夹具解除其武装；只移远或清空弹匣不足以隔离，AI 可返回并使用备弹换弹。独立普通/延迟北区战斗及 PVPVE 仍使用完整武装 AI，其通过结果不依赖这一隔离。

双客户端分区往返、六客户端与四 AI 的普通/延迟场景、装备与红点、钥匙房、战利品、撤离及持久化回归、正式地图最终冷启动复核与固定截图，以及上述六轮性能采样均已通过。归档程序逐项核对成功案例、当前输入哈希、正式地图哈希和截图审阅结果；大日志采用可逐字节还原的 gzip，归档索引记录每个文件的 SHA-256。

## 交付与证据入口

正式地图为 `/Game/Maps/SpaceCenter/L_SpaceCenterExtraction`，配套 `/Game/Maps/SpaceCenter/L_SpaceCenterNorth`。独立预览保留在 `/Game/Maps/SpaceCenter/L_SpaceCenterRestorationPreview`。

- [验收总表与所选运行](Phase7CEvidence/Summary.json)：明确列出采用的 A/B 流送、多人回归、打包、规则及性能记录。
- [最终核对](Phase7CValidation.json) 与 [归档索引](Phase7CEvidence/ArchiveIndex.json)：2,512 个索引文件，439,776,924 字节（约 419.40 MiB，另加索引本身）。索引 SHA-256 为 `f3da9f9904be7da1c67f033b03edf4f335f0d3017b07bf07d3e800be9d905537`。
- [原生资产哈希](Phase7CEvidence/NativeAssets.json)、[Development 包哈希](Phase7CEvidence/PackagedArtifacts.json)：对应当前重建资产与验收使用的包。
- [14 张截图审阅记录](Phase7CEvidence/files/Saved/SpaceCenter/Phase7C/EditorRuns/Phase7CIntegratedCapture01/VisualReview.json)：包括 [总体外观](Phase7CEvidence/files/Saved/SpaceCenter/Phase7C/EditorRuns/Phase7CIntegratedCapture01/overview.png)、[主楼室内](Phase7CEvidence/files/Saved/SpaceCenter/Phase7C/EditorRuns/Phase7CIntegratedCapture01/main_interior.png)、[东区建筑群](Phase7CEvidence/files/Saved/SpaceCenter/Phase7C/EditorRuns/Phase7CIntegratedCapture01/east_district.png) 和 [原始地形接缝](Phase7CEvidence/files/Saved/SpaceCenter/Phase7C/EditorRuns/Phase7CIntegratedCapture01/terrain_seam_native.png)。
- [完整性能报告（gzip）](Phase7CEvidence/files/Saved/Extraction/Phase7CPerformance01/Performance.json.gz)：保留全部采样、首次往返、内存、显存和尖峰；对应 CSV 与日志存放在相邻运行目录。
- 首次接入的地图备份保存在 `Phase7CEvidence/files/Saved/SpaceCenter/Phase7C/EditorRuns/Phase7CIntegrate01/OriginalMaps`，大地图按索引记录使用 gzip 保存原始字节。

本次实现保持未提交、未推送；未清理历史证据或原始 ZIP。
