#!/usr/bin/env python3
"""Verify the local candidate's metadata and native-file pairing, without installing."""
import argparse
import hashlib
import json
from pathlib import Path
from build_pilot_client import Dbc, SCHEMAS
from build_pilot_world import SPEC


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def check(client, combat, native, archive):
    p = json.loads(SPEC.read_text())
    evidence = json.loads((SPEC.parent / 'combat-reference.json').read_text())
    tables = {name: Dbc((client / 'DBFilesClient' / name).read_bytes(), fields)
              for name, fields in SCHEMAS.items()}
    if len(tables['Map.dbc'].rows) < 100 or len(tables['AreaTable.dbc'].rows) < 1000:
        raise ValueError('Extraction fixtures are not installable client metadata')
    map_row = tables['Map.dbc'].row(p['map'])
    area = tables['AreaTable.dbc'].row(p['area'])
    if map_row[2] != 1 or map_row[22] != p['area'] or map_row[65] != 5:
        raise ValueError('Pilot requires the expected non-raid map/area/capacity')
    if area[1] != p['map'] or not 0 < area[3] < 4096:
        raise ValueError('Pilot area/exploration flag mismatch')
    if not any(row[1] == p['map'] and row[2] == 0 and row[21] == 5
               for row in tables['MapDifficulty.dbc'].rows):
        raise ValueError('Missing normal five-player difficulty')
    for name, expected in evidence['sha256'].items():
        if sha256(combat / name) != expected:
            raise ValueError('Combat metadata baseline mismatch: ' + name)
    spells = Dbc((combat / 'Spell.dbc').read_bytes(), 234)
    for expected in evidence['spells']:
        row = spells.row(expected['id'])
        if row[71:74] != expected['effects'] or row[86:89] != expected['targetsA']:
            raise ValueError('Test spell payload/target mismatch')
    displays = Dbc((combat / 'CreatureDisplayInfo.dbc').read_bytes(), 16)
    objects = Dbc((combat / 'GameObjectDisplayInfo.dbc').read_bytes(), 19)
    for field in ('boss_display', 'brann_display'):
        displays.row(p[field])
    for field in ('objective_display', 'potion_display'):
        objects.row(p[field])
    expected_hash = p['client_mpq_sha256']
    if sha256(archive) != expected_hash:
        raise ValueError('Wrong client archive (old geometry is incompatible)')
    manifest = json.loads((native / 'geometry-manifest.json').read_text())
    if manifest['client_archive_sha256'] != expected_hash:
        raise ValueError('Native geometry belongs to another client candidate')
    seen = set()
    for item in manifest['files']:
        relative = Path(item['path'])
        if relative.is_absolute() or '..' in relative.parts or str(relative) in seen:
            raise ValueError('Invalid native manifest path')
        seen.add(str(relative))
        file = native / relative
        if not file.resolve().is_relative_to(native.resolve()):
            raise ValueError('Native manifest escapes input root')
        if file.stat().st_size != item['bytes'] or sha256(file) != item['sha256']:
            raise ValueError('Native geometry hash/size mismatch: ' + str(relative))
    required = {'mmaps/890.mmap', 'vmaps/890.vmtree'}
    if not required.issubset(seen):
        raise ValueError('Native map/collision definitions missing')
    return {'scope': 'offline pairing only; not gameplay or dependency-closure proof',
            'package': p['package'], 'client_archive_sha256': expected_hash,
            'native_files_checked': len(seen), 'spell_ids_checked': [r['id'] for r in evidence['spells']],
            'map': p['map'], 'area': p['area']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('client', 'combat', 'native', 'archive'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(check(args.client, args.combat, args.native, args.archive), indent=2))


if __name__ == '__main__':
    main()
