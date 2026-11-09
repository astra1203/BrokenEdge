"""Archive partial source recovery without presenting it as phase acceptance."""
import datetime
import gzip
import json
from pathlib import Path
import subprocess

from phase7b_source import ROOT, WORK, sha, write_json
from recover_phase7b_terrain import ASSETS


def archive():
    out = ROOT / 'Artifacts/Extraction/Phase7BEvidence'
    out.mkdir(parents=True, exist_ok=False)
    records = []

    def copy(source, destination, compress=False):
        raw = source.read_bytes()
        target = out / (destination + ('.gz' if compress else ''))
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(gzip.compress(raw, mtime=0) if compress else raw)
        records.append({'source': str(source.relative_to(ROOT)), 'archive': str(target.relative_to(out)).replace('\\', '/'),
                        'original_bytes': len(raw), 'original_sha256': sha(source), 'gzip': compress})

    terrain = WORK / 'TerrainRuns/Phase7BTerrainFinal01'
    materials = WORK / 'MaterialRuns/Phase7BMaterials03'
    verification = WORK / 'VerificationRuns/Phase7BVerification01/Report.json'
    validated = json.loads(verification.read_text(encoding='utf-8'))
    if validated['result'] != 'verification_passed' or validated['phase_status'] != 'blocked_on_original_material_data':
        raise ValueError('Expected verified recovery and an explicit blocked material gate')
    for name in ['Report.json', 'SourceExtractions.json', 'TerrainMesh.glb', 'TerrainSamples.json.gz']:
        copy(terrain / name, 'terrain/' + name)
    for name in ['Report.json', 'Graph.json', 'CustomExpressions.json']:
        copy(materials / name, 'materials/' + name, compress=name == 'Graph.json')
    copy(verification, 'Verification.json')
    copy(WORK / 'TerrainRuns/Phase7BTerrainFinalRepeat01/Report.json', 'repeat/Report.json')
    for source in sorted(materials.glob('round-*/SourceExtractions.json')):
        copy(source, 'materials/' + source.parent.name + '/SourceExtractions.json', compress=True)
    for tag in ['Phase7BSource01', 'Phase7BSource02']:
        for name in ['Report.json', 'SourceExtractions.json', 'Request.json', 'Parsed/Packages.json']:
            copy(WORK / 'SourceRuns' / tag / name, 'source/' + tag + '/' + name, compress=name.endswith('Packages.json'))
    for stem in ['CDLODAsset_Header', 'CDLODAsset_Heightmap_x5_y2', 'CDLODAsset_Heightmap_x5_y3']:
        for suffix in ['.uasset', '.uexp']:
            copy(ASSETS / (stem + suffix), 'terrain/source/' + stem + suffix, compress=True)
    for tag in ['Phase7BSource01', 'Phase7BConverter01', 'Phase7BSource02', 'Phase7BMaterials01',
                'Phase7BMaterials02', 'Phase7BMaterials03', 'Phase7BTerrain01', 'Phase7BTerrain02',
                'Phase7BTerrainRepeat01', 'Phase7BTerrainFinal01', 'Phase7BTerrainFinalRepeat01']:
        resource = ROOT / 'Saved/Extraction' / (tag + '-resources')
        for name in ['Report.json', 'Task.log']:
            copy(resource / name, 'resources/' + tag + '/' + name, compress=name.endswith('.log'))
    for tag in ['Phase7BMaterials01', 'Phase7BMaterials02']:
        copy(WORK / 'MaterialRuns' / tag / 'Report.json', 'diagnostics/' + tag + '/Report.json')
    gate = json.loads((materials / 'Report.json').read_text(encoding='utf-8'))
    proofs = []
    for item in gate['incomplete_graphs']:
        proof = item['raw_array_proof']
        source = ROOT / 'Saved/SpaceCenter/Source/DeltaForce/Content' / (item['package'].removeprefix('/Game/') + '.uexp')
        raw = source.read_bytes()
        offset, size = proof['array_uexp_offset'], proof['array_bytes']
        proofs.append({'package': item['package'], 'object': item['object'], **proof, 'source_bytes_hex': raw[offset:offset + size].hex()})
    write_json(out / 'materials/OriginalIndexArrays.json', proofs)
    scripts = ['phase7b_source.py', 'recover_phase7b_materials.py', 'recover_phase7b_terrain.py',
               'verify_phase7b_recovery.py', 'archive_phase7b_recovery.py']
    implementation = [ROOT / 'Scripts/SpaceCenter' / name for name in scripts]
    implementation += [ROOT / 'Scripts/SpaceCenter/Converter/Program.cs']
    summary = {'result': 'blocked', 'phase': '7B', 'completed_scope': 'Source height recovery and material completeness audit',
               'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
               'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'verification': {'result': 'passed', 'checks': len(validated['checks'])},
               'blocker': {'graphs_with_removed_nodes': len(gate['incomplete_graphs']),
                           'zero_source_indices': sum(len(p['null_source_indices']) for p in gate['incomplete_graphs']),
                           'raw_arrays_verified': len(proofs), 'missing_texture_packages': gate['missing_texture_packages'],
                           'required_input': 'Unstripped original material/function graphs and missing source dependencies, or equivalent original shader source.'},
               'terrain': {'patches': 110, 'vertices': validated['verified_vertices'],
                           'diagnostic_triangles': validated['verified_triangles'], 'repeat_bytes_identical': True},
               'not_performed': ['Native scene import', 'Production map replacement', 'Building-group expansion',
                                 'Multi-region streaming changes', 'UE build/package', 'Runtime/network/performance acceptance'],
               'diagnostics': {'Phase7BMaterials01': 'Dependency names parsed; earlier graph_recovered label did not establish connectivity.',
                               'Phase7BTerrain01': 'Rejected round-to-nearest tolerance; all selected extrema prove source downward quantization.'},
               'implementation_sha256': {str(p.relative_to(ROOT)).replace('\\', '/'): sha(p) for p in implementation},
               'copied_files': records}
    write_json(out / 'Summary.json', summary)
    index = {str(p.relative_to(out)).replace('\\', '/'): {'size': p.stat().st_size, 'sha256': sha(p)}
             for p in sorted(out.rglob('*')) if p.is_file()}
    write_json(out / 'ArchiveIndex.json', index)
    for name, expected in index.items():
        path = out / name
        if path.stat().st_size != expected['size'] or sha(path) != expected['sha256']:
            raise ValueError('Archive verification failed: ' + name)
    print(json.dumps({'result': 'archive_verified', 'phase_status': 'blocked', 'files': len(index),
                      'bytes': sum(p['size'] for p in index.values()), 'index_sha256': sha(out / 'ArchiveIndex.json')}))


if __name__ == '__main__':
    archive()
