"""Archive the focused optic correction, including bounded failed attempts."""
import argparse
import datetime
import gzip
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--owner-run', required=True)
    parser.add_argument('--hd-run', required=True)
    parser.add_argument('--observer-run', required=True)
    args = parser.parse_args()
    runs = {'owner-720': args.owner_run, 'owner-1080': args.hd_run, 'observer': args.observer_run}
    out = ROOT / 'Artifacts/Extraction/OpticCalibrationEvidence'
    assert not out.exists(), 'Use a new archive; never overwrite historical evidence'
    review_path = ROOT / 'Saved/Extraction/OpticVisualReview.json'
    review = json.loads(review_path.read_text(encoding='utf-8'))
    assert review['result'] == 'passed' and review['runs'] == runs
    reports = {}
    for name, tag in runs.items():
        assert tag and all(c.isalnum() or c in '-_' for c in tag)
        folder = ROOT / 'Saved/Extraction' / tag
        report = json.loads((folder / 'Report.json').read_text(encoding='utf-8'))
        guard = json.loads(folder.with_name(tag+'-resources').joinpath('Report.json').read_text(encoding='utf-8'))
        assert report['result'] == guard['result'] == 'passed', tag
        assert report['options']['players'] == 2
        assert report['options']['rendered_role'] == ('A' if name == 'observer' else 'B')
        if name != 'observer':
            pixels = json.loads((folder/'OpticPixels.json').read_text(encoding='utf-8'))
            assert len(pixels) == 7 and all(p['passed'] for p in pixels), tag
            assert report['options']['resolution'] == ('1280x720' if name == 'owner-720' else '1920x1080')
        else:
            assert report['options']['optic_remote_only']
        reports[name] = report
    out.mkdir(parents=True)
    copied = []

    def copy(source, relative, compress=False):
        destination = out / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        content = source.read_bytes()
        destination.write_bytes(gzip.compress(content, mtime=0) if compress else content)
        copied.append({'source': str(source.relative_to(ROOT)).replace('\\', '/'), 'archive': relative,
                       'original_sha256': hashlib.sha256(content).hexdigest(), 'gzip': compress})

    for name, tag in runs.items():
        folder = ROOT/'Saved/Extraction'/tag
        copy(folder/'Report.json', name+'/Report.json.gz', True)
        for path in sorted(folder.glob('*.png')):
            if 'optic-' in path.name or any(x in path.name for x in ['equipment-hip', 'equipment-ads', 'equipment-remote-', 'equipment-reload', 'equipment-swap-', 'equipment-dropped']):
                copy(path, name+'/'+path.name)
        if name != 'observer': copy(folder/'OpticPixels.json', name+'/OpticPixels.json')
        for path in sorted(folder.glob('*.log')): copy(path, name+'/'+path.name+'.gz', True)
        resource = folder.with_name(tag+'-resources')
        copy(resource/'Report.json', name+'/Resources.json')
    for tag in ['OpticNet01', 'OpticNet02']:
        folder = ROOT/'Saved/Extraction'/tag
        for source, name in [(folder/'Report.json', 'Report.json'), (folder.with_name(tag+'-resources')/'Report.json', 'Resources.json'),
                             (folder.with_name(tag+'-resources')/'Task.log', 'Task.log')]:
            copy(source, 'diagnostics/'+tag+'-'+name+'.gz', True)
    for tag in ['OpticAudit01', 'OpticAudit02', 'OpticAssets02']:
        folder = ROOT/'Saved/Extraction'/tag
        for name in ['Audit.json', 'Calibration.json', 'Geometry.json', 'Run.json']:
            if (folder/name).exists(): copy(folder/name, 'geometry/'+tag+'-'+name)
        copy(folder/'Editor.log', 'diagnostics/'+tag+'-Editor.log.gz', True)
    copy(ROOT/'Saved/Extraction/OpticAudit02/AK.fbx', 'geometry/AK.fbx.gz', True)
    lens = next((ROOT/'Saved/Extraction/Phase6ASources').rglob('Sco_Re_Reddot_035_1P_Rigid.glb'))
    copy(lens, 'geometry/OriginalOptic.glb.gz', True)
    copy(ROOT/'Artifacts/Extraction/Phase6AEvidence/equipment-six/B-equipment-ads.png', 'before/B-equipment-ads.png')
    copy(review_path, 'VisualReview.json')
    owner_scenario = ROOT/'Saved/Extraction/OpticOwnerTestedScenario.py'
    for report in [reports['owner-720'], reports['owner-1080']]:
        assert sha(owner_scenario) == report['input_sha256']['Scripts/Extraction/equipment_scenarios.py']
    copy(owner_scenario, 'diagnostics/EquipmentScenarioOwnerRuns.py')
    implementation = [ROOT/'Scripts/Extraction'/name for name in [
        'optic_calibration.py', 'calibrate_red_dot.py', 'optic_scenarios.py', 'verify_optic_capture.py',
        'verify_optic_geometry.py', 'prepare_phase6a.py', 'run_editor_task.py', 'run_network.py',
        'equipment_scenarios.py', 'archive_optic_calibration.py']]
    implementation += [ROOT/'Content/Extraction/Items/DA_Item_RedDot.uasset', ROOT/'.gitattributes']
    summary = {'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
               'scope': 'UE 5.8.3 editor game processes; dedicated server and two clients, one rendered at a time. No new packaged build.',
               'runs': runs, 'checks': {name: len(report['checks']) for name, report in reports.items()},
               'original_definition_sha256': sha(ROOT/'Saved/Extraction/OpticAssets01/DA_Item_RedDot.uasset'),
               'copied_files': copied,
               'implementation_sha256': {str(p.relative_to(ROOT)).replace('\\', '/'): sha(p) for p in implementation},
               'limits': 'Geometry rays use nominal ADS; captures cover selected steady poses and action samples, not every animation frame.',
               'scenario_revision': 'After owner runs, only the optic_remote_only branch gained three additional observer captures. The exact owner-run source is preserved and hash checked.',
               'visual_review': 'See VisualReview.json for direct inspection; passing center pixels alone is insufficient.'}
    (out/'Summary.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
    index = {str(p.relative_to(out)).replace('\\', '/'): {'size': p.stat().st_size, 'sha256': sha(p)}
             for p in sorted(out.rglob('*')) if p.is_file()}
    (out/'ArchiveIndex.json').write_text(json.dumps(index, indent=2)+'\n', encoding='utf-8')
    print('ARCHIVED', len(index), 'files', sum(v['size'] for v in index.values()), 'bytes')


if __name__ == '__main__':
    main()
