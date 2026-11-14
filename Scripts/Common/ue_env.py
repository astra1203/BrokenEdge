"""Explicit Unreal installation selection and reproducible run metadata."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def engine_root(engine=None, *, project_root=ROOT, environ=None):
    project_root = Path(project_root)
    environ = os.environ if environ is None else environ
    selected = engine or environ.get('BROKENEDGE_UE_ROOT')
    config_path = project_root / 'Scripts/local_config.json'
    if not selected and config_path.is_file():
        selected = json.loads(config_path.read_text(encoding='utf-8-sig')).get('engine')
    if not selected:
        raise ValueError('Specify --engine, BROKENEDGE_UE_ROOT or Scripts/local_config.json (engine).')
    root = Path(selected).expanduser()
    if not root.is_absolute():
        root = project_root / root
    root = root.resolve()
    if not (root / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe').is_file():
        raise ValueError(f'UnrealEditor-Cmd.exe not found under {root}')
    version = json.loads((root / 'Engine/Build/Build.version').read_text(encoding='utf-8-sig'))
    association = json.loads((project_root / 'BrokenEdge.uproject').read_text(encoding='utf-8-sig'))['EngineAssociation']
    match = re.fullmatch(r'(\d+)\.(\d+)(?:\.\d+)?', association)
    if match:
        expected = tuple(map(int, match.groups()))
        if expected != (version['MajorVersion'], version['MinorVersion']):
            raise ValueError(f'Engine version does not match project association {association}')
    else:
        # Source builds use a registered installation GUID, not a version number.
        try:
            import winreg
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Epic Games\Unreal Engine\Builds') as key:
                registered = Path(winreg.QueryValueEx(key, association)[0]).resolve()
            if registered != root:
                raise ValueError(f'Engine does not match registered association {association}')
        except (ImportError, OSError) as exc:
            raise ValueError(f'Cannot validate engine association {association}') from exc
    return root


def run_record(cmd, report_path, *, engine, script, project_root=ROOT):
    """Prepare provenance; caller owns execution, timeout and final exit_code/result."""
    def git(*args):
        return subprocess.check_output(['git', *args], cwd=project_root, text=True, encoding='utf-8').strip()
    script = Path(script).resolve()
    return {
        'engine_root': str(engine),
        'engine_version': json.loads((Path(engine) / 'Engine/Build/Build.version').read_text(encoding='utf-8-sig')),
        'git_head': git('rev-parse', 'HEAD'),
        'git_status': git('status', '--porcelain'),
        'script': str(script),
        'script_sha256': hashlib.sha256(script.read_bytes()).hexdigest(),
        'ue_env_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'inherited_cache_environment': {key: os.environ[key] for key in (
            'UE-LocalDataCachePath', 'UE-ZenDataPath', 'UE-ZenSubprocessDataPath') if key in os.environ},
        'command': list(map(str, cmd)),
        'report_path': str(Path(report_path).resolve()),
        'exit_code': None,
    }


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', type=Path)
    args = parser.parse_args()
    try:
        print(engine_root(args.engine))
    except (ValueError, OSError) as error:
        parser.error(str(error))
