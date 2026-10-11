"""Encode captured rendered frames with their measured intervals; reject missing frames."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Saved/Traversal/Tools'))
import imageio_ffmpeg
parser=argparse.ArgumentParser()
parser.add_argument('directory',type=Path)
args=parser.parse_args()
folder=args.directory.resolve()
assert folder.is_relative_to(ROOT/'Saved/Extraction')
rows=list(csv.reader((folder/'Frames.csv').read_text(encoding='utf-8').splitlines()))
assert len(rows)>3, 'Recording too short'
frames=[(folder/name,float(stamp)) for name,stamp in rows]
assert all(p.parent==folder and p.suffix=='.png' and p.is_file() for p,_ in frames), 'Missing requested rendered frame'
intervals=[b[1]-a[1] for a,b in zip(frames,frames[1:])]
assert all(0<dt<2 for dt in intervals), 'Capture gap exceeds two seconds'
concat=[]
for (path,_),dt in zip(frames,intervals+[intervals[-1]]):
    concat += ["file '"+path.name+"'",f'duration {dt:.9f}']
concat += ["file '"+frames[-1][0].name+"'"]
(folder/'Concat.txt').write_text('\n'.join(concat)+'\n',encoding='utf-8')
ffmpeg=imageio_ffmpeg.get_ffmpeg_exe()
output=folder.with_suffix('.mp4')
partial=output.with_suffix('.partial.mp4')
assert not output.exists() and not partial.exists()
command=[ffmpeg,'-hide_banner','-loglevel','error','-f','concat','-safe','0','-i',str(folder/'Concat.txt'),
         '-fps_mode','vfr','-c:v','libx264','-threads','2','-crf','18','-pix_fmt','yuv420p','-movflags','+faststart',str(partial)]
subprocess.run(command,check=True)
subprocess.run([ffmpeg,'-hide_banner','-v','error','-i',str(partial),'-f','null','-'],check=True)
partial.replace(output)
report=dict(file=str(output.relative_to(ROOT)),sha256=hashlib.sha256(output.read_bytes()).hexdigest(),
            frames=len(frames),seconds=frames[-1][1]-frames[0][1],
            measured_fps=(len(frames)-1)/sum(intervals),max_frame_interval=max(intervals),
            boundary='Rendered viewport frames timed by capture requests; no audio. Captures add readback overhead, separate from performance runs.',
            command=command,result='passed')
output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
