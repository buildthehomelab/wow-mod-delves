# Frozen level/progression scaling and startup lifecycle

This development slice replaces the fixed level30 combat candidate with an
administrator test envelope spanning levels10–80 and tiers1–3. It implements
initial Evermore balance, not retail tuning or a claim that every era/tier is
balanced. Ordinary player admission, the complete retail story and rewards still
remain incomplete.

## Initialization

The pinned core calls `ScriptMgr::Initialize` before `StartDB`. Module loader
functions must therefore register scripts without database, DBC, spell or world
catalog queries. The prior prototype violated this ordering when enabled; disabled
builds could not reveal it.

All delve scripts now register inertly at module load. A `WorldScript` arms them
only in `OnBeforeWorldInitialized`, after world data, script bindings and command
map creation. Until then the instance factory returns no instance script and test
handlers/arrival hooks perform no database reads. Command tables register early
because building them after this hook would miss the core's command-map phase.

Arming validates journal/visit schema, map metadata, dedicated instance/object/
boss script bindings, and any enabled companion/encounter/scaling configuration.
Failure leaves admission disabled. Startup configuration stays immutable across
ordinary config reloads. Expected success log is `Delve prototype initialized
after world data; revision ...`. All module defaults remain disabled.

Full-build CI also launches the linked binary with explicit delve enablement and
an unreachable fake local database on port1, with reconnect attempts disabled.
It must finish script registration and exit normally for the connection refusal,
without attempting delve world-data validation. It neither opens realm databases
nor starts a playable world. This test does not prove the later loaded-world
arming phase in a running realm.

## Progression contract and persistence

The verified Individual Progression data contract uses rewarded hidden quests
66001–66018: Classic stages0–7, TBC stages8–12, Wrath stages13–18. Stage11 is unused
in the current enum but the source's quest scan includes it. The independently
written adapter reads rewarded quest state through the core's public player API;
it does not copy or link the AGPL module implementation. Provenance: authenticated
IndividualProgression.cpp blob `b6978d598fb5ee95616cd46af9935203d2900236`, header
blob `510896357ac883590c6876ac2ec605878c6dfca1` in
https://github.com/buildthehomelab/wow-mod-individual-progression.

When scaling is enabled, startup requires Individual Progression enabled and its
known quest catalog present. Missing catalog/state fails closed. Stage0 now means
a verified Classic character, whereas an unknown visit uses UINT32_MAX. The new
characters migration adds that sentinel to old rows and is safe to reapply.
Apply the base characters schema and then
`2026_10_28_00_delves_progression.sql` to the isolated test database. It does not
invent a progression stage for a visit made by an older prototype.

Entry saves level, progression, tier and content revision before teleport. Arrival
rechecks the actual character and compares restored run inputs with that visit.
Begin independently checks the profile and snapshot. Kill/object credit, boss
combat and companion ownership reject changed level/progression inputs. Return
and abandonment remain available after a change or unknown legacy snapshot.
The existing run serialization already persists member level/progression and tier.
The candidate content revision advances to2026102802; old definitions must not be
resumed as the new scaling content. Retain the exact old manifest/source when
rolling back an older candidate—new manifest hashes cannot authorize its rollback.

## First balance policy

Eligible characters are levels10–80 with progression0–18, within era caps60/70/80.
The policy supports tiers1–11 internally; administrator controls currently expose
only1–3. It is not a tier-unlock implementation. A lower-level alt with advanced
progression uses the appropriate level-band stats (Classic through60, TBC61–70,
Wrath71–80), rather than potentially empty high-expansion rows at low levels.

Health uses the core's creature base health for frozen level/class/expansion,
template health multiplier, and `100 + 35*(tier-1)` percent for the boss. Base
weapon damage uses the appropriate base damage and `100 + 20*(tier-1)` percent.
Mana, armor and attack power use the level's base stats and template modifiers;
core stat-update APIs apply the resulting values. These are assignments from
base data, so retries do not multiply an already scaled value. Unsupported,
nonfinite, zero-health or overflowing inputs are rejected. Template/global aura
modifiers still participate in the core's final stats; combinations with other
realm modules require runtime checks.

Boss scaling runs for loaded, out-of-combat persistent actors after admission and
on later creature creation, so their visible level can match before a pull. It is
reapplied from frozen inputs at engagement. This last path never changes journal
progress. Brann receives owner-level health/mana/armor at summon, without boss tier
health inflation; his percentage-based healing follows the owner's health already.

The first scaled spell adaptations use a normal custom cast: bolt effect0 is
2x creature base damage, while each agony tick starts at0.15x, both multiplied by
the tier damage factor. Startup requires direct school damage and periodic damage
in those respective effect0 slots. Cast metadata, targets, interrupt/dispel and
stock aura behavior remain the core's. Shackles retains its configured stock root
payload. None of these is a retail spell conversion. Burning Cart, authentic spell
payloads, complete story progression and actual balance still need implementation.

## Validation limits

Sanitizer tests cover era boundary rejection, low-level advanced alts, unknown and
future progression, tier bounds, monotonic profile changes, overflow, rounding and
snapshot serialization recovery. Real-header checks compile the integration.
MySQL CI reapplies the migration, verifies legacy rows remain unknown and checks
a real new progression snapshot. Full link and negative pre-DB startup tests must
pass on the final revision. Cold loaded-world startup, entry at era boundaries,
first hit, resets/reconnects, pet threat, spell damage and potion balance still
require the eventual approved controlled test.
