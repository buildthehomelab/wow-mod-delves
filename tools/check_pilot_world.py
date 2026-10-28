#!/usr/bin/env python3
"""Destructive SQL checks for a disposable local CI database, never realm data."""
import argparse
from pathlib import Path
import re
import struct
import subprocess
import tempfile
from build_pilot_world import render
from build_cart_world import render as render_cart
import build_server_dbc

TABLES = ('instance_template', 'creature_template', 'creature_template_model',
          'gameobject_template', 'gameobject_template_addon', 'creature', 'gameobject')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, required=True)
    parser.add_argument('--database', required=True)
    args = parser.parse_args()
    if args.database != 'world_delve_test':
        parser.error('Only the disposable world_delve_test database is allowed')
    command = ['mysql', '--protocol=tcp', '-h', '127.0.0.1', '-uroot', '--batch', '--skip-column-names', args.database]

    def sql(text, success=True):
        result = subprocess.run(command, input=text, text=True, capture_output=True)
        if (result.returncode == 0) != success:
            raise AssertionError('Unexpected SQL result: ' + result.stderr)
        return result.stdout.strip()

    def clean_procedures():
        sql('DROP PROCEDURE IF EXISTS evermore_delve_install_v1; DROP PROCEDURE IF EXISTS evermore_delve_rollback_v1;')
        sql('DROP PROCEDURE IF EXISTS evermore_delve_cart_install_v1; DROP PROCEDURE IF EXISTS evermore_delve_cart_rollback_v1;')

    def empty():
        sql('SET FOREIGN_KEY_CHECKS=0;\n' + '\n'.join('DROP TABLE IF EXISTS ' + t + ';' for t in (*TABLES, 'creature_multispawn', 'mod_delves_world_package')))
        clean_procedures()
        for table in TABLES:
            dump = (args.core / 'data/sql/base/db_world' / (table + '.sql')).read_text()
            ddl = re.search(r'CREATE TABLE `' + table + r'` \(.*?\) ENGINE=.*?;', dump, re.S)
            if not ddl:
                raise AssertionError('Missing pinned core DDL: ' + table)
            sql(ddl.group())
        # The base dump still has creature.id1/id2/id3; this pinned-core update renames id1 to id and
        # moves id2/id3 into creature_multispawn, which is what the core reads and live realms have.
        sql((args.core / 'data/sql/updates/db_world/2026_06_16_00.sql').read_text())

    install, rollback = render()
    empty()
    sql(render('some_other_world')[0], False)  # An exact-name package refuses any other database.
    clean_procedures()
    assert sql('SELECT COUNT(*) FROM creature_template;') == '0'
    sql(render(args.database)[0])
    sql(render(args.database)[1])
    sql(install)
    assert sql('SELECT COUNT(*) FROM creature_template;') == '2'
    assert sql('SELECT COUNT(*) FROM gameobject;') == '4'
    assert sql('SELECT COUNT(*) FROM mod_delves_world_package;') == '1'
    sql(install, False)  # Existing receipt/IDs may never be replaced.
    clean_procedures()
    assert sql('SELECT COUNT(*) FROM gameobject;') == '4'
    sql(rollback)
    assert sql('SELECT COUNT(*) FROM creature_template;') == '0'
    assert sql('SELECT COUNT(*) FROM gameobject;') == '0'
    assert sql('SELECT COUNT(*) FROM instance_template;') == '0'
    assert sql('SELECT COUNT(*) FROM mod_delves_world_package;') == '0'
    sql(rollback, False)  # No receipt: no rollback authority.
    clean_procedures()

    # A pre-existing creature with the same ID is left intact, not DELETE/REPLACE'd.
    sql("INSERT INTO creature_template(entry,name) VALUES (1499000,'sentinel');")
    sql(install, False)
    clean_procedures()
    assert sql('SELECT name FROM creature_template WHERE entry=1499000;') == 'sentinel'
    assert sql('SELECT COUNT(*) FROM instance_template;') == '0'

    # Fail after template/spawn writes: the entire content transaction rolls back.
    empty()
    sql("CREATE TRIGGER reject_delve_object BEFORE INSERT ON gameobject FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='injected failure';")
    sql(install, False)
    clean_procedures()
    for table in (*TABLES, 'mod_delves_world_package'):
        assert sql('SELECT COUNT(*) FROM ' + table + ';') == '0', table
    sql('DROP TRIGGER reject_delve_object;')

    # Rollback refuses unknown actors and restores its earlier deletions on failure.
    sql(install)
    sql("INSERT INTO creature(guid,id,map,Comment) VALUES (14990001,1499000,890,'unexpected actor');")
    sql(rollback, False)
    clean_procedures()
    assert sql('SELECT COUNT(*) FROM gameobject;') == '4'
    assert sql('SELECT COUNT(*) FROM creature;') == '2'
    assert sql('SELECT COUNT(*) FROM mod_delves_world_package;') == '1'
    # Cart extension must never overwrite an occupied ID or outlive its base.
    empty()
    sql(install)
    cart_install, cart_rollback = render_cart()
    sql("UPDATE mod_delves_world_package SET manifest_sha256=REPEAT('0',64);")
    sql(cart_install, False)
    clean_procedures()
    assert sql('SELECT COUNT(*) FROM creature_template WHERE entry=1499002;') == '0'
    empty()
    sql(install)
    sql("INSERT INTO creature_template(entry,name) VALUES (1499002,'cart sentinel');")
    sql(cart_install, False)
    clean_procedures()
    assert sql('SELECT name FROM creature_template WHERE entry=1499002;') == 'cart sentinel'
    sql('DELETE FROM creature_template WHERE entry=1499002;')
    sql("CREATE TRIGGER reject_cart_model BEFORE INSERT ON creature_template_model FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='injected cart failure';")
    sql(cart_install, False)
    clean_procedures()
    assert sql('SELECT COUNT(*) FROM creature_template WHERE entry=1499002;') == '0'
    assert sql('SELECT COUNT(*) FROM mod_delves_world_package;') == '1'
    sql('DROP TRIGGER reject_cart_model;')
    sql(cart_install)
    sql(cart_install, False)
    clean_procedures()
    assert sql('SELECT COUNT(*) FROM creature_template;') == '3'
    assert sql('SELECT COUNT(*) FROM creature;') == '1'  # Cart is summoned only.
    assert sql('SELECT COUNT(*) FROM mod_delves_world_package;') == '2'
    sql(rollback, False)
    clean_procedures()
    assert sql('SELECT COUNT(*) FROM gameobject;') == '4'
    sql('UPDATE creature_template_model SET DisplayScale=2 WHERE CreatureID=1499002;')
    sql(cart_rollback, False)
    clean_procedures()
    assert sql('SELECT COUNT(*) FROM mod_delves_world_package;') == '2'
    sql('UPDATE creature_template_model SET DisplayScale=1 WHERE CreatureID=1499002;')
    sql(cart_rollback)
    assert sql('SELECT COUNT(*) FROM creature_template;') == '2'
    sql(cart_rollback, False)
    clean_procedures()
    sql(rollback)
    assert sql('SELECT COUNT(*) FROM creature_template;') == '0'

    # Server *_dbc rows: synthetic package DBCs sized from the core DDL, installed and rolled back
    # around the pilot package.
    dbc_tables = ('map_dbc', 'areatable_dbc', 'mapdifficulty_dbc', 'creaturemodeldata_dbc', 'creaturedisplayinfo_dbc')
    sql('\n'.join('DROP TABLE IF EXISTS ' + t + ';' for t in dbc_tables))
    for table in dbc_tables:
        dump = (args.core / 'data/sql/base/db_world' / (table + '.sql')).read_text()
        sql(re.search(r'CREATE TABLE `' + table + r'` \(.*?\) ENGINE=.*?;', dump, re.S).group())
    rows = {'Map.dbc': [(1,), (890,)], 'AreaTable.dbc': [(14999,)], 'MapDifficulty.dbc': [(1, 1), (20000, 890)],
            'CreatureModelData.dbc': [(149990,)], 'CreatureDisplayInfo.dbc': [(149990, 149990)]}
    with tempfile.TemporaryDirectory() as folder:
        client = Path(folder)
        for (name, records), table in zip(rows.items(), dbc_tables):
            fields = len(build_server_dbc.columns(args.core, table))
            body = b''.join(struct.pack('<%dI' % fields, *(record + (0,) * (fields - len(record)))) for record in records)
            (client / name).write_bytes(struct.pack('<4s4I', b'WDBC', len(records), fields, fields * 4, 1) + body + b'\0')
        dbc_install, dbc_rollback = build_server_dbc.render(client, args.core)
    sql(dbc_install)
    assert sql('SELECT COUNT(*) FROM map_dbc WHERE ID=890;') == '1'
    assert sql('SELECT MapID FROM mapdifficulty_dbc WHERE ID=20000;') == '890'
    sql(dbc_install, False)  # Already installed: nothing replaced.
    clean_procedures()
    sql('DROP PROCEDURE IF EXISTS evermore_delve_server_dbc_install_v1;')
    sql(install)
    sql(dbc_rollback, False)  # Pilot still installed.
    sql('DROP PROCEDURE IF EXISTS evermore_delve_server_dbc_rollback_v1;')
    assert sql('SELECT COUNT(*) FROM creaturedisplayinfo_dbc;') == '1'
    sql(rollback)
    sql(dbc_rollback)
    for table in dbc_tables:
        assert sql('SELECT COUNT(*) FROM ' + table + ';') == '0', table
    assert sql('SELECT COUNT(*) FROM mod_delves_world_package;') == '0'
    print('World install/collision/failure/rollback checks passed on pinned core DDL')


if __name__ == '__main__':
    main()
