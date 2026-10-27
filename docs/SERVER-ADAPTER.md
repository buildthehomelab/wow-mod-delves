# Development server adapter

This slice adds real AzerothCore hooks, confirmed checkpoints and optional
administrator test controls. It is **not a playable Earthcrawl delve**. Production
map/actor registration, encounter content, Brann, era scaling and rewards remain
outstanding. Do not install the workspace's one-row DBC extraction fixture.

## Configuration and test controls

The required module directory is `mod-delves`. All enablement is startup-only.
`Delves.Prototype.Enable` defaults to zero. Registration also requires the module's
characters schema, a non-raid Map.dbc entry for provisional map 890, and nonzero
objective, count, boss and immutable content revision values. This change neither
reserves that map ID nor modifies world templates or production DBCs.

An additional `Delves.Prototype.TestCommands.Enable` switch and explicit
`EntryConfigured` acknowledgement enable `.delve enter <tier>`, `.delve status`
and `.delve leave`. All commands require an actual administrator session, even if
world command permissions are changed. Entry coordinates must be supplied and
validated in a separate geometry test; zero defaults are not a verified spawn.

- Entry accepts one living human administrator, level 20-60, tier 1-3, ungrouped,
  on an ordinary continent, out of combat, flight, vehicle, transport and transfer.
  A durable origin/tier/level/revision row is read back before teleport begins.
- Arrival and login admit only the saved character, at the frozen level and
  revision, alone in the actual destination instance. Failed entry retains the
  origin; `.delve leave` returns there. A conflicting instance is never reset.
- Reconnect loads the same checkpoint and roster. The progression value zero is
  an **unknown development snapshot**, not verified IP eligibility. Tiers are
  frozen identifiers here; they do not yet scale encounters. These controls are
  not the future player admission service or Journal interface.
- Leaving marks the return durably, attempts terminal abandonment of an active
  owned run, and teleports to the origin. An uncertain abandonment never prevents
  escape. The visit is deleted only after arrival at the origin is observed;
  same-map returns are cleaned on login or the next entry attempt. Failed return
  keeps the saved origin available for another `.delve leave` attempt.
- A terminal run is not restarted in its existing instance. Core instance reset
  and new instance allocation must occur separately for another test. A recycled
  instance ID with an existing journal row fails closed. Do not delete journal
  rows to make room for IDs; production allocation/retention still needs design.

## Authoritative persistence

`data/sql/characters/2026_10_27_00_delves_journal.sql` creates two new InnoDB tables
without changing existing realm tables. It is development schema, manually staged
for an isolated test database, not an automatic live migration. Apply only as part
of an explicitly approved coordinated test installation. Keep and back up the
journal, visit and core instance rows together; never reset the run-ID sequence.

The journal allocates globally unique run IDs within this characters database.
Each mutation copies the confirmed run, validates the event, conditionally replaces
its exact previous checkpoint, and synchronously reads back the desired checkpoint.
Only then is the candidate published in memory. A failed or uncertain acknowledgement
freezes that loaded session. Reload restores the last confirmed database checkpoint,
including a commit whose acknowledgement was lost. Invalid/duplicate events do not
write at all. A lock protects snapshots and mutations across control and credit hooks.

Core's asynchronous instance save remains a compatibility mirror. Initialization
reads the module journal even when the core mirror is empty; subsequent mirror
loads cannot overwrite it. Corrupt or incompatible journal data fails closed.
A core save without a journal is refused, not silently migrated. Formatted SQL is
bounded server serialization and escaped with the core database API; modules cannot
allocate custom core prepared-statement enum slots.

This synchronous prototype performs database round trips on admission, mutation
and test control hooks. It is deliberately restricted to development: latency and
availability must be measured, and confirmed asynchronous persistence designed,
before broader player enablement. Storage durability still depends on normal
InnoDB/server backup settings. The journal does not implement a wallet transaction,
reward outbox or exactly-once payout. No item or coin is granted by this module.

## Actor credit

The kill hook requires a real admitted player and a dead persistent creature in
the same instance. Pet-owner credit follows the core hook. Objects explicitly bound
to `go_evermore_delve_objective` require a living admitted player, same instance,
persistent spawn and interaction range. Creature/object IDs occupy disjoint actor
namespaces and repeated credit remains rejected after reload. Summons without a
persistent spawn ID are unsupported. No existing object or dungeon is rebound.

## Validation and remaining server-test checklist

Standalone sanitizer tests exercise ordering, recovery, deduplication, lost write
acknowledgements, stale writers and frozen-session behavior. The real-header check
compiles every adapter source against core
`9e4a232a84d375a8906717c5139fb8d6bf2e41d6`, without mocks. CI also applies the new
schema twice to an isolated MySQL instance and checks identity/account constraints.
These checks do not establish a full worldserver link or gameplay behavior.

Before a controlled server gameplay test, finish and review the map/client/world
registration and content package, verify safe entry coordinates, build a compatible
worldserver and prepare installation/rollback instructions. Then test solo entry,
failed teleport, disconnect/restart with an empty or stale core mirror, duplicate
actors, outsider credit, database failure, frozen recovery, leave and new-instance
attempts. Brann, retail encounters, era balance and completion rewards need their
own implementation and validation before claiming a complete pilot.
