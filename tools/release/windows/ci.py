import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import zipfile


ROOT = Path(__file__).resolve().parents[3]
BUILD_ROOT = ROOT.parent
LIBRARIES = BUILD_ROOT / 'Libraries' / 'win64'
THIRD_PARTY = BUILD_ROOT / 'ThirdParty'
OUT = ROOT / 'out'
DIST = OUT / 'release-dist'
FOUNDATION = [
    'patches', 'msys64', 'python', 'NuGet', 'jom', 'rust', 'lzma',
    'zlib', 'mozjpeg', 'openssl3', 'opus', 'rnnoise', 'gas-preprocessor',
    'dav1d', 'openh264', 'libavif', 'libwebp', 'libjxl', 'libvpx',
    'liblcms2', 'nv-codec-headers', 'regex', 'ffmpeg', 'libheif',
    'openal-soft', 'qt6_windows7', 'tg_angle', 'ada',
]
ENGINE = ['tg_owt', 'tde2e', 'tlottie', 'wallet-engine', 'tdesktop_rust']
GROUPS = {'foundation': FOUNDATION, 'qt': ['qt_6.11.2'], 'engine': ENGINE}
TOOL_STAGES = {'msys64', 'python', 'NuGet', 'jom', 'rust'}


def run(arguments, cwd=ROOT, environment=None):
    print('Running:', Path(str(arguments[0])).name, flush=True)
    result = subprocess.run([str(a) for a in arguments], cwd=cwd, env=environment)
    if result.returncode:
        raise RuntimeError(f'{Path(str(arguments[0])).name} failed ({result.returncode})')


def cache_paths(group):
    if group == 'qt':
        return [LIBRARIES / 'Qt-6.11.2']
    if group == 'engine':
        return [LIBRARIES / stage for stage in ENGINE]
    return [THIRD_PARTY] + [LIBRARIES / stage for stage in FOUNDATION if stage not in TOOL_STAGES] + [LIBRARIES / 'local']


def prune_library(folder):
    folder = folder.resolve()
    if not folder.is_relative_to(LIBRARIES.resolve()):
        raise RuntimeError('Pruning target is outside the dependency directory')
    keep_extensions = {'.lib', '.a', '.exe', '.h', '.hpp', '.inc', '.cmake', '.pc'}
    for path in folder.rglob('*'):
        if not path.is_file() or path.is_symlink():
            continue
        relative = path.relative_to(folder).as_posix()
        keep = (
            path.suffix.lower() in keep_extensions
            or 'include' in path.relative_to(folder).parts
            or any(part.startswith('objects-') for part in path.parts)
            or folder.name in {'patches', 'qt6_windows7'}
            or '/src/wallet_engine/' in '/' + relative
        )
        if not keep:
            try:
                path.unlink()
            except PermissionError:
                try:
                    path.chmod(path.stat().st_mode | stat.S_IWRITE)
                    path.unlink()
                except PermissionError:
                    print('Retaining locked cache file:', path, flush=True)


def run_prepare(stages):
    if os.name != 'nt' or os.environ.get('Platform', '').lower() != 'x64':
        raise RuntimeError('Use an x64 MSVC 14.44 developer environment')
    environment = os.environ.copy()
    environment['CMAKE_GENERATOR'] = 'Ninja Multi-Config'
    environment['QT_ANGLE_CMAKE'] = str(ROOT / 'cmake/external/angle/qt.cmake')
    script = ROOT / 'Telegram/build/prepare/prepare.py'
    run([sys.executable, script, 'silent', *stages], environment=environment)


def legacy_cache_key(group):
    migration = json.loads(Path(__file__).with_name('cache-migration.json').read_text(encoding='utf-8'))
    inputs = [*(ROOT / 'Telegram/build/prepare').rglob('*'),
              ROOT / 'Telegram/build/qt_version.py', Path(__file__).resolve()]
    digest = hashlib.sha256()
    for path in sorted(p for p in inputs if p.is_file() and '__pycache__' not in p.parts):
        digest.update(path.relative_to(ROOT).as_posix().encode('utf-8'))
        digest.update(hashlib.sha256(path.read_bytes().replace(b'\r\n', b'\n')).digest())
    if digest.hexdigest() != migration['compatible_inputs']:
        return ''
    return migration['keys'].get(group, '')


def prepare(group):
    run_prepare(GROUPS[group])
    expected = {
        'foundation': ['zlib/Release/libzs.lib', 'mozjpeg/Release/jpeg-static.lib',
                       'openssl3/out/libcrypto.lib', 'tg_angle/out/Release/tg_angle.lib'],
        'qt': ['Qt-6.11.2/lib/Qt6Core.lib', 'Qt-6.11.2/lib/Qt6Network.lib',
               'Qt-6.11.2/lib/Qt6Gui.lib', 'Qt-6.11.2/lib/Qt6Widgets.lib'],
        'engine': ['tg_owt/out/Release/tg_owt.lib',
                   'tde2e/out/Release/tde2e/Release/tde2e.lib',
                   'tdesktop_rust/out/lib/tdesktop_rust.lib'],
    }
    for relative in expected[group]:
        if not (LIBRARIES / relative).is_file():
            raise RuntimeError(f'Missing Release dependency: {relative}')
    if group != 'qt':
        for folder in cache_paths(group):
            if folder.parent == LIBRARIES and folder.exists():
                prune_library(folder)


