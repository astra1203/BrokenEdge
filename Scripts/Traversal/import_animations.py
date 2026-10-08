"""Reuse the audited PSA decoder; import only selected files, keep all provenance."""
import json
import os
from pathlib import Path
import sys
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = Path(os.environ['TRAVERSAL_EDITOR_OUT'])
sys.path.insert(0, str(ROOT/'Scripts/Stinger'))
source = json.loads((ROOT/'Saved/Traversal/Sources.json').read_text(encoding='utf-8'))
namespace = {'__name__': 'traversal_psa'}
decoder = (ROOT/'Scripts/Stinger/repair_import.py').read_text(encoding='utf-8').split('\nselected=set(')[0]
exec(compile(decoder, 'repair_import.py', 'exec'), namespace)
namespace['SOURCE'] = ROOT/'Saved/Traversal/Source'
namespace['DEST'] = '/Game/Extraction/Traversal'
lib = unreal.EditorAssetLibrary
entries = {}
for clip in source['clips']:
    result = namespace['import_file'](Path(clip['file']))
    assert result['coverage'] >= .9, clip['name']
    for seq in result['sequences']:
        assert seq['validation'] == 'numeric_passed' and seq['assets'].get('Mapped'), clip['name']
        asset = unreal.load_asset(seq['assets']['Mapped'])
        asset.set_editor_property('enable_root_motion', False)
        asset.set_editor_property('force_root_lock', True)
        assert lib.save_loaded_asset(asset)
    entries[Path(clip['name']).stem] = result
    print('TRAVERSAL_IMPORTED', clip['name'], flush=True)
    unreal.SystemLibrary.collect_garbage()

existing = json.loads((ROOT/'Docs/Stinger/RepairImportManifest.json').read_text(encoding='utf-8'))['entries']
def animation(name):
    result = entries.get(name)
    if result is None:
        result = next(v for k, v in existing.items() if Path(k).stem == name)
    path = next(s['assets']['Mapped'] for s in result['sequences'] if s['name'] == name)
    asset = unreal.load_asset(path)
    assert asset, path
    return asset

path = '/Game/Extraction/Traversal/DA_TraversalAnimations'
asset = unreal.load_asset(path)
if not asset:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', unreal.TraversalAnimationSet)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset('DA_TraversalAnimations', '/Game/Extraction/Traversal', unreal.TraversalAnimationSet, factory)
rows = []
def row(kind, view, body, height=0, top=False):
    r = unreal.TraversalAnimation()
    r.set_editor_property('kind', kind)
    r.set_editor_property('height', height)
    r.set_editor_property('top', top)
    r.set_editor_property('first_person', animation(view))
    r.set_editor_property('third_person', animation(body))
    rows.append(r)

E = unreal.ExtractionTraversal
for height in (60,90,120):
    row(E.VAULT, f'AR_M_1P_Vault{height}', f'AR_M_3P_Climb{height}Run', height)
    row(E.MANTLE, f'AR_M_1P_Climb{height}', f'AR_M_3P_Climb{height}Stand', height)
row(E.MANTLE, 'AR_M_1P_Climb180Stand', 'AR_M_3P_Climb180Stand', 180)
row(E.LADDER, 'Unarmed_M_1P_ClimbLadderUP', 'Unarmed_M_3P_ClimbLadderUP')
for top, position in ((False,'Land'),(True,'Roof')):
    row(E.LADDER_ENTER, f'Unarmed_M_1P_ClimbLadder{position}Start', f'Unarmed_M_3P_ClimbLadder{position}Start', top=top)
    row(E.LADDER_EXIT, f'Unarmed_M_1P_ClimbLadder{position}EndL', f'Unarmed_M_3P_ClimbLadder{position}EndL', top=top)
asset.set_editor_property('animations', rows)
assert lib.save_loaded_asset(asset)
report = dict(result='passed', entries=entries, animation_set=asset.get_path_name(),
              deviations=['Third-person vault uses matching-height ClimbRun source clips; dedicated 3P Vault clips not present in converted source archives.',
                          'Ladder descent reverses height-driven UP pose; no separate DOWN source clip.'],
              visual_acceptance='pending')
(OUT/'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
