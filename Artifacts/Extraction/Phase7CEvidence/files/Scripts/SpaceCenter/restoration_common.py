"""Shared, bounded workspace for the approved SpaceCenter visual reconstruction."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import shutil

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'Saved/SpaceCenter'
WORK = BASE / 'Phase7C'
ORIGIN = [664000, -451000, -22400]
MINIMUM = [652000, -466000]
MAXIMUM = [681000, -433000]
os.environ['SPACECENTER_WORK'] = str(WORK)
from source_packages import extract, write_json


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


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
