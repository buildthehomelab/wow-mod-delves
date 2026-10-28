#!/usr/bin/env python3
"""Validate source/config/migration pairing without installing or running a realm."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(wallet):
    root = Path(__file__).resolve().parents[1]
    manifest = json.loads((root / 'test-package/release-inputs.json').read_text())
    world = json.loads((root / 'test-package/earthcrawl.json').read_text())
    config = {}
    for line in (root / 'test-package/earthcrawl-test.candidate.conf').read_text().splitlines():
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        key, value = (part.strip() for part in line.split('=', 1))
        if key in config:
            raise ValueError('Duplicate candidate config: ' + key)
        config[key] = value
    required = {'Delves.Prototype.Enable': 1, 'Delves.Prototype.Scaling.Enable': 1,
                'Delves.Prototype.Brann.Enable': 1, 'Delves.Prototype.Encounter.Enable': 1,
                'Delves.Prototype.Rewards.Enable': 1, 'BossCoins.DelveAwards.Enable': 1,
                'Delves.Prototype.ContentRevision': world['content_revision'],
                'Delves.Prototype.ObjectiveEntry': world['objective_entry'],
                'Delves.Prototype.BossEntry': world['boss_entry'],
                'Delves.Prototype.Brann.Entry': world['brann_entry'],
                'Delves.Prototype.Brann.PotionEntry': world['potion_entry'],
                'Delves.Prototype.ObjectiveCount': len(world['objective_spawns'])}
    for key, expected in required.items():
        if config.get(key) != str(expected):
            raise ValueError('Candidate config/manifest mismatch: ' + key)
    if 'client_mpq_sha256' not in world:
        raise SystemExit('Converted-client tooling: the ' + str(world.get('layout')) + ' layout uses a stock 3.3.5 WMO and '
                         'ships no converted client archive (see README, content revision 2026102804).')
    if manifest['client_mpq_sha256'] != world['client_mpq_sha256']:
        raise ValueError('Client archive identities differ')
    if subprocess.run(['git', '-C', str(wallet), 'diff', '--quiet', 'HEAD', '--'], check=False).returncode:
        raise ValueError('Wallet has tracked source modifications')
    extras = subprocess.check_output(['git', '-C', str(wallet), 'ls-files', '--others', '--', 'src', 'conf', 'data/sql'], text=True)
    if extras.strip():
        raise ValueError('Wallet has untracked build/config/schema inputs')
    actual_tree = subprocess.check_output(['git', '-C', str(wallet), 'rev-parse', 'HEAD^{tree}'], text=True).strip()
    if actual_tree != manifest['wallet_candidate']['tree']:
        raise ValueError('Wrong wallet source tree; branch names are insufficient')
    for source, base in (('delves_characters', root), ('wallet_characters', wallet)):
        for item in manifest[source]:
            relative = Path(item['path'])
            if relative.is_absolute() or '..' in relative.parts:
                raise ValueError('Invalid migration path')
            path = base / relative
            if not path.resolve().is_relative_to(base.resolve()) or digest(path) != item['sha256']:
                raise ValueError('Characters migration mismatch: ' + str(relative))
    return {'status': 'candidate source/config pairing verified; no installation or gameplay evidence',
            'wallet_candidate': manifest['wallet_candidate'], 'content_revision': world['content_revision'],
            'client_mpq_sha256': world['client_mpq_sha256'],
            'delves_characters': manifest['delves_characters'], 'wallet_characters': manifest['wallet_characters']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wallet', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(check(args.wallet.resolve()), indent=2))


if __name__ == '__main__':
    main()
