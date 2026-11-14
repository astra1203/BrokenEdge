"""Real two-client PIE lobby travel, including consecutive raids and the disabled-travel guard."""
import argparse
import datetime
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Scripts'))
from Common.ue_env import engine_root, run_record
parser = argparse.ArgumentParser()
parser.add_argument('--tag', required=True)
parser.add_argument('--engine', type=Path)
parser.add_argument('--mode', choices=['Extraction', 'Teams', 'FreeForAll'], default='Extraction')
parser.add_argument('--players', type=int, choices=[2, 3, 6], default=2)
parser.add_argument('--cross-maps', action='store_true')
parser.add_argument('--nullrhi', action='store_true', help='Functional multi-world validation without rendering')
args = parser.parse_args()
try:
    ENGINE = engine_root(args.engine)
except (ValueError, OSError, KeyError) as error:
    parser.error(str(error))
if not args.tag or not all(c.isalnum() or c in '-_' for c in args.tag):
    raise ValueError('Use an alphanumeric run tag')
out = ROOT / 'Saved/Extraction' / args.tag
out.mkdir(parents=True, exist_ok=False)
command = [str(ENGINE / 'Engine/Binaries/Win64/UnrealEditor.exe'), str(ROOT / 'BrokenEdge.uproject'),
           '/Game/Maps/Lobby', '-unattended', '-nosplash', '-nosound', '-NoLoadingScreen', '-RenderOffscreen',
           '-windowed', '-ResX=1280', '-ResY=720', '-abslog=' + str(out / 'Editor.log'),
           '-ExtractionStore=' + str(out / 'Store'), '-ExtractionProfile=' + args.tag,
           '-MatchType=' + args.mode, '-ExtractionPIEMode=' + args.mode,
           '-NumPublicConnections=6', '-MinPlayersToStart=' + str(args.players), '-ExtractionPIEPlayers=' + str(args.players), '-ExtractionGuards=0',
           '-ExecCmds=Automation RunTests BrokenEdge.Extraction.PIE.Lobby',
           '-TestExit=Automation Test Queue Empty', '-ReportExportPath=' + str(out / 'Automation')]
if args.cross_maps: command.append('-ExtractionPIECrossMaps')
if args.nullrhi: command.append('-NullRHI')
metadata = {'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'mode': args.mode, 'command': command}
metadata.update(run_record(command, out / 'Run.json', engine=ENGINE, script=__file__))
environment = os.environ.copy()
environment['UE_SKIP_UBT_SDK_SETUP'] = '1'
with (out / 'Process.log').open('wb') as log:
    proc = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    metadata['pid'] = proc.pid
    (out / 'Run.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    print('PIE_LOBBY_STARTED', args.mode, proc.pid, str(out), flush=True)
    try:
        metadata['exit_code'] = proc.wait(timeout=900)
    except subprocess.TimeoutExpired:
        subprocess.run(['taskkill', '/PID', str(proc.pid), '/T', '/F'], capture_output=True)
        metadata['exit_code'] = proc.wait(timeout=30)
        metadata['error'] = 'PIE lobby test timed out'
text = (out / 'Editor.log').read_text(encoding='utf-8', errors='replace')
metadata['active_rounds'] = re.findall(r'EXTRACTION_PIE_LOBBY_ACTIVE Round=(\d)', text)
metadata['settled_rounds'] = re.findall(r'EXTRACTION_PIE_LOBBY_SETTLED Round=(\d)', text)
metadata['returned_rounds'] = re.findall(r'EXTRACTION_PIE_LOBBY_RETURNED Round=(\d)', text)
successes = re.findall(r'Test Completed\. Result=\{(?:Success|\u6210\u529f)\}', text)
rounds = (3 if args.cross_maps else 2) if args.mode == 'Extraction' else 1
passed = (metadata['exit_code'] == 0 and len(successes) == 1 and 'Fatal error:' not in text
          and f'EXTRACTION_PIE_LOBBY_STOPPED Mode={args.mode} Rounds={rounds} Worlds=0' in text
          and 'Seamless travel is disabled in PIE' not in text
          and not re.search(r'Test Completed\. Result=\{(?:Fail|Failure|\u5931\u8d25)', text))
if args.mode == 'Extraction':
    passed = (passed and 'EXTRACTION_PIE_LOBBY_DISABLED_GUARD_OK' in text
              and all(metadata[key] == [str(i+1) for i in range(rounds)] for key in ['active_rounds', 'settled_rounds', 'returned_rounds']))
else:
    passed = (passed and f'EXTRACTION_PIE_LOBBY_LEGACY_OK Mode={args.mode}' in text
              and f'EXTRACTION_PIE_LEGACY_RESPAWN_OK Mode={args.mode}' in text)
metadata['result'] = 'passed' if passed else 'failed'
metadata['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
(out / 'Run.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
print(json.dumps({k: v for k, v in metadata.items() if k != 'command'}, ensure_ascii=True), flush=True)
sys.exit(0 if passed else 1)
