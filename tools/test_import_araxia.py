import json
import unittest
from import_araxia import ROOT, build_catalog, parse_dump


class AraxiaImportTests(unittest.TestCase):
    def test_catalog_reproduces_pinned_data(self):
        actual = build_catalog()
        expected = json.loads((ROOT / 'reference/araxia/elwynn-falls.catalog.json').read_text())
        self.assertEqual(actual, expected)
        self.assertEqual(actual['counts']['creature'], 55)  # Upstream header says 71.
        self.assertEqual(actual['counts']['smart_scripts'], 23)
        self.assertEqual(actual['counts']['creature_loot_template'], 136)
        self.assertEqual(actual['boss_entries'], [600633, 600634, 600635])
        # External loot entries stay unchanged; this is not an allocated remap.
        self.assertEqual(actual['tables']['creature_template'][0]['lootid'], 17805)

    def test_comments_strings_and_null(self):
        data = parse_dump("""-- ignored
DELETE FROM x WHERE entry = 1;
INSERT INTO `x` (`entry`,name,value) VALUES
(1,'Lord\\'s; -- title',NULL), /* between rows */
(2,'It''s a \\\\ path',-2.5);
""")
        self.assertEqual(data['x'], [
            {'entry': 1, 'name': "Lord's; -- title", 'value': None},
            {'entry': 2, 'name': "It's a \\ path", 'value': -2.5}])

    def test_reject_unhandled_sql_and_bad_rows(self):
        for sql in ["UPDATE x SET a=1;", "INSERT INTO x (a,b) VALUES (1);",
                    "INSERT INTO x (a) VALUES (1),;", "INSERT INTO x (a) VALUES (NOW());",
                    "INSERT INTO x (a) VALUES ('\\n');", "INSERT INTO x (a) VALUES (1)"]:
            with self.subTest(sql=sql), self.assertRaises(ValueError):
                parse_dump(sql)


if __name__ == '__main__':
    unittest.main()
