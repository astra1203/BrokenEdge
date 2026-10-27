"""探查 P_Clouds 资产：类型、发射器、暴露参数、依赖是否齐全。

只读，不改任何资产。用 UnrealEditor-Cmd -run=pythonscript 跑，
输出带 CLOUDS_ 前缀，方便 grep。
"""
import unreal

SYSTEM_PATH = "/Game/Lightning_Electricity_Pack/Particles/P_Clouds"


def log(msg):
    unreal.log("CLOUDS_ {0}".format(msg))


asset = unreal.EditorAssetLibrary.load_asset(SYSTEM_PATH)
if asset is None:
    log("LOAD_FAILED")
else:
    log("CLASS={0}".format(asset.get_class().get_name()))
    log("NAME={0}".format(asset.get_name()))

    # 是不是 NiagaraSystem，是的话把发射器逐个列出来
    if isinstance(asset, unreal.NiagaraSystem):
        try:
            handles = asset.get_emitter_handles()
            log("EMITTER_COUNT={0}".format(len(handles)))
            for h in handles:
                try:
                    name = h.get_name()
                except Exception as exc:
                    name = "?{0}".format(exc)
                enabled = "?"
                try:
                    enabled = h.get_editor_property("enabled")
                except Exception:
                    pass
                log("EMITTER name={0} enabled={1}".format(name, enabled))
        except Exception as exc:
            log("EMITTER_ERR={0}".format(exc))

        for prop in ("warmup_time", "warmup_tick_delta", "b_fixed_bounds",
                     "fixed_bounds", "b_override_system_defaults"):
            try:
                log("PROP {0}={1}".format(prop, asset.get_editor_property(prop)))
            except Exception as exc:
                log("PROP {0} ERR {1}".format(prop, exc))

        # 暴露给蓝图的用户参数，决定我们能不能从 C++ 调半径/浓度
        try:
            store = asset.get_exposed_parameters()
            for p in store.get_editor_property("parameters"):
                log("USERPARAM {0} type={1}".format(
                    p.get_editor_property("name"),
                    p.get_editor_property("type").get_class().get_name()))
        except Exception as exc:
            log("USERPARAM_ERR={0}".format(exc))

    # 依赖清单，用来确认材质、贴图、模块都搬全了
    try:
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        pkg = "/Game/Lightning_Electricity_Pack/Particles/P_Clouds"
        options = unreal.AssetRegistryDependencyOptions(
            include_soft_package_references=True,
            include_hard_package_references=True,
            include_searchable_names=False,
            include_soft_management_references=False,
            include_hard_management_references=False)
        deps = registry.get_dependencies(unreal.Name(pkg), options)
        missing = []
        for d in deps:
            path = str(d)
            if not unreal.EditorAssetLibrary.does_asset_exist(path):
                missing.append(path)
        log("DEP_COUNT={0} MISSING={1}".format(len(deps), len(missing)))
        for m in missing[:20]:
            log("MISSING {0}".format(m))
    except Exception as exc:
        log("DEP_ERR={0}".format(exc))

log("CLOUDS_DONE")
