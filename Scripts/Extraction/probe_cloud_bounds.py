"""量出 P_Clouds 的自然尺寸，并看看能不能碰到发射器和渲染器。

"太散太淡"最可能的原因是缩放系数算反了：云团自然尺度若是几十米，
用 Radius/100 去放大会得到一大片稀薄白雾。这里把真实包围盒读出来。
"""
import unreal

PATH = '/Game/Lightning_Electricity_Pack/Particles/P_Clouds.P_Clouds'
system = unreal.EditorAssetLibrary.load_asset(PATH)
if not system:
    unreal.log('BOUND_ERR asset missing')
else:
    unreal.log('BOUND_CLASS {0}'.format(system.get_class().get_name()))

    # 固定包围盒：作者如果设了 FixedBounds，这就是云的真实尺寸。
    for prop in ('fixed_bounds', 'FixedBounds', 'b_fixed_bounds'):
        try:
            value = system.get_editor_property(prop)
            unreal.log('BOUND_PROP {0} = {1}'.format(prop, value))
            for attr in ('origin', 'box_extent', 'sphere_radius'):
                try:
                    unreal.log('BOUND_ATTR {0}.{1} = {2}'.format(prop, attr, getattr(value, attr)))
                except Exception as exc:
                    unreal.log('BOUND_ATTR_ERR {0}.{1} {2}'.format(prop, attr, exc))
        except Exception as exc:
            unreal.log('BOUND_PROP_ERR {0} {1}'.format(prop, exc))

    # 是否暴露了用户参数（决定能不能从 C++ 调尺寸/浓度）
    try:
        exposed = system.get_exposed_parameters()
        unreal.log('BOUND_EXPOSED {0}'.format([str(p) for p in exposed.parameters]))
    except Exception as exc:
        unreal.log('BOUND_EXPOSED_ERR {0}'.format(exc))

    # 发射器与渲染器：拿到材质和精灵尺寸，才知道密度天花板在哪。
    try:
        handles = system.get_editor_property('emitter_handles')
        unreal.log('BOUND_HANDLES {0}'.format(len(handles)))
        for handle in handles:
            unreal.log('BOUND_HANDLE {0}'.format(handle.get_name() if hasattr(handle, 'get_name') else handle))
            try:
                emitter = handle.get_instance()
            except Exception as exc:
                unreal.log('BOUND_INSTANCE_ERR {0}'.format(exc))
                continue
            for prop in ('renderer_properties', 'RendererProperties'):
                try:
                    renderers = emitter.get_editor_property(prop)
                    unreal.log('BOUND_RENDERERS {0} = {1}'.format(prop, len(renderers)))
                    for renderer in renderers:
                        unreal.log('BOUND_RENDERER {0}'.format(renderer.get_class().get_name()))
                        for mat_prop in ('material', 'Material'):
                            try:
                                mat = renderer.get_editor_property(mat_prop)
                                unreal.log('BOUND_MATERIAL {0} -> {1}'.format(
                                    mat_prop, mat.get_path_name() if mat else 'None'))
                            except Exception as exc:
                                unreal.log('BOUND_MATERIAL_ERR {0} {1}'.format(mat_prop, exc))
                except Exception as exc:
                    unreal.log('BOUND_RENDERERS_ERR {0} {1}'.format(prop, exc))
    except Exception as exc:
        unreal.log('BOUND_HANDLES_ERR {0}'.format(exc))

unreal.log('BOUND_DONE')
