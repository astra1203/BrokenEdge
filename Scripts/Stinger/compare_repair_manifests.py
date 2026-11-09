"""对比重建前后的修复清单，确认重置结果与历史导入一致。

用法：
  python Scripts/Stinger/compare_repair_manifests.py [新清单] [基准清单]
默认：
  新清单   Saved/Stinger/AnimationOptimization/repair_full.json
  基准清单 Artifacts/Stinger/RepairImportManifest.json
输出 Saved/Stinger/AnimationOptimization/RepairDiff.json 并打印摘要。
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NEW = Path(sys.argv[1]) if len(sys.argv) > 1 else (
    ROOT / 'Saved/Stinger/AnimationOptimization/repair_full.json')
BASE = Path(sys.argv[2]) if len(sys.argv) > 2 else (ROOT / 'Artifacts/Stinger/RepairImportManifest.json')
DURATION_TOLERANCE = 1e-3
FPS_TOLERANCE = 1e-3


def load(path):
    data = json.loads(path.read_text(encoding='utf-8'))
    return data.get('entries', data)


def sequences(entry):
    return {seq['name']: seq for seq in entry.get('sequences', [])}


def main():
    new, base = load(NEW), load(BASE)
    only_new = sorted(set(new) - set(base))
    only_base = sorted(set(base) - set(new))
    common = sorted(set(new) & set(base))
    failed, mismatched = [], []
    duration_diffs = []
    for rel in common:
        n, b = new[rel], base[rel]
        if n.get('status') != 'written':
            failed.append({'entry': rel, 'status': n.get('status'), 'error': n.get('error')})
            continue
        problems = []
        if n.get('mapping_revision') != b.get('mapping_revision'):
            problems.append('mapping_revision %s->%s' % (b.get('mapping_revision'), n.get('mapping_revision')))
        if abs((n.get('coverage') or 0) - (b.get('coverage') or 0)) > 1e-6:
            problems.append('coverage %s->%s' % (b.get('coverage'), n.get('coverage')))
        if sorted(n.get('missing_bones') or []) != sorted(b.get('missing_bones') or []):
            problems.append('missing_bones changed')
        ns, bs = sequences(n), sequences(b)
        if set(ns) != set(bs):
            problems.append('sequence set %s -> %s' % (sorted(bs), sorted(ns)))
        for name in set(ns) & set(bs):
            # 修复清单里没有 imported_duration，用源帧率/帧数/资产路径三者共同校验。
            fn = ns[name].get('source_fps')
            fb = bs[name].get('source_fps')
            if fn is None or fb is None or abs(fn - fb) > FPS_TOLERANCE:
                duration_diffs.append({'entry': rel, 'sequence': name, 'base_fps': fb, 'new_fps': fn})
            if ns[name].get('frames') != bs[name].get('frames'):
                problems.append('frames changed for %s' % name)
            if (ns[name].get('assets') or {}) != (bs[name].get('assets') or {}):
                problems.append('asset paths changed for %s' % name)
        if problems:
            mismatched.append({'entry': rel, 'problems': problems})
    result = {
        'new_manifest': str(NEW),
        'base_manifest': str(BASE),
        'entries': {'new': len(new), 'base': len(base), 'common': len(common)},
        'only_in_new': only_new,
        'only_in_base': only_base,
        'failed': failed,
        'mismatched': mismatched,
        'duration_diffs': duration_diffs,
        'ok': not (failed or mismatched or duration_diffs or only_new or only_base),
    }
    out = ROOT / 'Saved/Stinger/AnimationOptimization/RepairDiff.json'
    out.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print('REPAIR_DIFF new=%d base=%d common=%d only_new=%d only_base=%d failed=%d mismatched=%d duration_diffs=%d ok=%s'
          % (len(new), len(base), len(common), len(only_new), len(only_base), len(failed), len(mismatched),
             len(duration_diffs), result['ok']))


if __name__ == '__main__':
    main()
