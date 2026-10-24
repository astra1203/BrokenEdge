"""Keep selected proof without credentials, profiles or full runtime snapshots."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--section',choices=['UI','Regression'],required=True)
parser.add_argument('--failed',action='append',default=[],help='Retain a failed report summary separately; never count it as passed')
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
    images=list(run.glob('A-*.png'))
    if args.section=='Regression':images+=list(run.glob('B-knife-*.png'))
    for source in sorted(images):
        if args.section=='Regression' and not source.stem.startswith(('A-knife-','B-knife-', 'A-inventory-', 'A-medical-', 'A-north-', 'A-streaming-')):continue
        target=out/(tag+'-'+source.name)
        shutil.copy2(source,target)
        record['screenshots'].append(dict(file=target.name,sha256=hashlib.sha256(source.read_bytes()).hexdigest()))
    records.append(record)
(out/'Acceptance.json').write_text(json.dumps(records,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
failures=[]
for tag in args.failed:
    path=root/'Saved/Extraction'/tag/'Report.json'
    report=json.loads(path.read_text(encoding='utf-8'))
    assert report['result']=='failed',tag
    failures.append(dict(run=tag,result='failed',error=report.get('error'),options=report['options'],
                         report_sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
if failures:
    (out/'Failures.json').write_text(json.dumps(failures,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('ARCHIVED',args.section,len(records))
