from pathlib import Path
import struct
import tempfile
import unittest
import build_server_dbc

TABLES = {
    'map_dbc': ('Map.dbc', ['`ID` int NOT NULL', '`Directory` varchar(100)', '`AreaTableID` int unsigned']),
    'areatable_dbc': ('AreaTable.dbc', ['`ID` int NOT NULL', '`ContinentID` int', '`MinElevation` float']),
    'mapdifficulty_dbc': ('MapDifficulty.dbc', ['`ID` int NOT NULL', '`MapID` int', '`MaxPlayers` int']),
    'creaturemodeldata_dbc': ('CreatureModelData.dbc', ['`ID` int NOT NULL', '`Flags` int', '`ModelName` varchar(100)']),
    'creaturedisplayinfo_dbc': ('CreatureDisplayInfo.dbc', ['`ID` int NOT NULL', '`ModelID` int', '`SoundID` int']),
}


def dbc(rows, strings=b'\0'):
    data = struct.pack('<4s4I', b'WDBC', len(rows), 3, 12, len(strings))
    return data + b''.join(struct.pack('<3I', *row) for row in rows) + strings


class ServerDbcTests(unittest.TestCase):
    def build(self, folder, cart_rows=1):
        core, client = Path(folder) / 'core', Path(folder) / 'client'
        (core / 'data/sql/base/db_world').mkdir(parents=True)
        client.mkdir()
        for table, (_, cols) in TABLES.items():
            body = ',\n'.join('  ' + c for c in cols)
            (core / 'data/sql/base/db_world' / (table + '.sql')).write_text(
                f'CREATE TABLE `{table}` (\n{body},\n  PRIMARY KEY (`ID`)\n) ENGINE=InnoDB;\n')
        strings = b"\0O'Neil\\path\0"
        (client / 'Map.dbc').write_bytes(dbc([(1, 0, 0), (890, 1, 14999)], strings))
        (client / 'AreaTable.dbc').write_bytes(
            dbc([(14999, 890, struct.unpack('<I', struct.pack('<f', -1.5))[0])]))
        (client / 'MapDifficulty.dbc').write_bytes(dbc([(1, 1, 5), (20000, 890, 5)]))
        (client / 'CreatureModelData.dbc').write_bytes(dbc([(149990, 0, 1)] * cart_rows, strings))
        (client / 'CreatureDisplayInfo.dbc').write_bytes(dbc([(149990, 149990, 0)]))
        return client, core

    def test_copies_pilot_rows_typed_by_core_ddl(self):
        with tempfile.TemporaryDirectory() as folder:
            install, rollback = build_server_dbc.render(*self.build(folder), 'acore_world')
        self.assertIn("VALUES (890,'O''Neil\\\\path',14999);", install)
        self.assertIn('VALUES (14999,890,-1.5);', install)
        self.assertIn('VALUES (20000,890,5);', install)
        self.assertNotIn('VALUES (1,', install)
        self.assertIn("IF DATABASE() <> 'acore_world' THEN", install)
        self.assertNotIn('DELETE FROM', install)
        self.assertIn('DELETE FROM creaturedisplayinfo_dbc WHERE ID = 149990;', rollback)

    def test_refuses_missing_or_repeated_rows(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(ValueError):
                build_server_dbc.render(*self.build(folder, cart_rows=2))

    def test_rollback_checks_row_contents_before_deleting(self):
        with tempfile.TemporaryDirectory() as folder:
            _, rollback = build_server_dbc.render(*self.build(folder))
        self.assertIn("CAST(`Directory` AS BINARY) <=> CAST('O''Neil\\\\path' AS BINARY)", rollback)
        self.assertIn('`MinElevation` <=> CAST(-1.5 AS FLOAT)', rollback)
        self.assertIn('`ModelID` <=> 149990', rollback)
        self.assertLess(rollback.index('rows were edited or removed'), rollback.index('DELETE FROM'))
        self.assertLess(rollback.index('rows are still referenced'), rollback.index('DELETE FROM'))
        for table in ('creature_template_model', 'instance_template', 'creature', 'gameobject',
                      'creature_addon', 'creature_template_addon', 'creature_model_info',
                      'player_totem_model', 'player_shapeshift_model', 'game_event_model_equip'):
            self.assertIn('EXISTS (SELECT 1 FROM ' + table + ' WHERE', rollback)
        self.assertIn("'earthcrawl-burning-cart-v1'", rollback)

    def test_default_guard_is_isolated_test_database(self):
        with tempfile.TemporaryDirectory() as folder:
            install, _ = build_server_dbc.render(*self.build(folder))
        self.assertIn("RIGHT(DATABASE(), 11) <> '_delve_test'", install)


if __name__ == '__main__':
    unittest.main()
