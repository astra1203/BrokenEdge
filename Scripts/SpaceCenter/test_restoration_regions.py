"""Regression tests for disjoint expansion bounds and source terrain weight placement."""
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import restoration_common as common
import build_restoration_manifest as manifest


class RegionTests(unittest.TestCase):
    def test_old_envelope_and_expansion_union(self):
        profile = dict(common.PROFILE)
        profile.pop('regions', None)
        with patch.object(common, 'PROFILE', profile), patch.object(common, 'MINIMUM', [0, 0]), patch.object(common, 'MAXIMUM', [10, 10]):
            self.assertTrue(common.intersects_region([10, 10], [11, 11]))
            self.assertFalse(common.intersects_region([11, 11], [12, 12]))
            profile['regions'] = [{'minimum': [0, 0], 'maximum': [2, 8]},
                                  {'minimum': [1, 7], 'maximum': [9, 9]}]
            self.assertTrue(common.intersects_region([1, 7], [2, 8]))
            self.assertFalse(common.intersects_region([5, 3], [6, 4]))

    def test_weight_origin_and_non_square_extent(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            entries = []
            for x in range(21, 25):
                for y in range(9, 16):
                    raw = folder / f'x{x}_y{y}.raw'
                    raw.write_bytes(struct.pack('<H', 0) * (128 * 128))
                    entries.append({'name': f'Mip0_x{x}_y{y}', 'raw_file': str(raw), 'object_path': str(raw)})
            (folder / 'Weights.json').write_text(json.dumps(entries))
            with patch.object(manifest, 'input_path', return_value=folder), patch.object(manifest, 'PROFILE', {'phase': '7E'}):
                result = manifest.weight_image(folder)
            report = json.loads((folder / 'TerrainWeightReconstruction.json').read_text())
            self.assertEqual(report['source_origin_cm'], [646800, -489600])
            self.assertEqual(report['size_cm'], [51200, 89600])
            self.assertEqual(struct.unpack('>II', Path(result['file']).read_bytes()[16:24]), (512, 896))
            self.assertEqual(result['mask_uv'][:2], [1/512, 1/896])
            self.assertEqual(result['revision'], common.sha(result['file'])[:12])
            entries.pop()
            (folder / 'Weights.json').write_text(json.dumps(entries))
            with patch.object(manifest, 'input_path', return_value=folder):
                with self.assertRaisesRegex(AssertionError, 'Incomplete weight rectangle'):
                    manifest.weight_image(folder)


if __name__ == '__main__':
    unittest.main()
