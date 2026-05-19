# RISC-V Simulator Context

This file records project-specific domain language used by architecture work.
It is a glossary, not a duplicate of `ARCHITECTURE.md`.

## Language

**Out-of-Order Pipeline**:
The CPU mode that models instruction flow through fetch, decode, issue, execute, writeback, and commit with speculative execution and in-order retirement.
_Avoid_: OOO blob, pipeline code

**Stage Context**:
A stage-specific adapter that exposes only the state and actions a pipeline stage needs for one execution step.
_Avoid_: passing raw `CPUState` as the stage interface

**Execute Memory Order**:
The execute-side rules that decide when loads may pass unresolved older stores and how the simulator recovers from a detected ordering violation.
_Avoid_: scattering addr-unknown speculation and recovery rules inside `ExecuteStage`

**Execute Control Recovery**:
The execute-side early recovery path for resolved branch/JALR mispredictions, including rename checkpoint restore, younger-work cleanup, RAS repair, and recovery counters.
_Avoid_: burying early control recovery inside generic execution-unit completion

**Execute DCache Access**:
The execute-side D$ timing handshake for load/store execution, including request start, blocked/outstanding handling, latency accounting, and cache counter updates.
_Avoid_: duplicating D$ request bookkeeping inside load/store execution branches

**Execute Load Value**:
The execute-side formatting rule that turns raw load bytes into the architectural value, including signed/unsigned integer extension and FLW NaN-boxing.
_Avoid_: duplicating load value extension switches across forwarding and memory paths

**Execute Host-Comm Access**:
The execute-side serialization rule for tohost/fromhost memory-mapped accesses, which must wait until the instruction is at the ROB head before touching host communication state.
_Avoid_: hiding host-comm MMIO ordering checks inside individual load/store completion branches

**Execute Memory Inflight**:
The execute-side queue for load/store D$ misses that have issued their cache request and no longer occupy a load/store execution unit or reservation-station entry.
_Avoid_: scattering inflight queue movement, wait-cycle advancement, and completion cleanup across `ExecuteStage`

**Execute Load Hazard**:
The execute-side replay decision for a load that sees an older AMO, address-unknown store, or overlapping store before it can read memory.
_Avoid_: spreading ROB store-hazard kind mapping, replay counters, and caused-by counters through load completion

**Execute Load Access**:
The execute-side load access path after hazard checks, including store-to-load forwarding, optional memory merge, D$ read timing, exception capture, and final value publication.
_Avoid_: keeping forwarding, memory read, cache wait, and result formatting interleaved in `ExecuteStage`

**Execute Load Completion**:
The execute-side orchestration that decides whether a ready load completes, replays, moves to memory inflight, waits on cache pressure, or publishes an exception.
_Avoid_: making `ExecuteStage` stitch together host-comm replay, ROB hazard replay, cache wait, and load result publication

**Execute Store Access**:
The execute-side store completion path, including host-comm serialization, D$ write timing, inflight movement, and memory-order violation recovery trigger.
_Avoid_: mixing store cache timing and recovery-trigger decisions directly into `ExecuteStage`

**OOO Recovery**:
The out-of-order pipeline rules that remove speculative work after a redirect, trap, fence, or other pipeline recovery reason.
_Avoid_: scattering flush cleanup rules across individual stages

**Commit Retire Effects**:
The bookkeeping that happens after an instruction is successfully retired, including store-buffer retirement, rename checkpoint cleanup, and load/store profile updates.
_Avoid_: mixing retired-work bookkeeping into the main commit loop

**Commit Memory Effects**:
The architectural memory and LR/SC reservation updates applied when a store, floating-point store, or AMO instruction retires.
_Avoid_: mixing store/AMO memory side effects into unrelated commit bookkeeping

**Commit Register Effects**:
The architectural register, floating-point flag, and rename-map updates applied when an instruction retires.
_Avoid_: spreading integer/floating-point writeback and rename commit rules through the commit loop

**Commit Control Flow Effects**:
The predictor/profile/counter updates and redirect decision applied when a branch, JAL, or JALR retires.
_Avoid_: mixing predictor training, profile accounting, and commit-loop flush orchestration in one block

**Commit System Effects**:
The CSR, trap, syscall, MRET, and FENCE.I effects applied when a serializing instruction retires.
_Avoid_: keeping privileged/system instruction semantics as ad hoc branches in the commit loop

**Addr-Unknown Store**:
An older store whose effective address is not yet known when a younger load is considered for dispatch or execution.
_Avoid_: unresolved store, pending store address

**Bad Addr-Unknown Pair**:
A remembered load/store PC pair where allowing the load to pass the addr-unknown store caused an ordering violation.
_Avoid_: blacklist entry

## Relationships

- An **Out-of-Order Pipeline** executes each stage through a **Stage Context**.
- **Execute Memory Order** observes **Addr-Unknown Store** state when deciding whether a younger load may proceed.
- **Execute Control Recovery** runs when an execution unit completes a control-flow instruction before commit has seen it.
- **Execute DCache Access** is the cache timing submodule used by execute-side load/store paths.
- **Execute Load Value** is shared by store-forwarded loads and memory-loaded values before writeback.
- **Execute Host-Comm Access** protects tohost/fromhost accesses from observing state before older instructions retire.
- **Execute Memory Inflight** owns already-issued load/store cache misses until they complete or request recovery.
- **Execute Load Hazard** decides whether a load replays or may continue before forwarding/memory access.
- **Execute Load Access** runs after **Execute Load Hazard** allows the load to proceed.
- **Execute Load Completion** orchestrates **Execute Load Hazard**, **Execute Load Access**, and cache/inflight follow-up for a ready load unit.
- **Execute Store Access** runs when a store execution unit reaches completion and may either finish, replay, move to inflight, or trigger recovery.
- **OOO Recovery** clears younger work or the full speculative pipeline after a stage has identified the recovery reason and restart point.
- **Commit Retire Effects** runs after the instruction's architectural state has been committed.
- **Commit Memory Effects** runs before generic retire bookkeeping so store/AMO state becomes architectural first.
- **Commit Register Effects** runs after memory effects and before generic retire bookkeeping so integer/floating-point register state is architectural before DiffTest/tracing.
- **Commit Control Flow Effects** runs after retire bookkeeping and reports only the redirect flush decision back to `CommitStage`.
- **Commit System Effects** reports stop/flush metadata back to `CommitStage`; it owns the privileged state mutation and serializing recovery.
- A **Bad Addr-Unknown Pair** causes **Execute Memory Order** to block later speculation for the same load/store PC pair.

## Example Dialogue

> **Dev:** "Should this load pass the older store with an unresolved address?"
> **Domain expert:** "That is an **Execute Memory Order** decision. If the PCs form a **Bad Addr-Unknown Pair**, block it; otherwise the load may speculate and **OOO Recovery** will clear the younger work if a violation is detected."

## Flagged Ambiguities

- Use **Execute Memory Order** for the decision and accounting around memory-order speculation. Use **OOO Recovery** for the shared pipeline cleanup rules.
