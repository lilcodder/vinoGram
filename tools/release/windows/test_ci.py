import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location('vinogram_ci', Path(__file__).with_name('ci.py'))
ci = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ci)


class ReleaseChecks(unittest.TestCase):
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


if __name__ == '__main__':
    unittest.main()
