# 57–67：背包、武器表现和地图交互

2026-11-06：57–67 已完成并分批提交推送。最终 Win64 Development 客户端/专服测试包位于 `Packaged/Fix5767Release`，构建源码提交为 `8b5543ec924cb5ac4035e8ba432529fc993b031c`。最终包通过 86 项背包/武器检查和 20 项苏联搜刮撤离检查。资源、日志、截图及校验清单见 [Final](Phase7Fix5767Evidence/Final)。

## 完成内容

| 编号 | 最终行为 | 验证 |
|---|---|---|
| 57 | 拖到已有物品上时交换；同类先堆叠，部分合并余量留在原位。被换物品先回原位置，再在来源分区找空位并尝试旋转；失败完整回滚，不挤动第三件物品 | 原生事务测试；真实 Slate 鼠标拖拽、请求重放、身份/弹匣/配件/第三件物品保持 |
| 58 | 霰弹枪独立配置第一人称变换和握点，枪口朝前 | HIP/ADS 画面、开火换弹、左右手距握点小于 2cm、多端枪口方向 |
| 59 | 四臂准星体现移动/姿态和开火散布；每次开火至少显示一帧脉冲，再按指数衰减；开镜隐藏 | 1080p 原始逐帧 PNG 检测，两把枪均为约 7.5 → 18.5 → 7.5px；包括截图写盘造成的长帧 |
| 60 | 移除角色与移动组件的环境自动蹲伏路径 | 17 扇门双向共 34 次站立通行、三架梯子往返站立、手动蹲起；倒地/手动趴下保留各自姿态规则 |
| 61 | 开局胸挂，始终具有五个独立 1×1 口袋 | 五格放入；长枪跨格和第六格拒绝且库存不变；重复授予开局装备不复制物品 |
| 62 | 苏联基地 196 块、航天基地 1570 块普通玻璃可被子弹、近战和爆炸打碎，保留窗框，服务器同步本局损坏 ID | 1766 个唯一 GUID 重载；专服、拥有者和观察者碰撞/显示一致；楼板继续遮挡爆炸 |
| 63 | 中央圆形楼北、南、西侧三架梯子，复用航天基地控制 | W 靠近/上爬、松键停留、顶部站立离开、顶部进入/S 下爬、空格蹬离均通过 |
| 64 | 移除中下方永久重复快捷键条 | 已检查实际画面；保留侧边、背包内提示及临时交互提示 |
| 65 | 导入 332 个原素材模型，更新 340 个定义；其余沿用具体模型。修复 Nanite 材质父级，旧附件等 12 个定义使用独立网格副本 | 367 个物品重载、363 个 Nanite 材质槽审计；头盔/护甲/背包/手雷真实丢弃；两图模型画面与日志 |
| 66 | 长枪独立设置世界手持/背负变换，与第一人称方向统一 | 服务器、拥有者、观察者方向一致；完整人物及地面影子画面检查 |
| 67 | 开局两颗可用破片手雷，优先胸挂、其次背包；复用已有实例，安全箱内手雷不计入可用数量 | 重试不复制；真实 Alt+G 消耗一颗，投掷模型和爆炸破窗同步 |

## 构建和打包回归

| 包/运行 | 结果 | 证据 |
|---|---|---|
| 首轮 `Packaged/Fix5767` | 客户端和专服构建通过；随后发现模型材质回退，不作为最终材质验收 | [Checkpoint03](Phase7Fix5767Evidence/Checkpoint03) |
| `Fix5767PackInventoryLoadout02` | 87 项背包/长枪检查通过 | 同上 |
| `Fix5767PackLaddersGlass01` | 75 项梯子/玻璃/掉落模型检查通过 | 同上 |
| `Fix5767PackDoors01` | 151 项门检查通过，34 次穿门记录均保持站立 | [StandingDoorAudit.json](Phase7Fix5767Evidence/Checkpoint03/Fix5767PackDoors01/StandingDoorAudit.json) |
| `Fix5767PackSpaceLoot01` | 18 项：原生容器搜索、转移、真实 F 拉两处电闸、两人撤离返仓、物品 GUID/数量持久化 | [Checkpoint03](Phase7Fix5767Evidence/Checkpoint03/Fix5767PackSpaceLoot01) |
| 材质修复包 `Packaged/Fix5767Final` | 客户端/专服构建通过；苏联玻璃/模型 35 项、航天 32 项通过；两图新增 WorldModels 材质缺失标记错误为 0 | [Final](Phase7Fix5767Evidence/Final) |
| `Fix5767ReticleEditor02` | Editor 编译后 76 项多人检查通过，逐帧证明双枪准星扩散/恢复 | [Checkpoint04](Phase7Fix5767Evidence/Checkpoint04) |
| 最终 `Packaged/Fix5767Release` | Client03、Server03 构建通过；源码为 `8b5543ec9` | [PackageManifest.json](Phase7Fix5767Evidence/Final/PackageManifest.json) |
| `Fix5767ReleaseReticle01` | 86 项：实际鼠标交换、五口袋、开局装备、双枪动作、开火/换弹及准星原始画面检测通过 | [ReticlePixels.json](Phase7Fix5767Evidence/Final/Fix5767ReleaseReticle01/ReticlePixels.json) |
| `Fix5767ReleaseSovietLoot01` | 20 项：原生容器搜索/转移、两人真实撤离倒计时、返大厅、物品 GUID/数量保存在仓库 | [Final](Phase7Fix5767Evidence/Final/Fix5767ReleaseSovietLoot01) |

