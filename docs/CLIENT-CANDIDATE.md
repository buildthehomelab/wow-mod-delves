# Offline client registration candidate

`tools/build_pilot_client.py` builds a new client file tree from already converted,
validated Earthcrawl geometry and four complete, explicitly hash-pinned 3.3.5a
DBC tables. It never edits the input client and refuses an existing output path.
Game assets and baseline DBCs are not stored in this repository.

```sh
python3 tools/build_pilot_client.py \
  --source /workspace/reference-dbc \
  --baseline /workspace/reference-dbc/baseline-sha256.json \
  --geometry /workspace/converted-earthcrawl \
  --out /workspace/new-client-candidate \
  --explore-flag 3500 --difficulty-id 20000
```

The baseline JSON maps each of `Map.dbc`, `AreaTable.dbc`, `MapDifficulty.dbc` and
`WMOAreaTable.dbc` to its expected SHA-256. Obtain these files from the full
preceding client patch chain, record provenance, review the hashes and reserve all
new IDs against current realm data before installation. Local absence is not a
live reservation. Map 890, area 14999, exploration flag 3500, difficulty record
20000 and WMO roots starting at 30000 are provisional development choices.

## Preserved data and registration

Every old DBC record retains its exact bytes, order and string offsets. Existing
string blocks remain unchanged prefixes. The builder adds one non-raid map record,
one area record and one normal-difficulty record; WMOAreaTable stays unchanged.
Standard continent and Deadmines map IDs must exist in the baseline, rejecting the
one-row extractor fixture. New IDs and exploration flags must be unused.

Runtime map 890 points to asset directory `world/maps/2680`. All terrain cells
must already use area 14999. The area has an independent exploration flag below
4096, the pinned core's explored-zone bit limit. It is marked indoors for this
mine prototype. Normal dungeon difficulty supports five players; delve tiers are
separate run metadata, and the current administrator controls still admit solo
only. No heroic record or encounter scaling is implied.

## WMO identity and matching server files

AzerothCore's WMO area lookup narrows the root ID to int16. The builder reserves
new positive signed-16-bit identities against **all narrowed root IDs** in the
baseline WMOAreaTable, then assigns one identity per converted WMO root. Only the
MOHD root-ID word changes; group geometry and other bytes stay untouched. This
avoids accidentally borrowing old zone/area data. It deliberately adds no WMOArea
rows: when no old root identity can match, the core uses the remapped terrain area.

The manifest records every source hash, added row count, root-ID mapping and
output file SHA-256. **Re-extract collision and regenerate navigation from this
candidate**, not from the earlier geometry-only archive. WMO identity changes
invalidate that earlier collision package even when vertices remain identical.
Never package the extractor fixture's one-row Map.dbc or empty display table into
a real client/server installation.

## Scope and validation

This builds a candidate file tree, not an MPQ, launcher publication, world SQL
migration, server installation or rendering proof. It does not add lighting,
sky, sound or minimap data. Safe entry coordinates, camera/rendering, area lookup,
collision, navigation routes and companion pathing still need direct checks.
The installed client patch must follow the actual shared-DBC load order and
archive-count limit in wow-server's client patch documentation.

Unit tests cover exact preservation of old rows/strings, wrapped-root collisions,
input immutability, baseline mismatch, occupied IDs, exploration overflow,
truncated geometry and malformed DBC records. Prior converter/native geometry
validation remains a separate prerequisite; this builder does not replace it.
