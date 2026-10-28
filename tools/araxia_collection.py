#!/usr/bin/env python3
"""Verify and describe the full pinned Araxia collection. Never executes source SQL/Lua."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
from import_araxia import TOKEN, scalar

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / 'reference/araxia/collection'
COMMIT = 'aaea411c6ea1831e19e284ed4e4445f83f796272'
SQL_TOKEN = re.compile(TOKEN.pattern.replace('[(),;=<>]', '[.(),;=<>]'), re.S)


def safe_path(value):
    path = PurePosixPath(value)
    if path.as_posix() != value or path.is_absolute() or not path.parts or any(part in ('.', '..') for part in path.parts) or '\\' in value:
        raise ValueError('Unsafe collection path: ' + value)
    return path


def read_sql(text):
    words, statements, pos = [], [], 0
    while pos < len(text):
        match = SQL_TOKEN.match(text, pos)
        if not match:
            raise ValueError('Unsupported SQL at offset ' + str(pos))
        word, pos = match.group(), match.end()
        if word.isspace() or word.startswith(('--', '/*')):
            continue
        if word == ';':
            if words:
                statements.append(words)
            words = []
        else:
            words.append(word)
    if words:
        raise ValueError('Unterminated SQL statement')
    tables, mutations = {}, []
    for words in statements:
        if words[:2] in (['DELETE', 'FROM'],) or words[0] == 'UPDATE':
            mutations.append(words[0])  # Retained source only, never applied to the insert inventory.
            continue
        if words[:2] != ['INSERT', 'INTO']:
            raise ValueError('Unsupported statement: ' + words[0])
        offset = 2
        table = words[offset].strip('`')
        if words[offset + 1] == '.':
            if table != 'acore_world':
                raise ValueError('Unexpected upstream schema: ' + table)
            offset += 2
            table = words[offset].strip('`')
        offset += 1
        if words[offset] != '(':
            raise ValueError('Explicit INSERT columns required')
        close = words.index(')', offset)
        columns = [w.strip('`') for w in words[offset + 1:close:2]]
        if len(set(columns)) != len(columns) or words[offset + 2:close:2] != [','] * (len(columns) - 1):
            raise ValueError('Invalid INSERT columns')
        if words[close + 1] != 'VALUES':
            raise ValueError('INSERT VALUES required')
        offset = close + 2
        while offset < len(words):
            if words[offset] != '(':
                raise ValueError('INSERT row required')
            close = words.index(')', offset)
            values = words[offset + 1:close:2]
            if len(values) != len(columns) or words[offset + 2:close:2] != [','] * (len(values) - 1):
                raise ValueError('Column/value mismatch')
            tables.setdefault(table, []).append(dict(zip(columns, map(scalar, values))))
            offset = close + 1
            if offset < len(words):
                if words[offset] != ',' or offset + 1 == len(words):
                    raise ValueError('Invalid row separator')
                offset += 1
    return {'tables': tables, 'ignored_mutations': mutations}


def verify_reference(root=REFERENCE):
    manifest = json.loads((root / 'provenance.json').read_text())
    if manifest['commit'] != COMMIT:
        raise ValueError('Unreviewed upstream revision')
    seen = set()
    for item in manifest['files']:
        path = safe_path(item['local_path'])
        if str(path).casefold() in seen:
            raise ValueError('Duplicate reference path')
        seen.add(str(path).casefold())
        file = root / path
        if file.is_symlink() or root.resolve() not in file.resolve().parents:
            raise ValueError('Reference path escapes collection')
        data = file.read_bytes()
        if len(data) != item['bytes'] or hashlib.sha256(data).hexdigest() != item['sha256']:
            raise ValueError('Reference hash mismatch: ' + str(path))
    actual = {p.relative_to(root).as_posix().casefold() for p in (root / 'upstream').rglob('*') if p.is_file()}
    if actual != seen:
        raise ValueError('Unmanifested or missing collection source')
    return manifest


def build_catalog(root=REFERENCE):
    manifest = verify_reference(root)
    sql = {}
    for item in manifest['files']:
        if item['upstream_path'].endswith('.sql'):
            sql[item['upstream_path']] = read_sql((root / item['local_path']).read_text())
    maps = list(csv.DictReader(io.StringIO((root / 'upstream/DBC_CSV/DBFilesClient/Map.csv.reference').read_text())))
    boss_scripts = {}
    for item in manifest['files']:
        if item['upstream_path'].startswith('lua_scripts/'):
            text = (root / item['local_path']).read_text()
            match = re.search(r'local BOSS_ID\s*=\s*(\d+)', text)
            if match:
                boss_scripts.setdefault(int(match.group(1)), []).append(item['upstream_path'])
    locations = []
    for row in maps:
        map_id = int(row['ID'])
        spawns, files = [], set()
        for name, parsed in sql.items():
            for spawn in parsed['tables'].get('creature', []):
                if spawn['map'] == map_id:
                    spawns.append(spawn)
                    files.add(name)
        locations.append({'name': row['MapName_Lang_enUS'], 'source_map': map_id,
                          'directory': row['Directory'], 'source_instance_type': int(row['InstanceType']),
                          'source_spawn_rows': len(spawns), 'spawn_files': sorted(files),
                          'source_template_entries': sorted({int(s.get('id1', s.get('id', 0))) for s in spawns}),
                          'boss_scripts': sorted({script for spawn in spawns
                              for script in boss_scripts.get(int(spawn.get('id1', spawn.get('id', 0))), [])}),
                          'upstream_wip': map_id in (901, 903),
                          'enabled': False})
    return {'schema_version': 1, 'upstream_commit': COMMIT,
            'status': 'Imported source collection; runtime conversion and installation pending',
            'locations': locations,
            'sql_files': {name: {'insert_counts': {table: len(rows) for table, rows in parsed['tables'].items()},
                                 'ignored_mutations': parsed['ignored_mutations'],
                                 'creature_spawn_maps': sorted({row['map'] for row in parsed['tables'].get('creature', [])}),
                                 'template_levels': sorted({row['minlevel'] for row in parsed['tables'].get('creature_template', [])})}
                          for name, parsed in sql.items()},
            'boss_script_count': sum(len(paths) for paths in boss_scripts.values()),
            'runtime_blockers': ['Native mmap tiles are version 19; pinned Evermore core9e4a232a requires20.',
                                 'ALE/Eluna scripts need an engine integration or C++ port; no Lua engine in checked server manifest.',
                                 'Allocate/check IDs and adapt raid metadata, tuning, progression and loot before world import.',
                                 'Rebuild client DBC additions on the current complete patch chain, preserving lighting changes.'],
            'source_files': len(manifest['files']),
            'note': 'Counts describe INSERT rows; upstream DELETE/UPDATE statements are recorded but not executed. '
                    'Files may overlap or reference external templates/items. Source IDs are not Evermore allocations.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    data = build_catalog()
    if args.output.exists():
        parser.error('Output must be a new file')
    args.output.write_text(json.dumps(data, indent=2, sort_keys=True) + '\n')


if __name__ == '__main__':
    main()
