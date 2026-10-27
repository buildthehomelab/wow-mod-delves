# Development server adapter

This slice adds actual AzerothCore hooks. It still does not provide a playable
Earthcrawl delve, player entry command, encounter catalog or currency grant.

`mod-delves` is the required core module folder name. Its loader reads
`Delves.Prototype.Enable` once at startup. The shipped value is zero; the catalog
and revision values are also zero. No scripts register with those defaults.
Even when explicitly enabled, a non-raid Map.dbc entry for provisional map 890
and nonzero objective, count, boss and revision values are required.

When eventually bound through reviewed world data, `instance_evermore_delves`
owns one `Run` per core instance. A future admission service calls
`InstanceRun::Begin` with a durably allocated unique run ID and already-validated
members. Begin rechecks that the characters are real, currently in that map, and
match account/level snapshots. It does not implement progression or tier unlocks;
those checks remain required in the admission service. There is no user-facing
path to call Begin in this slice.

- The core's `OnPlayerCreatureKillCredit` hook supplies kill credit, including pet
  owner credit through the core's normal path. The adapter ignores headless players,
  living targets, actors on another map and actors without a persistent spawn ID.
- A gameobject explicitly bound to `go_evermore_delve_objective` supplies interaction
  credit. It requires a living real player in the same instance and in interaction
  range. No generic object template or existing dungeon is rebound by this change.
- Creature and object spawn IDs use disjoint actor-key namespaces. Repeated events
  are deduplicated by Run, including after restoration. Summoned actors without a
  spawn ID are deliberately not supported yet.
- Core `Load`/`GetSaveData` bridge the run to normal instance saves. Invalid saves
  fail closed and retain their raw data rather than silently resetting progress.
  Saves for another instance or content revision are rejected.
- Every accepted mutation calls core `SaveToDB`. **That API is asynchronous**:
  this adapter does not establish durable acknowledgements, write ordering across
  crashes or an exactly-once payout. It emits no completion acknowledgement,
  treasure, item, currency or wallet call. A confirmed journal/outbox and wallet
  transaction are still required before releasing rewards or player gameplay.

Content definitions must remain immutable for their revision. Enablement and
catalog changes require a restart of a future test stack; this is not a request
to restart the live realm. No map allocation, production DBC changes,
instance-template row, objective-template binding or encounter spawns are included.

## Validation

`tools/check_core_headers.py --core <checkout>` compiles the adapter and loader
with the real core headers, without mocks or a worldserver link. Optional `--boost`
and `--openssl-include` point at local header locations. This is an API/syntax
check, not a complete server build or runtime hook test. The ordinary standalone
CMake/CTest suite still checks the Run engine separately.

The verified local core reference is
`9e4a232a84d375a8906717c5139fb8d6bf2e41d6`. GitHub CI checks the same pinned
reference. A deployed realm may be at another core revision; pin compatibility
must be checked again when there is an implementation merge to deploy.
