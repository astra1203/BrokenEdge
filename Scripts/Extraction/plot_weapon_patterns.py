"""Render native fixed-seed samples to a self-contained SVG; no external dependencies."""
import argparse
import csv
from collections import defaultdict
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--input', type=Path, default=Path('Saved/Extraction/WeaponPatterns/Patterns.csv'))
parser.add_argument('--output', type=Path, default=Path('Saved/Extraction/WeaponPatterns/Patterns.svg'))
args = parser.parse_args()
groups = defaultdict(list)
with args.input.open(encoding='utf-8-sig', newline='') as handle:
    for row in csv.DictReader(handle):
        groups[tuple(row[k] for k in ['weapon', 'aim', 'mode'])].append(row)
assert len(groups) == 8 and all(len(rows) == 90 for rows in groups.values())
parts = ['<svg xmlns="http://www.w3.org/2000/svg" width="1400" height="800" viewBox="0 0 1400 800">',
         '<rect width="1400" height="800" fill="#101b26"/>',
         '<g font-family="sans-serif" fill="#e1e9ee">',
         '<text x="25" y="30" font-size="22">50 m deterministic algorithm samples | seed 12345 | 30 shots per stance</text>',
         '<text x="25" y="55" font-size="14">Compatibility model. No gravity, collision, sway or mouse correction. Single: aim resets; burst: accumulated recoil.</text>',
         '<text x="25" y="78" font-size="14">Blue: standing | green: crouched | orange: moving. Shared cm axes within each panel; panels scale independently.</text>']
colors = {'stand': '#5db7ff', 'crouch': '#67dfa2', 'move': '#ffb35d'}
for index, (key, rows) in enumerate(sorted(groups.items())):
    left, top = 25 + (index % 4) * 345, 105 + (index // 4) * 345
    extent = max(10, max(max(abs(float(r['right_cm'])), abs(float(r['up_cm']))) for r in rows)) * 1.1
    scale = 125 / extent
    cx, cy = left + 155, top + 175
    parts += [f'<rect x="{left}" y="{top}" width="325" height="325" rx="8" fill="#1a2b3b"/>',
              f'<text x="{left+10}" y="{top+23}" font-size="17">{" / ".join(key)}</text>',
              f'<text x="{left+10}" y="{top+44}" font-size="12">axes +/- {extent:.1f} cm</text>',
              f'<path d="M {cx-125} {cy} H {cx+125} M {cx} {cy-125} V {cy+125}" stroke="#708090"/>']
    for row in rows:
        x, y = cx + float(row['right_cm']) * scale, cy - float(row['up_cm']) * scale
        parts.append(f'<circle cx="{x:.3f}" cy="{y:.3f}" r="2.8" fill="{colors[row["stance"]]}" opacity=".75"><title>{row["stance"]} shot {row["shot"]}: ({row["right_cm"]}, {row["up_cm"]}) cm</title></circle>')
parts += ['</g></svg>']
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text('\n'.join(parts), encoding='utf-8')
print(f'PATTERNS_RENDERED {len(groups)} panels / 720 samples: {args.output}')
