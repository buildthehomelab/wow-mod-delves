#!/usr/bin/env python3
"""Build an offline Earthcrawl client tree from pinned, complete 3.3.5 DBCs.

Never edits the input client. Output is a candidate, not a deployment authorization.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

MAP = 890
AREA = 14999
SCHEMAS = {'Map.dbc': 66, 'AreaTable.dbc': 36, 'MapDifficulty.dbc': 23, 'WMOAreaTable.dbc': 28}
GEOMETRY = {'.wdt', '.adt', '.wmo', '.m2', '.skin', '.anim', '.blp'}


def digest(data):
    return hashlib.sha256(data).hexdigest()


class Dbc:
    def __init__(self, data, fields):
        if len(data) < 20:
            raise ValueError('truncated DBC header')
        magic, count, width, size, strings = struct.unpack_from('<4s4I', data)
        if magic != b'WDBC' or width != fields or size != fields * 4:
            raise ValueError('unsupported DBC schema')
        end = 20 + count * size
        if strings < 1 or len(data) != end + strings or data[end] != 0 or data[-1] != 0:
            raise ValueError('invalid DBC framing/string block')
        self.fields = fields
        self.rows = [list(struct.unpack_from('<' + str(fields) + 'I', data, 20 + i * size)) for i in range(count)]
        self.strings = bytearray(data[end:])
        self.original_rows = data[20:end]
        self.original_strings = data[end:]
        ids = [r[0] for r in self.rows]
        if len(set(ids)) != len(ids):
            raise ValueError('duplicate DBC primary ID')

    def row(self, key):
        return next(r for r in self.rows if r[0] == key)

    def text(self, value):
        offset = len(self.strings)
        self.strings.extend(value.encode('utf-8') + b'\0')
        return offset

    def append(self, row):
        if len(row) != self.fields or any(r[0] == row[0] for r in self.rows):
            raise ValueError('invalid or occupied DBC primary ID')
        self.rows.append(row)

    def encode(self):
        records = b''.join(struct.pack('<' + str(self.fields) + 'I', *r) for r in self.rows)
        if not records.startswith(self.original_rows) or not self.strings.startswith(self.original_strings):
            raise ValueError('existing DBC records or strings changed')
        return struct.pack('<4s4I', b'WDBC', len(self.rows), self.fields, self.fields * 4, len(self.strings)) + records + self.strings


def chunks(data):
    """Yield logical tag, payload offset, size; accept only the known MVER framing."""
    if data[:4] not in (b'MVER', b'REVM'):
        raise ValueError('geometry has no MVER chunk')
    reverse = data[:4] == b'REVM'
    cursor = 0
    while cursor < len(data):
        if len(data) - cursor < 8:
            raise ValueError('truncated geometry chunk header')
        raw, size = struct.unpack_from('<4sI', data, cursor)
        end = cursor + 8 + size
        if end > len(data):
            raise ValueError('truncated geometry chunk payload')
        yield raw[::-1] if reverse else raw, cursor + 8, size
        cursor = end


def normalize_root(value):
    # AzerothCore WMOAreaTableKey narrows the root to int16. Occupancy must
    # consider wrapped modern IDs, not just their full uint32 values.
    return value & 0xffff


def prepare(source, geometry, baseline, explore_flag, difficulty_id, root_start):
    if not isinstance(baseline, dict):
        raise ValueError('baseline must be a JSON filename-to-hash object')
    if not 0 < explore_flag < 4096 or not 0 < difficulty_id <= 0xffffffff or not 0 < root_start < 32768:
        raise ValueError('invalid exploration flag, difficulty ID or WMO root range')
    tables = {}
    source_hashes = {}
    for name, fields in SCHEMAS.items():
        data = (source / name).read_bytes()
        if digest(data) != baseline.get(name):
            raise ValueError('baseline SHA-256 mismatch: ' + name)
        tables[name] = Dbc(data, fields)
        source_hashes[name] = digest(data)
    maps, areas, difficulties, wmo_areas = (tables[name] for name in SCHEMAS)
    if not {0, 1, 36, 530, 571}.issubset({r[0] for r in maps.rows}):
        raise ValueError('source Map.dbc lacks standard maps; extractor fixtures are forbidden')
    if any(r[0] == MAP for r in maps.rows) or any(r[0] == AREA or r[3] == explore_flag for r in areas.rows):
        raise ValueError('pilot map, area or exploration flag already occupied')
    if any(r[1] == MAP for r in difficulties.rows):
        raise ValueError('pilot map already has difficulty records')

    row = [0] * 66
    row[0:3] = [MAP, maps.text('2680'), 1]
    row[5] = maps.text('Earthcrawl Mines - Evermore Test')
    row[21] = 0x00ff00fe
    row[22] = AREA
    row[58] = 0x3f800000
    row[59] = row[62] = 0xffffffff
    row[65] = 5
    maps.append(row)
    row = [0] * 36
    row[0:5] = [AREA, MAP, 0, explore_flag, 0x02000000]
    row[11] = areas.text('Earthcrawl Mines - Evermore Test')
    row[27] = 0x00ff00fe
    areas.append(row)
    row = [0] * 23
    row[0:3] = [difficulty_id, MAP, 0]  # normal difficulty; delve tier is separate
    row[21] = 5
    difficulties.append(row)

    files = {}
    root_mappings = []
    terrain_areas = set()
    # Reserve against every base root identity, even if a particular ADT/group
    # combination would not collide today. No existing WMOArea row is modified.
    occupied_roots = {normalize_root(r[1]) for r in wmo_areas.rows}
    candidate = root_start
    for path in sorted(geometry.rglob('*')):
        if not path.is_file() or path.suffix.lower() not in GEOMETRY:
            continue
        relative = path.relative_to(geometry).as_posix()
        if relative != relative.lower() or relative.lower() in files:
            raise ValueError('geometry names must be unique lowercase client paths')
        data = path.read_bytes()
        if path.suffix == '.wmo':
            headers = [(offset, size) for tag, offset, size in chunks(data) if tag == b'MOHD']
            if len(headers) > 1:
                raise ValueError('duplicate WMO root header')
            if headers:
                offset, size = headers[0]
                if size != 64:
                    raise ValueError('not a Wrath WMO root header')
                while candidate in occupied_roots and candidate < 32768:
                    candidate += 1
                if candidate >= 32768:
                    raise ValueError('WMO root range exhausted')
                old = struct.unpack_from('<I', data, offset + 32)[0]
                changed = bytearray(data)
                struct.pack_into('<I', changed, offset + 32, candidate)
                data = bytes(changed)
                root_mappings.append({'path': relative, 'old_root': old, 'new_root': candidate})
                occupied_roots.add(candidate)
                candidate += 1
        elif path.suffix == '.adt':
            cells = [(offset, size) for tag, offset, size in chunks(data) if tag == b'MCNK']
            if len(cells) != 256 or any(size < 128 for _, size in cells):
                raise ValueError('not a complete Wrath terrain ADT')
            terrain_areas.update(struct.unpack_from('<I', data, offset + 52)[0] for offset, _ in cells)
        files[relative] = data
    if terrain_areas != {AREA} or not root_mappings or 'world/maps/2680/2680.wdt' not in files:
        raise ValueError('geometry is not the expected converted pilot or area mapping')
    for name, table in tables.items():
        files['DBFilesClient/' + name] = table.encode()
    report = {
        'scope': 'Offline candidate only; no installation or live ID reservation.',
        'map': MAP, 'area': AREA, 'exploration_flag': explore_flag, 'difficulty_id': difficulty_id,
        'baseline_sha256': source_hashes,
        'dbc_rows': {name: {'before': len(table.original_rows) // (table.fields * 4), 'after': len(table.rows)} for name, table in tables.items()},
        'wmo_roots': root_mappings, 'terrain_areas': sorted(terrain_areas),
        'files': {name: digest(data) for name, data in sorted(files.items())},
        'limitations': ['IDs reserved against the supplied local tables only, not the current realm.',
                        'Rebuild collision/navigation from these remapped WMO files; old geometry is incompatible.',
                        'No lighting/sky/sound registration, minimap, world SQL, encounters, safe entrance or client rendering proof.'],
    }
    return files, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='complete baseline DBC directory')
    parser.add_argument('--geometry', type=Path, required=True, help='converted client geometry tree')
    parser.add_argument('--baseline', type=Path, required=True, help='JSON filename -> expected SHA-256')
    parser.add_argument('--out', type=Path, required=True, help='new candidate directory; must not exist')
    parser.add_argument('--explore-flag', type=int, required=True)
    parser.add_argument('--difficulty-id', type=int, required=True)
    parser.add_argument('--root-start', type=int, default=30000)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('--out must not exist')
    for input_root in (args.source.resolve(), args.geometry.resolve()):
        if input_root == args.out.resolve() or input_root in args.out.resolve().parents:
            parser.error('--out must be outside both input trees')
    try:
        files, report = prepare(args.source, args.geometry, json.loads(args.baseline.read_text()),
                                args.explore_flag, args.difficulty_id, args.root_start)
        args.out.mkdir(parents=True, exist_ok=False)
        for name, data in files.items():
            destination = args.out / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
        (args.out / 'candidate-manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    except (ValueError, OSError, StopIteration) as error:
        parser.exit(1, str(error) + '\n')
    print(json.dumps({'candidate': str(args.out), 'files': len(files), 'wmo_roots_remapped': len(report['wmo_roots'])}))


if __name__ == '__main__':
    main()
