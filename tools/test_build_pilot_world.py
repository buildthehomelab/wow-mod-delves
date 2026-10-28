import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from build_pilot_world import SPEC, render


class PilotWorldTests(unittest.TestCase):
    def test_catalog_and_config_agree_with_world_bindings(self):
        p = json.loads(SPEC.read_text())
        config = {}
        for line in (SPEC.parent / 'earthcrawl-test.candidate.conf').read_text().splitlines():
            if line.strip() and not line.lstrip().startswith('#'):
                key, value = line.split('=', 1)
                config[key.strip()] = value.strip()
        prefix = 'Delves.Prototype.'
        for field, setting in [('boss_entry', 'BossEntry'), ('objective_entry', 'ObjectiveEntry'),
                               ('brann_entry', 'Brann.Entry'), ('potion_entry', 'Brann.PotionEntry'),
                               ('content_revision', 'ContentRevision')]:
            self.assertEqual(int(config[prefix + setting]), p[field])
        for axis, value in zip('XYZ', p['entry_xyz']):
            self.assertEqual(float(config[prefix + 'TestCommands.Entry' + axis]), value)
        self.assertEqual(int(config[prefix + 'ObjectiveCount']), len(p['objective_spawns']))
        install, rollback = render()
        digest = hashlib.sha256(SPEC.read_bytes()).hexdigest()
        self.assertIn(digest, install)
        self.assertIn(digest, rollback)
        self.assertIn("'Web General Ab''enar'", install)
        self.assertIn("RIGHT(DATABASE(), 11) <> '_delve_test'", install)
        self.assertIn('RESIGNAL', install)
        self.assertNotIn('REPLACE INTO', install)
        self.assertNotIn('DELETE FROM', install)

    def test_cli_refuses_overwrite(self):
        script = Path(__file__).with_name('build_pilot_world.py')
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder) / 'world'
            result = subprocess.run([sys.executable, str(script), '--output', str(out)], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            saved = (out / 'world.install.sql').read_bytes()
            second = subprocess.run([sys.executable, str(script), '--output', str(out)], capture_output=True)
            self.assertNotEqual(second.returncode, 0)
            self.assertEqual((out / 'world.install.sql').read_bytes(), saved)


if __name__ == '__main__':
    unittest.main()
