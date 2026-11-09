"""Bounded, source-first recovery probes; never import approximations into the map.

Each invocation has its own immutable evidence directory. Original ZIPs are only
read, and all extracted packages go through the existing CRC/hash/budget checks.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'Saved/SpaceCenter/Phase7B'
os.environ['SPACECENTER_WORK'] = str(WORK)
from source_packages import BASE_WORK, extract, write_json

TERRAIN = '/Game/Maps/BuildMaps_HD/SpaceCenter_Ma1/Runtime/LandscapeCDLOD/'
INITIAL = [TERRAIN + name for name in [
    'LandscapeCDLOD', 'Asset/CDLODAsset_Header', 'Asset/CDLODAsset_CoarseHeightmap',
    'MI_Terrain_SpaceCenter_HD_CDLOD', 'WeightmapClipmap',
]] + [
    '/Game/MaterialLib/Materials/Scene/NRMACMat/M_SW_NRMAC_Master',
    '/Game/MaterialLib/Materials_HD/Scene/BasePBR/M_Scene_BasePBR_HD',
    '/Game/MaterialLib/Materials_HD/Scene/MatBlend/M_MatBlend_Props_Master',
    '/Game/MaterialLib/Materials_HD/Scene/MatBlend/M_MatBlend_Ground_Master',
]


def sha(path):
    with path.open('rb') as handle:
        return hashlib.file_digest(handle, 'sha256').hexdigest()


def inspect(packages, out):
    out.mkdir(parents=True, exist_ok=False)
    config = json.loads((ROOT / 'Scripts/SpaceCenter/north_config.json').read_text(encoding='utf-8'))
    catalog = json.loads((BASE_WORK / 'ArchiveIndex.json').read_text(encoding='utf-8'))
    for entry in catalog['archives']:
        actual = (Path(config['source']) / entry['archive']).stat()
        if actual.st_size != entry['size'] or actual.st_mtime_ns != entry['mtime_ns']:
            raise RuntimeError('Source archive changed: ' + entry['archive'])
    result = extract(sorted(set(packages)), limit_gib=2)
    write_json(out / 'SourceExtractions.json', result)
    request = {'mode': 'inspect', 'source': str(BASE_WORK / 'Source/DeltaForce'),
               'packages': sorted(set(packages)), 'output': str(out / 'Parsed')}
    write_json(out / 'Request.json', request)
    command = [str(BASE_WORK / 'Tools/dotnet/dotnet.exe'),
               str(ROOT / 'Scripts/SpaceCenter/Converter/bin/Release/net10.0/SpaceCenter.Converter.dll'),
               str(out / 'Request.json')]
    with (out / 'Converter.log').open('wb') as log:
        process = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                 creationflags=subprocess.CREATE_NO_WINDOW)
    parsed = json.loads((out / 'Parsed/Packages.json').read_text(encoding='utf-8'))
    report = {'result': 'inspected' if process.returncode == 0 else 'diagnostic',
              'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'exit_code': process.returncode, 'packages': len(parsed),
              'missing': result['missing'], 'source_bytes': result['bytes'],
              'parsed_sha256': sha(out / 'Parsed/Packages.json'),
              'boundary': 'Source inspection only; no material, terrain, UE import or runtime acceptance claim.'}
    write_json(out / 'Report.json', report)
    print(json.dumps(report))
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--tag', required=True)
    parser.add_argument('--packages', type=Path, help='JSON array of exact package paths; defaults to initial probes')
    args = parser.parse_args()
    if not args.tag or not all(c.isalnum() or c in '-_' for c in args.tag):
        parser.error('Use a new alphanumeric run tag')
    packages = json.loads(args.packages.read_text(encoding='utf-8-sig')) if args.packages else INITIAL
    inspect(packages, WORK / 'SourceRuns' / args.tag)