原生测试：`Fix5767InventoryTests01` 19 项通过；`Fix5767Unit02` 16 项通过；修复重复窗格后的 `Fix5767GlassUnit03` 1 项通过。`Fix5767Audit04` 独立进程重载 367 个定义和 1766 块玻璃通过。记录分别位于 [Checkpoint01](Phase7Fix5767Evidence/Checkpoint01)、[Checkpoint02](Phase7Fix5767Evidence/Checkpoint02)、[Checkpoint03](Phase7Fix5767Evidence/Checkpoint03)。

## 资源与复现

三架梯子底部约为北 `(500,1463,802)`、南 `(-500,-1463,802)`、西 `(-1463,500,802)` 厘米，屋顶 Z=1350，实际攀爬高差约 5.48 米。位置按墙面碰撞、角色胶囊和顶部退出路径验证。

原素材 `D:\司晨\源代码和素材\搜打撤游戏资产` 保持不变，项目模型位于 `/Game/Extraction/Items/WorldModels`。缺少原始模型的诺翁芯片使用 CPU 芯片，通用电子元件使用电路板，通用钥匙使用已有钥匙卡，安全箱使用带材质保险箱的便携缩放版本。源路径/哈希、选取映射和导入报告已归档。

项目根目录运行时设置 `UE_ENGINE_ROOT=C:\UnrealProjects\UnrealEngine-5.8.3-release`，使用新标签避免覆盖证据。脚本为 [run_fixes5767.py](../../Scripts/Extraction/run_fixes5767.py)、[run_glass_models.py](../../Scripts/Extraction/run_glass_models.py)、[run_loot_extract.py](../../Scripts/Extraction/run_loot_extract.py)。共同参数：

```text
--tag <唯一新标签> --scenario soviet --players 2 --guards 0 --rendered-clients 1 --bounded-rendering --isolated-room --packaged --package-root Packaged/Fix5767Release --port 27917 --resolution 1920x1080
```

背包/武器使用 `run_fixes5767.py --fix-focus inventory-loadout`，梯子/玻璃使用 `--fix-focus ladders-glass`。航天基地使用 `run_glass_models.py --scenario scar` 或 `run_loot_extract.py --scenario scar`，替换共同参数中的 scenario。

## 证据边界与修复过程

多人运行使用专服、一个 1080p 有画面的拥有者和一个 NullRHI 观察客户端，采用有界渲染。使用明确记录的位置夹具，不代表全地图连续徒步、最高画质或性能验收。开局装备、容器搜索和撤离结算没有注入物品；模型专项另有三类明确标记的收藏品外观夹具。玻璃重载/新对局由原生测试覆盖，没有声称验证政策不允许的中途加入对局。

门/梯子回归后，后续资源改动仅修复物品材质；最后 C++ 改动仅为 CombatComponent 的准星衰减。表格保留各包与运行的对应关系，不把早期检查称作最后包重复实测。最终包校验清单包含可执行文件、pak、utoc、ucas 的 SHA256、构建输入及运行日志审计。

保留的失败记录及处理：

- 模型首次导入完成映射，但源码管理插件对新目录产生 996 条报错；关闭插件后的独立资产重载通过。
- 苏联窗户蓝图重新生成 68 块重叠玻璃；按网格和变换关闭旧窗格碰撞/显示，清除重复烘焙窗框，保留原窗框。
- 爆炸破窗被散落 M4A1 的拾取交互盒阻挡；运行日志确认命中组件后，爆炸改为忽略散落物，实体容器/墙/楼板仍遮挡。调试器附加但源断点未绑定，没有取得该次断点局部变量证据。
- 原 5 秒开镜输入在截图/报告写盘时超时，改为明确保持并释放；旧单张开火截图错过峰值，改为引擎逐帧记录。
- 逐帧写盘造成大于 125ms 的帧，原插值在绘制前清零准星。现保留新开火一帧并指数衰减，Editor 和最终包的原始 PNG 均确认扩散及恢复。
- Interchange 共用材质缺少 Nanite 标记，且设置材质实例父级不会标脏；改用项目内父级并强制保存，再独立重载和打包验证。

构建仍包含已有 UI/语音 NodeGuid、未使用 Muriel 动画/重定向资源等警告；早期弹道测试有 3 条 `World has no context` 警告，不能称为零警告构建或测试。

用户新增的 `Docs/复刻三角洲行动的需求.txt`、`Docs/复刻三角洲行动的需求2.txt` 已随 `2408f249c` 原样提交。全部实现分批推送到 `github/master`；本地 `.uproject` 引擎关联改动保留在工作区，没有修改引擎源码。
