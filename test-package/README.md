# Earthcrawl coordinated combat-test candidate

**Not approved for installation or server-test readiness yet.** This candidate
connects existing admission/checkpoint, objective, boss and healer scripts to
matching world/config/client/native inputs. It remains a development combat
layout: four synthetic rescue markers and one boss, using stock visuals/spell
analogues. It does not replace the retail-faithful Earthcrawl story goal.

`earthcrawl.json` is the immutable world placement/ID manifest. All IDs are
provisional and collision-guarded. Positions come from connected Detour ground
polygons with supporting VMap floors, plus 0.2 units of clearance. They are not
retail spawn captures or client-validated safe positions. The first candidate
restricts administrator admission to level30/tier1 while scaling is unfinished.
The default module configuration stays disabled. The separate candidate config
contains explicit enablement for an eventual approved, isolated test stack.

## Inputs that must travel together

- Reviewed module revision containing the encounter and this package. PR #9 is
  the encounter dependency; do not deploy a branch name as a server version pin.
  After implementation merges, prepare a reviewed wow-server pin to the actual
  resulting commit. A pin is currently blocked by the incomplete gameplay release.
- Existing characters schema in `data/sql/characters`, applied to an isolated
  characters database, with its journal/visit tables retained across reconnects.
- Generated world install SQL from this directory's manifest. It only accepts
  a database whose name ends `_delve_test`; use a separately backed-up clone.
- Complete `client-registered-candidate/DBFilesClient` tables and the matching
  `earthcrawl-registered-candidate.MPQ`, SHA256
  `ba874d4c974b68493b55a6ebb3b1f413d460c4dead6b2b188dc5d22701be9ac4`.
  These are local audit artifacts, not checked-in game binaries. Never install
  `registered-extractor-fixture` or any one-row metadata fixture.
- The corresponding registered native archive/files, with
  `geometry-manifest.json` identifying that exact client archive. Old geometry
  used different WMO roots and is incompatible even if filenames match.
- Existing client/server Spell.dbc, CreatureDisplayInfo.dbc and
  GameObjectDisplayInfo.dbc matching `combat-reference.json`. These are unchanged
  baseline tables, not new retail spell ports. World spell overrides still need
  checking on the actual test stack.
- `earthcrawl-test.candidate.conf`, merged into the approved isolated stack's
  configuration overlay using wow-server's documented override conventions.
  This file is an input, not authorization to install or restart anything.

The asset checker verifies full metadata framing, map/area/difficulty, relevant
spell/display entries, all170native file hashes and the client archive hash.
It does not prove model/texture dependency closure or game behavior:

```sh
python3 tools/build_pilot_world.py --output /tmp/earthcrawl-world-candidate
python3 tools/check_pilot_assets.py \
  --client /path/to/client-registered-candidate \
  --combat /path/to/combat-reference \
  --native /path/to/registered-native \
  --archive /path/to/earthcrawl-registered-candidate.MPQ
```

## Approved isolated-test installation procedure

Before executing these steps, finish package reviews/CI and verify the current
core/module pins, client patch ordering and archive budget against authenticated
server state. Preserve separate database and client/data-directory backups.
Stop the isolated test realm before changing its world data. This document does
not authorize a live realm stop, database write or restart.

1. Build the reviewed module under the exact folder `modules/mod-delves` using
   the documented wow-server build flow. Stage matching native data and complete
   DBCs in the test server's data directory. Do not replace existing DBCs with an
   extraction fixture. Preserve each overwritten file's backup and hash.
2. Apply characters schema only to the isolated characters clone. Apply the
   generated `world.install.sql` only to the isolated world clone ending in
   `_delve_test`, using a MySQL client that stops on errors. **Never use `--force`.**
   The installer uses a transaction and refuses pre-existing map/actor IDs. It
   records the exact manifest SHA256 only after all content inserts succeed.
