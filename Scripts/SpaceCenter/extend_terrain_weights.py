"""Decode source weight tiles covering an expanded profile, reusing verified layer arrays."""
import argparse
import math
from restoration_common import *


def main(tag):
    out = WORK / 'TerrainTextureRuns' / tag
    out.mkdir(parents=True, exist_ok=False)
    original = read(input_path('terrain_texture_seed', 'TerrainTextureRuns/Seed') / 'Report.json')
    # Source clipmap coordinates, established by the original 21,10 tile at 646800,-476800.
    base = [378000, -604800]
    low = [math.floor((MINIMUM[i] - base[i]) / 12800) for i in (0, 1)]
    high = [math.floor((MAXIMUM[i] - base[i]) / 12800) for i in (0, 1)]
    root = '/Game/Maps/BuildMaps_HD/SpaceCenter_Ma1/Runtime/LandscapeCDLOD/WeightmapClipmap/'
    names = [root + f'Mip0_x{x}_y{y}' for x in range(low[0], high[0]+1)
             for y in range(low[1], high[1]+1)]
    extract_recorded(names, out / 'Weights')
    packages = run_converter('rawtexture', names, out / 'Weights/Raw')
    weights = [o for p in packages for o in p.get('objects', []) if o.get('pixel_format') == 'PF_G16']
    assert len(weights) == len(names), 'Missing original terrain weight tiles'
    assert all(o['raw_width'] == o['raw_height'] == 128 for o in weights)
    write_json(out / 'Weights.json', weights)
    write_json(out / 'Report.json', original | {'weight_tiles': len(weights), 'tile_min': low, 'tile_max': high})
    print('TERRAIN_WEIGHTS', len(weights), low, high, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--tag', required=True)
    main(parser.parse_args().tag)
