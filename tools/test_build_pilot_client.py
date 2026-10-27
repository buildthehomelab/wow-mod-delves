import hashlib
import json
import subprocess
import sys
from pathlib import Path
import struct
import tempfile
import unittest

from build_pilot_client import AREA, Dbc, SCHEMAS, prepare


def dbc(fields, rows, strings=b'\0old name\0'):
    return struct.pack('<4s4I', b'WDBC', len(rows), fields, fields * 4, len(strings)) + b''.join(
        struct.pack('<' + str(fields) + 'I', *r) for r in rows) + strings


def row(fields, key):
    return [key] + [0] * (fields - 1)


def chunk(tag, payload):
    return tag[::-1] + struct.pack('<I', len(payload)) + payload


class PilotClientTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.source = root / 'dbc'
        self.geometry = root / 'geometry'
        self.source.mkdir()
        self.geometry.mkdir()
        self.original = {}
        self.baseline = {}
        for name, fields in SCHEMAS.items():
            rows = [row(fields, key) for key in (0, 1, 36, 530, 571)] if name == 'Map.dbc' else [row(fields, 1)]
            if name == 'WMOAreaTable.dbc':
                rows[0][1] = 65536 + 30000  # Full-width ID wraps into the candidate root range.
            data = dbc(fields, rows)
            self.put_dbc(name, data)
        self.put('world/maps/2680/2680.wdt', chunk(b'MVER', struct.pack('<I', 18)))
        cell = bytearray(128)
        struct.pack_into('<I', cell, 52, AREA)
        terrain = chunk(b'MVER', struct.pack('<I', 18)) + chunk(b'MCNK', cell) * 256
        self.put('world/maps/2680/2680_31_31.adt', terrain)
        header = bytearray(64)
        struct.pack_into('<I', header, 32, 90000)
        self.wmo = chunk(b'MVER', struct.pack('<I', 17)) + chunk(b'MOHD', header)
        self.put('world/test.wmo', self.wmo)

    def put_dbc(self, name, data):
        (self.source / name).write_bytes(data)
        self.original[name] = data
        self.baseline[name] = hashlib.sha256(data).hexdigest()

    def put(self, name, data):
        path = self.geometry / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def build(self):
        return prepare(self.source, self.geometry, self.baseline, 3500, 20000, 30000)

    def test_preserves_every_existing_record_and_string(self):
        files, report = self.build()
        for name, fields in SCHEMAS.items():
            before = Dbc(self.original[name], fields)
            after = Dbc(files['DBFilesClient/' + name], fields)
            self.assertEqual(after.rows[:len(before.rows)], before.rows)
            self.assertTrue(after.strings.startswith(before.strings))
            self.assertEqual((self.source / name).read_bytes(), self.original[name])
        self.assertEqual(report['dbc_rows']['Map.dbc'], {'before': 5, 'after': 6})
        self.assertEqual(Dbc(files['DBFilesClient/Map.dbc'], 66).row(890)[22], AREA)
        self.assertEqual(Dbc(files['DBFilesClient/AreaTable.dbc'], 36).row(AREA)[3], 3500)

    def test_remaps_roots_using_core_narrowed_identity(self):
        files, report = self.build()
        self.assertEqual(report['wmo_roots'][0]['new_root'], 30001)
        changed = files['world/test.wmo']
        self.assertEqual(struct.unpack_from('<I', changed, 12 + 8 + 32)[0], 30001)
        self.assertEqual(changed[:52], self.wmo[:52])
        self.assertEqual(changed[56:], self.wmo[56:])
        self.assertEqual((self.geometry / 'world/test.wmo').read_bytes(), self.wmo)
        for name, data in files.items():
            if name.startswith('world/') and not name.endswith('.wmo'):
                self.assertEqual(data, (self.geometry / name).read_bytes())

    def test_rejects_baseline_mismatch_fixture_and_occupied_ids(self):
        self.baseline['Map.dbc'] = 'wrong'
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            self.build()
        self.put_dbc('Map.dbc', dbc(66, [row(66, 890)]))
        with self.assertRaisesRegex(ValueError, 'standard maps'):
            self.build()
        self.put_dbc('Map.dbc', dbc(66, [row(66, key) for key in (0, 1, 36, 530, 571, 890)]))
        with self.assertRaisesRegex(ValueError, 'occupied'):
            self.build()

    def test_rejects_area_flag_collision_and_invalid_geometry(self):
        area = row(36, 1)
        area[3] = 3500
        self.put_dbc('AreaTable.dbc', dbc(36, [area]))
        with self.assertRaisesRegex(ValueError, 'occupied'):
            self.build()
        self.put_dbc('AreaTable.dbc', dbc(36, [row(36, 1)]))
        self.put('world/test.wmo', self.wmo[:-1])
        with self.assertRaisesRegex(ValueError, 'truncated'):
            self.build()

    def test_cli_never_overwrites_or_writes_inside_inputs(self):
        baseline = Path(self.temp.name) / 'baseline.json'
        baseline.write_text(json.dumps(self.baseline))
        script = Path(__file__).with_name('build_pilot_client.py')
        for output in (self.source, self.geometry / 'nested-output'):
            result = subprocess.run([sys.executable, str(script), '--source', str(self.source),
                                     '--geometry', str(self.geometry), '--baseline', str(baseline),
                                     '--out', str(output), '--explore-flag', '3500', '--difficulty-id', '20000'],
                                    capture_output=True)
            self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.geometry / 'nested-output').exists())
        for name, data in self.original.items():
            self.assertEqual((self.source / name).read_bytes(), data)

    def test_rejects_exploration_overflow_and_malformed_dbc(self):
        with self.assertRaisesRegex(ValueError, 'exploration'):
            prepare(self.source, self.geometry, self.baseline, AREA, 20000, 30000)
        for data in (b'WDBC', self.original['Map.dbc'][:-1], self.original['Map.dbc'] + b'\0'):
            with self.assertRaises(ValueError):
                Dbc(data, 66)
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            Dbc(dbc(66, [row(66, 1), row(66, 1)]), 66)


if __name__ == '__main__':
    unittest.main()
