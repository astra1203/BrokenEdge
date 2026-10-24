"""Keep selected proof without credentials, profiles or full runtime snapshots."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--section',choices=['UI','Regression'],required=True)
parser.add_argument('runs',nargs='+')
args=parser.parse_args()
out=root/'Docs/Extraction/RestorationEvidence'/args.section
out.mkdir(parents=True,exist_ok=True)
records=[]
for tag in args.runs:
    run=root/'Saved/Extraction'/tag
    report_path=run/'Report.json'
    report=json.loads(report_path.read_text(encoding='utf-8'))
    assert report['result']=='passed',tag
    record=dict(run=tag,result=report['result'],options=report['options'],
                checks=[c['name'] for c in report['checks']],
                report_sha256=hashlib.sha256(report_path.read_bytes()).hexdigest(),screenshots=[])
    for source in sorted(run.glob('A-*.png')):
        if args.section=='Regression' and not source.stem.startswith('A-knife-'):continue
        target=out/(tag+'-'+source.name)
        shutil.copy2(source,target)
        record['screenshots'].append(dict(file=target.name,sha256=hashlib.sha256(source.read_bytes()).hexdigest()))
    records.append(record)
(out/'Acceptance.json').write_text(json.dumps(records,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('ARCHIVED',args.section,len(records))
