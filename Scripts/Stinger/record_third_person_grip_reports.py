"""Archive measured grip regressions and the reviewed capture from the same run."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--ffmpeg', required=True)
args = parser.parse_args()
project = Path(__file__).resolve().parents[2]
saved = project / 'Saved/Stinger'
docs = project / 'Docs/Stinger'
capture = docs / 'ThirdPersonGripCapture'
capture.mkdir(exist_ok=True)

def read(name):
    return json.loads((saved / name).read_text(encoding='utf-8-sig'))

single = read('third_person_grip_final_1p.json')
multi = read('third_person_grip_final_2p.json')
pitch = read('third_person_grip_pitch_2p.json')
local_stills = read('third_person_grip_fit_1p.json')
capture_tail = read('third_person_grip_capture_tail_1p.json')
review = read('third_person_grip_visual_review.json')
for report in [single, multi, pitch, local_stills, capture_tail]:
    assert report['result'] == 'passed'
    assert all(c['passed'] for c in report['checks'])
assert review['result'] == 'accepted'
assert review['frame_directory'] == multi['frame_directory']
frames = Path(multi['frame_directory'])
captures = [item for item in multi['captures'] if (frames / item['file']).is_file()]
omitted = multi['captures'][len(captures):]
# Preserve the raw capture requests. An unflushed final request may be omitted, never filled or duplicated.
assert captures == multi['captures'][:len(captures)] and len(omitted) <= 1
assert [item['file'] for item in omitted] == review['omitted_capture_requests']
video = capture / 'third_person_ak_client.mp4'
subprocess.run([args.ffmpeg, '-hide_banner', '-loglevel', 'error', '-y',
                '-framerate', '30', '-start_number', '1', '-i', str(frames / '%05d.png'),
                '-frames:v', str(len(captures)), '-c:v', 'libx264', '-crf', '18',
                '-pix_fmt', 'yuv420p', str(video)], check=True)
probe = Path(args.ffmpeg).with_name('ffprobe.exe')
video_info = json.loads(subprocess.check_output([str(probe), '-v', 'error', '-select_streams', 'v:0',
    '-count_frames', '-show_entries', 'stream=width,height,r_frame_rate,nb_read_frames:format=duration',
    '-of', 'json', str(video)], text=True))
assert video_info['streams'][0]['r_frame_rate'] == '30/1'
assert int(video_info['streams'][0]['nb_read_frames']) == len(captures)
for phase in ['idle_left', 'idle_front', 'aim', 'look_up', 'look_down', 'crouch', 'walk', 'strafe',
              'fire', 'after_reload', 'first_person', 'first_person_aim', 'third_person']:
    matches = [item for item in captures if item['phase'] == phase]
    shutil.copy2(frames / matches[-1]['file'], capture / (phase + '.png'))
baseline = read('third_person_grip_baseline_2p.json')
for name in ['idle_left', 'idle_front']:
    shutil.copy2(Path(baseline['frame_directory']) / (name + '.png'), capture / ('before_' + name + '.png'))
mapping = {
    'third_person_grip_baseline_2p.json': 'ThirdPersonGripBefore.json',
    'third_person_grip_diagnosis.json': 'ThirdPersonGripDiagnosis.json',
    'third_person_grip_configuration.json': 'ThirdPersonGripConfiguration.json',
    'third_person_grip_final_1p.json': 'ThirdPersonGrip1p.json',
    'third_person_grip_final_2p.json': 'ThirdPersonGrip2p.json',
    'third_person_grip_pitch_2p.json': 'ThirdPersonGrip2pGameplayCamera.json',
    'third_person_grip_fit_1p.json': 'ThirdPersonGrip1pStills.json',
    'third_person_grip_capture_tail_1p.json': 'ThirdPersonGripCaptureTail.json',
    'third_person_grip_visual_review.json': 'ThirdPersonGripVisualReview.json',
}
for source, target in mapping.items():
    (docs / target).write_text(json.dumps(read(source), ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
for phase in ['idle_left', 'aim', 'first_person', 'first_person_aim']:
    shutil.copy2(Path(local_stills['frame_directory']) / (phase + '.png'), capture / ('local_' + phase + '.png'))
raw = (saved / 'third_person_grip_build.txt').read_bytes()
build = raw.decode('utf-16' if raw.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig')
assert 'Result: Succeeded' in build
(docs / 'ThirdPersonGripBuild.txt').write_text('\n'.join(line.rstrip() for line in build.replace('\r', '').split('\n')).rstrip() + '\n', encoding='utf-8', newline='\n')
summary = {
    'result': 'passed', 'engine': '5.8.3', 'branch': 'ue5.8-20260930',
    'scope': 'Stinger third-person AK47 grip; original third-person animation assets retained',
    'single_player_frames': len(single['samples']), 'two_player_frames': len(multi['samples']),
    'single_player_checks': single['checks'], 'two_player_checks': multi['checks'],
    'two_player_gameplay_camera_checks': pitch['checks'],
    'capture_exit_regression_checks': capture_tail['checks'],
    'video': video_info, 'visual_review': review,
    'capture_requests': len(multi['captures']), 'encoded_frames': len(captures), 'omitted_capture_requests': omitted,
    'limitations': ['Same-process PIE, not independent-process latency or packet loss',
                    '30 fps video from consecutive fixed simulation steps within each captured phase, not a performance benchmark',
                    'Capture-only fill light; hidden/offscreen meshes forced to refresh bones for measurement',
                    'Actual +55/-65 degree aim is checked with real gameplay cameras in separate 1p and 2p numerical runs; external capture cameras can change the observer pawn aim',
                    'Existing imported third-person animation quality and unsupported weapon fitting remain outside this fix',
                    'Commandlet emits pre-existing GameFeatureData configuration errors; script save/compile and PIE are checked separately'],
}
(docs / 'ThirdPersonGripSummary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('STINGER_THIRD_PERSON_GRIP_REPORTS_RECORDED')
