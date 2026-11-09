# 阶段 7B：原始地形恢复与建筑群补全

2026-10-05。**7B 尚未完成：原始高度样本已恢复，材质原始数据完整性门槛未通过。**
起点为 `948ce3f71d559e4fd1b58998120e6c4ce074b79e`，目标引擎仍为 UE 5.8.3。
按本轮确认的“原始还原优先”要求，没有用人工补建、灰材质或单层 PBR 代替缺失的原始数据，也没有更新正式地图。

证据入口：[恢复摘要](Phase7BEvidence/Summary.json)、[完整性索引](Phase7BEvidence/ArchiveIndex.json)、
[20 项验证](Phase7BEvidence/Verification.json)、[源文件可用性检查](Phase7BSourceAvailability.json)。
这里的验证通过仅指恢复工具、源数据核对及门槛拒绝正确，不表示本阶段或实机验收通过。

## 确认范围与当前交付

范围为当前六个源分块的建筑群及周边。已有解析记录共 2,036 个组件，1,309 个进入既有地图；
其余 727 个仍需审查、去重和完整性检查。本轮没有把这些候选组件写入 Content。

已增加独立源包探测、材质依赖与原始索引审计、CDLOD 高度恢复、验证和归档工具。
转换器的 inspect 模式新增源导出偏移、长度、编辑器数据过滤标记和未经过 JSON 引用解析的原始表达式索引。
新工具使用 `Saved/SpaceCenter/Phase7B`，原始 ZIP 只读；生产 C++、Content、Config 及 7A 证据未改变。

## 原始高度恢复

从源 `CDLODComponent`、`CDLODAsset_Header` 和两份高精度高度载荷中交叉核对采样布局。
读取原始 BinaryAsset 的内联 bulk header，验证类型、大小、偏移和包尾标记；没有按文件长度猜测高度图尺寸。

| 项目 | 核对结果 |
| --- | --- |
| 候选源范围 | X=652000～681000，Y=-466000～-433000 cm；完整边界块随样本保留 |
| 项目原点 | `(664000,-451000,-22400)` cm |
| 原始四叉树 | 21,845 节点，父子指针、层级、有效采样矩形及范围通过检查 |
| 覆盖载荷 | `CDLODAsset_Heightmap_x5_y2`、`CDLODAsset_Heightmap_x5_y3` |
| 恢复范围 | 110 个 LOD0 高度块，113,313 个唯一采样点 |
| 拼接核对 | 6,477 个重复边界样本的原始高度值与两字节法线数据完全一致 |
| 原始量化步长 | 世界空间约 0.489237 cm |
| 源块极值误差 | 最大约 0.485415 cm，不超过一个量化步长 |
| 诊断 GLB | 225,280 个规则网格三角形；全顶点坐标往返误差不超过 0.000378 cm |

高度使用源组件的 `HeightBiasInLocalSpace=-128`、`HeightScaleInLocalSpace=1.99609375`、
缩放 `(100,100,125)`。解码为 `(RG16大端值-32768)/(HeightScale×128)+HeightBias`，随后应用源缩放和项目原点。
所有选中块的解码极值均位于独立源四叉树极值的“向下一个量化步长至原值”区间，支持源数据向下量化的解释。
首次探测错误地使用半量化步长阈值而被拒绝；失败日志保留，修正依据是全部源块极值，并非删除不合格块。

产物：[高度恢复报告](Phase7BEvidence/terrain/Report.json)、
[原始采样及索引](Phase7BEvidence/terrain/TerrainSamples.json.gz)、
[诊断 GLB](Phase7BEvidence/terrain/TerrainMesh.glb)。两次最终恢复的上述数据和报告字节完全一致。

**边界：**GLB 使用明确记录的规则网格对角线，仅供检查高度形状；原引擎的 LOD 拼接、三角形生成、
原始法线着色和地形材质尚未恢复。原始法线字节保留，没有猜测其解码规则。
该文件没有导入正式关卡，碰撞、导航、通行和原版渲染结果尚未验收。

## 材质门槛：源包已裁剪关键节点

从现有区域材质与地形材质的 567 个根引用出发，解析 746 个依赖包、3,378 个对象，
涉及 37 个母材质、134 个材质函数。依赖包解析本身通过，没有把可读的参数名称当成完整计算图。

