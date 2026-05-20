# Domain Docs

This repo uses a single-context documentation layout for agent-facing domain understanding.

## Read Order For Engineering Skills

Before non-trivial exploration or implementation, read the relevant files in this order:

1. `README.md` for build, run, and test commands.
2. `ARCHITECTURE.md` for module boundaries, directory responsibilities, and main execution paths.
3. `PLANS.md` for ExecPlan conventions.
4. `tasks/` for current task background, refactor plans, and phase records.
5. `CONTEXT.md` for the project glossary and canonical domain language.
6. `docs/exec-plans/active/` for active long-running work.

## Domain Language

Use `CONTEXT.md` vocabulary when naming concepts in plans, hypotheses, tests, issue titles, refactor proposals, and commit messages.

Do not duplicate large architecture descriptions into `AGENTS.md`, `README.md`, or `tasks/`. Keep architecture content centralized in `ARCHITECTURE.md`.

## Architecture Decisions

This repo does not currently have a committed `docs/adr/` convention.

If ADRs are added later, read the relevant ADRs before proposing or changing architecture in the affected area.
