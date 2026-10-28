# Pilot reward delivery

The optional delivery path connects completion intent to the receipt-confirmed
API from wow-mod-boss-coins PR #22, head472de63ffc3a321c1226bc38c294e6589a89b32c.
Both modules must be included in the same worldserver build. This source reference
is a test dependency, not a deployable server pin: resolve actual merged commits
and obtain reviewed wow-server pin updates before rollout.

Set `Delves.Prototype.Rewards.Enable=1` and `BossCoins.DelveAwards.Enable=1`
through the separately authorized server configuration workflow, after installing
both modules' characters migrations on an isolated test realm. Defaults are off.
The delve pilot refuses to arm rewards if the durable wallet headers are absent.
Delivery waits for the wallet's own startup schema checks and balance load.
No database access occurs during script registration before StartDB.

A real player's update checks at most one pending award every ten seconds. The
query matches both character and account and works in any map, including after
leaving a completed instance or logging back in after a restart. Offline players'
intent stays durable until they return. No raw Player pointer is retained.
The provisional farming award is one coin per selected tier; a new run has a new
key, so repeat farming is allowed while retrying the same run cannot pay again.

Only Paid or AlreadyPaid acknowledges the outbox row. If the wallet commits but
updating the marker fails, the next attempt reads the exact receipt and marks it
without another credit. Deferred retains intent for retry. Rejected or Uncertain
quarantines that character until controlled startup recovery and logs one error.
The wallet also freezes all account mutations after an uncertain acknowledgement.
Do not clear receipts or force delivered=1 to hide a failure. Diagnose journal,
outbox, receipt, balance and ledger together before an authorized recovery.

Self-Found admission is excluded when rewards are enabled because its personal
bag-coin route is unfinished. The wallet independently rejects shared-wallet awards
for Self-Found players, including players who switch that challenge after completion.
Such intent is retained for investigation; do not bypass this restriction. Other
admission/progression checks continue to apply.

## Controlled gameplay checks after review/build

1. Use a non-Self-Found test character and note its wallet balance. Complete the
   pilot at tier1; within ten seconds check for exactly one additional coin and
   matching run/member receipt, ledger and delivered outbox row.
2. Reconnect and revisit the completed run. Balance must remain unchanged. Complete
   a separately admitted new run; its distinct run ID should award again.
3. Repeat at a permitted tier2/3 and a lower-level progression character, checking
   tier coins and frozen account/character identity.
4. In an isolated disposable environment, exercise a restart between payout and
   outbox acknowledgement. Keep all tables. Recovery should finish delivery once.
5. Check full wallet capacity and existing bag/shop activity: delivery defers without
   a partial award or consumed key. Confirm Self-Found admission does not proceed.

CI compiles with and without wallet headers and links both modules into worldserver,
asserting the real delivery branch is present. The negative startup check enables
both reward flags against an unreachable fake DB. Generated SQL from both modules
also tests payout-to-marker failure/recovery with one ledger credit. This does not
replace live gameplay, thread/latency or client validation. Periodic synchronous
queries are intended for a small controlled pilot; measure before wider rollout.

Rollback disables both reward flags and follows reviewed module pins. Preserve all
run/outbox/wallet/receipt/ledger data and backups. No restart or live SQL has been
performed by this development task.

## Follow-up test feedback

Administrator entry checks wallet readiness and Self-Found eligibility before
creating a new return visit or attempting teleport. The instance admission check
still repeats this guard. A disabled reward feature does not require the wallet.
An unavailable wallet while rewards are enabled refuses a new entry; existing
completion intent remains durable for recovery.

`.delve status` reports tier and a named run state, followed by a read-only reward
summary for the exact run/member/account: disabled, potential completion amount,
abandoned, missing record, queued, stopped for investigation or delivered.
Delivered describes the durable outbox acknowledgement; `.wallet` shows the
current spendable account balance. It does not assume the reward remains unspent.
The status command never grants coins, updates receipts, clears quarantine or
marks delivery. Failure to read the database produces an unavailable message.