**171 个母材质／函数的表达式列表存在原始零索引，共 5,091 项。**
这些不是 JSON 序列化器解析引用失败：工具按源导出偏移和长度直接读取 `.uexp`，
将数组数量及每一个 int32 索引与解析结果逐字节比对，171 组均一致。
对应包标记为过滤编辑器数据；函数输入、输出和部分运算节点没有保留在导出表中。

| 源对象 | 表达式槽位 | 原始零索引 |
| --- | ---: | ---: |
| `M_SW_NRMAC_Master` | 22 | 5 |
| `MF_Basic_MatLayer` | 50 | 37 |
| `MF_HD_Data_BNMRA_Matlayer` | 50 | 31 |
| `LandscapeDemo0Mat_NoDistanceBlend_New_HD` | 406 | 235 |

详情：[门槛报告](Phase7BEvidence/materials/Report.json)、
[原始索引字节证据](Phase7BEvidence/materials/OriginalIndexArrays.json)、
[保留下来的依赖及对象数据](Phase7BEvidence/materials/Graph.json.gz)。
初次探测记录中的 `graph_recovered` 仅代表依赖解析，当时尚未加入连通性门槛，不能作为完整图恢复结论；最终记录为 `blocked`。

地形引用的 `CMO_Iris_LandscapeTint_HD` 在已配置 ZIP 索引中也不存在。
还检索了 58 份原始 ZIP 的全部 1,620,926 个中央目录条目（包含 58 份归档清单）：
找到一份 `.uproject` 配置和四项编译着色器数据，没有 `.usf/.ush/.hlsl/.glsl/.cpp/.h` 源码。
抽查的三个关键材质／函数在这些归档中没有同名第二份版本。此检查仅覆盖已配置源目录，不代表扫描了用户全部磁盘。

缺失节点无法通过参数名、残留 ExpressionName 或编译着色器文件直接证明 UE 5.8 材质等价。
当前没有可验证的完整原始材质图，因此没有生成猜测的材质并放行后续场景替换。

## 验证、资源与剩余工作

转换器构建通过；最终 20 项恢复校验通过，覆盖全部输出顶点、重复性、原始节点索引、
被截断头数据、越界四叉树指针拒绝，以及生产资产和旧证据保持不变。
材质工具以退出码 2 表示数据门槛被阻塞；资源包装器据此记录失败，不能把该记录写成恢复成功。
完整证据索引包含 53 个文件，约 9.6 MiB；大型 JSON 和日志采用无损 gzip。

本轮未运行 UE 编译、Client／Server 打包、PIE、联机或性能验收；它们依赖原始数据门槛通过后的正式实现。
没有改动原用户编辑器进程、系统分页配置或历史备份。运行源恢复任务时沿用资源保护和 D 盘工作目录。
本次提交保存源恢复成果与材质阻塞证据；归档中的基线提交和暂存状态保留验证时快照。

继续完成 7B 需要未裁剪的原始材质／函数图及缺失依赖，或可验证等价的原版着色器源码。
获得数据后应先重跑材质门槛，再继续原生预览、建筑群补全、多个区域的流送改造和完整运行验收；
届时保留本轮证据为历史记录，使用新的运行标签与验收归档。

## 复现入口

使用 Python 3.12。所有重任务通过 `Scripts/Extraction/resource_guard.py` 包装执行，传入新的标签。

```text
Scripts/SpaceCenter/phase7b_source.py --tag <新标签>
Scripts/SpaceCenter/recover_phase7b_materials.py --tag <新标签>
Scripts/SpaceCenter/recover_phase7b_terrain.py --tag <新标签> --source-probe <包含CDLOD组件的源探测目录>
Scripts/SpaceCenter/verify_phase7b_recovery.py --tag <新标签> --terrain-tag <高度恢复标签> --repeat-tag <重复恢复标签> --materials-tag <材质审计标签>
```

`archive_phase7b_recovery.py` 固定归档本轮最终标签，目标目录已存在时拒绝覆盖。
恢复工具不会自动导入资产或绕过材质门槛；现有版本的完整玩法结果仍以 7A 报告为准。
