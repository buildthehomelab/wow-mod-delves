#!/usr/bin/env python3
"""Render the server's *_dbc rows for the pilot map and cart from the client DBCs. No DB connections.

The core loads Map, AreaTable, MapDifficulty, CreatureDisplayInfo and CreatureModelData from both
the DBC files and the matching acore_world *_dbc tables, so the server needs no DBC file swaps:
these rows are copied field for field from the client package, typed by the pinned core's DDL.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from build_pilot_world import database_guard

ROOT = Path(__file__).resolve().parents[1]
PILOT = ROOT / 'test-package/earthcrawl.json'
CART = ROOT / 'test-package/burning-cart.json'
PACKAGE = 'earthcrawl-server-dbc-v1'


def read_dbc(path):
    data = path.read_bytes()
    magic, count, fields, size, strings = struct.unpack_from('<4s4I', data)
    if magic != b'WDBC' or size != fields * 4:
        raise ValueError(f'{path.name} is not a plain 3.3.5 WDBC file')
    rows = [data[20 + i * size:20 + (i + 1) * size] for i in range(count)]
    return fields, rows, data[20 + count * size:20 + count * size + strings]


def columns(core, table):
    dump = (core / 'data/sql/base/db_world' / (table + '.sql')).read_text()
    ddl = re.search(r'CREATE TABLE `' + table + r'` \((.*?)\n\) ENGINE', dump, re.S)
    if not ddl:
        raise ValueError('Missing pinned core DDL: ' + table)
    found = re.findall(r'^\s*`(\w+)` (\w+)(?:\([^)]*\))?( unsigned)?', ddl.group(1), re.M)
    return [(name, kind.lower(), bool(unsigned)) for name, kind, unsigned in found]


def value(raw, strings, kind, unsigned):
    if kind in ('float', 'double'):
        return '%.9g' % struct.unpack('<f', raw)[0]
    if kind in ('varchar', 'text', 'char', 'tinytext', 'mediumtext'):
        offset = struct.unpack('<I', raw)[0]
        text = strings[offset:strings.index(b'\0', offset)].decode('utf-8')
        return "'" + text.replace('\\', '\\\\').replace("'", "''") + "'"
    return str(struct.unpack('<I' if unsigned else '<i', raw)[0])


def render(client, core, database=None):
    pilot = json.loads(PILOT.read_bytes())
    cart = json.loads(CART.read_bytes())
    wanted = [('map_dbc', 'Map.dbc', lambda r: r[0] == pilot['map']),
              ('areatable_dbc', 'AreaTable.dbc', lambda r: r[0] == pilot['area']),
              ('mapdifficulty_dbc', 'MapDifficulty.dbc', lambda r: r[1] == pilot['map']),
              ('creaturemodeldata_dbc', 'CreatureModelData.dbc', lambda r: r[0] == cart['cart_model']),
              ('creaturedisplayinfo_dbc', 'CreatureDisplayInfo.dbc', lambda r: r[0] == cart['cart_display'])]
    inserts, ids, unchanged = [], [], []
    for table, name, match in wanted:
        fields, rows, strings = read_dbc(client / name)
        cols = columns(core, table)
        if len(cols) != fields:
            raise ValueError(f'{table} has {len(cols)} columns but {name} has {fields} fields')
        found = [r for r in rows if match(struct.unpack('<%dI' % fields, r))]
        if len(found) != 1:
            raise ValueError(f'Expected exactly one pilot row in {name}, found {len(found)}')
        row = found[0]
        if table == 'mapdifficulty_dbc' and struct.unpack_from('<I', row)[0] < len(rows):
            # MapDifficulty is a sorted store: file rows sit at their position, not their ID, so a
            # SQL row with a smaller ID would replace another map's difficulty on the server.
            raise ValueError('MapDifficulty ID must not be below the DBC row count')
        values = [value(row[i * 4:i * 4 + 4], strings, kind, unsigned) for i, (_, kind, unsigned) in enumerate(cols)]
        inserts.append(f"    INSERT INTO {table} ({','.join('`' + c[0] + '`' for c in cols)})\n"
                       f"        VALUES ({','.join(values)});\n")
        ids.append((table, int(values[0])))
        comparisons = []
        for (column, kind, _), literal in zip(cols, values):
            if kind in ('varchar', 'text', 'char', 'tinytext', 'mediumtext'):
                # Byte comparison also catches case-only edits and trailing spaces
                # that the world's case-insensitive collation would otherwise hide.
                comparisons.append(f"CAST(`{column}` AS BINARY) <=> CAST({literal} AS BINARY)")
            elif kind in ('float', 'double'):
                # Compare the stored precision, rather than FLOAT to a DECIMAL
                # literal; otherwise unchanged non-integral values can mismatch.
                comparisons.append(f"`{column}` <=> CAST({literal} AS {kind.upper()})")
            else:
                comparisons.append(f"`{column}` <=> {literal}")
        unchanged.append(f"NOT EXISTS (SELECT 1 FROM {table} WHERE " +
                         " AND ".join(comparisons) + ")")
    digest = hashlib.sha256(''.join(inserts).encode()).hexdigest()
    marker = f"package = '{PACKAGE}' AND manifest_sha256 = '{digest}'"
    occupied = '\n        OR '.join(f'EXISTS (SELECT 1 FROM {t} WHERE ID = {i})' for t, i in ids)
    engine = f"""    IF (SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = DATABASE()
        AND table_name IN ({','.join("'" + t + "'" for t, _ in ids)},'mod_delves_world_package')
        AND engine = 'InnoDB') <> {len(ids) + 1} THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Server DBC rows require the InnoDB world tables and pilot receipt table';
    END IF;
