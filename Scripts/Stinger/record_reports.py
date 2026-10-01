"""归档导入证据；在普通 Python 中运行，不启动 UE、不重导入资产。"""
from pathlib import Path
from fractions import Fraction
from collections import Counter
import json

project = Path(__file__).resolve().parents[2]
saved = project/'Saved'/'Stinger'
docs = project/'Docs'/'Stinger'
docs.mkdir(parents=True,exist_ok=True)
manifest = json.loads((saved/'import_manifest.json').read_text(encoding='utf-8'))
for entry in manifest['entries'].values():
    for sequence in entry.get('sequences',[]):
        fps, frames = sequence['fps'], sequence['frames']
        numerator = 65536
        while (frames-1)*round(numerator/fps)>=2**24: numerator//=2
        rate = Fraction(numerator,max(1,round(numerator/fps)))
        sequence.update(imported_fps=float(rate),source_duration=max(0,frames-1)/fps,
                        imported_duration=max(1,frames-1)/float(rate))
(docs/'ImportManifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
summary = {'files':len(manifest['entries']),
           'statuses':dict(Counter(e['status'] for e in manifest['entries'].values())),
           'groups':dict(Counter(p.split('/')[0] for p in manifest['entries'])),
           'sequences':sum(len(e.get('assets',[])) for e in manifest['entries'].values()),
           'files_with_missing_bones':sum(bool(e.get('missing_bones')) for e in manifest['entries'].values()),
           'max_fps_relative_error':max(abs(s['imported_fps']/s['fps']-1) for e in manifest['entries'].values() for s in e.get('sequences',[]))}
(docs/'ImportSummary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
for source,dest in [('asset_validation.json','AssetValidation.json'),('pie_validation.json','PIEValidation.json'),('visual_validation.json','VisualValidation.json'),('level_validation.json','LevelValidation.json')]:
    if (saved/source).exists(): (docs/dest).write_bytes((saved/source).read_bytes())
print(json.dumps(summary,ensure_ascii=False,indent=2))
