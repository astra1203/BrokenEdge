"""Check rejected build options without starting a build or writing run artifacts."""
import subprocess
import sys
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name('run_build.py')
# If an early option guard regresses, tag validation still stops the child before
# output-directory creation or any external tool invocation.
INVALID_TAG = 'cli-fastlink-invalid!'


class BuildCliTests(unittest.TestCase):
    def invoke(self, *arguments):
        return subprocess.run(
            [sys.executable, '-B', str(SCRIPT), *arguments],
            capture_output=True, text=True, encoding='utf-8', timeout=10,
        )

    def test_help_describes_fast_link_symbols(self):
        result = self.invoke('--help')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('--fast-link-symbols', result.stdout)
        self.assertIn('FASTLINK', result.stdout)

    def test_fast_link_symbols_rejects_default_and_explicit_editor(self):
        for target in ([], ['--target', 'Editor']):
            with self.subTest(target=target):
                result = self.invoke(*target, '--fast-link-symbols', '--tag', INVALID_TAG)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertIn('--fast-link-symbols requires --target Client or Server', result.stderr)
                self.assertNotIn('BUILD_STARTED', result.stdout)

    def test_fast_link_symbols_rejects_compile_only(self):
        for target in ('Client', 'Server'):
            with self.subTest(target=target):
                result = self.invoke('--target', target, '--compile-only',
                                     '--fast-link-symbols', '--tag', INVALID_TAG)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertIn('--fast-link-symbols cannot be used with --compile-only', result.stderr)
                self.assertNotIn('BUILD_STARTED', result.stdout)


if __name__ == '__main__':
    unittest.main()
