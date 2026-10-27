"""诊断：翻越/攀爬序列的根骨是否随动画旋转（会导致人物朝向与障碍不符）。

用法（只读，编辑器开着也能跑）：
  UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript ^
      -script="<绝对路径>/Scripts/Stinger/inspect_traversal_orientation.py" -NullRHI -unattended -nosound
输出 Saved/Stinger/AnimationOptimization/TraversalOrientation.json
"""
import json
from pathlib import Path
import unreal

TRAVERSAL = '/Game/Extraction/Traversal/DA_TraversalAnimations.DA_TraversalAnimations'
SAMPLES = 5


def probe_api(sequence):
    """打印可用的取骨方式，便于确认 Python 侧的动画采样接口。"""
    names = [name for name in dir(sequence) if 'bone' in name.lower() or 'transform' in name.lower()]
    unreal.log_warning('TRAVERSAL_ORIENT_API %s' % ', '.join(sorted(names)))


def sample_root(sequence):
    rows = []
    length = sequence.get_play_length()
    for index in range(SAMPLES):
        time = length * index / max(SAMPLES - 1, 1)
        try:
            transform = sequence.get_bone_transform(0, time, False)
        except Exception as error:
            return rows, str(error)
        rot = transform.rotation.rotator()
        rows.append({'t': round(time, 3), 'yaw': round(rot.yaw, 1), 'pitch': round(rot.pitch, 1),
                     'roll': round(rot.roll, 1),
                     'loc': [round(transform.translation.x, 1), round(transform.translation.y, 1),
                             round(transform.translation.z, 1)]})
    return rows, ''


def main():
    data = unreal.load_asset(TRAVERSAL)
    assert data is not None, TRAVERSAL
    report, probed = [], False
    for entry in data.get_editor_property('Animations'):
        kind = str(entry.get_editor_property('kind'))
        height = entry.get_editor_property('height')
        top = entry.get_editor_property('bTop')
        for role in ('firstPerson', 'thirdPerson'):
            sequence = entry.get_editor_property(role)
            if sequence is None:
                continue
            if not probed:
                probe_api(sequence)
                probed = True
            rows, error = sample_root(sequence)
            yaws = [row['yaw'] for row in rows]
            report.append({
                'kind': kind, 'height': height, 'bTop': top, 'role': role,
                'sequence': sequence.get_path_name(),
                'root_motion': sequence.get_editor_property('enable_root_motion'),
                'force_root_lock': sequence.get_editor_property('force_root_lock'),
                'length': round(sequence.get_play_length(), 3),
                'samples': rows, 'sample_error': error,
                'yaw_span': round(max(yaws) - min(yaws), 1) if yaws else 0.0,
                'yaw_start': yaws[0] if yaws else 0.0,
                'yaw_end': yaws[-1] if yaws else 0.0,
            })
    out = (Path(unreal.Paths.project_saved_dir()) / 'Stinger' / 'AnimationOptimization'
           / 'TraversalOrientation.json')
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    for row in report:
        unreal.log_warning(
            'TRAVERSAL_ORIENT %-10s h=%-4s top=%d %-11s yaw=%.0f->%.0f span=%.0f rm=%d lock=%d %s %s' % (
                row['kind'], row['height'], row['bTop'], row['role'],
                row['yaw_start'], row['yaw_end'], row['yaw_span'],
                row['root_motion'], row['force_root_lock'],
                row['sequence'].rsplit('/', 1)[-1], row['sample_error']))


if __name__ == '__main__':
    main()
