import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'out'
LIBRARIES = ROOT.parent / 'Libraries/win64'


def run(arguments):
    subprocess.run([str(value) for value in arguments], cwd=ROOT, check=True)


def main():
    api_id = os.environ.get('TDESKTOP_API_ID', '')
    api_hash = os.environ.get('TDESKTOP_API_HASH', '')
    if not api_id.isdecimal() or int(api_id) <= 0 or not re.fullmatch('[a-fA-F0-9]{32}', api_hash):
        raise RuntimeError('Configure the TDESKTOP_API_ID and TDESKTOP_API_HASH repository secrets')
    for name in ['Qt6Cored.lib', 'Qt6Networkd.lib', 'Qt6Guid.lib', 'Qt6Widgetsd.lib']:
        if not (LIBRARIES / 'Qt-6.11.2/lib' / name).is_file():
            raise RuntimeError(f'The cached Qt installation is missing the Debug library {name}')
    with tempfile.TemporaryDirectory(prefix='vinogram-debug-') as folder:
        cache = Path(folder) / 'api.cmake'
        cache.write_text(
            f'set(TDESKTOP_API_ID "{api_id}" CACHE STRING "" FORCE)\n'
            f'set(TDESKTOP_API_HASH "{api_hash}" CACHE STRING "" FORCE)\n', encoding='utf-8')
        run(['cmake', '-S', ROOT, '-B', OUT, '-G', 'Ninja Multi-Config', '-C', cache,
             '-DCMAKE_CONFIGURATION_TYPES=Debug', '-Dqt_requested=6.11.2',
             '-DTDESKTOP_API_TEST=OFF', '-DDESKTOP_APP_DISABLE_AUTOUPDATE=ON',
             '-DDESKTOP_APP_DISABLE_CRASH_REPORTS=ON', '-DDESKTOP_APP_ENABLE_LTO=OFF'])
    run(['cmake', '--build', OUT, '--config', 'Debug', '--target', 'Telegram', '--parallel', '2'])
    run([sys.executable, ROOT / 'tools/subscription/prepare_xray.py', '--output', OUT / 'Debug/xray'])
    stage = OUT / 'debug-stage/vinoGram'
    stage.mkdir(parents=True, exist_ok=False)
    shutil.copy2(OUT / 'Debug/Telegram.exe', stage / 'Telegram.exe')
    shutil.copytree(OUT / 'Debug/xray', stage / 'xray')
    shutil.copy2(ROOT / 'LICENSE', stage / 'LICENSE.txt')
    shutil.copy2(ROOT / 'LEGAL', stage / 'THIRD-PARTY-NOTICES.txt')
    module = OUT / 'Debug/modules/x64/d3d/d3dcompiler_47.dll'
    target = stage / 'modules/x64/d3d'
    target.mkdir(parents=True)
    shutil.copy2(module, target / module.name)
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    (stage / 'BUILD.txt').write_text(f'vinoGram Debug\nCommit: {revision}\n', encoding='utf-8')
    dist = OUT / 'debug-dist'
    dist.mkdir(parents=True, exist_ok=False)
    archive = Path(shutil.make_archive(str(dist / 'vinoGram-Debug-x64'), 'zip', stage.parent))
    with archive.open('rb') as source:
        checksum = hashlib.file_digest(source, 'sha256').hexdigest()
    (dist / 'SHA256SUMS.txt').write_text(f'{checksum}  {archive.name}\n', encoding='utf-8')
    print('Portable Debug client ready:', archive.name)


if __name__ == '__main__':
    main()
