# Evermore Delves

An [AzerothCore](https://www.azerothcore.org/) (WotLK 3.3.5a) module bringing
retail-style delves to Evermore: short solo adventures with a companion, selectable
tiers and repeatable **Tarnished Undermine Real** rewards.

The current **Earthcrawl combat/economy pilot** has solo administrator entry,
four rescue markers, a first Ab'enar encounter, healer Brann, level/progression
scaling and durable coin rewards. It is for an isolated test realm. Full retail
story stages, Burning Cart, ordinary-player entry and Self-Found personal rewards
are still being developed.

## What players get

- A solo instance with saved objectives that survive reconnects and restarts.
- Brann follows the admitted character and supplies personal healing potions.
- Levels 10–80 and tiers 1–3 in the scaled test configuration, within the character's
  individual-progression era cap.
- One Undermine coin per selected tier on completion, delivered to the account
  wallet. A new run can pay again; reconnecting or retrying the same completion
  cannot grant another payment.
- A saved return location and `.delve leave` to return there.

## Install

### Server

Clone into the core's `modules` directory:

```bash
cd azerothcore-wotlk/modules
git clone https://github.com/buildthehomelab/wow-mod-delves.git mod-delves
```

Use **`mod-delves`**, without `wow-`: the folder name must match the module loader.
For coin rewards, also install or update
[mod-boss-coins](https://github.com/buildthehomelab/wow-mod-boss-coins):

```bash
git clone https://github.com/buildthehomelab/wow-mod-boss-coins.git mod-boss-coins
```

Skip that clone if the wallet module is already installed; update it through your
normal reviewed module workflow. The tested wallet merge is
`40e035e145c4f0354874f46f77d91f8b23100ac1`. The scaled pilot also requires
[mod-individual-progression](https://github.com/buildthehomelab/wow-mod-individual-progression)
and its hidden quest catalog. The tested core is Evermore's `9e4a232a` revision.

**Re-run CMake, rebuild and install the server**, as for the other modules.
Copy `conf/mod_delves.conf.dist` into your module configuration directory as
`mod_delves.conf`; retain the wallet's `mod_boss_coins.conf` too. All delve features
are disabled in the distributed defaults until the matching data is installed.

On the managed Evermore server, use the reviewed `wow-server` manifest pins and
normal build/overlay process. Put settings in the documented configuration override
and run `sync-overlay.sh`; do not replace a locally customized configuration file.

### Databases

Back up the databases and use an **isolated test stack**, with the realm stopped
while installing the coordinated data. This pilot's delve SQL is staged manually;
cloning the module and rebuilding is not the complete installation.

Apply these files, in order, to the test **characters** database:

| Module | File |
| --- | --- |
| Delves | `data/sql/characters/2026_10_27_00_delves_journal.sql` |
| Delves | `data/sql/characters/2026_10_28_00_delves_progression.sql` |
| Delves | `data/sql/characters/2026_10_28_01_delves_rewards.sql` |
| Boss Coins | `data/sql/db-characters/mod_boss_coins_characters.sql` |
| Boss Coins | `data/sql/db-characters/mod_boss_coins_delve_awards.sql` |

Each path is relative to its own module folder. An existing wallet installation
still needs the durable delve receipt/procedure migration. Check the wallet's
normal SQL update workflow when staging it; retain existing balances and receipts.
The required tables must use InnoDB, and the receipt migration needs permission
to create its stored procedure.

Generate the pilot's **world** install and rollback files from the delve module:

```bash
cd mod-delves
python3 tools/build_pilot_world.py --output /tmp/earthcrawl-world
```

Use a new output directory. Apply the generated `world.install.sql` to a backed-up
world clone whose database name ends **`_delve_test`**, for example
`acore_world_delve_test`. The installer deliberately refuses other names and
pre-existing pilot IDs. Use a MySQL client that stops on errors; never use `--force`.
Keep the generated `world.rollback.sql` with this exact installation.

To install on a realm without a separate test stack, pass the exact world database
name instead: `--database acore_world`. The generated SQL then refuses every other
database. `tools/build_cart_world.py` takes the same option.

The pilot uses provisional map 890, area 14999 and actor entries 1499000–1499001.
Check these and the remaining manifest IDs against your test stack before installing.

The server also needs the map, area, difficulty and cart model/display rows. Rather
than replacing the server's DBC files, write them into the world's `*_dbc` tables,
which the core loads alongside the files:

```bash
python3 tools/build_server_dbc.py --client /path/to/package/DBFilesClient \
  --core /path/to/azerothcore --output /tmp/earthcrawl-server-dbc --database acore_world
```

Apply `server-dbc.install.sql` before starting the worldserver. Its rollback refuses
while the pilot world package is still installed.
The [coordinated package guide](test-package/README.md) describes collision checks,
installer error handling and the full installation sequence.

### Client patch and server map data

Get the **matching converted Earthcrawl client and native-data bundle** from the
Evermore test delivery. Game assets are not included in this source repository;
the source checkout alone cannot supply the map. If that bundle has not been
provided, obtain it before enabling the module.

- Install the bundle's `earthcrawl-registered-candidate.MPQ` in the test client's
  approved patch slot/order. Its SHA256 is
  `ba874d4c974b68493b55a6ebb3b1f413d460c4dead6b2b188dc5d22701be9ac4`.
  Do not choose a patch filename independently of the realm's shared DBC patches.
- Give the server the same map, area and difficulty rows: either stage the matching
  complete DBC files in the server's DBC directory, or use `tools/build_server_dbc.py`
  (above) to put them in the world's `*_dbc` tables. Either way they must agree with
  the client. A DBC replaced by the patch must carry every row of the last earlier
  patch that ships it (Reforged patch-M's AreaTable, for example), or that patch's
  rows disappear for whoever loads this one.
- Merge the matching `maps`, `vmaps` and `mmaps` files into the server's configured
  data directories. Preserve the existing directories and back up overwritten files.
- Never install one-row extractor fixtures or native data generated from an older
  client candidate. The candidate's geometry manifest identifies the matching MPQ.

Offline checks verify archive/native hashes and named file references; actual
client rendering, collision and navigation still need the controlled game test.
See [client data](docs/CLIENT-CANDIDATE.md) and
[asset validation](docs/CLIENT-DEPENDENCIES.md).

## Configuration

After all matching data is staged, merge
[`test-package/earthcrawl-test.candidate.conf`](test-package/earthcrawl-test.candidate.conf)
into the isolated test stack's configuration. It supplies the actual entry
coordinates, world entries, spells, scaling and companion settings; enabling only
the master switch is insufficient.

For coin delivery, both settings must be enabled in the configuration actually
loaded by the core:

```ini
Delves.Prototype.Rewards.Enable = 1
BossCoins.DelveAwards.Enable = 1
```

The candidate enables these along with `Delves.Prototype.Enable`, test commands,
Brann, the encounter and scaling. Enablement is checked at startup. Start the test
realm through your normal server workflow and check for missing schemas, script
bindings, spell entries or disabled-wallet errors before entering.

## Commands and first test

Use a real, non-Self-Found **administrator**, initially level 30 in Classic
progression, solo on an ordinary continent, alive and outside combat/transport.

| Command | What it does |
| --- | --- |
| `.delve enter 1` | Enter the tier1 pilot and save the origin. Tiers2–3 are also supported. |
| `.delve status` | Inspect the admitted run and confirmed objective progress. |
| `.delve leave` | Return to the saved origin. Leaving an active run abandons it. |
| `.wallet` | Check the account's coin balance, provided by Boss Coins. |

Visit all four markers to unlock Ab'enar, then defeat him. Check Brann's potions,
interrupts/dispels, saved progress after reconnecting and the wallet reward.
Delivery checks pending rewards every ten seconds while the character is online,
even after leaving the instance. Some older pilot chat wording still says no
rewards; use the configured reward switches, `.wallet` and the durable records
to verify actual delivery.

A completed or abandoned run cannot restart in its existing instance. Another
attempt requires a fresh core instance allocation; the test commands do not
reset/recycle terminal IDs. Do not delete journal rows or reward receipts to
make another run. See [reward tests](docs/REWARD-DELIVERY.md) for recovery cases.

## Uninstalling

Return test characters to their saved origins first. Stop the isolated realm,
disable its candidate settings and apply the matching generated world rollback
to the same `_delve_test` database. Restore the previous client/native files and
module pins through your normal reviewed workflow.

**Keep** characters journal, visit, instance, reward outbox, wallet, ledger and
receipt data together. Never reset their identity sequences or delete receipts:
that can permit a completion to pay again. Full rollback and recovery details are
in the [package guide](test-package/README.md).

## Development

Standalone recovery tests require CMake 3.16+ and a C++17 compiler:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
python3 -m unittest discover -s tools -p 'test_*.py'
```

A core build compiles the actual server scripts. See
[server adapter](docs/SERVER-ADAPTER.md), [scaling](docs/SCALING-AND-STARTUP.md),
[encounter](docs/EARTHCRAWL-ENCOUNTER.md) and
[reward delivery](docs/REWARD-DELIVERY.md) for implementation boundaries.


## Araxia collection

The full [Araxia delve collection](reference/araxia/collection/README.md) is now
imported as pinned source and an offline asset staging manifest: 13 locations,
27 boss scripts and all 581 upstream files. Run `tools/stage_araxia_collection.py`
against the full pinned upstream checkout to verify and stage the assets.
The upstream mmap19 tiles require rebuilding for Evermore's mmap20, and its
ALE/Eluna encounters still require an engine integration or C++ ports before
these locations can be enabled. This collection does not alter Earthcrawl.
