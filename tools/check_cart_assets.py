#!/usr/bin/env python3
"""Verify coordinated cart candidate inputs; no installation or realm connection."""
import argparse
import hashlib
import json
from pathlib import Path
from build_cart_client import build, digest
from build_cart_world import render, SPEC
from build_pilot_client import Dbc
from check_pilot_assets import check as check_base


def check(args):
    root = Path(__file__).resolve().parents[1]
    release = json.loads((root / 'test-package/burning-cart-release.json').read_text())
    base = check_base(args.base_client, args.combat, args.native, args.base_archive)
    staged = build(args.base_client, args.display, args.baseline, args.native,
                   args.base_archive, args.client, verify_only=True)
    if digest(args.archive) != release['client_archive_sha256'] or \
            staged['base_archive_sha256'] != release['base_client_archive_sha256'] or \
            len(staged['files']) != release['client_files'] or \
            staged['native_files_verified'] != release['native_files'] or \
            staged['content_revision'] != release['content_revision']:
        raise ValueError('Cart release inputs differ')
    receipt = json.loads(args.archive_receipt.read_text())
    if receipt['sha256'] != release['client_archive_sha256'] or \
            receipt['files_verified'] != release['client_files'] or \
            receipt['bytes'] != args.archive.stat().st_size:
        raise ValueError('Missing matching full archive readback receipt')
    if digest(SPEC) != release['world_manifest_sha256']:
        raise ValueError('Cart world specification differs')
    for operation, text in zip(('install', 'rollback'), render()):
        expected = release['world_' + operation + '_sha256']
        if hashlib.sha256(text.encode()).hexdigest() != expected or \
                digest(args.world / ('world.' + operation + '.sql')) != expected:
            raise ValueError('Generated cart world SQL differs: ' + operation)
    spells = Dbc((args.combat / 'Spell.dbc').read_bytes(), 234)
    for spell, effects, targets in ((20832, [2, 0, 0], [6, 0, 0]), (28330, [6, 0, 0], [1, 0, 0])):
        row = spells.row(spell)
        if row[71:74] != effects or row[86:89] != targets or row[89:92] != [0, 0, 0]:
            raise ValueError('Cart spell effect/target metadata differs')
    return {'scope': 'Offline candidate pairing/readback receipt only; no rendering or gameplay evidence',
            'content_revision': release['content_revision'], 'client_archive_sha256': receipt['sha256'],
            'client_files': len(staged['files']), 'unchanged_base_assets': staged['unchanged_base_assets'],
            'native_files_verified': base['native_files_checked'],
            'native_manifest_sha256': staged['native_manifest_sha256'],
            'native_reuse': 'All base assets byte-identical; only creature model/display DBCs added'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('base-client', 'client', 'display', 'baseline', 'combat', 'native',
                 'base-archive', 'archive', 'archive-receipt', 'world'):
        parser.add_argument('--' + name, type=Path, required=True)
    print(json.dumps(check(parser.parse_args()), indent=2))


if __name__ == '__main__':
    main()
