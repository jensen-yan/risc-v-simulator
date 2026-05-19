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

**Addr-Unknown Store**:
An older store whose effective address is not yet known when a younger load is considered for dispatch or execution.
_Avoid_: unresolved store, pending store address

**Bad Addr-Unknown Pair**:
A remembered load/store PC pair where allowing the load to pass the addr-unknown store caused an ordering violation.
_Avoid_: blacklist entry

## Relationships

- An **Out-of-Order Pipeline** executes each stage through a **Stage Context**.
- **Execute Memory Order** observes **Addr-Unknown Store** state when deciding whether a younger load may proceed.
- A **Bad Addr-Unknown Pair** causes **Execute Memory Order** to block later speculation for the same load/store PC pair.

## Example Dialogue

> **Dev:** "Should this load pass the older store with an unresolved address?"
> **Domain expert:** "That is an **Execute Memory Order** decision. If the PCs form a **Bad Addr-Unknown Pair**, block it; otherwise the load may speculate and recovery will handle violations."

## Flagged Ambiguities

- "recovery" can mean early branch/JALR redirect or memory-order violation recovery. Use **Execute Memory Order** only for the latter.
