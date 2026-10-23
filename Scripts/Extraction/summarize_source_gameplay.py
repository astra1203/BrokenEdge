"""Record authored gameplay anchors without treating POIs or guessed grouping as spawns."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[2]
source = root/'Saved/Extraction/SpaceCenterExpansion01/OriginalGameplay/Decoded/Packages.json'
packages = json.loads(source.read_text(encoding='utf-8'))
origin = (664000,-451000,-22400)
result = {'schema':1,'decoded_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
          'source_origin':origin,'status':'authored_anchors_only_not_runtime_approved','variants':[]}
for package in packages:
    if 'GameplayConfig' not in package['package']: continue
    objects = {o['export_index']:o for o in package['objects']}
    variant = {'package':package['package'],'spawns':[],'exits':[],'exit_triggers':[]}
    for obj in objects.values():
        spawn = obj['Type']=='PlayerStart'
        if not spawn and not obj['Type'].startswith('BP_PlayerExit'): continue
        props = obj.get('Properties',{})
        path = props['RootComponent']['ObjectPath']
        component = objects[int(path.rsplit('.',1)[-1])]['Properties']
        assert not component.get('AttachParent'),obj['Name']
        p = component['RelativeLocation']
        row = {'actor':obj['Name'],'source_position':[p[k] for k in ('X','Y','Z')],
               'world_position':[p[k]-o for k,o in zip(('X','Y','Z'),origin)],
               'rotation':component.get('RelativeRotation',{}),'export_index':obj['export_index']}
        if not spawn:
            row['authored_overrides'] = {k:v for k,v in props.items() if not isinstance(v,(dict,list))}
            row['authored_rules'] = {k:props[k] for k in ('ExitOpenConfig','ExitConditionConfig','ExitCloseConfig','UIConfig') if k in props}
            row['limits']='Blueprint defaults, trigger linkage, eligibility, timing and names require separate resolution'
        variant['spawns' if spawn else 'exits'].append(row)
    for obj in objects.values():
        props = obj.get('Properties',{})
        if not props.get('TargetExit'): continue
        component = objects[int(props['RootComponent']['ObjectPath'].rsplit('.',1)[-1])]['Properties']
        p = component.get('RelativeLocation')
        variant['exit_triggers'].append({'actor':obj['Name'],'export_index':obj['export_index'],
            'target':props['TargetExit'], 'source_position':p,
            'world_position':[p[k]-o for k,o in zip(('X','Y','Z'),origin)] if p else None})
    result['variants'].append(variant)
out = root/'Docs/Extraction/SpaceCenterExpansion/SourceGameplayAnchors.json'
out.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print([(v['package'],len(v['spawns']),len(v['exits'])) for v in result['variants']])