def release_version():
    value = os.environ.get('VINOGRAM_VERSION', '0.1.0').removeprefix('vinogram-v')
    if not re.fullmatch(r'(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)', value):
        raise RuntimeError('Version must have the form 0.1.0')
    return value


def build():
    version = release_version()
    api_id = os.environ.get('TDESKTOP_API_ID', '')
    api_hash = os.environ.get('TDESKTOP_API_HASH', '')
    if not api_id.isdecimal() or int(api_id) <= 0 or not re.fullmatch('[a-fA-F0-9]{32}', api_hash):
        raise RuntimeError('Set the TDESKTOP_API_ID and TDESKTOP_API_HASH repository secrets')
    cache = Path(os.environ.get('RUNNER_TEMP', OUT)) / 'vinogram-api.cmake'
    cache.write_text(
        f'set(TDESKTOP_API_ID "{api_id}" CACHE STRING "" FORCE)\n'
        f'set(TDESKTOP_API_HASH "{api_hash}" CACHE STRING "" FORCE)\n',
        encoding='utf-8',
    )
    try:
        run([
            'cmake', '-S', ROOT, '-B', OUT, '-G', 'Ninja Multi-Config', '-C', cache,
            '-DCMAKE_CONFIGURATION_TYPES=Release', '-Dqt_requested=6.11.2',
            '-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=',
            '-DTDESKTOP_API_TEST=OFF', '-DDESKTOP_APP_DISABLE_AUTOUPDATE=ON',
            '-DDESKTOP_APP_DISABLE_CRASH_REPORTS=ON', '-DDESKTOP_APP_ENABLE_LTO=OFF',
        ])
    finally:
        cache.unlink(missing_ok=True)
    run(['cmake', '--build', OUT, '--config', 'Release', '--target', 'Telegram', '--parallel', '3'])
    run([sys.executable, ROOT / 'tools/subscription/prepare_xray.py', '--output', OUT / 'Release/xray'])
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'vinogram-release.json').write_text(json.dumps({'version': version}) + '\n', encoding='utf-8')


def stage_files(stage):
    binary = OUT / 'Release/Telegram.exe'
    if not binary.is_file():
        raise RuntimeError('Release Telegram.exe is missing')
    stage.mkdir(parents=True, exist_ok=False)
    shutil.copy2(binary, stage / binary.name)
    shutil.copytree(OUT / 'Release/xray', stage / 'xray')
    shutil.copy2(ROOT / 'LICENSE', stage / 'LICENSE.txt')
    shutil.copy2(ROOT / 'LEGAL', stage / 'THIRD-PARTY-NOTICES.txt')
    module = OUT / 'Release/modules/x64/d3d/d3dcompiler_47.dll'
    if not module.is_file():
        raise RuntimeError('Required D3D compiler module is missing')
    target = stage / 'modules/x64/d3d'
    target.mkdir(parents=True)
    shutil.copy2(module, target / module.name)


def package():
    version = json.loads((OUT / 'vinogram-release.json').read_text(encoding='utf-8'))['version']
    stage = OUT / 'release-stage'
    stage_files(stage)
    DIST.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which('ISCC.exe')
    if not compiler:
        for location in [os.environ.get('ProgramFiles(x86)', ''), os.environ.get('ProgramFiles', '')]:
            candidate = Path(location) / 'Inno Setup 6/ISCC.exe'
            if candidate.is_file():
                compiler = str(candidate)
                break
    if not compiler:
        raise RuntimeError('Inno Setup 6 is not installed')
    run([
        compiler, f'/DMyAppVersion={version}', f'/DReleasePath={stage}',
        f'/DOutputPath={DIST}', ROOT / 'tools/release/windows/setup.iss',
    ])
    with zipfile.ZipFile(DIST / f'vinoGram-Portable-{version}-x64.zip', 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in sorted(stage.rglob('*')):
            if path.is_file():
                archive.write(path, Path('vinoGram') / path.relative_to(stage))
    sums = []
    for path in sorted(DIST.iterdir()):
        if path.suffix in {'.exe', '.zip'}:
            with path.open('rb') as binary:
                sums.append(f'{hashlib.file_digest(binary, "sha256").hexdigest()}  {path.name}')
    (DIST / 'SHA256SUMS.txt').write_text('\n'.join(sums) + '\n', encoding='utf-8')
    print('Packages ready:', DIST)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('command', choices=['paths', 'prepare', 'refresh-foundation', 'build', 'package'])
    parser.add_argument('group', nargs='?', choices=GROUPS)
    args = parser.parse_args()
    if args.command in {'paths', 'prepare'} and not args.group:
        parser.error('Specify a dependency group')
    if args.command == 'paths':
        with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as output:
            output.write('paths<<VINOGRAM_CACHE_PATHS\n')
            output.write('\n'.join(str(path) for path in cache_paths(args.group)))
            output.write('\nVINOGRAM_CACHE_PATHS\n')
            output.write(f'legacy-key={legacy_cache_key(args.group)}\n')
    elif args.command == 'prepare':
        prepare(args.group)
    elif args.command == 'refresh-foundation':
        run_prepare(['qt6_windows7'])
    elif args.command == 'build':
        build()
    else:
        package()


if __name__ == '__main__':
    main()
