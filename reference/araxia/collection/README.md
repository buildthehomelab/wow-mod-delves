# Full Araxia delve collection

Imported from [araxiaonline/Delves](https://github.com/araxiaonline/Delves) at
`aaea411c6ea1831e19e284ed4e4445f83f796272`, verified as the current upstream
reference during this import. The original MIT license is retained verbatim in
`upstream/LICENSE.reference`; original author notices remain in every source file.

This imports the entire collection, expanding the earlier Elwynn-only source
import. All 64 text source files are retained byte-for-byte: 23 SQL files, 28 server
Lua files (27 boss scripts and the teleporter), nine DBC CSVs, two addon source
files, README and LICENSE. Their hashes are in `provenance.json`. The full upstream
manifest `upstream-files.json` covers all 581 original files / 366,888,109 bytes,
including 318 client MPQ inputs and 173 native map files. Large game assets stay
in the staged import bundle, outside this source repository.

## Imported locations

Source-map IDs and names are provenance, not approved Evermore allocations. Counts
include shared source files such as spirit healers; they are INSERT inventory,
not a claim of unique or valid live spawns.

| Source map | Location | Spawn rows | Boss scripts | Upstream state |
| --- | --- | ---: | ---: | --- |
| 805 | Mustafar | 72 | 4 | Marked done upstream; untested here |
| 900 | Stonetalon Ruins | 66 | 3 | Marked done upstream; untested here |
| 901 | Demonhunter Cove | 1 | 0 | WIP |
| 902 | Stonetalon Logging Camp | 79 | 2 | Marked done upstream; untested here |
| 903 | Old Ironforge | 1 | 0 | WIP |
| 904 | Blackchar Cave | 62 | 1 | Marked done upstream; untested here |
| 905 | SandfuryDelve | 58 | 3 | Marked done upstream; untested here |
| 906 | Arathi Farmlands | 121 | 3 | Marked done upstream; untested here |
| 907 | Elwynn Falls | 56 | 3 | Marked done upstream; untested here |
| 908 | Quel'Thalas Island | 44 | 2 | Marked done upstream; untested here |
| 909 | Forgotten Gnome Camp | 58 | 2 | Marked done upstream; untested here |
| 910 | Newman's Landing | 39 | 2 | Marked done upstream; untested here |
| 911 | Altar of Storms | 103 | 2 | Marked done upstream; untested here |

## Verify and stage the complete import

From the module checkout:

```sh
python3 tools/araxia_collection.py --output /tmp/araxia-catalog.json
python3 tools/stage_araxia_collection.py --source /path/to/full/araxia-checkout \
  --output /path/to/new/araxia-import
python3 -m unittest discover -s tools -p 'test_*.py'
```

The catalog generator verifies every imported text hash before reading it. It
inventories explicit INSERT rows and records upstream DELETE/UPDATE statements
without executing them. It does not simulate those mutations or produce an
installable merged database state. Per-file counts retain overlapping definitions.

Staging verifies the pinned Git commit, complete tracked-file set and every file's
SHA256/size, refuses symlinks/traversal/overwrites and only publishes a bundle after
all copies verify. It preserves every upstream file under `upstream/`, outside
realm import roots. `UPSTREAM-FILES.json` and `COMPATIBILITY.json` identify the exact
inputs and native header audit. Do not install the original SQL/Lua directly.

## Runtime integration gates

- All 53 upstream mmap tiles use version **19**; tested Evermore core `9e4a232a`
  requires **20**. Rebuild navigation from the matching client data with the pinned
  core. Map version9 and VMAP4.8 headers match, but this does not prove model
  availability, complete collision or traversable player/companion routes.
- The checked current server manifest has no ALE/Eluna module. The 27 boss scripts
  need a reviewed engine integration or C++ ports. Lua syntax checks alone do not
  exercise the engine APIs or encounter behavior.
- Most templates are tuned for levels82–85. Adapt them to Evermore's frozen
  level/era/tier scaling and coin receipts. Original loot/chest IDs, currency,
  template IDs, spawn GUIDs, shared definitions and external references must be
  reconciled against the actual world before producing an installer.
- Upstream map rows use raid metadata. Convert admission/instance definitions
  intentionally rather than activating ordinary raid access. The original
  teleporter has an in-memory return location and is not Evermore's durable entry
  and return contract.
- Demon Hunter Cove and Old Ironforge are upstream WIP. Old Ironforge's creature
  export targets map0, and its upstream teleporter likewise goes to the continent,
  despite having a separate map903 definition. Those rows must not be installed on
  map0 as a shortcut. The catalogue exposes their real map assignments.
- DBC CSVs contain additions, not complete client tables. Merge against the actual
  current patch chain and preserve Earthcrawl's lighting edits and all existing
  rows. Keep the matching client/native data, manifests and rollback together.

The full source/asset import is complete; these gates prevent claiming all13 are
already playable on Evermore. Nothing here registers new runtime maps, enables
entry, applies SQL, loads boss scripts, grants loot or changes the tested realm.
Subsequent implementation needs reviewed PRs, actual merged-commit server pins,
coordinated client delivery and gameplay checks for each location.
