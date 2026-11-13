"""Pure checks for native runtime asset coverage; never start UE or cook."""
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from check_runtime_cook_coverage import (
    covered_by, cook_directories, inspect_requirement, registry_packages, inspect_cooked_output, package_path,
)


class CookCoverageTests(unittest.TestCase):
    def test_generated_class_and_object_resolve_to_source_package(self):
        expected = '/Game/UI/Looting/WBP_LootingPickupView_Pc'
        self.assertEqual(package_path(expected + '.WBP_LootingPickupView_Pc_C'), expected)
        self.assertEqual(package_path("BlueprintGeneratedClass'" + expected + ".WBP_LootingPickupView_Pc_C'"), expected)
        self.assertEqual(package_path('/Game/Items/Mesh.Mesh'), '/Game/Items/Mesh')

    def test_dynamic_and_non_game_paths_are_not_concrete_packages(self):
        for value in ('/Engine/BasicShapes/Cube.Cube', '/Game/Items/DA_%s.DA_%s',
                      '/Game/Items/', '/Game/../Other/Asset'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                package_path(value)

    def test_root_boundary_does_not_mask_missing_looting_directory(self):
        package = '/Game/UI/Looting/WBP_LootingPickupView_Pc'
        self.assertTrue(covered_by(package, ['/Game/UI/Looting/']))
        self.assertFalse(covered_by(package, ['/Game/UI/Extraction']))
        self.assertFalse(covered_by('/Game/UI/LootingExtra/Widget', ['/Game/UI/Looting']))

    def test_packaging_section_and_array_operations(self):
        ini = """
[Other]
+DirectoriesToAlwaysCook = (Path="/Game/Wrong")
[/Script/UnrealEd.ProjectPackagingSettings]
+DirectoriesToAlwaysCook = (Path="/Game/Old")
!DirectoriesToAlwaysCook = ClearArray
+DirectoriesToAlwaysCook = (Path="/Game/UI/Looting")
+DirectoriesToAlwaysCook = (Path="/Game/Remove")
-DirectoriesToAlwaysCook = (Path="/Game/Remove")
+DirectoriesToAlwaysCook = (Path="/Game/UI/Looting")
; +DirectoriesToAlwaysCook = (Path="/Game/Comment")
"""
        self.assertEqual(cook_directories(ini), ['/Game/UI/Looting'])

    def test_registry_export_uses_relocated_asset_and_rejects_stale_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            asset = root / 'Content/Extraction/Registry/DA_ItemRegistry.uasset'
            asset.parent.mkdir(parents=True)
            asset.write_bytes(b'fixture serialized registry')
            export = root / 'Config/ExtractionItemRegistry.json'
            export.parent.mkdir()
            data = dict(schema_version=1, registry='/Game/Extraction/Registry/DA_ItemRegistry.DA_ItemRegistry',
                        registry_sha256=hashlib.sha256(asset.read_bytes()).hexdigest(),
                        entries={'AK': '/Game/Relocated/Rifle.Rifle', 'Collectible': '/Game/Other/Valuable.Valuable'})
            export.write_text(json.dumps(data), encoding='utf-8')
            packages = registry_packages(root)
            self.assertIn('/Game/Relocated/Rifle', packages)
            self.assertIn('/Game/Other/Valuable', packages)
            self.assertNotIn('/Game/Extraction/Items/DA_Item_AK', packages)
            asset.write_bytes(b'edited registry')
            with self.assertRaisesRegex(ValueError, 'stale'):
                registry_packages(root)

    def test_cook_output_is_a_separate_stage(self):
        packages = ['/Game/Relocated/Rifle', '/Game/Other/Valuable']
        self.assertEqual(inspect_cooked_output(packages, None)['status'], 'not_run')
        with tempfile.TemporaryDirectory() as directory:
            content = Path(directory)
            (content / 'Relocated').mkdir()
            (content / 'Relocated/Rifle.uasset').write_bytes(b'cooked fixture')
            result = inspect_cooked_output(packages, content)
            self.assertEqual(result['status'], 'failed')
            self.assertEqual(result['missing'], ['/Game/Other/Valuable'])
            (content / 'Other').mkdir()
            (content / 'Other/Valuable.uasset').write_bytes(b'cooked fixture')
            self.assertEqual(inspect_cooked_output(packages, content)['status'], 'passed')

    def test_source_presence_and_cook_policy_are_independent(self):
        package = '/Game/UI/Looting/Widget'
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            file = root / 'Content/UI/Looting/Widget.uasset'
            file.parent.mkdir(parents=True)
            missing = inspect_requirement(root, package, ['Controller.h'], ['/Game/UI/Looting'])
            self.assertIsNone(missing['source_asset'])
            self.assertTrue(missing['cook_directory_covered'])
            file.write_bytes(b'fixture asset bytes')
            omitted = inspect_requirement(root, package, ['Controller.h'], ['/Game/UI/Extraction'])
            self.assertIsNotNone(omitted['source_asset'])
            self.assertFalse(omitted['cook_directory_covered'])
            included = inspect_requirement(root, package, ['Controller.h'], ['/Game/UI/Looting'])
            self.assertIsNotNone(included['source_asset'])
            self.assertTrue(included['cook_directory_covered'])

    def test_lfs_pointer_is_missing_source_not_a_usable_asset(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            file = root / 'Content/Items/DA_Item_AK.uasset'
            file.parent.mkdir(parents=True)
            file.write_bytes(b'version https://git-lfs.github.com/spec/v1\noid sha256:fixture\n')
            entry = inspect_requirement(root, '/Game/Items/DA_Item_AK', [], ['/Game/Items'])
            self.assertIsNone(entry['source_asset'])


if __name__ == '__main__':
    unittest.main()
