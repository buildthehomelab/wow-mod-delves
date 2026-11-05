# Objective guidance

Entry, reconnect and `.delve status` show the current objective's action, loaded
world-template name and confirmed count. Stages are numbered from one in this
message. The existing diagnostic stage field remains zero-based. Missing templates
fall back to the object/creature entry so the command remains usable for diagnosis.

Successful rescue interactions and kills announce only after the journal confirms
the new checkpoint. Partial objectives display the saved count; a stage transition
displays the next objective. Repeated actors and unrelated events produce no
success announcement. If the persistence attempt blocks the run, that attempt
reports the blocked checkpoint instead of claiming progress. Recovery reads the
same durable state; messages do not write objectives, reset stages or grant coins.

Completion tells the player to check reward delivery with `.delve status`, which
retains its separate outbox summary. It does not assert a wallet payout. Abandoned
runs point to `.delve leave` and explain that another attempt needs a fresh instance.
Blocked runs show recovery/exit guidance rather than an apparently actionable goal.
The existing ownership and frozen-member checks still gate credit and status.

This presents the pilot's existing synthetic rescue markers and boss objective;
it adds no retail story stages, navigation markers, localized templates, SQL or
client assets. Runtime chat ordering and presentation still need the controlled
client test.

The standalone `delve_objective_tests` covers initial/partial/next-stage guidance,
terminal states, recovered checkpoints, read-only access, missing names, duplicate
actors and failed versus confirmed journal writes. Compile the adapter against the
pinned core headers as well; standalone formatter tests cannot prove live hook
behavior. In-game checks should include all four distinct markers, one repeated
marker, reconnect after partial progress, boss unlock, completion/reward delay and
abandonment. Use a disposable database fault test to check the blocked message;
do not alter the active testing database to simulate failure.
