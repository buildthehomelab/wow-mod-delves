# Completion reward outbox

This journal records immutable completion intent. Optional delivery through the
wallet API is documented in [REWARD-DELIVERY.md](REWARD-DELIVERY.md); it defaults
to disabled until the matching reviewed modules, schema and configuration are ready.

Apply `data/sql/characters/2026_10_28_01_delves_rewards.sql` to the isolated
characters database alongside the journal and progression migrations, only with
separate SQL authorization. Enabled startup requires the new table and checks
that run/outbox tables both use InnoDB. No production SQL has been applied.

A confirmed completion updates the checkpoint and inserts one reward row per frozen
member in the same synchronous transaction. Policy1 records one coin per selected
tier (1–11 supported by the engine, currently 1–3 in administrator admission).
This is a provisional farming balance, not a claim about retail rewards. Active
and abandoned runs record no awards. Ordinary checkpoint changes remain journaled.
The inserted account/character/amount never change on retry. Both the exact
checkpoint and every expected award must read back before in-memory completion is
acknowledged. A database error or mismatch blocks the run through ConfirmedRun.

The insert requires the exact completed checkpoint, run ID and instance ID to be
present. A stale compare-and-swap cannot create intent for a different state.
Identical retries preserve existing rows, including their delivery marker; conflicting
amounts/accounts fail acknowledgement. Old completed runs are not retroactively
backfilled. Delivery must consume these rows even after leaving the instance and
must mark delivered only after Paid or AlreadyPaid from the matching durable wallet
receipt. Do not use legacy Wallet.Add. Self-Found needs a separate personal route;
do not bypass its account-wallet restriction.

Retain run, outbox, wallet and receipt data together across backups/rollbacks.
Disable the feature and revert reviewed pins for rollback; never truncate the
outbox or receipts to repeat a test. Reinstalling schema preserves intent.

Validation includes sanitizer recovery tests and the actual generated insert SQL
against disposable MySQL: success, identical retry, stale CAS and injected failure
between checkpoint and outbox. In-game delivery, client rendering, live admission
and combined wallet linkage remain unvalidated.
