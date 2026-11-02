"""Pure manifest, backup and invariant checks; no Unreal import or process launch."""
import copy
import json
from pathlib import Path
import tempfile
import unittest

from repair_nanite_usage import (
    ALLOWLIST, asset_relative_path, backup_asset, check_only_nanite_changed,
    non_usage_overrides, preflight_all, sha, validate_manifest,
)
from run_nanite_usage import create_run_directory


class NaniteUsageTests(unittest.TestCase):
    def manifest(self):
        return json.loads(Path(__file__).with_name('nanite_usage_manifest.json').read_text(encoding='utf-8'))

    def test_manifest_is_exact_fixed_twelve(self):
        self.assertEqual(set(validate_manifest(self.manifest())), ALLOWLIST)
        self.assertEqual(len(ALLOWLIST), 12)
        for change in ('omit', 'duplicate', 'replace', 'parent'):
            data = self.manifest()
            if change == 'omit':
                data['assets'].pop()
            elif change == 'duplicate':
                data['assets'][-1] = data['assets'][0]
            elif change == 'replace':
                data['assets'][-1]['package'] = '/Game/Unreviewed/Material'
            else:
                data['expected_parent'] = '/Game/AnotherParent'
            with self.subTest(change=change), self.assertRaises(ValueError):
                validate_manifest(data)

    def test_backup_keeps_same_named_assets_distinct_and_never_overwrites(self):
        packages = [p for p in ALLOWLIST if p.endswith('/MI_H_Building_FutureTech_AutomaticWorkshop_01_Decal_HD')]
        self.assertEqual(len(packages), 2)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            out = create_run_directory(root, 'repair01')
            backups = []
            for index, package in enumerate(packages):
                file = root / asset_relative_path(package)
                file.parent.mkdir(parents=True)
                file.write_bytes(f'unique asset {index}'.encode())
                path = backup_asset(root, out, package, sha(file))
                self.assertEqual((out / path).read_bytes(), file.read_bytes())
                backups.append(path)
                with self.assertRaises(FileExistsError):
                    backup_asset(root, out, package, sha(file))
            self.assertNotEqual(*backups)
            with self.assertRaises(FileExistsError):
                create_run_directory(root, 'repair01')
            with self.assertRaises(ValueError):
                create_run_directory(root, '../escape')
            with self.assertRaises(ValueError):
                asset_relative_path('/Game/Other/Material')

    def test_stale_source_is_rejected_before_backup(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            out = create_run_directory(root, 'stale01')
            package = next(iter(ALLOWLIST))
            file = root / asset_relative_path(package)
            file.parent.mkdir(parents=True)
            file.write_bytes(b'new bytes')
            with self.assertRaises(RuntimeError):
                backup_asset(root, out, package, '0' * 64)
            self.assertFalse((out / 'Backup').exists())

    def test_all_preflight_errors_are_collected_before_mutation_stage(self):
        visited = []
        def inspect(package):
            visited.append(package)
            if package in ('bad1', 'bad2'):
                raise ValueError('preflight rejected')
            return {'ok': True}
        states, errors = preflight_all(['bad1', 'good', 'bad2'], inspect)
        self.assertEqual(visited, ['bad1', 'good', 'bad2'])
        self.assertEqual(list(states), ['good'])
        self.assertEqual([row['package'] for row in errors], ['bad1', 'bad2'])

    def test_only_usage_fields_are_removed_from_struct_snapshot(self):
        text = '(bOverride_BlendMode=False,bOverride_UsageFlags=0,BlendMode=BLEND_Opaque,UsageFlags=0)'
        changed = text.replace('bOverride_UsageFlags=0', 'bOverride_UsageFlags=262144').replace('UsageFlags=0)', 'UsageFlags=262144)')
        self.assertEqual(non_usage_overrides(text), non_usage_overrides(changed))
        self.assertNotEqual(non_usage_overrides(text), non_usage_overrides(text.replace('BLEND_Opaque', 'BLEND_Masked')))
        with self.assertRaises(ValueError):
            non_usage_overrides('(UsageFlags=unknown)')

    def test_parameter_parent_and_other_usage_drift_fail(self):
        before = dict(sha256='before', parent='parent', parameters={'texture': ['same']},
                      nanite_enabled=False, nanite_override=False,
                      all_usage={'MATUSAGE_NANITE': False, 'MATUSAGE_STATIC_MESH': True})
        after = copy.deepcopy(before)
        after.update(sha256='after', nanite_enabled=True, nanite_override=True)
        after['all_usage']['MATUSAGE_NANITE'] = True
        check_only_nanite_changed(before, after)
        for change in ('parent', 'parameters', 'usage', 'missing_override'):
            bad = copy.deepcopy(after)
            if change == 'parent':
                bad['parent'] = 'different'
            elif change == 'parameters':
                bad['parameters']['texture'] = ['different']
            elif change == 'usage':
                bad['all_usage']['MATUSAGE_STATIC_MESH'] = False
            else:
                bad['nanite_override'] = False
            with self.subTest(change=change), self.assertRaises(RuntimeError):
                check_only_nanite_changed(before, bad)


if __name__ == '__main__':
    unittest.main()
