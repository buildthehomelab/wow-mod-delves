# First healer companion

This is the first solo healer-role implementation. It is disabled by default and
requires dedicated world templates bound to `npc_evermore_delve_brann` and
`go_evermore_delve_potion`. No existing NPC or object is rebound, no templates or
spawn IDs are allocated here, and this is not a complete retail Brann recreation.

After confirmed admission, the instance owns one runtime companion GUID. Duplicate
arrival/login callbacks reuse the living companion; a dead one is removed before
replacement. The companion stores GUIDs and the immutable run ID, follows only
that admitted human, and checks the active run, account, phase, map and solo roster.
Ending/blocking the run, leaving, logging out or joining a group removes his
support. Death clears potions and schedules the corpse for removal. Summons are
not persisted; reconnect reconstructs support from the confirmed active run.

The first combat transition schedules a potion throw. While combat continues,
Brann places up to three potions every 30 seconds near the owner's current
position. He must be within 30 units and line of sight. Every candidate position
requires nearby collision height and owner line of sight. No point is forced
through a wall or onto a distant floor. A new throw removes the previous set;
potions expire after 30 seconds and are cleared when combat ends.

An injured, living owner picks up a potion by moving within 1.5 units, in phase
and line of sight. Other players, pets and bots cannot consume or receive its
heal. Each object marks itself consumed before calling healing hooks. It then
uses the core's heal/absorb/combat-log path with Brann as healer, not an inventory
item or a client-requested amount. Both owner and healer must still exist in the
same active run and phase. Objects are removed when those conditions fail.

The default immediate heal is 20% of maximum health (configurable 1–30%). This is
an explicit development balance choice. Stock spell 2061 supplies loaded healing
metadata for the log; the script does not cast Flash Heal, spend mana, apply
spell-power bonuses or import retail spell IDs. Invalid templates, script bindings,
object type, heal metadata or percentage prevent companion registration.

Retail talents, experience/level progression, damage role, dispels, healing-over-
time stacks, dialogue and knockout/revive behavior are not implemented in this
slice. Brann is passive and can die; this is not retail's knockout system. The
actual follow path, potion placement, pickup latency, absorb/healing interactions,
reconnect cleanup and combat balance need controlled server/client validation.
Header and link compilation alone cannot establish those behaviors. World data,
client displays and the complete encounter/test installation remain separate work.
