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

**OOO Recovery**:
The out-of-order pipeline rules that remove speculative work after a redirect, trap, fence, or other pipeline recovery reason.
_Avoid_: scattering flush cleanup rules across individual stages

**Commit Retire Effects**:
The bookkeeping that happens after an instruction is successfully retired, including store-buffer retirement, rename checkpoint cleanup, and load/store profile updates.
_Avoid_: mixing retired-work bookkeeping into the main commit loop

**Addr-Unknown Store**:
An older store whose effective address is not yet known when a younger load is considered for dispatch or execution.
_Avoid_: unresolved store, pending store address

**Bad Addr-Unknown Pair**:
A remembered load/store PC pair where allowing the load to pass the addr-unknown store caused an ordering violation.
_Avoid_: blacklist entry

## Relationships

- An **Out-of-Order Pipeline** executes each stage through a **Stage Context**.
- **Execute Memory Order** observes **Addr-Unknown Store** state when deciding whether a younger load may proceed.
- **OOO Recovery** clears younger work or the full speculative pipeline after a stage has identified the recovery reason and restart point.
- **Commit Retire Effects** runs after the instruction's architectural state has been committed.
- A **Bad Addr-Unknown Pair** causes **Execute Memory Order** to block later speculation for the same load/store PC pair.

## Example Dialogue

> **Dev:** "Should this load pass the older store with an unresolved address?"
> **Domain expert:** "That is an **Execute Memory Order** decision. If the PCs form a **Bad Addr-Unknown Pair**, block it; otherwise the load may speculate and **OOO Recovery** will clear the younger work if a violation is detected."

## Flagged Ambiguities

- Use **Execute Memory Order** for the decision and accounting around memory-order speculation. Use **OOO Recovery** for the shared pipeline cleanup rules.
