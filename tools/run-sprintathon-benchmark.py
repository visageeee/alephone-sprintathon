#!/usr/bin/env python3
"""Run a stationary Sprintathon benchmark; results are stored outside the source tree."""
import argparse
import csv
import datetime
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def positive(value):
    result = int(value)
    if not 1 <= result <= 32767:
        raise argparse.ArgumentTypeError('must be between 1 and 32767')
    return result


def resolution(value):
    match = re.fullmatch(r'(\d{3,5})[xX](\d{3,5})', value)
    if not match or not all(320 <= int(x) <= 16384 for x in match.groups()):
        raise argparse.ArgumentTypeError('expected WIDTHxHEIGHT, each 320..16384')
    return tuple(map(int, match.groups()))


def percentile(values, p):
    ordered = sorted(values)
    k = (len(ordered) - 1) * p
    i = int(k)
    return ordered[i] + (ordered[min(i + 1, len(ordered) - 1)] - ordered[i]) * (k - i)


def git(*args):
    try:
        return subprocess.check_output(['git', '-C', str(ROOT), *args], stderr=subprocess.DEVNULL, text=True).strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def fields(line):
    if not line:
        return None
    result = {}
    for key, value in re.findall(r'([a-z_]+)=([^ ]+)', line):
        result[key] = int(value) if value.isdigit() else value
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scenario', default=str(Path.home() / 'Games/ax'))
    parser.add_argument('--binary', default=str(ROOT / 'Source_Files/sprintathon'))
    parser.add_argument('--map', help='override the scenario default campaign map')
    parser.add_argument('--level', type=positive, default=3, help='one-based map level (default: 3)')
    parser.add_argument('--preset', choices=['low', 'medium', 'high', 'total'], help='apply the same preset as the preferences UI in a private temporary profile')
    parser.add_argument('--resolution', type=resolution)
    parser.add_argument('--render-scale', type=int, choices=[50, 67, 75, 85, 100])
    parser.add_argument('--fps-cap', type=int, choices=[0, 30, 60, 120])
    parser.add_argument('--colored-light-intensity', type=int, choices=range(201), metavar='0..200')
    parser.add_argument('--texture-light-intensity', type=int, choices=range(201), metavar='0..200')
    parser.add_argument('--texture-light-limit', type=int, choices=range(21), metavar='0..20')
    for option in ('vsync', 'fullscreen', 'batch', 'texture-arrays', 'hud', 'soft-sector-edges', 'texture-light-sources', 'per-pixel-lighting', 'baked-texture-lighting'):
        parser.add_argument('--' + option, choices=['on', 'off'])
    parser.add_argument('--runs', type=int, default=1)
    parser.add_argument('--output', help='new results directory outside the source tree')
    args = parser.parse_args()
    binary = Path(args.binary).expanduser().resolve()
    scenario = Path(args.scenario).expanduser().resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        parser.error('executable not found: ' + str(binary))
    if not scenario.is_dir():
        parser.error('scenario directory not found: ' + str(scenario))
    if args.map and not Path(args.map).expanduser().is_file():
        parser.error('map not found: ' + args.map)
    if not 1 <= args.runs <= 20:
        parser.error('--runs must be 1..20')
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    out = Path(args.output).expanduser().resolve() if args.output else Path.home() / 'sprintathon-benchmarks' / stamp
    if out == ROOT or ROOT in out.parents:
        parser.error('results must be outside the source tree')
    try:
        out.mkdir(parents=True, exist_ok=False)
    except OSError as exc:
        parser.error('cannot create results directory: ' + str(exc))

    env = os.environ.copy()
    env.update(SPRINTATHON_AUTO_BENCHMARK='1', SPRINTATHON_BENCHMARK_LEVEL=str(args.level))
    if args.map:
        env['SPRINTATHON_BENCHMARK_MAP'] = str(Path(args.map).expanduser().resolve())
    if args.preset:
        env['SPRINTATHON_BENCHMARK_PRESET'] = str(['low', 'medium', 'high', 'total'].index(args.preset) + 1)
    if args.resolution:
        env['SPRINTATHON_BENCHMARK_WIDTH'], env['SPRINTATHON_BENCHMARK_HEIGHT'] = map(str, args.resolution)
    for attr, name in [('render_scale', 'RENDER_SCALE'), ('fps_cap', 'FPS_CAP'), ('vsync', 'VSYNC'), ('fullscreen', 'FULLSCREEN'),
                       ('batch', 'BATCH'), ('texture_arrays', 'ARRAYS'), ('hud', 'HUD'), ('soft_sector_edges', 'SOFT_EDGES'),
                       ('texture_light_sources', 'TEXTURE_LIGHT_SOURCES'), ('per_pixel_lighting', 'PER_PIXEL_LIGHTING'),
                       ('baked_texture_lighting', 'BAKED_TEXTURE_LIGHTING'), ('colored_light_intensity', 'COLORED_LIGHT_INTENSITY'),
                       ('texture_light_intensity', 'TEXTURE_LIGHT_INTENSITY'),
                       ('texture_light_limit', 'TEXTURE_LIGHT_LIMIT')]:
        value = getattr(args, attr)
        if value is not None:
            env['SPRINTATHON_BENCHMARK_' + name] = str({'on': 1, 'off': 0}.get(value, value))
    if args.texture_arrays and 'SPRINTATHON_WORLD_TEXTURE_ARRAYS' in env:
        parser.error('unset SPRINTATHON_WORLD_TEXTURE_ARRAYS when using --texture-arrays')
    if args.batch and 'SPRINTATHON_OPAQUE_BATCH' in env:
        parser.error('unset SPRINTATHON_OPAQUE_BATCH when using --batch')
    metadata = {'status': 'running', 'scenario': str(scenario), 'binary': str(binary), 'requested': vars(args),
                'warmup_s': 10, 'measurement_s': 30, 'seed': 12345,
                'revision': git('rev-parse', 'HEAD'), 'working_tree': git('status', '--short'),
                'environment': {k: v for k, v in env.items() if k.startswith('SPRINTATHON_') or k in ('DRI_PRIME', 'LIBGL_ALWAYS_SOFTWARE', 'MESA_LOADER_DRIVER_OVERRIDE')}}
    (out / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print('Results:', out, flush=True)
    print('Leave controls untouched and keep the game focused. This is not a deterministic replay.', flush=True)
    # The presets are defined by the preferences UI. Prepare a private profile
    # in a separate process so the measured process sees the real preset before
    # window/context and render-resource creation. No normal preferences are saved.
    temporary_profile = None
    if args.preset:
        temporary_profile = tempfile.TemporaryDirectory(prefix='sprintathon-settings-')
        profile = Path(temporary_profile.name) / 'profile.xml'
        prepare_env = {key: value for key, value in env.items()
                       if not key.startswith(('SPRINTATHON_BENCHMARK_', 'SPRINTATHON_SETTING_'))
                       and key != 'SPRINTATHON_SETTINGS_PROFILE'}
        prepare_env.update(SPRINTATHON_BENCHMARK_PREPARE='1',
                           SPRINTATHON_BENCHMARK_PRESET=env['SPRINTATHON_BENCHMARK_PRESET'],
                           SPRINTATHON_BENCHMARK_PROFILE_OUT=str(profile))
        preparation_log = out / 'prepare.log'
        print(f'Preparing {args.preset} preset using the preferences UI...', flush=True)
        try:
            with preparation_log.open('w') as log:
                prepared = subprocess.run([str(binary), str(scenario)], env=prepare_env, cwd=ROOT,
                                          stdout=log, stderr=subprocess.STDOUT, timeout=120)
        except (subprocess.TimeoutExpired, KeyboardInterrupt, OSError) as exc:
            metadata['status'] = 'interrupted'
            (out / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
            print(f'Preset preparation interrupted or failed: {exc}', file=sys.stderr)
            if preparation_log.exists():
                print('\n'.join(preparation_log.read_text(errors='replace').splitlines()[-35:]), file=sys.stderr)
            return 1
        if prepared.returncode or not profile.is_file():
            metadata['status'] = 'failed'
            (out / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
            print(f'Preset preparation failed (exit {prepared.returncode}); last log lines:', file=sys.stderr)
            print('\n'.join(preparation_log.read_text(errors='replace').splitlines()[-35:]), file=sys.stderr)
            return 1
        env['SPRINTATHON_SETTINGS_PROFILE'] = str(profile)
        metadata['preset_preparation'] = {'preset': args.preset, 'status': 'complete', 'log': str(preparation_log)}
        (out / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    reports = []
    for number in range(1, args.runs + 1):
        path = out / f'run-{number}.csv'
        env['SPRINTATHON_BENCHMARK_CSV'] = str(path)
        metadata.setdefault('run_environment', []).append({'run': number, 'SPRINTATHON_BENCHMARK_CSV': str(path)})
        (out / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
        log_path = out / f'run-{number}.log'
        print(f'Run {number}/{args.runs}: loading, then 10 s warm-up and 30 s measurement', flush=True)
        try:
            with log_path.open('w') as log:
                result = subprocess.run([str(binary), str(scenario)], env=env, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=600)
        except (subprocess.TimeoutExpired, KeyboardInterrupt, OSError) as exc:
            metadata['status'] = 'interrupted'
            (out / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
            print(f'Run {number} interrupted or failed: {exc}', file=sys.stderr)
            print('\n'.join(log_path.read_text(errors='replace').splitlines()[-35:]), file=sys.stderr)
            return 1
        lines = log_path.read_text(errors='replace').splitlines()
        status = Path(str(path) + '.status')
        if result.returncode or not status.exists() or status.read_text().strip() != 'complete':
            metadata['status'] = 'failed'
            (out / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
            print(f'Run {number} incomplete (exit {result.returncode}); last log lines:', file=sys.stderr)
            print('\n'.join(lines[-35:]), file=sys.stderr)
            return 1
        with path.open(newline='') as source:
            rows = list(csv.DictReader(source))
        if not rows:
            metadata['status'] = 'failed'
            (out / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
            print('No frame samples recorded.', file=sys.stderr)
            return 1
        times = [float(row['frame_ms']) for row in rows]
        report = {'run': number, 'frames': len(times), 'average_fps': 1000 / statistics.mean(times),
                  'mean_ms': statistics.mean(times), 'median_ms': statistics.median(times),
                  'p95_ms': percentile(times, .95), 'p99_ms': percentile(times, .99), 'max_ms': max(times),
                  'stage_mean_ms': {k: statistics.mean(float(row[k]) for row in rows) for k in rows[0] if k.endswith('_ms') and k != 'frame_ms'},
                  'gpu_timing': {'status': 'unavailable: per-frame benchmark GPU aggregation not yet implemented'}}
        # Setup scopes sample selected frames only. Restrict to the measured
        # interval; these are CPU wall times, not GPU execution times.
        setup = {}
        measuring = False
        for line in lines:
            if line.startswith('Benchmark: measuring'):
                measuring = True
            elif line.startswith('Benchmark: complete'):
                measuring = False
            elif measuring and line.startswith(('World setup CPU frame ', 'World preparation CPU frame ')):
                for name, elapsed, calls in re.findall(r'([a-z][a-z-]*) ([0-9.]+)/([0-9]+)', line):
                    setup.setdefault(name, []).append((float(elapsed), int(calls)))
        report['sampled_world_setup_cpu'] = {name: {'samples': len(values),
            'mean_ms': statistics.mean(v[0] for v in values), 'mean_calls': statistics.mean(v[1] for v in values)}
            for name, values in setup.items()}
        presentation = Path(str(path) + '.presentation.csv')
        if presentation.exists():
            with presentation.open(newline='') as source:
                samples = list(csv.DictReader(source))
            if samples:
                report['gpu_timing'] = {'status': 'available', 'samples': len(samples),
                    'mean_ms': {key: statistics.mean(float(row[key]) for row in samples) for key in samples[0] if key.startswith('gpu_')},
                    'note': 'GPU timestamp intervals include submission gaps, overlap CPU time, and do not cover swap.'}
                report['presentation_cpu_mean_ms'] = {key: statistics.mean(float(row[key]) for row in samples) for key in samples[0] if key.startswith('cpu_')}
        elif any('Presentation trace: unsupported' in line for line in lines):
            report['gpu_timing'] = {'status': 'unsupported by this OpenGL context'}
        elif env.get('SPRINTATHON_PRESENT_TRACE') != '1':
            report['gpu_timing'] = {'status': 'disabled; set SPRINTATHON_PRESENT_TRACE=1'}
        else:
            report['gpu_timing'] = {'status': 'no completed measured GPU samples'}
        reports.append(report)
        metadata.setdefault('run_details', []).append({'run': number, 'settings': fields(next((line for line in lines if line.startswith('Benchmark settings:')), None)),
            'map': next((line.removeprefix('Benchmark: campaign map: ') for line in lines if line.startswith('Benchmark: campaign map: ')), None),
            'graphics': [line for line in lines if line.startswith(('GL_VENDOR:', 'GL_RENDERER:', 'GL_VERSION:', 'Benchmark GL:'))],
            'display': fields(next((line for line in lines if line.startswith('Benchmark display:')), None)),
            'render_targets': [fields(line) for line in lines if line.startswith('Benchmark render target:')]})
        (out / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
        (out / 'summary.json').write_text(json.dumps(reports, indent=2) + '\n')
        print(f"Run {number}: {report['average_fps']:.1f} FPS; median {report['median_ms']:.2f}, p95 {report['p95_ms']:.2f}, p99 {report['p99_ms']:.2f} ms", flush=True)
    metadata['status'] = 'complete'
    (out / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
