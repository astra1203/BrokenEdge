"""Verify recovered samples and faithfully failing material gates independently."""
import argparse
import ast
import gzip
import json
import math
from pathlib import Path
import struct
import subprocess

from phase7b_source import ROOT, WORK, sha, write_json
from recover_phase7b_materials import audit_connectivity
from recover_phase7b_terrain import ASSETS, parse_header, read_binary_asset


def verify(tag, terrain_tag, repeat_tag, materials_tag):
    out = WORK / 'VerificationRuns' / tag
    out.mkdir(parents=True, exist_ok=False)
    terrain = WORK / 'TerrainRuns' / terrain_tag
    repeat = WORK / 'TerrainRuns' / repeat_tag
    materials = WORK / 'MaterialRuns' / materials_tag
    checks = []

    def check(name, passed):
        checks.append({'name': name, 'passed': bool(passed)})
        if not passed:
            raise AssertionError(name)

    try:
        report = json.loads((terrain / 'Report.json').read_text(encoding='utf-8'))
        check('source height sample audit passed', report['result'] == 'source_height_samples_verified')
        for name in ['TerrainMesh.glb', 'TerrainSamples.json.gz', 'Report.json']:
            check('deterministic repeat: ' + name, (terrain / name).read_bytes() == (repeat / name).read_bytes())
        data = json.loads(gzip.decompress((terrain / 'TerrainSamples.json.gz').read_bytes()))
        samples, indices = data['samples'], data['indices']
        check('unique source sample coordinates', len({tuple(p[:2]) for p in samples}) == len(samples))
        check('finite original samples and bounded RG/normal channels', all(
            all(math.isfinite(v) for v in p) and 0 <= p[2] <= 65535 and all(0 <= v <= 255 for v in p[3:]) for p in samples))
        check('valid non-degenerate triangle indices', len(indices) % 3 == 0 and all(0 <= i < len(samples) for i in indices)
              and all(len(set(indices[i:i + 3])) == 3 for i in range(0, len(indices), 3)))
        glb = (terrain / 'TerrainMesh.glb').read_bytes()
        check('valid GLB container length', struct.unpack_from('<3I', glb) == (0x46546C67, 2, len(glb)))
        json_size, json_type = struct.unpack_from('<2I', glb, 12)
        document = json.loads(glb[20:20 + json_size])
        binary_size, binary_type = struct.unpack_from('<2I', glb, 20 + json_size)
        start = 28 + json_size
        check('valid GLB chunk layout', json_type == 0x4E4F534A and binary_type == 0x004E4942 and start + binary_size == len(glb))
        vertex_bytes = document['bufferViews'][0]['byteLength']
        check('GLB vertex count agrees with source samples', vertex_bytes == len(samples) * 12)
        errors = []
        for p, xyz in zip(samples, struct.iter_unpack('<3f', glb[start:start + vertex_bytes])):
            expected_z = ((p[2] - 32768) / (report['height_scale'] * 128) + report['height_bias']) * report['scale'][2] - report['origin_cm'][2]
            errors.append(max(abs(xyz[0] * 100 + report['origin_cm'][0] - p[0]),
                              abs(xyz[2] * 100 + report['origin_cm'][1] - p[1]), abs(xyz[1] * 100 - expected_z)))
        check('every GLB vertex round-trips to source sample within 0.01 cm', max(errors) <= 0.01)
        header = read_binary_asset(ASSETS / 'CDLODAsset_Header')
        check('source quadtree connectivity', len(parse_header(header)[1]) == report['header_nodes'])
        for name, damaged in [('truncated header', header[:-1]), ('truncated section table', header[:32]),
                              ('invalid child pointer', header[:100] + struct.pack('<i', 999999) + header[104:])]:
            try:
                parse_header(damaged)
            except ValueError:
                check('rejects ' + name, True)
            else:
                check('rejects ' + name, False)
        graph = json.loads((materials / 'Graph.json').read_text(encoding='utf-8'))
        gate = json.loads((materials / 'Report.json').read_text(encoding='utf-8'))
        incomplete = audit_connectivity(graph)
        check('material gate rejects removed source nodes', gate['result'] == 'blocked' and bool(incomplete))
        check('all missing-node findings independently match original bytes', incomplete == gate['incomplete_graphs'])
        check('dependency parse success is separate from source completeness', gate['dependency_parse'] == 'passed' and gate['result'] != 'source_gate_passed')
        protected = subprocess.check_output(['git', 'status', '--porcelain=v1', '--untracked-files=all', '--',
                                             'Content', 'Config', 'Source', 'Artifacts/Extraction/Phase7AEvidence'], cwd=ROOT)
        check('production assets, native gameplay and old evidence unchanged', not protected.strip())
        for path in [ROOT / 'Scripts/SpaceCenter' / name for name in ['phase7b_source.py', 'recover_phase7b_materials.py',
                                                                    'recover_phase7b_terrain.py', 'verify_phase7b_recovery.py']]:
            ast.parse(path.read_text(encoding='utf-8-sig'), filename=str(path))
        check('changed Python syntax valid', True)
        result = {'result': 'verification_passed', 'phase_status': 'blocked_on_original_material_data',
                  'checks': checks, 'terrain_run': terrain_tag, 'repeat_run': repeat_tag, 'materials_run': materials_tag,
                  'verified_vertices': len(samples), 'verified_triangles': len(indices) // 3,
                  'source_graphs_with_removed_nodes': len(incomplete),
                  'zero_source_indices': sum(len(g['null_source_indices']) for g in incomplete),
                  'max_glb_roundtrip_error_cm': max(errors),
                  'input_sha256': {str(p.relative_to(ROOT)): sha(p) for p in [terrain / 'Report.json', materials / 'Report.json', materials / 'Graph.json']},
                  'boundary': 'Checks validate source recovery evidence and refusal to bypass missing material data. No UE build, native scene import or runtime acceptance was performed.'}
    except Exception as error:
        write_json(out / 'Report.json', {'result': 'failed', 'checks': checks, 'error': repr(error)})
        raise
    write_json(out / 'Report.json', result)
    print(json.dumps({k: v for k, v in result.items() if k not in {'checks', 'input_sha256'}} | {'checks': len(checks)}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for name in ['tag', 'terrain-tag', 'repeat-tag', 'materials-tag']:
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    if not all(value and all(c.isalnum() or c in '-_' for c in value) for value in vars(args).values()):
        parser.error('Use alphanumeric run tags')
    verify(args.tag, args.terrain_tag, args.repeat_tag, args.materials_tag)
