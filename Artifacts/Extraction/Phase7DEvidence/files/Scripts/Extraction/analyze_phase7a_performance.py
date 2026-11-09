"""Summarize native UE CSV measurements without discarding slow warm frames."""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--tag', required=True, help='Performance acceptance group tag')
parser.add_argument('--routes',nargs='+',choices=['old','north','east'],default=['old','north'])
parser.add_argument('--combat-tag', action='append', default=[], help='Additional successful six-player/four-AI functional profile')
parser.add_argument('--network-tag', help='Separate short old/north profile group with net.EnableNetStats enabled')
args = parser.parse_args()
out = ROOT / 'Saved/Extraction' / args.tag


def summary(values):
    values = sorted(values)
    if not values:
        return None
    return {'count': len(values), 'min': values[0], 'mean': statistics.fmean(values),
            'p50': values[math.ceil(.5 * len(values)) - 1],
            'p95': values[math.ceil(.95 * len(values)) - 1],
            'p99': values[math.ceil(.99 * len(values)) - 1], 'max': values[-1]}


def capture(path):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        rows = list(csv.reader(stream))
    headers = [row for row in rows if 'FrameTime' in row and row[0] == 'EVENTS']
    # UE appends newly encountered stat columns during capture, then writes the
    # complete header at the end. Earlier data rows legitimately have fewer cells.
    header = headers[-1]
    assert all(header[:len(row)] == row for row in headers), str(path)
    metadata = {row[i][1:-1]: row[i + 1] for row in rows if row and row[0] == '[HasHeaderRowAtEnd]'
                for i in range(len(row) - 1) if row[i].startswith('[') and row[i].endswith(']')
                and row[i][1:-1] in ['platform', 'config', 'engineversion', 'os', 'cpu', 'gpu', 'gpudriver',
                    'systemresolution.resx', 'systemresolution.resy', 'rhiname', 'rhifeaturelevel',
                    'captureduration', 'starttimestamp', 'endtimestamp', 'vsyncenabled']}
    names = ['FrameTime', 'GameThreadTime', 'RenderThreadTime', 'GPUTime',
             'Exclusive/GameThread/NetworkIncoming', 'Exclusive/GameThread/NetworkOutgoing',
             'Exclusive/GameThread/ServerReplicateActors', 'Exclusive/GameThread/TickActors',
             'GPUMem/LocalUsedMB', 'GPUMem/LocalBudgetMB', 'GPUMem/SystemUsedMB']
    values = {name: [] for name in names if name in header}
    events, slow, elapsed = [], [], 0.
    for row in rows:
        if len(row) > len(header) or len(row) <= header.index('FrameTime'):
            continue
        try:
            frame = float(row[header.index('FrameTime')])
        except ValueError:
            continue
        elapsed += frame / 1000
        for name in values:
            try:
                values[name].append(float(row[header.index(name)]))
            except (ValueError, IndexError):
                pass
        event = ' '.join(v for v in row if 'NorthRequested=' in v or 'NorthLoaded=' in v)
        if event:
            events.append({'seconds': elapsed, 'event': event})
        if frame > 100:
            slow.append({'seconds': elapsed, 'frame_ms': frame})
    metrics = {name: summary(v) for name, v in values.items()}
    assert abs(elapsed - float(metadata['captureduration'])) < 1, ('Incomplete CSV frames', str(path), elapsed, metadata)
    result = {'file': str(path.relative_to(ROOT)), 'elapsed_seconds': elapsed, 'metadata': metadata, 'metrics': metrics,
              'stream_events': events, 'frames_over_100ms': slow,
              'stream_window_frames_over_100ms': [f for f in slow if any(abs(f['seconds'] - e['seconds']) <= 2 for e in events)]}
    warm_client = path.name.startswith('A-performance-')
    if warm_client:
        result['target_passed'] = (metadata.get('systemresolution.resx') == '1920'
                                   and metadata.get('systemresolution.resy') == '1080'
                                   and elapsed >= 180 and metrics['FrameTime']['p95'] <= 16.7
                                   and not result['stream_window_frames_over_100ms'])
    return result


report = {'runs': [], 'boundary': 'Same-host Development/Null, one 1080p medium rendered client, NullRHI peer and dedicated server. Environment routes have zero AI. First visit is separate; no OS cache purge.'}
for route in args.routes:
    folder = ROOT / 'Saved/Extraction' / (args.tag + '-' + route)
    network = json.loads((folder / 'Report.json').read_text(encoding='utf-8'))
    assert network['result'] == 'passed', str(folder)
    captures = [capture(p) for p in sorted(folder.glob('*.csv'))]
    assert sum('target_passed' in c for c in captures) == 3
    telemetry = [json.loads(line) for line in (folder / 'Telemetry.jsonl').read_text(encoding='utf-8').splitlines()]
    peer_metrics = {peer: {key: summary([s['peers'][peer][key] for s in telemetry if s['peers'][peer].get(key) is not None]) if key == 'physical_mib' or network['options'].get('network_stats') else None
                          for key in ['physical_mib', 'net_in_bytes_per_second', 'net_out_bytes_per_second']}
                    for peer in ['A', 'B', 'Server']}
    report['runs'].append({'route': route, 'captures': captures, 'peer_metrics': peer_metrics,
                          'network_note': 'Disabled UE byte-rate counters are unavailable, not zero traffic. See the separate network profile.'})
report['target_passed'] = all(c['target_passed'] for r in report['runs'] for c in r['captures'] if 'target_passed' in c)
report['result'] = 'passed' if report['target_passed'] else 'failed'
report['ai_stress'] = []
report['network_profiles'] = []
if args.network_tag:
    for route in args.routes:
        folder = ROOT / 'Saved/Extraction' / (args.network_tag + '-' + route)
        run = json.loads((folder / 'Report.json').read_text(encoding='utf-8'))
        assert run['result'] == 'passed' and run['options']['network_stats']
        telemetry = [json.loads(line) for line in (folder / 'Telemetry.jsonl').read_text(encoding='utf-8').splitlines()]
        metrics = {peer: {key: summary([s['peers'][peer][key] for s in telemetry if s['peers'][peer].get(key) is not None])
                         for key in ['net_in_bytes_per_second', 'net_out_bytes_per_second']}
                   for peer in ['A', 'B', 'Server']}
        assert all(v and v['max'] > 0 for peer in metrics.values() for v in peer.values()), metrics
        report['network_profiles'].append({'route': route, 'folder': str(folder.relative_to(ROOT)),
            'boundary': 'Separate >=60 s route sample plus first visit, same map/binaries/1080p medium, net.EnableNetStats=1. Not substituted for the three long frame-time samples.',
            'samples': len(telemetry), 'peer_metrics': metrics})
for tag in args.combat_tag:
    folder = ROOT / 'Saved/Extraction' / tag
    run = json.loads((folder / 'Report.json').read_text(encoding='utf-8'))
    assert run['result'] == 'passed', tag
    report['ai_stress'].append({'tag': tag, 'boundary': run['profile_boundary'],
                                'captures': [capture(p) for p in sorted(folder.glob('*-north-combat.csv'))]})
(out / 'Performance.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'target_passed': report['target_passed'], 'routes': [
    {'route': r['route'], 'p95_ms': [c['metrics']['FrameTime']['p95'] for c in r['captures'] if 'target_passed' in c]}
    for r in report['runs']]}, indent=2))
if not report['target_passed']:
    raise SystemExit(1)
