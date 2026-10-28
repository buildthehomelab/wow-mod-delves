#!/usr/bin/env python3
"""Exercise module registration with enablement on and an unreachable fake DB."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, required=True)
    parser.add_argument('--binary', type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve()
    source = (args.core / 'src/server/apps/worldserver/worldserver.conf.dist').read_text()
    with tempfile.TemporaryDirectory(prefix='delve-pre-db-') as folder:
        root = Path(folder)
        settings = {'LoginDatabaseInfo': '127.0.0.1;1;delve_no_db;delve_no_db;delve_no_db',
                    'CharacterDatabaseInfo': '127.0.0.1;1;delve_no_db;delve_no_db;delve_no_db',
                    'WorldDatabaseInfo': '127.0.0.1;1;delve_no_db;delve_no_db;delve_no_db',
                    'LogsDir': str(root), 'SourceDirectory': str(args.core.resolve()),
                    'Appender.Console': '1,4,0', 'Logger.server': '4,Console Server'}
        for key, value in settings.items():
            source, count = re.subn(r'^' + re.escape(key) + r'\s*=.*$', key + ' = "' + value + '"', source, flags=re.M)
            if count != 1:
                raise ValueError('Missing/duplicate core config key: ' + key)
        source += '\nDelves.Prototype.Enable = 1\nDelves.Prototype.Scaling.Enable = 1\n'
        config = root / 'worldserver.conf'
        config.write_text(source)
        env = dict(os.environ, AC_DELVES_PROTOTYPE_ENABLE='1', AC_DELVES_PROTOTYPE_SCALING_ENABLE='1',
                   AC_DATABASE_RECONNECT_ATTEMPTS='0', AC_UPDATES_AUTO_SETUP='0')
        result = subprocess.run([str(binary), '-c', str(config)], cwd=root, env=env,
                                text=True, capture_output=True, timeout=30)
        output = result.stdout + result.stderr
        if result.returncode != 1 or 'Initializing Scripts' not in output or 'Login' not in output:
            raise AssertionError('Expected normal DB connection refusal after script registration:\n' + output[-10000:])
        if 'Delve prototype not registered' in output or 'Delve prototype initialized after world data' in output:
            raise AssertionError('Delve validation ran before world data was loaded:\n' + output[-10000:])
        print('Enabled module registered; unreachable fake DB refused normally before world data/startup arming')


if __name__ == '__main__':
    main()
