import ast,json,os
from pathlib import Path
root=Path.cwd();base=root/'Saved/Extraction'
selected=None
for group in ['Phase7DPerformance01','Phase7DRegression03','Phase7DRegression02','Phase7DRegression01','Phase7DStreaming01','Phase7DEast05']:
    report=base/group/'Report.json'
    if report.exists():
        d=json.loads(report.read_text(encoding='utf-8'))
        if d['result']=='running':
            selected=group+'-'+d['steps'][-1]['name'];print('ACTIVE',selected);break
if selected is None:
    print('No active final acceptance group');raise SystemExit(0)
folder=base/selected;guard=base/(selected+'-resources')
log=guard/'Task.log'
if log.exists():print('\n'.join(log.read_text(encoding='utf-8',errors='replace').splitlines()[-5:]))
report=guard/'Report.json'
if report.exists():
    d=json.loads(report.read_text(encoding='utf-8'));v=d['samples'][-1]
    print('RESOURCES',d['result'],{k:round(v[k],2) for k in ['c_free_gib','d_free_gib','ram_free_gib']})
if 'Performance' not in selected:
    tree=ast.parse((root/'Scripts/Extraction/run_network.py').read_text(encoding='utf-8'))
    fn=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='read_snapshot')
    exec(compile(ast.Module(body=[fn],type_ignores=[]),'<read_snapshot>','exec'))
    for role in ['A','B']:
        try:d=json.loads(read_snapshot(folder/(role+'-state.json')))
        except (OSError,ValueError):continue
        own=next((x for x in d.get('players',[]) if x['name']=='EX_'+role),{})
        print(role,'walk',str(d.get('walk_index'))+'/'+str(d.get('walk_count')),'position',[round(v,1) for v in own.get('position',[])],'spectated',d.get('spectated'),'error',d.get('error'))
else:
    telemetry=folder/'Telemetry.jsonl'
    if telemetry.exists():
        with telemetry.open('rb') as stream:
            stream.seek(max(0,telemetry.stat().st_size-4096));lines=stream.read().splitlines()
        for line in reversed(lines):
            try:sample=json.loads(line)
            except ValueError:continue
            print('PROFILE_ROUTE',sample['route']);break
    import datetime
    if report.exists():
        first=datetime.datetime.fromisoformat(d['samples'][0]['utc'])
        print('CASE_ELAPSED_SECONDS',round((datetime.datetime.now(datetime.timezone.utc)-first).total_seconds()))
