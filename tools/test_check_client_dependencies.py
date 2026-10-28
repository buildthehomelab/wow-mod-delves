import struct
import tempfile
from pathlib import Path
import unittest

from check_client_dependencies import audit, chunks, references


def chunk(name, data):
    return name[::-1].encode('ascii') + struct.pack('<I', len(data)) + data


def model(texture=b'textures\\sample.blp\0', external=False):
    data = bytearray(0x130)
    data[:4] = b'MD20'
    struct.pack_into('<I', data, 4, 264)
    struct.pack_into('<I', data, 0x44, 1)
    struct.pack_into('<II', data, 0x50, 1, len(data))
    data += struct.pack('<4I', 0, 0, len(texture), len(data) + 16) + texture
    if external:
        struct.pack_into('<II', data, 0x1c, 1, len(data))
        sequence = bytearray(64)
        struct.pack_into('<HH', sequence, 0, 3, 1)
        data += sequence
    return data


class DependencyAuditTests(unittest.TestCase):
    def test_rejects_absent_file_empty_and_unrelated_inputs(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            with self.assertRaises(ValueError):
                audit(root / 'missing')
            with self.assertRaises(ValueError):
                audit(root)
            unrelated = root / 'readme.txt'
            unrelated.write_text('not a converted candidate')
            with self.assertRaises(ValueError):
                audit(unrelated)
            with self.assertRaises(ValueError):
                audit(root)

    def test_model_companions_and_missing_file(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'unit.m2').write_bytes(model())
            (root / 'unit00.skin').write_bytes(b'skin-fixture-not-decoded')
            (root / 'textures').mkdir()
            texture = root / 'textures/sample.blp'
            texture.write_bytes(b'texture-fixture-not-decoded')
            report = audit(root)
            self.assertEqual(report['errors'], [])
            self.assertEqual(report['missing'], [])
            texture.unlink()
            report = audit(root)
            self.assertEqual(report['missing'], [{'source': 'unit.m2', 'target': 'textures/sample.blp'}])

    def test_external_animation_derived_name(self):
        refs, dynamic = references(Path('models/unit.m2'), model(external=True))
        self.assertIn('models/unit0003-01.anim', refs)
        self.assertEqual(dynamic, [])

    def test_repeat_terrain_chunks_and_truncation(self):
        data = chunk('MVER', struct.pack('<I', 18)) + chunk('MCNK', b'first') + chunk('MCNK', b'second')
        self.assertIn('MCNK', chunks(data))
        with self.assertRaises(ValueError):
            chunks(data[:-1])

    def test_world_groups_and_active_tile(self):
        header = struct.pack('<16I', 0, 2, *([0] * 14))
        refs, _ = references(Path('world/a.wmo'), chunk('MVER', struct.pack('<I', 17)) + chunk('MOHD', header))
        self.assertEqual(refs, ['world/a_000.wmo', 'world/a_001.wmo'])
        main = bytearray(64 * 64 * 8)
        struct.pack_into('<I', main, (7 * 64 + 9) * 8, 1)
        refs, _ = references(Path('world/maps/test/test.wdt'), chunk('MVER', struct.pack('<I', 18)) + chunk('MAIN', main))
        self.assertEqual(refs, ['world/maps/test/test_9_7.adt'])

    def test_unsafe_and_out_of_bounds_texture(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            path = root / 'unit.m2'
            path.write_bytes(model(b'../escape.blp\0'))
            self.assertTrue(audit(root)['errors'])
            data = model()
            struct.pack_into('<I', data, 0x50 + 4, len(data) + 1)
            path.write_bytes(data)
            self.assertTrue(audit(root)['errors'])


if __name__ == '__main__':
    unittest.main()
