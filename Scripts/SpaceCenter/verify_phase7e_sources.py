"""Prove that the expanded source manifest preserves the accepted scene and dependencies."""
import argparse
from restoration_common import *


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--manifest', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    old_path = input_path('previous_manifest', '')
    old, new = read(old_path), read(args.manifest)
    before = {a['id']: a for a in old['actors']}
    after = {a['id']: a for a in new['actors']}
    assert len(after) == len(new['actors']), 'Duplicate source instance IDs'
    assert before.keys() <= after.keys(), 'Old scene instances disappeared'
    for ident, actor in before.items():
        assert all(actor[k] == after[ident][k] for k in ('mesh', 'transform', 'materials')), ident
    for path, mesh in old['meshes'].items():
        assert new['meshes'][path]['sha256'] == mesh['sha256'], path
    for path, recipe in old['materials'].items():
        if path.startswith('terrain:'):
            continue
        assert new['materials'][path] == recipe, path
    assert all(intersects_region(a['bounds']['min'], a['bounds']['max']) for a in new['actors'])
    assert any('RoomBuilder_CtControl_01' in a['source_package'] for a in new['actors'])
    assert any('LaunchLauncher_01' in a['mesh'] for a in new['actors'])
    for entry in new['meshes'].values():
        assert sha(entry['file']) == entry['sha256'], entry['source']
    for entry in new['texture_bindings'].values():
        assert sha(entry['file']) == entry['sha256'], entry['source']
    report = {'result': 'passed', 'previous_manifest_sha256': sha(old_path),
              'manifest_sha256': sha(args.manifest), 'preserved_instances': len(before),
              'new_instances': len(after)-len(before), 'total_instances': len(after),
              'new_meshes': len(new['meshes'].keys()-old['meshes'].keys()),
              'total_meshes': len(new['meshes']), 'terrain_tiles': len(new['terrain']['tiles']),
              'new_texture_bindings': len(new['texture_bindings'].keys()-old['texture_bindings'].keys()),
              'regions': PROFILE['regions'],
              'boundary': 'Source manifest and import-byte validation only; native visual and traversal reports are separate.'}
    write_json(Path(args.output), report)
    print(report, flush=True)


if __name__ == '__main__':
    main()
