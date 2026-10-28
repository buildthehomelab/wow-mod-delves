# Evermore Delves — development foundation and adapter

**Development only. The server adapter is disabled by default; there is no playable delve yet.**

The target is a faithful Earthcrawl Mines pilot with Brann, progression-era scaling
and repeatable Tarnished Undermine Reals. This first component implements the
server-independent run state machine. It is kept separate so recovery and event
isolation can be checked before adding live gameplay or money grants.

## Build

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Requires CMake 3.16+ and a C++17 compiler. The current Evermore core uses C++20;
this component uses the compatible C++17 subset and has no external dependencies.
The tests use synthetic objective entries. They do not reproduce retail encounter
content, create creatures, allocate a game instance or pay currency.

## Development server adapter

The optional AzerothCore adapter adds instance-save, real-player kill-credit and
objective-interaction hooks, confirmed run checkpoints and optional administrator
entry/return controls. It has no production content or rewards.
See [SERVER-ADAPTER.md](docs/SERVER-ADAPTER.md) for its boundaries and validation.
Standalone builds compile only the Run component; a core build collects `src/`
through its normal module loader.

The offline [client candidate builder](docs/CLIENT-CANDIDATE.md) preserves the full
preceding DBC tables and assigns isolated WMO root identities. Its output still
requires matching native extraction and controlled client validation.

An optional [healer companion prototype](docs/BRANN-PROTOTYPE.md) follows the
admitted solo character and places personal proximity-pickup healing potions.
It requires dedicated world templates and remains disabled by default.

## Integration contract

- Allocate a durable, globally unique run ID before admission. Never derive it
  solely from an instance ID, which can be reused after reset.
- Validate real-player admission, level/progression requirements, group agreement,
  difficulty unlocks and map/instance ownership in the server adapter. `Member`
  is already-admitted data, not a request from an addon.
- Freeze roster, tier, content revision and each member's level/progression.
  Definitions must be immutable for a revision. Objective entries/counts are
  content data; they have not yet been assigned to the retail pilot.
- Supply server-authored events only. Resolve actor membership in the exact run
  and actual alive/death/interaction state before calling `Credit`. The API rejects
  wrong map/instance, nonmembers, wrong order and duplicate actor identities.
- Actor credit identity is unique across the whole run; actors must not be reused
  as credit sources for multiple objectives. A respawn does not earn fresh credit.
- Persist `Save()` after every accepted mutation before acknowledging it to the
  client. Use instance-owned synchronization; `Run` is not thread-safe. A save is
  trusted server storage, never client input. Parsing is bounded and rejects
  invalid status/stage/count combinations, unknown revisions and duplicate credit.
- `RewardKey()` returns a stable personal completion key, **not a payout**. Before
  enabling rewards, implement an outbox and wallet transaction that atomically
  records this key and changes the balance. A repeated call must never pay twice.
  The existing wallet's in-memory balance must remain consistent on DB failure.
- Abandonment is terminal. A new attempt requires a new durable run ID. Completion
  is also terminal and cannot be abandoned to erase reward receipts.

## Remaining components

Player admission and entry, verified retail objective/encounter
catalog, creature/gameobject templates and spawn data, complete retail Brann behavior, difficulty/era
scaling, exactly-once wallet integration, client DBC/map packaging, Journal UI,
server build/CI, and gameplay validation are not yet implemented here.

The asset discovery/conversion experiments and source provenance are recorded in
the Evermore workspace's `artifacts/delves/` directory. Game assets are not stored
in this source repository. No realm build, restart or database change is performed
by these tests.
