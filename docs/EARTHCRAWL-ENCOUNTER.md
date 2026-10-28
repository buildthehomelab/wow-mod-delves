# Earthcrawl first boss combat adaptation

This slice implements `boss_evermore_abenar`, an explicitly disabled first combat
adaptation. It is not the complete retail Ab'enar encounter or a playable package.
No creature templates/spawns or spell ports are installed by this change.

A persistent creature matching `Delves.Prototype.BossEntry` can engage only the
living admitted solo player or that player's living pet/charmed unit on the same
map and phase. The owner must be ungrouped, real and present with the
same frozen account and level. Its confirmed run must be active, healthy and have
that exact boss kill as its current objective. A pre-objective pull or damage from
an outsider, environmental source or unrelated NPC is rejected. Damage by the
admitted player's owned pets is accepted through the core's owner lookup. Pets
can also pull/tank; victim validation resolves and checks the same admitted owner. Brann
is a passive healer in the existing prototype and supplies no boss damage.

The combat pull binds the run ID. Loss of an eligible victim, logout, death,
grouping, frozen persistence, abandonment or completion causes evade/reset on the
next AI update. Reset clears the event schedule and run binding. A retry uses the
existing core creature reset behavior; it never resets the confirmed objectives.
The ordinary kill-credit hook remains the sole completion path and still requires
a persistent spawn. The AI does not manufacture run credit or pay currency.

Enablement requires three explicitly configured harmful spells present in the
3.3.5 spell store. The stock configuration uses zero IDs and prevents registration.
These settings are test adaptations, not retail spell conversions. The spell
payload, target rules, interrupt flags, dispels, damage and duration require review
as part of the coordinated world/client package. A loaded harmful spell alone is
not evidence that its effects faithfully reproduce a retail ability.

The three casts use normal, interruptible core casting rather than triggered
casts: Web Bolt first at 2.2 seconds, then 6 seconds; Agony first at 6.2 seconds,
then 24.2 seconds; Shackles first at 20.2 seconds, then 32.9 seconds. They target
the eligible victim (the admitted player or their owned unit). These timings are provisional observations recorded from the
historical DBM-Dungeons Earthcrawl encounter metadata (snapshot
89e204e7c4a12ee0f1da4d8c35a9aded6dae33ad, encounter 2877), not verified server
rules. No DBM source code is included. This script is independently implemented.
Interruptions consume that scheduled cast attempt; there is no instant catch-up
burst. A long cast can delay later events. Burning Cart, cart paths/hazards,
retail immunity/target rules, story stages and level/era/tier scaling remain work.

## Validation

The standalone sanitizer test exercises boss unlock through partial objectives,
checkpoint restoration, exact entry matching, completion and abandonment.
`tools/check_core_headers.py` compiles the new AI with the pinned real core headers.
Full worldserver link CI must also pass on the final revision. Threat, evade,
interrupt/dispel behavior, pet ownership, navigation and runtime spell effects
still require the eventual controlled game test.
