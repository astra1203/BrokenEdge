"""Archive passed visibility checks and a manual review of the same capture."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--ffmpeg', default='ffmpeg')
args = parser.parse_args()
project = Path(__file__).resolve().parents[2]
saved = project / 'Saved/Stinger'
docs = project / 'Docs/Stinger'
capture = docs / 'WeaponVisibilityCapture'
capture.mkdir(exist_ok=True)

def read(name):
    return json.loads((saved / name).read_text(encoding='utf-8'))

single = read('weapon_visibility_1p.json')
multi = read('weapon_visibility_2p.json')
stills = read('weapon_visibility_2p_stills.json')
review = read('weapon_visibility_visual_review.json')
for report in [single, multi, stills]:
    assert report['result'] == 'passed'
    assert all(check['passed'] for check in report['checks'])
    assert not any(sample['violations'] for sample in report['samples'])
assert review['result'] == 'accepted'
assert review['frame_directory'] == multi['frame_directory']
assert review['stills_directory'] == stills['frame_directory']
assert review['reviewed_frame_count'] == len(multi['captures'])

frames = Path(multi['frame_directory'])
assert all((frames / item['file']).is_file() for item in multi['captures'])
video = capture / 'dual_weapon_client.mp4'
subprocess.run([args.ffmpeg, '-hide_banner', '-loglevel', 'error', '-y',
                '-framerate', str(multi['fps']), '-start_number', '1',
                '-i', str(frames / '%05d.png'), '-frames:v', str(len(multi['captures'])),
                '-c:v', 'libx264', '-crf', '18', '-pix_fmt', 'yuv420p', str(video)], check=True)
ffmpeg = Path(shutil.which(args.ffmpeg) or args.ffmpeg)
probe = ffmpeg.with_name('ffprobe' + ffmpeg.suffix)
video_info = json.loads(subprocess.check_output([
    str(probe), '-v', 'error', '-select_streams', 'v:0', '-count_frames',
    '-show_entries', 'stream=width,height,r_frame_rate,nb_read_frames:format=duration',
    '-of', 'json', str(video)], text=True))
stream = video_info['streams'][0]
assert int(stream['nb_read_frames']) == len(multi['captures'])
assert stream['r_frame_rate'] == str(multi['fps']) + '/1'

files = {
    'visibility_before.json': 'WeaponVisibilityBefore.json',
    'weapon_visibility_1p.json': 'WeaponVisibility1p.json',
    'weapon_visibility_2p.json': 'WeaponVisibility2p.json',
    'weapon_visibility_2p_stills.json': 'WeaponVisibility2pStills.json',
    'weapon_visibility_visual_review.json': 'WeaponVisibilityVisualReview.json',
}
for source, target in files.items():
    shutil.copy2(saved / source, docs / target)
build_bytes = (saved / 'weapon_visibility_build.txt').read_bytes()
build = build_bytes.decode('utf-16' if build_bytes.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig')
assert 'Result: Succeeded' in build
build = '\n'.join(line.rstrip() for line in build.replace('\r', '').split('\n')).rstrip('\n') + '\n'
(docs / 'WeaponVisibilityBuild.txt').write_text(build, encoding='utf-8', newline='\n')
for phase in ['dual_down', 'crouch_down', 'aim_down', 'swap1', 'swap2', 'swap_view']:
    matches = [item for item in multi['captures'] if item['phase'] == phase]
    shutil.copy2(frames / matches[len(matches) // 2]['file'], capture / (phase + '.png'))
for name in ['equip_secondary', 'third_person', 'first_person', 'drop', 're_pickup',
             'repossess', 'respawn_equip', 'destroy_weapon']:
    shutil.copy2(Path(stills['frame_directory']) / (name + '.png'), capture / (name + '.png'))

summary = {
    'result': 'passed', 'engine': '5.8.3', 'branch': 'ue5.8-20260930',
    'map': '/Game/Characters/Stinger/Maps/L_StingerTest', 'build': 'succeeded',
    'single_player': {'mode': 'standalone PIE, NullRHI', 'frames': len(single['samples']),
                      'checks': len(single['checks'])},
    'two_player': {'mode': 'same-process listen server and client PIE, rendered',
                   'frames': len(multi['samples']), 'checks': len(multi['checks'])},
    'lifecycle_stills': {'frames': len(stills['samples']), 'checks': len(stills['checks'])},
    'video': video_info, 'visual_review': review,
    'limitations': ['No independent-process latency or packet-loss test',
                    'Peer damage disabled during action checks, enabled for deliberate death test',
                    'AK47 visibility regression only; existing animation fitting unchanged',
                    '30 fps video reconstructed from each fixed simulation frame, not a wall-clock performance capture'],
}
(docs / 'WeaponVisibilitySummary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')
print('STINGER_WEAPON_VISIBILITY_REPORTS_RECORDED')
