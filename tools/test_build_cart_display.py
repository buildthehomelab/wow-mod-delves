import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest

from build_cart_display import build
from build_pilot_client import Dbc


def dbc(fields, count):
    rows = b''.join(struct.pack('<' + 'I' * fields, index + 1, *([0] * (fields - 1))) for index in range(count))
    return struct.pack('<4s4I', b'WDBC', count, fields, fields * 4, 1) + rows + b'\0'


class CartDisplayTests(unittest.TestCase):
    def fixture(self, base):
        source, client = base / 'source', base / 'client'
        source.mkdir();client.mkdir()
        (source / 'CreatureModelData.dbc').write_bytes(dbc(28, 100))
        (source / 'CreatureDisplayInfo.dbc').write_bytes(dbc(16, 1000))
        baseline = base / 'baseline.json'
        baseline.write_text(json.dumps({p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in source.iterdir()}))
        data = bytearray(0x130 + 96)
        data[:8] = b'MD20' + struct.pack('<I', 264)
        struct.pack_into('<II', data, 0x3c, 2, 0x130)
        struct.pack_into('<3f', data, 0x130, -3, -2, 0)
        struct.pack_into('<3f', data, 0x130 + 48, 3, 2, 3)
        (client / 'cart.m2').write_bytes(data)
        return source, baseline, client

    def test_preserves_baseline_and_binds_named_model(self):
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder)
            source, baseline, client = self.fixture(base)
            before = {p.name: p.read_bytes() for p in source.iterdir()}
            build(source, baseline, client, 'cart.m2', 149990, 149990, base / 'out')
            for name, fields in [('CreatureModelData.dbc', 28), ('CreatureDisplayInfo.dbc', 16)]:
                old = Dbc(before[name], fields);new = Dbc((base / 'out' / name).read_bytes(), fields)
                self.assertEqual(new.rows[:-1], old.rows)
                self.assertTrue(new.strings.startswith(old.strings))
                self.assertEqual((source / name).read_bytes(), before[name])
            models = Dbc((base / 'out/CreatureModelData.dbc').read_bytes(), 28)
            displays = Dbc((base / 'out/CreatureDisplayInfo.dbc').read_bytes(), 16)
            self.assertEqual(displays.row(149990)[1], 149990)
            self.assertEqual(models.strings[models.row(149990)[2]:], b'cart.m2\0')
            self.assertEqual(struct.unpack('<f', struct.pack('<I', models.row(149990)[14]))[0], 2)

    def test_collision_hash_fixture_and_unsafe_output_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder)
            source, baseline, client = self.fixture(base)
            for model_id, model_path, output in [(1, 'cart.m2', base / 'collision'),
                                                   (149990, '../escape.m2', base / 'escape'),
                                                   (149990, 'cart.m2', client / 'inside')]:
                with self.assertRaises(ValueError):
                    build(source, baseline, client, model_path, model_id, 149990, output)
                self.assertFalse(output.exists())
            (source / 'CreatureModelData.dbc').write_bytes(dbc(28, 1))
            with self.assertRaises(ValueError):
                build(source, baseline, client, 'cart.m2', 149990, 149990, base / 'bad-hash')
            baseline.write_text(json.dumps({p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in source.iterdir()}))
            with self.assertRaises(ValueError):
                build(source, baseline, client, 'cart.m2', 149990, 149990, base / 'fixture')


if __name__ == '__main__':
    unittest.main()
