"""Exercise real PIE start/stop twice for listen and dedicated two-player sessions."""
import argparse
import datetime
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
ENGINE = Path('C:/UnrealProjects/UnrealEngine-5.8.3-release')
parser = argparse.ArgumentParser()
parser.add_argument('--tag', required=True)
args = parser.parse_args()
if not args.tag or not all(c.isalnum() or c in '-_' for c in args.tag):
    raise ValueError('Use an alphanumeric run tag')
out = ROOT / 'Saved/Extraction' / args.tag
out.mkdir(parents=True, exist_ok=False)
command = [str(ENGINE / 'Engine/Binaries/Win64/UnrealEditor.exe'), str(ROOT / 'BrokenEdge.uproject'),
           '-unattended', '-nosplash', '-nosound', '-NoLoadingScreen', '-RenderOffscreen',
           '-windowed', '-ResX=1280', '-ResY=720', '-abslog=' + str(out / 'Editor.log'),
           '-ExtractionStore=' + str(out / 'Store'), '-ExtractionProfile=' + args.tag,
           '-ExecCmds=Automation RunTests BrokenEdge.Extraction.PIE.Teardown',
           '-TestExit=Automation Test Queue Empty', '-ReportExportPath=' + str(out / 'Automation')]
metadata = {'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'command': command}
environment = os.environ.copy()
environment['UE_SKIP_UBT_SDK_SETUP'] = '1'
with (out / 'Process.log').open('wb') as log:
    proc = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    metadata['pid'] = proc.pid
    (out / 'Run.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    print('PIE_TEARDOWN_STARTED', proc.pid, str(out), flush=True)
    try:
        metadata['exit_code'] = proc.wait(timeout=900)
    except subprocess.TimeoutExpired:
        subprocess.run(['taskkill', '/PID', str(proc.pid), '/T', '/F'], capture_output=True)
        metadata['exit_code'] = proc.wait(timeout=30)
        metadata['error'] = 'PIE teardown test timed out'
text = (out / 'Editor.log').read_text(encoding='utf-8', errors='replace')
metadata['ready_cycles'] = re.findall(r'EXTRACTION_PIE_TEARDOWN_READY Cycle=(\d) Worlds=(\d) Players=(\d)', text)
metadata['stopped_cycles'] = re.findall(r'EXTRACTION_PIE_TEARDOWN_STOPPED Cycle=(\d) Worlds=0', text)
successes = re.findall(r'Test Completed\. Result=\{(?:Success|\u6210\u529f)\}', text)
passed = (metadata['exit_code'] == 0 and len(successes) == 1
          and metadata['ready_cycles'] == [('1', '2', '2'), ('2', '2', '2'), ('3', '3', '2'), ('4', '3', '2')]
          and metadata['stopped_cycles'] == ['1', '2', '3', '4']
          and 'EXTRACTION_SETTLED' not in text and 'Fatal error:' not in text
          and not re.search(r'Test Completed\. Result=\{(?:Fail|Failure|\u5931\u8d25)', text))
metadata['result'] = 'passed' if passed else 'failed'
metadata['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
(out / 'Run.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
print(json.dumps(metadata, ensure_ascii=True), flush=True)
sys.exit(0 if passed else 1)
