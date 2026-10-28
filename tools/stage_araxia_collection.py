#!/usr/bin/env python3
"""Stage every pinned Araxia file for offline adaptation; never install into a realm."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import struct
import tempfile
from araxia_collection import COMMIT, REFERENCE, safe_path


def verify_source(source, manifest):
    source = source.resolve()
    head = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    if head != COMMIT or manifest['commit'] != COMMIT:
        raise ValueError('Wrong Araxia source revision')
    expected, folded = set(), set()
    for item in manifest['files']:
        relative = safe_path(item['path'])
        name = str(relative)
        if name in expected or name.casefold() in folded:
            raise ValueError('Duplicate upstream path')
        expected.add(name)
        folded.add(name.casefold())
        path = source / relative
        if path.is_symlink() or source not in path.resolve().parents:
            raise ValueError('Source path escapes checkout')
        if path.stat().st_size != item['bytes'] or hashlib.sha256(path.read_bytes()).hexdigest() != item['sha256']:
            raise ValueError('Wrong or incomplete upstream file: ' + name)
    tracked = set(subprocess.check_output(['git', '-C', str(source), 'ls-tree', '-r', '--name-only', 'HEAD'], text=True).splitlines())
    if tracked != expected:
        raise ValueError('Incomplete upstream manifest')
    return source


def audit_native(source):
    """Header compatibility against the tested core9e4a232a, not a navigation test."""
    report = {'core_commit': '9e4a232a84d375a8906717c5139fb8d6bf2e41d6',
              'expected_map_version': 9, 'expected_vmap_magic': 'VMAP_4.8',
              'expected_mmap_version': 20, 'mmap_versions': {}, 'header_errors': [],
              'native_files': 0, 'ready_for_installation': False}
    for path in sorted((source / 'Server Map Files').rglob('*')):
        if not path.is_file():
            continue
        report['native_files'] += 1
        data = path.read_bytes()
        relative = path.relative_to(source).as_posix()
        if path.suffix == '.map':
            if len(data) < 8 or data[:8] != b'MAPS\x09\x00\x00\x00':
                report['header_errors'].append(relative)
        elif path.suffix in ('.vmtree', '.vmtile', '.vmo'):
            if data[:8] != b'VMAP_4.8':
                report['header_errors'].append(relative)
        elif path.suffix == '.mmtile':
            if len(data) < 20:
                report['header_errors'].append(relative)
                continue
            magic, detour, version, size = struct.unpack_from('<4I', data)
            report['mmap_versions'][str(version)] = report['mmap_versions'].get(str(version), 0) + 1
            # Only the common prefix is decoded for unsupported versions. The
            # pinned core's version20 header is56bytes including Recast config.
            if magic != 0x4d4d4150 or detour != 7 or data[16] > 1 or (version == 20 and len(data) != size + 56):
                report['header_errors'].append(relative)
    report['note'] = ('Upstream mmap19 must be rebuilt for core mmap20. Header checks do not prove '
                      'collision, model availability, routes or companion/player movement.')
    return report


def stage(source, output, manifest_path=REFERENCE / 'upstream-files.json'):
    manifest = json.loads(manifest_path.read_text())
    source = verify_source(source, manifest)
    output = output.resolve()
    if output.exists() or output.is_relative_to(source) or source.is_relative_to(output):
        raise ValueError('Output must be new and outside the source checkout')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.araxia-stage-', dir=output.parent) as temporary:
        temporary = Path(temporary)
        for item in manifest['files']:
            relative = safe_path(item['path'])
            # All source lives under upstream, outside any realm's SQL/Lua/DBC
            # import roots. Native files are quarantined with their original paths.
            destination = temporary / 'upstream' / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / relative, destination)
            if hashlib.sha256(destination.read_bytes()).hexdigest() != item['sha256']:
                raise ValueError('Staged copy differs: ' + str(relative))
        shutil.copyfile(manifest_path, temporary / 'UPSTREAM-FILES.json')
        (temporary / 'COMPATIBILITY.json').write_text(json.dumps(audit_native(temporary / 'upstream'), indent=2) + '\n')
        (temporary / 'START-HERE.txt').write_text(
            'Complete Araxia source/assets for Evermore adaptation. NOT an installation package.\n'
            'Do not import the original SQL or load original Lua into the realm.\n'
            'Original IDs/loot/level tuning require adaptation. Native mmap tiles use\n'
            'version 19, while the tested Evermore core requires version 20; rebuild them.\n'
            'Boss scripts require ALE/Eluna or a C++ port. Preserve current shared client\n'
            'DBC rows and lighting patches when building a later coordinated archive.\n'
            'See reference/araxia/collection/README.md in wow-mod-delves for next gates.\n')
        # Publish only a fully verified bundle. Never overwrite an existing directory.
        if output.exists():
            raise ValueError('Output appeared while staging')
        temporary.rename(output)
    return {'commit': COMMIT, 'files_verified': len(manifest['files']),
            'bytes_verified': sum(item['bytes'] for item in manifest['files']),
            'status': 'Full upstream import staged; no realm installation'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(stage(args.source, args.output), indent=2))


if __name__ == '__main__':
    main()