3. If a call fails, content writes roll back but the stored procedure remains.
   After inspecting the error, clean up its installer routine in that same test
   database before retrying:

   ```sql
   DROP PROCEDURE IF EXISTS evermore_delve_install_v1;
   ```

   A second install deliberately fails rather than replacing existing content.
   Do not delete other rows or receipts to make the collision checks pass.
4. Stage the reviewed config overlay and client patch in the test client. Resolve
   the patch's exact filename/order from the current patch inventory; no patch
   slot is reserved by this package. Verify all archived files can be read back,
   and update the test client's cache/version through the normal client process.
5. With separate authorization, start only the isolated test realm. Check startup
   logs for missing spells, models, script bindings and disabled-module messages.
   A build/SQL pass alone is insufficient to invite gameplay testing.

## Controlled gameplay checks

Use a real level30 administrator, solo, outside combat on an ordinary continent.
Use ordinary movement and combat; disable GM invulnerability/invisibility for the
combat portion. Keep a second administrator available to recover the test actor.

1. Record the origin, use `.delve enter 1`, and confirm client load, expected floor,
   instance script, `.delve status`, one Brann, and the retained origin. Refused
   transfers must not lose the origin. Check walls, ramps, camera, textures and
   the connected route; stop if collision or navigation is unsafe.
2. Pull the boss before the four objectives. It must not engage or lose health.
   Click one marker repeatedly; progress increases only once. Reconnect after
   partial progress and confirm that consumed marker credit stays deduplicated.
3. Reach all four markers, confirm boss unlock, then test a normal pull and an
   owned-pet pull/taunt. Check cast visibility, interrupt, curse removal/root
   breaking, mana, damage and leash. These are 3.3.5 analogues, not final retail
   effects. Reset/wipe must retain confirmed objective progress.
4. Verify Brann follows through the route, throws personal potions in combat,
   respects floor/LOS, heals only the injured owner once per pickup, and removes
   old potions on logout, leaving, death or terminal run. Healer levels/talents
   and retail knockout/revive remain future work.
5. Complete the boss kill and verify confirmed completion. Duplicate kills and
   outsiders must never receive credit. No loot or coins are enabled by this
   package; reward transactions remain an explicit incomplete requirement.
6. In the isolated stack, test disconnect/restart recovery, stale core mirrors,
   unavailable journal writes and frozen-session escape. Use `.delve leave` and
   verify arrival at the saved origin before the visit is removed. Retain journal
   rows and sequences; never delete them to recycle run or instance IDs.
7. Test rejected levels/tiers, a second player/group, failed teleport and terminal
   instance reuse. Ended runs must not restart in-place. A fresh core instance
   allocation is required for another run; old journal identity remains retained.

## Rollback after the approved test

Return test characters to their saved origins first and verify retained visit
records. Stop the isolated test realm with authorization. Disable/remove the test
config overlay, then apply generated `world.rollback.sql` to the same test world.
It requires the exact manifest receipt and refuses unexpected actors; a failure
rolls its deletions back. Inspect failure before routine cleanup/retry:

```sql
DROP PROCEDURE IF EXISTS evermore_delve_rollback_v1;
```

Restore the backed-up server/client files and previous module pin through the
normal reviewed workflow. Keep characters journal/visit/instance data together;
rollback does not delete them or reset sequences. The world receipt table remains
empty after successful rollback. If a character has no usable origin, recover it
before uninstalling the test map. No automatic live SQL, file replacement, restart
or deployment is performed by the package tools.

## Remaining readiness gates

MySQL install/collision/injected-failure/rollback CI must pass against the pinned
core schema. Formal review and complete worldserver link are required on the
final source revision. Finish retail story/encounter mechanics, level/era/tier
scaling, idempotent Undermine wallet integration and actual test-stack baseline
checks. The current harness is useful for staged validation but does not satisfy
the full gameplay goal or establish a morning-test promise.
