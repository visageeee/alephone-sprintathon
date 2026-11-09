"""Startup validation tests; skip when the local game or AX scenario is absent."""
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT / 'Source_Files/sprintathon'
SCENARIO = Path.home() / 'Games/ax'


@unittest.skipUnless(BINARY.is_file() and SCENARIO.is_dir(), 'requires a built game and local AX scenario')
class TransientSettingsTest(unittest.TestCase):
    def launch(self, **settings):
        env = {key: value for key, value in os.environ.items() if not key.startswith('SPRINTATHON_')}
        env.update(SPRINTATHON_TRANSIENT_SETTINGS='1', **settings)
        return subprocess.run([str(BINARY), str(SCENARIO)], cwd=ROOT, env=env,
                              text=True, capture_output=True, timeout=15)

    def test_invalid_value_is_rejected(self):
        result = self.launch(SPRINTATHON_SETTING_VSYNC='banana')
        self.assertEqual(result.returncode, 2)
        self.assertIn('Invalid transient setting SPRINTATHON_SETTING_VSYNC', result.stderr)

    def test_incomplete_resolution_is_rejected(self):
        result = self.launch(SPRINTATHON_SETTING_WIDTH='1024')
        self.assertEqual(result.returncode, 2)
        self.assertIn('require both WIDTH and HEIGHT', result.stderr)

    def test_missing_profile_is_rejected(self):
        result = self.launch(SPRINTATHON_SETTINGS_PROFILE='/nonexistent/sprintathon-settings.xml')
        self.assertEqual(result.returncode, 2)
        self.assertIn('could not load settings profile', result.stderr)


if __name__ == '__main__':
    unittest.main()
