"""离线比较 PSA 参考记录、首帧键与 UE 导入的骨骼参考姿态。"""
import ast,struct,json,re,math
from pathlib import Path
source=Path(__file__).with_name('import_assets.py').read_text(encoding='utf-8')
reader=next(n for n in ast.parse(source).body if isinstance(n,ast.FunctionDef) and n.name=='read_psa')
exec(compile(ast.Module(body=[reader],type_ignores=[]),'<psa reader>','exec'))
path=next(Path(r'D:\司晨\源代码和素材\蜂医的人物模型和相关动画资产').rglob('AR_M_1P_Idle01.psa'))
data,chunks,bones,sequences=read_psa(path)
reference=json.loads(Path('Saved/Stinger/inspect.json').read_text())['reference_pose']
for name in ['Root','Hips','Spine','RightShoulder','RightArm','RightForeArm','RightHand']:
    index=next(i for i,b in enumerate(bones) if b[0]==name)
    bone=bones[index]
    values=struct.unpack_from('<8f',data,chunks['ANIMKEYS'][0]+index*32)
    qx,qy,qz,qw=values[3:7]
    ref=[float(v) for v in re.search(r'rotation: \{x: (.*?), y: (.*?), z: (.*?), w: (.*?)\}',reference[name]).groups()]
    q=(-qx,qy,-qz,qw)
    print(name,'PSAref',bone[2:],'KEY',values[:7],'UEref',ref,'DOT',abs(sum(a*b for a,b in zip(q,ref))))
