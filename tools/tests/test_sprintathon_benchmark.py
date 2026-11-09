import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

RUNNER = Path(__file__).resolve().parents[1] / 'run-sprintathon-benchmark.py'


class RunnerTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.scenario = self.root / 'scenario'
        self.scenario.mkdir()
        self.binary = self.root / 'fake-game'
        self.binary.write_text('''#!/usr/bin/env python3
import os, pathlib
if os.environ.get('SPRINTATHON_BENCHMARK_PREPARE'):
    if os.environ.get('FAIL_PREPARE'):
        print('preset preparation failed deliberately')
        raise SystemExit(2)
    assert 'SPRINTATHON_BENCHMARK_BATCH' not in os.environ
    pathlib.Path(os.environ['SPRINTATHON_BENCHMARK_PROFILE_OUT']).write_text('private profile')
    raise SystemExit(0)
if os.environ.get('SPRINTATHON_BENCHMARK_PRESET'):
    assert pathlib.Path(os.environ['SPRINTATHON_SETTINGS_PROFILE']).is_file()
path = pathlib.Path(os.environ['SPRINTATHON_BENCHMARK_CSV'])
if os.environ.get('FAIL_FAKE'):
    print('Benchmark: simulated startup failure')
    raise SystemExit(2)
print('Benchmark settings: preset=1 width=1280 height=720')
print('Benchmark: campaign map: /scenario/map')
path.write_text('time_s,frame_ms,render_ms,swap_ms\\n1,20,8,10\\n2,25,9,12\\n')
pathlib.Path(str(path)+'.status').write_text('complete\\n')
if os.environ.get('SPRINTATHON_PRESENT_TRACE') == '1':
    pathlib.Path(str(path)+'.presentation.csv').write_text('frame_id,gpu_world_ms,cpu_world_ms\\n7,3.5,8.0\\n')
''')
        self.binary.chmod(0o755)

    def tearDown(self):
        self.temp.cleanup()

    def run_benchmark(self, *args, env=None):
        return subprocess.run(['python3', str(RUNNER), '--binary', str(self.binary),
                               '--scenario', str(self.scenario), '--output', str(self.root / 'out'), *args],
                              text=True, capture_output=True, env=env)

    def test_summary_and_overrides(self):
        result = self.run_benchmark('--resolution', '1280x720', '--level', '3', '--vsync', 'off', '--fullscreen', 'on', '--texture-light-sources', 'off', '--runs', '1')
        self.assertEqual(result.returncode, 0, result.stderr)
        metadata = json.loads((self.root / 'out/metadata.json').read_text())
        report = json.loads((self.root / 'out/summary.json').read_text())[0]
        self.assertEqual(metadata['status'], 'complete')
        self.assertEqual(metadata['requested']['resolution'], [1280, 720])
        self.assertEqual(metadata['environment']['SPRINTATHON_BENCHMARK_VSYNC'], '0')
        self.assertEqual(metadata['environment']['SPRINTATHON_BENCHMARK_FULLSCREEN'], '1')
        self.assertEqual(metadata['environment']['SPRINTATHON_BENCHMARK_TEXTURE_LIGHT_SOURCES'], '0')
        self.assertEqual(metadata['run_details'][0]['map'], '/scenario/map')
        self.assertAlmostEqual(report['mean_ms'], 22.5)
        self.assertIn('disabled', report['gpu_timing']['status'])

    def test_gpu_samples_are_reported_when_available(self):
        env = dict(os.environ, SPRINTATHON_PRESENT_TRACE='1')
        result = self.run_benchmark(env=env)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads((self.root / 'out/summary.json').read_text())[0]
        self.assertEqual(report['gpu_timing']['status'], 'available')
        self.assertEqual(report['gpu_timing']['mean_ms']['gpu_world_ms'], 3.5)

    def test_preset_preparation_uses_private_profile(self):
        result = self.run_benchmark('--preset', 'low', '--batch', 'off', '--per-pixel-lighting', 'on',
                                    '--colored-light-intensity', '100', '--texture-light-intensity', '125', '--texture-light-limit', '4')
        self.assertEqual(result.returncode, 0, result.stderr)
        metadata = json.loads((self.root / 'out/metadata.json').read_text())
        self.assertEqual(metadata['preset_preparation']['status'], 'complete')
        self.assertEqual(metadata['environment']['SPRINTATHON_BENCHMARK_BATCH'], '0')
        self.assertEqual(metadata['environment']['SPRINTATHON_BENCHMARK_COLORED_LIGHT_INTENSITY'], '100')
        self.assertEqual(metadata['environment']['SPRINTATHON_BENCHMARK_TEXTURE_LIGHT_INTENSITY'], '125')
        self.assertEqual(metadata['environment']['SPRINTATHON_BENCHMARK_TEXTURE_LIGHT_LIMIT'], '4')

    def test_preset_preparation_failure_is_reported(self):
        env = dict(os.environ, FAIL_PREPARE='1')
        result = self.run_benchmark('--preset', 'low', env=env)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('preset preparation failed deliberately', result.stderr)
        self.assertEqual(json.loads((self.root / 'out/metadata.json').read_text())['status'], 'failed')

    def test_bad_arguments_rejected(self):
        result = self.run_benchmark('--resolution', 'bogus')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('expected WIDTHxHEIGHT', result.stderr)
        self.assertFalse((self.root / 'out').exists())

    def test_failure_displays_reason_and_no_summary(self):
        env = dict(os.environ, FAIL_FAKE='1')
        result = self.run_benchmark(env=env)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('simulated startup failure', result.stderr)
        self.assertFalse((self.root / 'out/summary.json').exists())
        self.assertEqual(json.loads((self.root / 'out/metadata.json').read_text())['status'], 'failed')


if __name__ == '__main__':
    unittest.main()
