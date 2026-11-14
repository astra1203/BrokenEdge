import json
from pathlib import Path
import tempfile
import unittest
import subprocess
import sys
import uuid

from ue_env import engine_root, automation_succeeded


class EngineSelectionTests(unittest.TestCase):
    def test_automation_completion_rejects_mixed_or_incomplete_results(self):
        good = 'Test Completed. Result={Success}'
        self.assertTrue(automation_succeeded(good))
        self.assertTrue(automation_succeeded('Test Completed. Result={\u6210\u529f}'))
        self.assertTrue(automation_succeeded(good + '\n' + good, expected=2))
        for log in ['', good + '\n' + good, 'Test Started. Name={ProcessDriver}',
                    good + '\nFatal error: crash after completion',
                    good + '\nTraceback (most recent call last):',
                    *[good + '\nTest Completed. Result={' + failure + '}'
                      for failure in ('Fail', 'Failed', 'Failure', '\u5931\u8d25', 'Skipped', 'Cancelled', 'Unknown')]]:
            with self.subTest(log=log):
                self.assertFalse(automation_succeeded(log))
        with self.assertRaises(ValueError):
            automation_succeeded(good, expected=0)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'Scripts').mkdir()
        (self.root / 'BrokenEdge.uproject').write_text(json.dumps({'EngineAssociation': '5.8'}))

    def engine(self, name, minor=8):
        root = self.root / name
        binary = root / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
        binary.parent.mkdir(parents=True)
        binary.touch()
        version = root / 'Engine/Build/Build.version'
        version.parent.mkdir(parents=True)
        version.write_text(json.dumps({'MajorVersion': 5, 'MinorVersion': minor}))
        return root

    def resolve(self, explicit=None, env=None):
        return engine_root(explicit, project_root=self.root, environ=env or {})

    def test_precedence(self):
        cli, env, config = (self.engine(name) for name in ('cli', 'env', 'config'))
        (self.root / 'Scripts/local_config.json').write_text(json.dumps({'engine': str(config)}))
        self.assertEqual(self.resolve(cli, {'BROKENEDGE_UE_ROOT': str(env)}), cli)
        self.assertEqual(self.resolve(env={'BROKENEDGE_UE_ROOT': str(env)}), env)
        self.assertEqual(self.resolve(), config)

    def test_missing_configuration(self):
        with self.assertRaisesRegex(ValueError, 'Specify --engine'):
            self.resolve()

    def test_missing_installation(self):
        with self.assertRaisesRegex(ValueError, 'not found'):
            self.resolve(self.root / 'missing')

    def test_incompatible_version(self):
        with self.assertRaisesRegex(ValueError, 'does not match'):
            self.resolve(self.engine('old', minor=7))

    def test_invalid_explicit_does_not_fall_back(self):
        with self.assertRaises(ValueError):
            self.resolve(self.root / 'missing', {'BROKENEDGE_UE_ROOT': str(self.engine('valid'))})

    def test_cli_invalid_engine_never_creates_success_report(self):
        project = Path(__file__).resolve().parents[2]
        entries = [
            ('Extraction/run_editor_task.py', ['inventory'], 'Saved/Extraction'),
            ('Extraction/run_build.py', [], 'Saved/Extraction'),
            ('Extraction/run_network.py', [], 'Saved/Extraction'),
            ('Extraction/run_pie_lobby.py', [], 'Saved/Extraction'),
            ('Extraction/run_storage_restarts.py', [], 'Saved/Extraction'),
            ('Extraction/run_soviet_audit.py', [], 'Saved/Extraction'),
            ('Extraction/run_nanite_usage.py', [], 'Saved/Extraction/NaniteUsage'),
            ('Extraction/run_pie_teardown.py', [], 'Saved/Extraction'),
            ('Traversal/run_entry_tests.py', [], 'Saved/Traversal/EditorRuns'),
            ('Traversal/run_editor.py', ['geometry'], 'Saved/Traversal/EditorRuns'),
            ('Stinger/run_baseline.py', ['audit'], 'Saved/Stinger/Baseline'),
            ('Stinger/run_locomotion.py', [], 'Saved/Stinger/Locomotion/Runs'),
            ('SpaceCenter/run_editor.py', ['audit'], 'Saved/SpaceCenter/Runs'),
            ('SpaceCenter/run_restoration_editor.py', ['audit'], 'Saved/SpaceCenter/Restoration/EditorRuns'),
            ('SpaceCenter/capture_restoration_runtime.py', ['--manifest', 'missing.json'], 'Saved/SpaceCenter/Restoration/EditorRuns'),
        ]
        for engine in (self.root / 'missing', self.engine('incompatible', minor=7)):
            for script, arguments, output in entries:
                with self.subTest(script=script, engine=engine):
                    tag = 'invalid_engine_' + uuid.uuid4().hex
                    result = subprocess.run([sys.executable, str(project / 'Scripts' / script),
                                             *arguments, '--engine', str(engine), '--tag', tag],
                                            capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('error:', result.stderr)
                    self.assertIn('not found' if engine.name == 'missing' else 'does not match', result.stderr)
                    self.assertFalse((project / output / tag).exists())



if __name__ == '__main__':
    unittest.main()
