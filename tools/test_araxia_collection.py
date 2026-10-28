import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import araxia_collection as collection
import stage_araxia_collection as staging


class CollectionTests(unittest.TestCase):
    def test_all_locations_and_boss_sources_imported(self):
        catalog = collection.build_catalog()
        self.assertEqual(len(catalog['locations']), 13)
        self.assertEqual(catalog['boss_script_count'], 27)
        self.assertEqual(catalog['source_files'], 64)
        self.assertEqual(len(catalog['sql_files']), 23)
        self.assertEqual({r['source_map'] for r in catalog['locations'] if r['upstream_wip']}, {901, 903})
        self.assertTrue(all(not row['enabled'] for row in catalog['locations']))
        recorded = json.loads((collection.REFERENCE / 'catalog.json').read_text())
        self.assertEqual(catalog, recorded)
        old_ironforge = catalog['sql_files']['data/sql/db-world/base/delves_OldIronforge.sql']
        self.assertIn(0, old_ironforge['creature_spawn_maps'])  # Upstream content still targets the continent.

    def test_qualified_insert_preserves_strings_and_records_mutations(self):
        result = collection.read_sql("DELETE FROM `x` WHERE id=1; UPDATE `x` SET name='old'; "
                                     "INSERT INTO acore_world.x (id,name) VALUES (1,'O''Neil; safe');")
        self.assertEqual(result['tables'], {'x': [{'id': 1, 'name': "O'Neil; safe"}]})
        self.assertEqual(result['ignored_mutations'], ['DELETE', 'UPDATE'])
        for invalid in ("DROP TABLE x;", "INSERT INTO other.x (id) VALUES (1);",
                        "INSERT INTO x (id,id) VALUES (1,2);", "INSERT INTO x (id) VALUES (1)"):
            with self.assertRaises(ValueError):
                collection.read_sql(invalid)

    def test_paths_reject_traversal_and_aliases(self):
        for value in ('../outside', '/absolute', 'a/../b', 'a//b', './b', 'a\\b'):
            with self.assertRaises(ValueError):
                collection.safe_path(value)

    def fixture(self, root):
        source = root / 'source'
        (source / 'lua_scripts').mkdir(parents=True)
        data = b'original upstream script'
        (source / 'lua_scripts/boss.lua').write_bytes(data)
        manifest = {'commit': collection.COMMIT, 'files': [{'path': 'lua_scripts/boss.lua',
                    'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}]}
        path = root / 'manifest.json'
        path.write_text(json.dumps(manifest))
        return source, path

    def git(self, command, **kwargs):
        return collection.COMMIT + '\n' if 'rev-parse' in command else 'lua_scripts/boss.lua\n'

    def test_full_stage_and_overwrite_refusal(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(staging.subprocess, 'check_output', self.git):
            root = Path(folder)
            source, manifest = self.fixture(root)
            output = root / 'output'
            # Untracked native files must neither be copied nor affect the audit.
            extra = source / 'Server Map Files/mmaps/9990000.mmtile'
            extra.parent.mkdir(parents=True)
            extra.write_bytes(struct.pack('<4IB3x', 0x4d4d4150, 7, 19, 0, 0))
            result = staging.stage(source, output, manifest)
            self.assertEqual(result['files_verified'], 1)
            self.assertEqual((output / 'upstream/lua_scripts/boss.lua').read_bytes(), b'original upstream script')
            self.assertFalse((output / 'lua_scripts').exists())
            self.assertEqual(json.loads((output / 'COMPATIBILITY.json').read_text())['native_files'], 0)
            self.assertFalse((output / 'upstream/Server Map Files').exists())
            with self.assertRaises(ValueError):
                staging.stage(source, output, manifest)

    def test_modified_missing_symlink_and_unmanifested_source_refused(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(staging.subprocess, 'check_output', self.git):
            root = Path(folder)
            source, path = self.fixture(root)
            target = source / 'lua_scripts/boss.lua'
            target.write_bytes(b'changed')
            with self.assertRaises(ValueError):
                staging.stage(source, root / 'bad', path)
            self.assertFalse((root / 'bad').exists())
            target.unlink()
            with self.assertRaises(FileNotFoundError):
                staging.stage(source, root / 'bad', path)
            outside = root / 'outside'
            outside.write_bytes(b'original upstream script')
            target.symlink_to(outside)
            with self.assertRaises(ValueError):
                staging.stage(source, root / 'bad', path)
            target.unlink()
            target.write_bytes(b'original upstream script')
            with patch.object(staging.subprocess, 'check_output', side_effect=[collection.COMMIT+'\n', 'lua_scripts/boss.lua\nunrecorded\n']):
                with self.assertRaises(ValueError):
                    staging.stage(source, root / 'bad', path)

    def test_native_version_19_is_reported_without_installation(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'Server Map Files/mmaps').mkdir(parents=True)
            (root / 'Server Map Files/mmaps/9070000.mmtile').write_bytes(struct.pack('<4IB3x', 0x4d4d4150, 7, 19, 0, 0))
            report = staging.audit_native(root)
            self.assertEqual(report['mmap_versions'], {'19': 1})
            self.assertEqual(report['expected_mmap_version'], 20)
            self.assertEqual(report['header_errors'], [])
            self.assertFalse(report['ready_for_installation'])


if __name__ == '__main__':
    unittest.main()
