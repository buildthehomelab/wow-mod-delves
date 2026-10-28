import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import build_cart_world


class CartWorldTests(unittest.TestCase):
    def test_refuses_different_base_or_runtime_identity(self):
        original = json.loads(build_cart_world.SPEC.read_text())
        with tempfile.TemporaryDirectory() as folder:
            spec = Path(folder) / 'cart.json'
            for key, value in [('base_manifest_sha256', '0' * 64), ('cart_entry', 42),
                               ('cart_display', 1), ('content_revision', 2026102804),
                               ('speed', 30), ('lane', [[0, 0, 0], [1, 0, 0]])]:
                data = dict(original)
                data[key] = value
                spec.write_text(json.dumps(data))
                with patch.object(build_cart_world, 'SPEC', spec):
                    with self.assertRaises(ValueError, msg=key):
                        build_cart_world.render()

    def test_cli_never_overwrites_existing_sql(self):
        script = Path(build_cart_world.__file__)
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / 'cart'
            command = [sys.executable, str(script), '--output', str(output)]
            first = subprocess.run(command, capture_output=True)
            self.assertEqual(first.returncode, 0, first.stderr)
            install = output / 'world.install.sql'
            install.write_text('operator reviewed bytes')
            second = subprocess.run(command, capture_output=True)
            self.assertNotEqual(second.returncode, 0)
            self.assertEqual(install.read_text(), 'operator reviewed bytes')

    def test_enabled_config_preserves_base_except_revision_and_cart(self):
        def config(name):
            result = {}
            for line in (build_cart_world.SPEC.parent / name).read_text().splitlines():
                line = line.split('#', 1)[0].strip()
                if line:
                    key, value = [part.strip() for part in line.split('=', 1)]
                    self.assertNotIn(key, result)
                    result[key] = value
            return result
        base = config('earthcrawl-test.candidate.conf')
        cart = config('burning-cart-test.candidate.conf')
        base['Delves.Prototype.ContentRevision'] = '2026102805'
        base.update({'Delves.Prototype.Encounter.Cart.Enable': '1',
                     'Delves.Prototype.Encounter.Cart.Entry': '1499002',
                     'Delves.Prototype.Encounter.Cart.Display': '149990',
                     'Delves.Prototype.Encounter.Cart.Lane': '114 -26.39 -8.7425 126 -5.61 -6.5631'})
        self.assertEqual(cart, base)
