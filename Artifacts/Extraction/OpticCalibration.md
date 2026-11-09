# AK 红点安装与瞄准校准

2026-10-05，基线提交 `22e4c71a84536094e7a97116d67120d044387235`。红点资产已修正，720p／1080p
两轮装备流程和观察客户端近景复核通过。[归档索引](OpticCalibrationEvidence/Summary.json)
保存校准、运行和资源记录；[视觉复核](OpticCalibrationEvidence/VisualReview.json)逐项标识实际查看的画面。

## 原因与修正

从实际 `SK_KA47_ViewFit` 导出的 6,178 个顶点、6,813 个三角形测得：安装区域 Y=9..15 cm 的机匣顶面 Z=12.440467 cm，全枪最高机瞄顶点
Z=13.772024 cm。原红点底座 Z=11 cm，陷入机匣约 1.44 cm。

镜片本地高度范围为 Z=0.580842..2.498598 cm。旧实现能把红点对到屏幕中心，但枪体遮住了镜窗；“点居中”不是安装正确的充分条件。

| 参数（枪模型坐标，厘米） | 之前          | 修正后        |
|--------------------------|---------------|---------------|
| 第一／第三人称镜座位置   | `(0,12,11)`   | `(0,12,14)`   |
| 支架中心                 | `(0,12,10.5)` | `(0,12,13.2)` |
| 支架尺寸                 | `3×6×1`       | `3×6×1.6`     |
| ADS 额外垂直平移         | `+1.254147`   | `−1.745596`   |

支架下表面 Z=12.4，接触机匣并嵌入约 0.04 cm；上表面 Z=14，贴合镜座。原模型比例、前后位置、镜片材质和既有第一人称 0.75°
俯仰补偿保留。ADS 平移由原始镜片中心和角色 Aim 变换重新求解。拆镜仍走原机瞄姿态。

正式资产只更新 `/Game/Extraction/Items/DA_Item_RedDot`。`optic_calibration.py` 是独立校准入口和 `prepare_phase6a.py`
共用的参数来源，避免重新准备阶段 6A 时写回旧值。独立入口不导入模型、不重建地图和补给箱。资产重复应用前后 SHA-256 一致。

## 验证

- 独立射线／三角形检查：镜窗中央 60% 区域的 25 条名义 ADS 视线，旧位置 8 条被枪体遮挡，新位置 0 条。它验证静态几何，不代替实机动画检查。
- `OpticNet03`：1280×720，108 项装备流程检查；站立／蹲下各 0°、+35°、−35°，以及拆下重装，共 7 张瞄准画面，最大中心偏差 0.068502
  像素。
- `OpticNet04`：1920×1080，同样 108 项检查、7 张瞄准画面，最大中心偏差 0.160411 像素。两种分辨率均满足不超过 2
  像素的门槛，直接查看画面确认中央镜窗无遮挡。
- 两轮实际装卸、切枪、丢弃／拾取、换弹、服务器实弹及附件身份保存通过。保留原有伤害、散布、消耗和存档规则。动作截图属于采样，切枪截图可能位于提交换枪前，不能据此声称每帧挂载均已穷尽验证。
- `OpticObserver02`：38 项装配／复制检查，单独渲染观察客户端，补拍举枪后的左右侧面及斜上方视角，确认镜座、支架和机匣接触，无明显悬空或穿插。初次握把近景被角色装备遮挡，未用于宣称红点接触正确。

当前验证范围为 UE 5.8.3 编辑器的独立 `-server`／`-game` 进程、同机
OnlineSubsystemNull、独立测试账号；两客户端始终联网，每轮只渲染一个客户端。未渲染的客户端只提供功能状态，不作为骨骼姿态或视觉证据。本次没有修改
C++，没有重新编译引擎、项目或制作新打包版本；现有 `Packaged` 中的旧包仍需重新烘焙才会包含新资产。

两轮装备流程之后仅为 `optic_remote_only` 分支补充了三张观察截图；归档保留与两轮报告 SHA-256 完全一致的原测试脚本，最终观察分支由
`OpticObserver02` 验证。历史阶段 6A／7A／7B 证据未改写，本次未提交或推送。

### 直接画面对比

| 旧位置：镜窗被遮挡                                             | 修正后：720p 水平 ADS                                                |
|----------------------------------------------------------------|----------------------------------------------------------------------|
| ![旧位置](OpticCalibrationEvidence/before/B-equipment-ads.png) | ![修正后](OpticCalibrationEvidence/owner-720/B-optic-standing-0.png) |

![第三人称侧面支架接触](OpticCalibrationEvidence/observer/A-optic-remote-right.png)

## 有界失败记录

- `OpticAudit01`：NullRHI 下 FBX 骨骼模型导出触发 `GetCPUSkinnedVertices` 的 MeshObject 断言；`OpticAudit02`
  使用离屏渲染成功导出，未保存枪械资产。
- `OpticNet01`：两个客户端同时渲染时可用内存降至约 1.8 GiB，资源保护终止了本次测试进程树。此轮不计为通过，也没有关闭共享应用。
- `OpticNet02`：未渲染观察客户端的远端骨骼不刷新，旧握把距离断言超时。验收改为检查服务器姿态，并另外渲染观察客户端做视觉复核。

## 重现

先用 `Scripts/Assert-EditorClosed.ps1` 检查项目占用。以下命令须通过现有 `resource_guard.py` 包装执行，并每次使用新的运行标签：

```text
python Scripts/Extraction/run_editor_task.py optic-assets --tag <新的资产标签>
python Scripts/Extraction/run_network.py --tag <新的720p标签> --scenario equipment --players 2 --optic-validation --resolution 1280x720 --rendered-role B
python Scripts/Extraction/run_network.py --tag <新的1080p标签> --scenario equipment --players 2 --optic-validation --resolution 1920x1080 --rendered-role B
python Scripts/Extraction/run_network.py --tag <新的观察标签> --scenario equipment --players 2 --rendered-role A --optic-remote-only
```

`verify_optic_capture.py` 只读取截图像素，不编辑图片；中心检测与人工镜窗／支架检查必须分别完成。`verify_optic_geometry.py`
接收实际枪械 ASCII FBX、原始红点 GLB 和数值校准报告，保留原坐标轴和单位转换记录。
