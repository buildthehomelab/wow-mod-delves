#!/usr/bin/env python3
"""Audit converted 3.3.5 map/model file references, without installing game data."""
import argparse
import collections
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct


def span(data, offset, size):
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError('Reference table outside file')
    return data[offset:offset + size]


def array(data, at, size):
    count, offset = struct.unpack('<II', span(data, at, 8))
    return count, span(data, offset, count * size)


def text(data):
    return data.split(b'\0', 1)[0].decode('ascii')


def chunks(data):
    offset = 0
    result = {}
    while offset < len(data):
        header = span(data, offset, 8)
        name = header[:4][::-1].decode('ascii')
        size = struct.unpack_from('<I', header, 4)[0]
        payload = span(data, offset + 8, size)
        if name in result and name != 'MCNK':
            raise ValueError('Duplicate root chunk: ' + name)
        # ADT repeats one MCNK per terrain cell; external filenames are in
        # the top-level tables audited below, not the terrain cell payload.
        result.setdefault(name, payload)
        offset += 8 + size
    return result


def normalize(name):
    name = name.replace('\\', '/').lower()
    path = PurePosixPath(name)
    if not name or path.is_absolute() or '..' in path.parts or ':' in name:
        raise ValueError('Unsafe or empty reference path: ' + name)
    if path.suffix == '.mdx':
        path = path.with_suffix('.m2')
    return str(path)


def references(path, data):
    refs = []
    dynamic = []
    suffix = path.suffix.lower()
    if suffix == '.m2':
        if data[:4] != b'MD20' or struct.unpack('<I', span(data, 4, 4))[0] != 264:
            raise ValueError('Expected converted MD20 version264')
        skins = struct.unpack('<I', span(data, 0x44, 4))[0]
        if skins > 4:
            raise ValueError('Too many 3.3.5 skin profiles')
        refs += [str(path.with_suffix('')) + f'{index:02d}.skin' for index in range(skins)]
        count, textures = array(data, 0x50, 16)
        for index in range(count):
            kind, _, size, offset = struct.unpack_from('<4I', textures, index * 16)
            if kind == 0:
                if not size:
                    raise ValueError('Named texture has no filename')
                refs.append(text(span(data, offset, size)))
            else:
                dynamic.append({'texture_index': index, 'replaceable_type': kind})
        count, sequences = array(data, 0x1c, 64)
        for index in range(count):
            animation, variation = struct.unpack_from('<HH', sequences, index * 64)
            flags = struct.unpack_from('<I', sequences, index * 64 + 12)[0]
            if not flags & 0x170: # embedded/internal or alias; no external .anim lookup
                refs.append(str(path.with_suffix('')) + f'{animation:04d}-{variation:02d}.anim')
        count, particles = array(data, 0x128, 476)
        for index in range(count):
            for at in (24, 32):
                size, offset = struct.unpack_from('<II', particles, index * 476 + at)
                if size:
                    name = text(span(data, offset, size))
                    if name:
                        refs.append(name)
    elif suffix in ('.adt', '.wdt', '.wmo'):
        tables = chunks(data)
        if 'MVER' not in tables or len(tables['MVER']) != 4:
            raise ValueError('Missing format version')
        version = struct.unpack('<I', tables['MVER'])[0]
        if version != (17 if suffix == '.wmo' else 18):
            raise ValueError('Unexpected converted chunk format version')
        for tag in ('MTEX', 'MMDX', 'MWMO', 'MOTX', 'MODN', 'MOSB'):
            for name in tables.get(tag, b'').split(b'\0'):
                if name:
                    refs.append(name.decode('ascii'))
        if suffix == '.wmo' and 'MOHD' in tables:
            groups = struct.unpack('<I', span(tables['MOHD'], 4, 4))[0]
            if groups > 10000:
                raise ValueError('Unreasonable WMO group count')
            refs += [str(path.with_suffix('')) + f'_{index:03d}.wmo' for index in range(groups)]
        if suffix == '.wdt' and 'MAIN' in tables:
            main = tables['MAIN']
            if len(main) != 64 * 64 * 8:
                raise ValueError('Wrong WDT MAIN tile table')
            for y in range(64):
                for x in range(64):
                    if struct.unpack_from('<I', main, (y * 64 + x) * 8)[0] & 1:
                        refs.append(str(path.parent / f'{path.stem}_{x}_{y}.adt'))
    return refs, dynamic


def audit(root):
    if not root.is_dir():
        raise ValueError("Client input must be an existing directory")
    files = {}
    for path in root.rglob('*'):
        if path.is_file():
            if not path.resolve().is_relative_to(root.resolve()):
                raise ValueError('Candidate file escapes root')
            name = normalize(path.relative_to(root).as_posix())
            if name in files:
                raise ValueError('Case-insensitive duplicate path: ' + name)
            files[name] = path
    if not files:
        raise ValueError("Client input contains no files")
    if not any(path.suffix.lower() in (".m2", ".adt", ".wdt", ".wmo") for path in files.values()):
        raise ValueError("Client input contains no supported model or map files")
    inventory = hashlib.sha256()
    for name, path in sorted(files.items()):
        inventory.update(name.encode("utf8") + b"\0" + hashlib.sha256(path.read_bytes()).digest())
    edges, missing, errors, dynamic = [], [], [], []
    counts = collections.Counter()
    for name, path in sorted(files.items()):
        if path.suffix.lower() not in ('.m2', '.adt', '.wdt', '.wmo'):
            continue
        counts[path.suffix.lower()] += 1
        try:
            refs, bindings = references(PurePosixPath(name), path.read_bytes())
            dynamic += [{'source': name, **binding} for binding in bindings]
            for ref in refs:
                target = normalize(ref)
                edge = {'source': name, 'target': target}
                edges.append(edge)
                if target not in files:
                    missing.append(edge)
        except (ValueError, UnicodeError, struct.error) as error:
            errors.append({'source': name, 'error': str(error)})
    return {'scope': 'Converted file reference audit; not client rendering or complete DBC/effect/sound validation',
            'content_inventory_sha256': inventory.hexdigest(),
            'files': len(files), 'parsed': dict(counts), 'edges': len(edges),
            'missing': missing, 'errors': errors, 'dynamic_texture_bindings': dynamic,
            'limits': ['Replaceable texture types need world/DBC binding validation.',
                       'No BLP decoding, M2 track/skin validity, shader, sound, lighting or client execution proof.']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = audit(args.client.resolve())
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: value for key, value in report.items() if key not in ('missing', 'errors', 'dynamic_texture_bindings')}))
    print(f"Missing references: {len(report['missing'])}; parse errors: {len(report['errors'])}; dynamic textures: {len(report['dynamic_texture_bindings'])}")
    return bool(report['missing'] or report['errors'])


if __name__ == '__main__':
    raise SystemExit(main())
