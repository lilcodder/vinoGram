import ast
import importlib.util
import os
import re
import shutil
import stat
import subprocess
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location('vinogram_ci', Path(__file__).with_name('ci.py'))
ci = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ci)


class ReleaseChecks(unittest.TestCase):
    @unittest.skipUnless(os.name == 'nt', 'Requires Windows cmd.exe')
    def test_batch_failures_inside_parentheses_stop_preparation(self):
        script = ci.ROOT / 'Telegram/build/prepare/prepare.py'
        tree = ast.parse(script.read_text(encoding='utf-8'))
        function = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == 'winFailOnEach')
        namespace = {'re': re}
        exec(compile(ast.Module(body=[function], type_ignores=[]), str(script), 'exec'), namespace)
        with tempfile.TemporaryDirectory() as directory:
            batch = Path(directory) / 'check.cmd'
            for status in [0, 23, -1]:
                commands = f'for %%i in (first second) do (\ncmd /c exit {status}\nif errorlevel 1 (\necho FAILED\nexit /b 1\n)\n)\necho FINISHED\n'
                batch.write_bytes(('@echo off\r\n' + namespace['winFailOnEach'](commands)).encode('utf-8'))
                result = subprocess.run([os.environ['COMSPEC'], '/d', '/c', str(batch)], capture_output=True, text=True)
                self.assertEqual(result.returncode == 0, status == 0)
                self.assertEqual('FINISHED' in result.stdout, status == 0)

    def test_qt_uses_the_configurations_installed_by_the_recipe(self):
        with tempfile.TemporaryDirectory() as directory:
            libraries = Path(directory)
            qt = libraries / 'Qt-6.11.2/lib'
            qt.mkdir(parents=True)
            for module in ['Core', 'Network', 'Gui', 'Widgets']:
                (qt / f'Qt6{module}.lib').touch()
            with patch.object(ci, 'LIBRARIES', libraries), patch.object(ci, 'run_prepare') as recipe, patch.object(ci, 'run', side_effect=AssertionError('Qt is already built and installed')):
                ci.prepare('qt')
                recipe.assert_called_once_with(ci.GROUPS['qt'])
                (qt / 'Qt6Widgets.lib').unlink()
                with self.assertRaisesRegex(RuntimeError, 'Qt6Widgets.lib'):
                    ci.prepare('qt')

    def test_legacy_caches_are_limited_to_verified_inputs_and_groups(self):
        self.assertTrue(ci.legacy_cache_key('foundation'))
        self.assertTrue(ci.legacy_cache_key('engine'))
        self.assertTrue(ci.legacy_cache_key('qt'))
        self.assertEqual(ci.legacy_cache_key('unknown'), '')
        with patch.object(ci.hashlib, 'sha256') as digest:
            digest.return_value.hexdigest.return_value = 'changed'
            digest.return_value.digest.return_value = b'changed'
            self.assertEqual(ci.legacy_cache_key('foundation'), '')

    @unittest.skipUnless(os.name == 'nt' and os.environ.get('Platform', '').lower() == 'x64', 'Requires x64 MSVC')
    def test_release_link_does_not_generate_a_full_pdb(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            environment = {
                'TDESKTOP_API_ID': '1', 'TDESKTOP_API_HASH': '0' * 32,
                'VINOGRAM_VERSION': '0.1.0', 'RUNNER_TEMP': directory,
            }
            with patch.dict(os.environ, environment), patch.object(ci, 'OUT', root / 'out'), patch.object(ci, 'run') as commands:
                ci.build()
            configure = commands.call_args_list[0].args[0]
            option = '-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT='
            self.assertIn(option, configure)
            source = root / 'probe'
            source.mkdir()
            (source / 'main.cpp').write_text('int main() { return 0; }\n', encoding='utf-8')
            (source / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.25)\n'
                'project(LinkProbe LANGUAGES CXX)\n'
                'set(build_win64 ON)\n'
                f'include("{ci.ROOT.as_posix()}/cmake/variables.cmake")\n'
                'add_library(common_options INTERFACE)\n'
                f'include("{ci.ROOT.as_posix()}/cmake/options_win.cmake")\n'
                'add_executable(probe main.cpp)\n'
                'target_link_libraries(probe PRIVATE common_options)\n', encoding='utf-8')
            binary = root / 'probe-build'
            cmake = shutil.which('cmake')
            subprocess.run([cmake, '-S', str(source), '-B', str(binary), '-G', 'Ninja Multi-Config',
                            '-DCMAKE_CONFIGURATION_TYPES=Release', option], check=True, capture_output=True)
            ninja = (binary / 'CMakeFiles/impl-Release.ninja').read_text(encoding='utf-8')
            self.assertIn('/DEBUG:NONE', ninja)
            self.assertNotRegex(ninja, r'/DEBUG(?:\s|:FULL)')
            self.assertNotRegex(ninja, r'/(?:Z7|Zi|ZI)\b')
            subprocess.run([cmake, '--build', str(binary), '--config', 'Release'], check=True, capture_output=True)
            self.assertTrue((binary / 'Release/probe.exe').is_file())
            self.assertFalse(list(binary.rglob('*.pdb')))

    def test_versions_cannot_be_shell_commands_or_paths(self):
        for value in ['0.1.0', 'vinogram-v1.2.3']:
            with patch.dict(os.environ, {'VINOGRAM_VERSION': value}):
                self.assertRegex(ci.release_version(), r'^\d+\.\d+\.\d+$')
        for value in ['../0.1.0', '0.1.0;echo hello', '0.1.0-beta', '01.2.3']:
            with patch.dict(os.environ, {'VINOGRAM_VERSION': value}):
                with self.assertRaises(RuntimeError):
                    ci.release_version()

    def test_staging_excludes_accounts_and_private_build_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, output, stage = root / 'source', root / 'out', root / 'stage'
            source.mkdir()
            (source / 'LICENSE').write_text('GPL', encoding='utf-8')
            (source / 'LEGAL').write_text('Notices', encoding='utf-8')
            release = output / 'Release'
            for name in ['Telegram.exe', 'Telegram.pdb', 'api.local.json', 'log.txt',
                         'tdata/key_data', 'xray/xray.exe', 'xray/LICENSE',
                         'modules/x64/d3d/d3dcompiler_47.dll']:
                path = release / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'test')
            with patch.object(ci, 'ROOT', source), patch.object(ci, 'OUT', output):
                ci.stage_files(stage)
            files = {p.relative_to(stage).as_posix() for p in stage.rglob('*') if p.is_file()}
            self.assertEqual(files, {
                'Telegram.exe', 'xray/xray.exe', 'xray/LICENSE',
                'modules/x64/d3d/d3dcompiler_47.dll',
                'LICENSE.txt', 'THIRD-PARTY-NOTICES.txt',
            })

    def test_pruning_keeps_build_inputs_and_checks_its_root(self):
        with tempfile.TemporaryDirectory() as directory:
            libraries = Path(directory) / 'Libraries'
            folder = libraries / 'tdesktop_rust'
            for name in ['out/lib/a.lib', 'out/include/no-extension',
                         'out/src/wallet_engine/wallet_engine.cpp', 'target/a.obj']:
                path = folder / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('test', encoding='utf-8')
            with patch.object(ci, 'LIBRARIES', libraries):
                ci.prune_library(folder)
                with self.assertRaises(RuntimeError):
                    ci.prune_library(Path(directory))
            self.assertTrue((folder / 'out/lib/a.lib').is_file())
            self.assertTrue((folder / 'out/include/no-extension').is_file())
            self.assertTrue((folder / 'out/src/wallet_engine/wallet_engine.cpp').is_file())
            self.assertFalse((folder / 'target/a.obj').exists())

    def test_dependency_groups_do_not_overlap(self):
        groups = [set(paths) for paths in ci.GROUPS.values()]
        for index, group in enumerate(groups):
            for other in groups[index + 1:]:
                self.assertFalse(group & other)

    def test_pruning_removes_read_only_git_pack_files(self):
        with tempfile.TemporaryDirectory() as directory:
            libraries = Path(directory) / 'Libraries'
            folder = libraries / 'lzma'
            pack = folder / '.git/objects/pack/test.idx'
            pack.parent.mkdir(parents=True)
            pack.write_bytes(b'git pack index')
            pack.chmod(stat.S_IREAD)
            with patch.object(ci, 'LIBRARIES', libraries):
                ci.prune_library(folder)
            self.assertFalse(pack.exists())


if __name__ == '__main__':
    unittest.main()
