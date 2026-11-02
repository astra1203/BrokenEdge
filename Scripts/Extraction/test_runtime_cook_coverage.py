"""Pure checks for native runtime asset coverage; never start UE or cook."""
import tempfile
import unittest
from pathlib import Path

from check_runtime_cook_coverage import (
    covered_by, cook_directories, inspect_requirement, item_packages, package_path,
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

    def test_id_list_controls_formatted_runtime_packages(self):
        source = """
const TArray<FName>& UExtractionItemDefinition::Ids() {
static const TArray<FName> Names = {TEXT("AK"), TEXT("M4A1")}; return Names;
}
TEXT("/Game/Extraction/Items/DA_Item_%s.DA_Item_%s")
"""
        self.assertEqual(item_packages(source), ['/Game/Extraction/Items/DA_Item_AK', '/Game/Extraction/Items/DA_Item_M4A1'])
        with self.assertRaises(ValueError):
            item_packages(source.replace('TEXT("M4A1")', 'TEXT("AK")'))
        with self.assertRaises(ValueError):
            item_packages(source.replace('/Items/', '/Changed/'))

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
