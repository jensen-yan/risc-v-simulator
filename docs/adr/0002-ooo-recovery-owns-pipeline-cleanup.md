# OOO Recovery owns pipeline cleanup

Out-of-order recovery is centralized in `OooRecovery`; execute, commit, memory-order, trap, and redirect paths identify the recovery reason and restart or younger-work range, then delegate cleanup to the shared recovery module. This avoids each trigger point inventing its own partial flush rules for ROB, RS, rename checkpoints, CDB, store buffer, cache inflight state, and execution units.

## Considered Options

- Let each stage clean the resources it knows about. This looks local at first, but recovery semantics drift as new structures are added.
- Route every recovery through `OooRecovery`. This makes trigger code slightly more formal, but concentrates correctness rules in one place.

## Consequences

New recovery semantics should extend `OooRecovery` request/result shapes first. A stage should not add a private flush path unless it is deliberately outside OOO pipeline recovery.
