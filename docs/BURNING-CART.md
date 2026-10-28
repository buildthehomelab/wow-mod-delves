> **Superseded layout (2026-10-28):** the lane coordinates, native rail landmarks and content
> revision 2026102803 below describe the converted Earthcrawl geometry. Since revision 2026102805
> the lane is `Delves.Prototype.Encounter.Cart.Lane` on the stock Spider Mine layout
> (`test-package/burning-cart.json`); the controller, impact and cleanup rules are unchanged.

# Burning Cart work in progress

The installed pilot does not yet implement Burning Cart. This worktree now wires
the geometry/time controller to an owner-bound CreatureScript and boss event.
Matching world/config and full client/native inputs are now staged offline.
Real SQL/full worldserver CI and actual client validation remain unfinished. Do not enable or advertise it as a playable mechanic yet.

Historical primary addon observations recorded in the workspace's
retail-s1-encounter-audit.json identify retail spell448412 / observed unit
spell448348, first12.1s and advisory repeat35.2s. Those timers are observations,
not authoritative server rules; no DBM source code is copied. A gameplay guide
also describes a sidestep response:
https://www.icy-veins.com/wow/earthcrawl-mines-delve-guide . Geometry and damage
values still need historical capture/client validation; stock 3.3.5 spells cannot
be assumed equivalent to those retail IDs.

## Controller implemented here

A bounded polyline is traversed at constant speed after a warning period. Each
update checks the entire traveled subpath rather than only the new cart position,
including through turns and a long update interval. The hit volume intersects a
horizontal radius with a vertical slab, so slopes are handled without treating
other floors as the same lane. A cart may impact only its exact frozen
run/character/account once. Elapsed time saturates safely when finished.

The adapter remains responsible for validating live player, map, instance, phase,
run ownership/status and encounter eligibility before supplying a victim. The
controller accepts no client coordinates or requests. It neither casts spells nor
creates objects, changes persistence or pays rewards.

Sanitizer checks cover warning timing, sidestepping, fast movement, curved paths,
lag/overflow, cross-floor rejection, slope/vertical intersection, duplicate impact,
wrong account/run and invalid coordinates. This is offline controller evidence.

## Required next integration

1. Identify and validate the actual rail lane against the converted room and its
   native collision/navigation, rather than inventing a straight line to a player.
2. Register a matching reviewed cart visual. Converted Earthen cart models already
   exist in the candidate, but a creature display/template binding is still needed.
3. Integrate a boss event with visible warning, constant-speed movement and scaled
   damage/effects, normal owner eligibility checks and combat reset cleanup. Use
   observed retail behavior as the target and document every 3.3.5 adaptation.
4. Add world/config/client/native pairing for the new revision, full core compile/
   link and controlled gameplay checks. Keep the original pilot test baseline stable.
5. Obtain independent/formal review, resolve findings, and use the actual merged
   commit in a separately reviewed server pin before any future rollout.

## New visual and rail evidence

`tools/build_cart_display.py` preserves full baseline CreatureModelData/DisplayInfo
rows and strings, verifies their recorded hashes, rejects occupied IDs/fixtures
and adds a named converted minecart model/display. It uses MD20 mesh bounds for
an unmounted unit's model metadata and keeps scale1. This creates an offline DBC
candidate, not a new installed client archive or a world creature template.
The source model is the converted `11ea_earthen_minecart04.m2`, already present in
the map candidate. Display/model149990 are provisional and must be reserved against
the actual stack before installation. The in-client creature render/animation path
must be checked; a valid static model alone does not prove a working creature.

The native collision files contain247 rail/cart landmarks. A straight track near
the pilot boss includes model placements435,437,395 and475 with matching rotation
around52degrees. These are extracted model transforms, not an invented aim-at-player
line. Supporting floor height, uninterrupted travel, lane ends and safe sidestep
space still need validation before using them as encounter movement data.

## Native rail probe checkpoint

`tools/check_cart_lane.cpp` loads the pinned native map890 collision/navmesh and
samples the actual placement395→437 rail centerline at31 points. It checks
collision floor, nearby safe ground polygons, complete paths and direct Detour
walkability rays. Six additional destinations are four yards to either side at
center samples14–16, with paths/rays from the center to the sidestep destination.
Exit1 means at least one required sample failed; it must not be treated as an
approved encounter lane. The probe neither installs data nor contacts a realm.

The current centerline passes31 samples, approximately
(-791.20508,-330.61328,76.4421)→(-773.48242,-307.92969,76.4466).
All six sidestep destinations have floor, complete paths and direct walkability
rays under the pinned core's non-headless player / walking-creature ground filter.
The earlier ground-only probe rejected the rail edges: its strict ray fractions
are retained as `rayFraction`, alongside `steepRayFraction`. The normal filter
includes NAV_GROUND_STEEP (PathGenerator.cpp CreateFilter); the headless-bot
filter excludes it. The probe still rejects all liquid hazards. This is a
correction to the actor model based on pinned core behavior, not evidence that
headless bots can safely cross those edges.

`--profile` is a separate diagnostic mode:21 floor/ground projections across the
midpoint at half-yard intervals, plus42 collision rays at0.5 and1.5 yards above
local floors. All projections resolve and all42 rays are unobstructed. Untested
navigation rays are `null`, not an implied pass. These rays do not model a swept
player capsule, foot stepping, or real client movement. Actual sidestepping and
companion traversal remain controlled gameplay checks before enabling the mechanic.

The cart controller also exposes the first swept contact position separately
from its final position after a delayed tick. A future knockback must originate
from contact, not the cart's final position beyond the player.


## Runtime integration checkpoint

