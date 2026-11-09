"""Shared, bounded workspace for the approved SpaceCenter visual reconstruction."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import shutil
import argparse
import sys

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'Saved/SpaceCenter'
# Consume this shared CLI option before each stage parses its own arguments.
# The environment carries the same explicit profile into owned UE processes.
_profile_parser = argparse.ArgumentParser(add_help=False)
_profile_parser.add_argument('--profile', default=os.environ.get('SPACECENTER_RESTORE_PROFILE'))
_profile_args, _remaining = _profile_parser.parse_known_args()
sys.argv = [sys.argv[0]] + _remaining
_active_file = ROOT/'Scripts/SpaceCenter/restoration_active.json'
_active_profile = json.loads(_active_file.read_text(encoding='utf-8')).get('profile_file') if _active_file.exists() else None
PROFILE_FILE = Path(_profile_args.profile or _active_profile or 'Scripts/SpaceCenter/restoration_profile_7c.json')
if not PROFILE_FILE.is_absolute(): PROFILE_FILE = ROOT/PROFILE_FILE
PROFILE_FILE = PROFILE_FILE.resolve()
PROFILE = json.loads(PROFILE_FILE.read_text(encoding='utf-8'))
assert PROFILE['version'] == 1
if PROFILE['phase']=='7D':
    assert PROFILE.get('preview_map')=='/Game/Maps/SpaceCenter/L_SpaceCenterRestorationPreview7D','7D requires its own preview map'
    assert {'cameras','traverse_route','acceptance'}.issubset(PROFILE['inputs']),'7D must not fall back to historical validation inputs'
WORK = (ROOT/PROFILE['work_directory']).resolve()
assert WORK.is_relative_to(BASE) and WORK != BASE, 'Profile output must be a bounded phase workspace'
ORIGIN = PROFILE['origin']
MINIMUM = PROFILE['minimum']
MAXIMUM = PROFILE['maximum']
assert len(ORIGIN)==3 and len(MINIMUM)==len(MAXIMUM)==2 and all(a<b for a,b in zip(MINIMUM,MAXIMUM))
CACHE_WORKSPACES = [(ROOT/p).resolve() for p in PROFILE.get('cache_workspaces',[])]
assert all(p.is_relative_to(BASE) and p!=WORK for p in CACHE_WORKSPACES)
os.environ['SPACECENTER_RESTORE_PROFILE'] = str(PROFILE_FILE)
os.environ['SPACECENTER_WORK'] = str(WORK)
from source_packages import extract, write_json


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def input_path(name, default):
    configured = PROFILE.get('inputs',{}).get(name)
    return ROOT/configured if configured else WORK/default


def cache_files(pattern):
    return [p for folder in [*CACHE_WORKSPACES, WORK] for p in sorted(folder.glob(pattern))]


def sha(path):
    with io_path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def io_path(path):
    absolute=str(Path(path).absolute())
    return Path('\\\\?\\'+absolute) if os.name=='nt' and not absolute.startswith('\\\\?\\') else Path(absolute)


def canonical(path):
    return path.split('.')[0].replace('DeltaForce/Content/', '/Game/').replace('Engine/Content/', '/Engine/')


def identity(value):
    return hashlib.sha256(value.encode('utf-8')).hexdigest()[:16]


def stage_import(entry):
    """UE's embedded Python can reject deep Windows paths; stage byte-identical inputs."""
    original=Path(entry['file'])
    digest=sha(original)
    target=WORK/'Import'/identity(entry['source'])/original.name
    if len(str(target))>=240:raise ValueError('Import filename too long: '+str(target))
    target.parent.mkdir(parents=True,exist_ok=True)
    if not target.exists():shutil.copyfile(io_path(original),target)
    if sha(target)!=digest:raise RuntimeError('Staged source bytes differ: '+str(target))
    return entry|{'file':str(target),'exported_file':str(original),'sha256':digest}


def run_converter(mode, packages, out, *, recovery=(), partial=False, extra=None):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=False)
    request = {'mode': mode, 'source': str(BASE / 'Source/DeltaForce'),
               'packages': sorted(set(packages)), 'output': str(out / 'Parsed'),
               'geometry_recovery': list(recovery)}
    request.update(extra or {})
    write_json(out / 'Request.json', request)
    command = [str(BASE / 'Tools/dotnet/dotnet.exe'),
               str(ROOT / 'Scripts/SpaceCenter/Converter/bin/Release/net10.0/SpaceCenter.Converter.dll'),
               str(out / 'Request.json')]
    with (out / 'Converter.log').open('wb') as log:
        result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                creationflags=subprocess.CREATE_NO_WINDOW)
    write_json(out / 'Process.json', {'command': command, 'exit_code': result.returncode,
                                     'partial_allowed': partial})
    if result.returncode and not partial:
        raise RuntimeError('Converter failed: ' + str(out))
    return read(out / 'Parsed/Packages.json')


def extract_recorded(packages, out, limit_gib=2):
    result = extract(sorted(set(packages)), limit_gib=limit_gib)
    write_json(Path(out) / 'Extractions.json', result)
    if result['missing']:
        raise RuntimeError('Missing source packages: ' + str(result['missing']))
    return result
