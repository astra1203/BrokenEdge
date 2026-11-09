# 航天基地小样工具

地图入口：`/Game/Maps/SpaceCenter/L_SpaceCenterSample`。已生成的 UE 资源在 `Content/Maps/SpaceCenter`；直接打开地图即可运行，不需要先解包源资源。玩法沿用蜂医与 AK 基线。

本工具只覆盖 `sample_config.json` 固定的 6 个原始分块和空间范围。格式恢复、人工近似及验收边界见 [转换问题](../../Artifacts/SpaceCenter/ConversionIssues.md) 和 [阶段记录](../../Artifacts/SpaceCenter/Phase1.md)。

## 从原始 ZIP 重建

使用 UE 5.8.3，先核对配置中的引擎、ZIP 目录。PowerShell：

```powershell
$uePython = 'C:/UnrealProjects/UnrealEngine-5.8.3-release/Engine/Binaries/ThirdParty/Python3/Win64/python.exe'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Scripts/SpaceCenter/bootstrap.ps1
& $uePython Scripts/SpaceCenter/run_conversion.py --tag my-conversion-01
& $uePython Scripts/SpaceCenter/run_editor.py import --tag my-import-01
& $uePython Scripts/SpaceCenter/run_editor.py audit --tag my-audit-01
& $uePython Scripts/SpaceCenter/run_editor.py traverse --players 1 --tag my-walk-1p
& $uePython Scripts/SpaceCenter/run_editor.py traverse --players 2 --tag my-walk-2p
```

每条命令成功退出后再执行下一条。标签不能复用；失败应先查看对应目录的 `Report.json`、`Run.json`、`Editor.log` 或转换日志。`bootstrap.ps1` 的执行策略只作用于该子进程。原始 ZIP 只读，完整包路径用于消除同名冲突；解压有路径边界、CRC、SHA-256 和大小预算检查。

缓存位于 `Saved/SpaceCenter`，不进入 Content。转换器采用固定 CUE4Parse 提交和独立 .NET SDK。5 个距离场异常网格使用明确白名单恢复原顶点、索引和材质槽；其他几何失败、未展开实例和未解析材质槽均阻止组装。

重复组装按源对象 ID 更新生成对象；相同 GLB/PNG 的 SHA-256 命中导入缓存。输入内容变化会明确报错，需检查差异后重新生成相应资源。手放对象不清理；需要保护的生成对象 ID 放入 `overrides.json` 的 `preserve_actors`。门的固定开启位移也集中在该文件，避免人工修改被下一次组装覆盖。

## 通路与图像

```powershell
& $uePython Scripts/SpaceCenter/run_editor.py capture --tag my-probes
& $uePython Scripts/SpaceCenter/plan_route.py Saved/SpaceCenter/Runs/my-probes/Probes.json Scripts/SpaceCenter/waypoints.json Scripts/SpaceCenter/route.json
```

`capture` 在真正的 PIE 物理场景中按多层高度采样，输出可站立点和双向胶囊连线阻挡。图搜索只生成候选路线；`traverse` 必须用 Enhanced Input 驱动实际角色验证，失败不会以传送绕过。脚本创建自己的隐藏编辑器进程，不接管用户的编辑器或调试器。

UE Commandlet 不提供这里需要的 PIE 碰撞场景；`audit` 只检查冷加载后的资源、材质、位置、旋转、比例和包围盒，不能替代运行验收。
