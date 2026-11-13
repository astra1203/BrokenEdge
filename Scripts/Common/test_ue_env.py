import json
from pathlib import Path
import tempfile
import unittest
import subprocess
import sys
import uuid

from ue_env import engine_root


class EngineSelectionTests(unittest.TestCase):
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
        for engine in (self.root / 'missing', self.engine('incompatible', minor=7)):
            tag = 'invalid_engine_' + uuid.uuid4().hex
            result = subprocess.run([sys.executable, str(project / 'Scripts/Extraction/run_editor_task.py'),
                                     'inventory', '--engine', str(engine), '--tag', tag],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('error:', result.stderr)
            self.assertFalse((project / 'Saved/Extraction' / tag).exists())


if __name__ == '__main__':
    unittest.main()
