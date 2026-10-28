#!/usr/bin/env python3
"""Destructive SQL checks for a disposable local CI database, never realm data."""
import argparse
from pathlib import Path
import re
import subprocess
from build_pilot_world import render

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

    def empty():
        sql('SET FOREIGN_KEY_CHECKS=0;\n' + '\n'.join('DROP TABLE IF EXISTS ' + t + ';' for t in (*TABLES, 'mod_delves_world_package')))
        clean_procedures()
        for table in TABLES:
            dump = (args.core / 'data/sql/base/db_world' / (table + '.sql')).read_text()
            ddl = re.search(r'CREATE TABLE `' + table + r'` \(.*?\) ENGINE=.*?;', dump, re.S)
            if not ddl:
                raise AssertionError('Missing pinned core DDL: ' + table)
            sql(ddl.group())

    install, rollback = render()
    empty()
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
    sql("INSERT INTO creature(guid,id1,map,Comment) VALUES (14990001,1499000,890,'unexpected actor');")
    sql(rollback, False)
    clean_procedures()
    assert sql('SELECT COUNT(*) FROM gameobject;') == '4'
    assert sql('SELECT COUNT(*) FROM creature;') == '2'
    assert sql('SELECT COUNT(*) FROM mod_delves_world_package;') == '1'
    print('World install/collision/failure/rollback checks passed on pinned core DDL')


if __name__ == '__main__':
    main()