The boss summons one passive, non-attackable cart after its first12.1s event,
then on advisory35.2s repeats. Events wait for the boss casting state to clear.
The stationary flaming cart supplies a two-second warning, then begins a straight
spline on the validated rails at8yd/s. The hazard clock starts after the warning
transition, preventing a delayed update from applying damage ahead of the visual.
Each update re-resolves owner/boss GUIDs and validates the exact active frozen
run, character/account/level/progression, live solo non-headless owner, map/phase,
current boss objective and alive/combat state. No Player pointer is retained.

A swept contact deals one2xscaled-bolt fire hit to the admitted owner only and
knocks the survivor away from first contact. The2.5yd radius,2yd vertical slab,
2s warning,8yd/s speed and knockback8/3 are provisional test tuning, not measured
retail values. No retail spell binaries or stun behavior are implied. Boss reset/
death clears its cart; owner death, abandon, disconnect, invalid/final run and
boss combat exit cause cart despawn. No objective credit or rewards come from it.

Enablement requires scaling, encounter enablement, new content revision2026102803,
loaded summoned-only entry1499002/script npc_evermore_burning_cart and display/
model149990. Startup checks stock20832 single direct enemy damage and28330 single
self dummy aura metadata, rejecting additional effects/targets. These spell IDs
are3.3.5 adaptations. Defaults remain off and IDs are provisional. A matching
world package and full DBC/client pairing are now staged offline; CI and gameplay
validation remain required before rollout.
Do not change a persisted active run in place; retain journals and start a fresh
revision run using the test/rollback procedure once the package is reviewed.

Contact validation includes map LOS from the swept contact point to the owner's
mid-collision height, including static and dynamic collision in the actor's phase.
It does not use the cart's final position after a delayed tick. An occluded contact
is rejected before consuming the one-hit state, so a later exposed update can
still hit. The controller regression test checks both rejection and later exposure.
As with victim-position sampling, this does not reconstruct changing geometry or
continuous player motion during a lagged tick; an occluded first contact fails
closed rather than manufacturing an impact later in that same straight segment.

## Additive world package checkpoint

`tools/build_cart_world.py --output NEW_DIRECTORY` emits install/rollback SQL
without connecting to a database. It requires a world database ending `_delve_test`,
InnoDB tables and the exact base pilot package receipt. It refuses occupied cart
entry/display bindings and installs only a summoned creature template/model plus
its manifest receipt; there is no persistent cart spawn. SQL errors roll back the
content transaction. Rollback refuses missing/wrong receipts, persistent cart
actors, changed known template/model fields and unexpected display reuse. Roll
back this extension before the base pilot; the updated base rollback enforces it.
If a failed CALL leaves its stored procedure, inspect the failure before dropping
that exact generated procedure and retrying. Do not use mysql --force.

`test-package/burning-cart-test.candidate.conf` preserves the base test settings
but selects fresh content revision2026102803 and enables the cart. Finish/abandon
old runs and save/return players before stopping the isolated world to change
revision. Preserve all characters journals, visit origins, reward receipts/outbox
and wallet data; this extension never edits them. Revert to the base config and
matching base client/native package after extension rollback. Do not reuse an old
active instance as a new revision run or reset identity sequences.

The existing disposable-MySQL world CI checker now tests wrong base receipts,
occupied cart IDs, mid-install rollback after injected failure, repeated install,
base-before-extension rollback refusal, edited-model refusal and full cleanup.
These real SQL cases require CI/MySQL execution; local Python checks alone do not
prove SQL execution. Full DBC/MPQ/native pairing is now checked offline; final CI/reviews and client
checks remain unfinished, so this is not an installation-ready extension yet.


## Coordinated client/native candidate

`tools/build_cart_client.py` stages a complete preserved client asset directory,
adds the two full cart DBC tables and records an explicit native reuse chain.
It validates the recorded base client/display/native manifest hashes, every808
base asset, every170 native file, complete preserved baseline DBC rows/strings
and the named model/display binding before writing. It rejects input/output
containment, unmanifested client/native geometry and occupied cart DBC inputs.
`--verify-existing` rechecks a staged directory without writing.

The offline archive `earthcrawl-burning-cart-candidate.MPQ` has810 game files,
SHA256 `65f376304f9892c19bbcd7b00d7f2e514fa6152b800254a7e9f728dea46cbf8d`.
StormLib readback verified all810 against the staged files. Only the two creature
model/display DBCs are added; all808 base assets, including map/area metadata,
terrain, WMO and model geometry, remain byte-identical. The pinned map/vmap tools
do not consume these creature DBCs, so the unchanged170-file native package is
reused with its original manifest, not relabeled as newly extracted geometry.
The release manifest records both archive identities and the native manifest hash.
Do not replace the base native manifest's original archive hash.

`tools/check_cart_assets.py` verifies the base pilot inputs, the staged candidate,
matching archive readback receipt, new archive identity, generated install/rollback
SQL and stock cart spell effect/target metadata against burning-cart-release.json.
Supply `--base-client`, `--client`, `--display`, `--baseline`, `--combat`, `--native`,
`--base-archive`, `--archive`, `--archive-receipt` and `--world` from the recorded
local candidate directories. The readback receipt is evidence of the separate
StormLib archive readback; this checker validates its identity, not a fresh archive
unpack. Keep the archive/receipt together and rerun readback after repacking.

This patch replaces the earlier pilot MPQ for the controlled cart revision; do not
invent a production patch letter/order. Verify the actual test client's full DBC
baseline and later patches before choosing its filename, because these two whole
DBC replacements must win without dropping other features. Server DBC copies must
match. Keep the earlier archive/config and rollback SQL for recovery. Model149990
is provisional: in-client creature animation/rendering and usable sidestep/companion
movement remain explicit gameplay tests. Asset hashes do not establish those.
