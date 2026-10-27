"""探查 Niagara 在 Python 侧暴露到什么程度。

决定"能不能改 P_Clouds 的发射器材质/参数"，还是只能靠组件缩放和叠层。
"""
import unreal

for cls in (unreal.NiagaraSystem, unreal.NiagaraComponent, unreal.NiagaraEmitter):
    names = [n for n in dir(cls) if not n.startswith('_')]
    picked = [n for n in names if any(k in n.lower() for k in
              ('emitter', 'material', 'renderer', 'param', 'compile', 'script'))]
    unreal.log('API {0}: {1}'.format(cls.__name__, picked))

# M_Cloud 暴露的标量参数，决定能不能用材质实例压暗/加浓
mat = unreal.EditorAssetLibrary.load_asset('/Game/Lightning_Electricity_Pack/Materials/M_Cloud')
unreal.log('MAT_CLASS {0}'.format(mat.get_class().get_name()))
try:
    lib = unreal.MaterialEditingLibrary
    names = [n for n in dir(lib) if 'parameter' in n.lower()]
    unreal.log('MATLIB {0}'.format(names))
except Exception as exc:
    unreal.log('MATLIB_ERR {0}'.format(exc))

# 项目里现成的厚烟材质，作为替代方案候选
for path in ('/Game/ParagonMuriel/FX/Materials/Smoke/M_ThickSmoke_MASTER',
             '/Game/Extraction/Abilities/M_ExtractionSmoke'):
    a = unreal.EditorAssetLibrary.load_asset(path)
    unreal.log('CAND {0} -> {1}'.format(path, a.get_class().get_name() if a else 'None'))

unreal.log('API_DONE')
