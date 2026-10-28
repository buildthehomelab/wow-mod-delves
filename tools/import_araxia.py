#!/usr/bin/env python3
"""Read the pinned reference dump into JSON. Never connects to a database."""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / 'reference/araxia'
TOKEN = re.compile(r"\s+|--[^\n]*|/(?:\*.*?\*/)|'(?:\\.|''|[^'\\])*'|`[A-Za-z0-9_]+`|[A-Za-z_][A-Za-z0-9_]*|[-+]?\d+(?:\.\d+)?|[(),;=<>]", re.S)


def tokens(text):
    result = []
    pos = 0
    while pos < len(text):
        match = TOKEN.match(text, pos)
        if not match:
            raise ValueError('Unsupported SQL at offset %d' % pos)
        word = match.group()
        pos = match.end()
        if word.isspace() or word.startswith(('--', '/*')):
            continue
        result.append(word)
    return result


def scalar(word):
    if word.upper() == 'NULL':
        return None
    if word.startswith("'"):
        # Only accept the escape forms found in this source; reject ambiguity.
        body = word[1:-1].replace("''", "'")
        def unescape(match):
            value = match.group(1)
            if value not in "'\\":
                raise ValueError('Unsupported string escape')
            return value
        return re.sub(r'\\(.)', unescape, body)
    if re.fullmatch(r'[-+]?\d+(?:\.\d+)?', word):
        return float(word) if '.' in word else int(word)
    raise ValueError('Unsupported value: ' + word)


def parse_dump(text):
    """Accept explicit-column INSERTs and discard DELETEs; reject other SQL."""
    statements, current = [], []
    for token in tokens(text):
        if token == ';':
            statements.append(current)
            current = []
        else:
            current.append(token)
    if current:
        raise ValueError('Unterminated SQL statement')
    tables = {}
    for words in statements:
        if not words:
            continue
        if words[:2] == ['DELETE', 'FROM']:
            continue
        if words[:2] != ['INSERT', 'INTO']:
            raise ValueError('Unexpected SQL statement')
        table = words[2].strip('`')
        if words[3] != '(':
            raise ValueError('Explicit columns required')
        end = words.index(')', 4)
        columns = [word.strip('`') for word in words[4:end:2]]
        if words[5:end:2] != [','] * (len(columns) - 1) or len(set(columns)) != len(columns):
            raise ValueError('Invalid columns')
        if words[end + 1] != 'VALUES':
            raise ValueError('VALUES required')
        rows = tables.setdefault(table, [])
        pos = end + 2
        while pos < len(words):
            if words[pos] != '(':
                raise ValueError('Row required')
            close = words.index(')', pos + 1)
            values = words[pos + 1:close:2]
            if words[pos + 2:close:2] != [','] * (len(values) - 1) or len(values) != len(columns):
                raise ValueError('Column/value width mismatch')
            rows.append(dict(zip(columns, map(scalar, values))))
            pos = close + 1
            if pos < len(words):
                if words[pos] != ',' or pos + 1 == len(words):
                    raise ValueError('Invalid row separator')
                pos += 1
    return tables


def build_catalog():
    provenance = json.loads((REFERENCE / 'provenance.json').read_text())
    for item in provenance['files']:
        data = (REFERENCE / item['local_path']).read_bytes()
        if hashlib.sha256(data).hexdigest() != item['sha256']:
            raise ValueError('Reference hash mismatch: ' + item['local_path'])
    tables = parse_dump((REFERENCE / 'elwynn-falls.sql.reference').read_text())
    entries = {row['entry'] for row in tables['creature_template']}
    if entries != set(range(600624, 600636)):
        raise ValueError('Unexpected creature ownership')
    spawns = tables['creature']
    if any(row['map'] != 907 or row['id1'] not in entries or row['id2'] or row['id3'] for row in spawns):
        raise ValueError('Unexpected spawn ownership')
    if len({row['guid'] for row in spawns}) != len(spawns):
        raise ValueError('Duplicate spawn GUID')
    return {'schema_version': 1, 'status': 'reference-only; not installable',
            'name': 'Elwynn Falls', 'source_map': 907,
            'upstream_commit': provenance['commit'],
            'boss_entries': [600633, 600634, 600635],
            'counts': {name: len(rows) for name, rows in tables.items()},
            'tables': tables}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(json.dumps(build_catalog(), indent=2, sort_keys=True) + '\n')


if __name__ == '__main__':
    main()
