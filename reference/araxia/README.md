# Araxia Elwynn Falls import

Pinned upstream: https://github.com/araxiaonline/Delves at
`aaea411c6ea1831e19e284ed4e4445f83f796272`. Original MIT license is retained
in `LICENSE`; Araxia contributors supplied this content. The upstream history
credits Manmadedrummer for the SQL export. Files in `provenance.json` are copied
byte-for-byte with SHA-256 checksums. The generated catalog is derived from that
SQL; it preserves source IDs and values, including external references.

This is imported development data, **not an installation package**. The `.reference`
extensions and this directory keep upstream SQL and Eluna scripts outside module
SQL import paths and Lua script loaders. Never rename/copy them into automatic
load directories. The upstream dump contains destructive DELETEs and IDs that
have not been allocated on Evermore.

Generate the reviewable catalog without a database:

```sh
python3 tools/import_araxia.py --output /tmp/elwynn-falls.catalog.json
python3 -m unittest discover -s tools -p 'test_*.py' -v
```

The dump actually contains **55 spawns**, not the 71 claimed by its header:
12 creature templates/models, 19 template spells, 9 equipment rows, 4 reputation
rows, 136 loot rows and 23 SmartAI rows. Boss entries are 600633 (Lord Mwargadon),
600634 (Gurlgles; Lua filename Gurgles), and 600635 (Aguaspawn; Lua filename
Aquaspawn). Spelling differences are retained rather than silently corrected.

The parser accepts the limited explicit-column INSERT grammar in this pinned
export, drops DELETE statements and rejects unsupported expressions/statements.
It is an offline converter, not a general MySQL parser or a SQL sanitization tool.
It verifies all reference hashes before generating JSON. The catalog is not read
by the server and grants no loot, currency, entry access or completion credit.

## Adaptation work still required

- Allocate map/area/template/spawn IDs against the current server/client. Upstream
  map 907 uses raid metadata; Evermore's solo/small-group delve needs coordinated
  non-raid registration. Source IDs in JSON are provenance, not reservations.
- Classify owned and external references before remapping. For example ordinary
  trash uses existing loot IDs such as 17805, while bosses own source loot IDs
  600633–600635. Spell, faction, equipment, item, display and SmartAI parameter
  values must not receive a blanket numeric replacement.
- Port the three Eluna bosses to the supported server scripting API, verify spell
  effects and summons, and connect encounter deaths to the confirmed Run journal.
- Replace level-83 tuning and original loot with reviewed leveling/era/tier rules
  and the Evermore coin transaction path. Preserve Earthcrawl as the retail pilot.
- Rebuild native map/collision/navigation data against the pinned Evermore core.
  Upstream native files are reference samples, not validated server outputs.
- Verify client asset dependency closure, entrance, traversable route and collision
  before enabling admission. Game binaries are kept in a local audit cache, not
  redistributed in this public source import.

No SQL has been applied and no realm/client installation is part of this import.
