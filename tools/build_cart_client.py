#!/usr/bin/env python3
"""Stage a full cart client candidate and prove geometry reuse. No installation."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
from build_pilot_client import Dbc

TABLES = {'CreatureModelData.dbc': 28, 'CreatureDisplayInfo.dbc': 16}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def contained(root, name):
    relative = Path(name)
    if relative.is_absolute() or '..' in relative.parts or '\\' in name:
        raise ValueError('Unsafe manifest path')
    path = root / relative
    if path.is_symlink() or not path.resolve().is_relative_to(root.resolve()):
        raise ValueError('Manifest path escapes input')
    return path


def build(client, display, baseline, native, archive, output, verify_only=False):
    sources = [path.resolve() for path in (client, display, baseline, native)]
    output = output.resolve()
    if (not verify_only and output.exists()) or any(output.is_relative_to(path) or path.is_relative_to(output) for path in sources):
        raise ValueError('Output must be new and outside input directories')
    root = Path(__file__).resolve().parents[1]
    spec = json.loads((root / 'test-package/earthcrawl.json').read_text())
    release = json.loads((root / 'test-package/burning-cart-release.json').read_text())
    if 'client_mpq_sha256' not in spec:
        raise SystemExit('Converted-client tooling: the ' + str(spec.get('layout')) + ' layout uses a stock 3.3.5 WMO and '
                         'ships no converted client archive (see README, content revision 2026102804).')
    if digest(archive) != spec['client_mpq_sha256']:
        raise ValueError('Wrong base archive identity')
    if digest(client / 'candidate-manifest.json') != release['base_client_manifest_sha256'] or \
            digest(display / 'cart-display.json') != release['cart_display_manifest_sha256'] or \
            digest(native / 'geometry-manifest.json') != release['native_manifest_sha256']:
        raise ValueError('Wrong recorded release input manifests')
    manifest = json.loads((client / 'candidate-manifest.json').read_text())
    files = manifest['files']
    if any('DBFilesClient/' + name in files for name in TABLES):
        raise ValueError('Cart DBC already present in base client')
    if len(files) < 700 or len({name.casefold() for name in files}) != len(files):
        raise ValueError('Expected full unique base asset manifest')
    actual = {p.relative_to(client).as_posix() for p in client.rglob('*') if p.is_file()}
    if actual != set(files) | {'candidate-manifest.json'}:
        raise ValueError('Unmanifested or missing base client input')
    for name, sha in files.items():
        if digest(contained(client, name)) != sha:
            raise ValueError('Base client asset differs: ' + name)
    receipt = json.loads((display / 'cart-display.json').read_text())
    if receipt['model_id'] != 149990 or receipt['display_id'] != 149990 or \
            receipt['model_path'] != 'world/expansion10/doodads/earthen/11ea_earthen_minecart04.m2' or \
            digest(contained(client, receipt['model_path'])) != receipt['model_sha256']:
        raise ValueError('Cart model identity differs')
    for name, fields in TABLES.items():
        old_path, new_path = baseline / name, display / name
        if digest(old_path) != receipt['baseline_sha256'][name] or digest(new_path) != receipt['output_sha256'][name]:
            raise ValueError('Cart DBC hash mismatch')
        old, new = Dbc(old_path.read_bytes(), fields), Dbc(new_path.read_bytes(), fields)
        if len(old.rows) < 1000 or len(new.rows) != len(old.rows) + 1 or \
                new.rows[:-1] != old.rows or not new.strings.startswith(old.strings):
            raise ValueError('Cart must preserve full baseline DBC rows and strings')
        if new.rows[-1][0] != 149990 or any(row[0] == 149990 for row in old.rows):
            raise ValueError('Cart DBC ID collision')
    model = Dbc((display / 'CreatureModelData.dbc').read_bytes(), 28)
    offset = model.rows[-1][2]
    if offset >= len(model.strings):
        raise ValueError('Invalid model name offset')
    end = model.strings.find(0, offset)
    if end < 0 or model.strings[offset:end].decode('utf-8').lower().replace('\\', '/') != receipt['model_path']:
        raise ValueError('Model binding points to another asset')
    if Dbc((display / 'CreatureDisplayInfo.dbc').read_bytes(), 16).rows[-1][1] != 149990:
        raise ValueError('Display does not bind the cart model')
    geometry_path = native / 'geometry-manifest.json'
    geometry = json.loads(geometry_path.read_text())
    if geometry['client_archive_sha256'] != spec['client_mpq_sha256'] or len(geometry['files']) < 100:
        raise ValueError('Wrong base native geometry')
    seen = set()
    for item in geometry['files']:
        name = item['path']
        path = contained(native, name)
        if name in seen or path.stat().st_size != item['bytes'] or digest(path) != item['sha256']:
            raise ValueError('Native geometry hash/size mismatch')
        seen.add(name)
    if not {'vmaps/890.vmtree', 'mmaps/890.mmap'}.issubset(seen):
        raise ValueError('Missing native map definitions')
    native_extensions = {'.map', '.mmap', '.mmtile', '.vmtree', '.vmtile', '.vmo', '.dtree'}
    actual_native = {p.relative_to(native).as_posix() for p in native.rglob('*')
                     if p.is_file() and p.suffix in native_extensions}
    if actual_native != seen:
        raise ValueError('Unmanifested or missing native geometry input')
    # All validations finish before writing; preserve every baseline asset byte.
    new_files = dict(files)
    for name in TABLES:
        new_files['DBFilesClient/' + name] = digest(display / name)
    report = {'scope': 'Offline staged cart client; archive readback and gameplay remain required',
              'content_revision': json.loads((root / 'test-package/burning-cart.json').read_text())['content_revision'],
              'base_archive_sha256': spec['client_mpq_sha256'],
              'base_manifest_sha256': digest(client / 'candidate-manifest.json'),
              'native_manifest_sha256': digest(geometry_path), 'native_files_verified': len(seen),
              'unchanged_base_assets': len(files), 'cart_display': receipt, 'files': new_files}
    if verify_only:
        actual = {p.relative_to(output).as_posix() for p in output.rglob('*') if p.is_file()}
        if actual != set(new_files) | {'cart-client-manifest.json'}:
            raise ValueError('Missing or unmanifested staged cart input')
        for name, sha in new_files.items():
            if digest(contained(output, name)) != sha:
                raise ValueError('Staged cart asset changed: ' + name)
        if json.loads((output / 'cart-client-manifest.json').read_text()) != report:
            raise ValueError('Staged cart receipt differs')
    else:
        output.mkdir(parents=True)
        for name in files:
            target = contained(output, name)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(contained(client, name), target)
        for name in TABLES:
            shutil.copyfile(display / name, output / 'DBFilesClient' / name)
        (output / 'cart-client-manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('client', 'display', 'baseline', 'native', 'archive', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--verify-existing', action='store_true')
    args = parser.parse_args()
    report = build(args.client, args.display, args.baseline, args.native, args.archive, args.out, args.verify_existing)
    print(json.dumps({key: value for key, value in report.items() if key not in ('files', 'cart_display')}, indent=2))


if __name__ == '__main__':
    main()