"""
    install = engine + f"""    START TRANSACTION;
    IF EXISTS (SELECT 1 FROM mod_delves_world_package WHERE package = '{PACKAGE}')
        OR {occupied}
        OR EXISTS (SELECT 1 FROM mapdifficulty_dbc WHERE MapID = {pilot['map']}) THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Server DBC row collision or already installed; no rows replaced';
    END IF;
""" + ''.join(inserts) + f"""    INSERT INTO mod_delves_world_package (package,manifest_sha256) VALUES ('{PACKAGE}','{digest}');
"""
    row_edits = '\n        OR '.join(unchanged)
    rollback = engine + f"""    START TRANSACTION;
    IF NOT EXISTS (SELECT 1 FROM mod_delves_world_package WHERE {marker}) THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Missing matching server DBC receipt; rollback refused';
    END IF;
    IF EXISTS (SELECT 1 FROM mod_delves_world_package WHERE package IN ('{pilot['package']}','{cart['package']}')) THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Roll back the pilot and cart world packages before their server DBC rows';
    END IF;
    IF {row_edits} THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Server DBC rows were edited or removed; rollback refused';
    END IF;
    IF EXISTS (SELECT 1 FROM creature_template_model WHERE CreatureDisplayID = {cart['cart_display']})
        OR EXISTS (SELECT 1 FROM creature_addon WHERE mount = {cart['cart_display']})
        OR EXISTS (SELECT 1 FROM creature_template_addon WHERE mount = {cart['cart_display']})
        OR EXISTS (SELECT 1 FROM creature_model_info WHERE DisplayID = {cart['cart_display']} OR DisplayID_Other_Gender = {cart['cart_display']})
        OR EXISTS (SELECT 1 FROM player_totem_model WHERE ModelID = {cart['cart_display']})
        OR EXISTS (SELECT 1 FROM player_shapeshift_model WHERE ModelID = {cart['cart_display']})
        OR EXISTS (SELECT 1 FROM game_event_model_equip WHERE modelid = {cart['cart_display']})
        OR EXISTS (SELECT 1 FROM creaturedisplayinfo_dbc WHERE ModelID = {cart['cart_model']} AND ID <> {cart['cart_display']})
        OR EXISTS (SELECT 1 FROM mapdifficulty_dbc WHERE MapID = {pilot['map']} AND ID <> {dict(ids)['mapdifficulty_dbc']})
        OR EXISTS (SELECT 1 FROM areatable_dbc WHERE (ContinentID = {pilot['map']} OR ParentAreaID = {pilot['area']}) AND ID <> {pilot['area']})
        OR EXISTS (SELECT 1 FROM map_dbc WHERE AreaTableID = {pilot['area']} AND ID <> {pilot['map']})
        OR EXISTS (SELECT 1 FROM instance_template WHERE map = {pilot['map']})
        OR EXISTS (SELECT 1 FROM creature WHERE map = {pilot['map']})
        OR EXISTS (SELECT 1 FROM gameobject WHERE map = {pilot['map']}) THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Server DBC rows are still referenced; rollback refused';
    END IF;
""" + ''.join(f'    DELETE FROM {t} WHERE ID = {i};\n' for t, i in ids) + f"""    DELETE FROM mod_delves_world_package WHERE {marker};
"""

    def wrap(body, operation):
        procedure = 'evermore_delve_server_dbc_' + operation + '_v1'
        return f"""-- Generated by tools/build_server_dbc.py. Apply before starting the worldserver that loads the pilot.
DELIMITER //
CREATE PROCEDURE {procedure}()
BEGIN
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        RESIGNAL;
    END;
{database_guard(database)}    CREATE TABLE IF NOT EXISTS mod_delves_world_package (
        package VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL PRIMARY KEY,
        manifest_sha256 CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL
    ) ENGINE=InnoDB;
{body}    COMMIT;
END//
DELIMITER ;
CALL {procedure}();
DROP PROCEDURE {procedure};
"""
    return wrap(install, 'install'), wrap(rollback, 'rollback')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', type=Path, required=True, help='folder with the package client DBCs')
    parser.add_argument('--core', type=Path, required=True, help='pinned core checkout (for the *_dbc DDL)')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--database', help='exact world database name to allow instead of *_delve_test')
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Output must be a new directory')
    install, rollback = render(args.client, args.core, args.database)
    args.output.mkdir(parents=True)
    (args.output / 'server-dbc.install.sql').write_text(install)
    (args.output / 'server-dbc.rollback.sql').write_text(rollback)


if __name__ == '__main__':
    main()
