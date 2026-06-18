# Retire CDB as the OOO Completion Boundary

The out-of-order CPU will use **Completion Fabric** as the domain concept for execution-result completion instead of continuing to treat the Common Data Bus as the architecture boundary. Existing CDB-shaped code may remain during migration, but the design direction is to reduce or remove it because a single global bus hides performance-visible distinctions between wakeup, physical-register writeback, and ROB completion.

**Status**: accepted

**Considered Options**

- Keep CDB as the primary concept and add more limits around it.
- Retire CDB as a domain concept and migrate toward a Completion Fabric with separate completion consumers.

**Consequences**

- New design docs and tests should describe completion behavior in terms of Completion Fabric rather than CDB.
- Migration can be incremental; existing CDB queues do not need to disappear in the first patch.
- The first implementation should focus on completion-event arbitration and fanout to existing wakeup, physical-register writeback, and ROB-completion consumers.
- More realistic bypass, wakeup, and select networks are deferred until the Completion Fabric boundary is established.
- The first implementation should model completion backpressure: a completed execution unit keeps holding its result when completion bandwidth is exhausted instead of placing every result into an unbounded queue.
- IPC improvement is not an acceptance criterion for the first implementation. A lower IPC is acceptable when it follows from modeling a previously missing structural constraint more realistically.
