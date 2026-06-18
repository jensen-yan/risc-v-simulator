# Domain Docs

This repo uses a single-context documentation layout for agent-facing domain understanding.

## Read Order For Engineering Skills

Before non-trivial exploration or implementation, read the relevant files in this order:

1. `README.md` for build, run, and test commands.
2. `ARCHITECTURE.md` for module boundaries, directory responsibilities, and main execution paths.
3. `docs/adr/` for accepted architecture and process decisions that constrain future changes.
4. `PLANS.md` for ExecPlan conventions.
5. `tasks/` for current task background, refactor plans, and phase records.
6. `CONTEXT.md` for the project glossary and canonical domain language.
7. `docs/exec-plans/active/` for active long-running work.

## Domain Language

Use `CONTEXT.md` vocabulary when naming concepts in plans, hypotheses, tests, issue titles, refactor proposals, and commit messages.

Do not duplicate large architecture descriptions into `AGENTS.md`, `README.md`, or `tasks/`. Keep architecture content centralized in `ARCHITECTURE.md`.

## Architecture Decisions

This repo uses lightweight ADRs in `docs/adr/` for decisions that are hard to reverse, surprising without context, and based on a real trade-off.

Read the relevant ADRs before proposing or changing architecture in the affected area. Do not create ADRs for small reversible implementation choices; keep those in code review notes, task docs, or ExecPlans.
