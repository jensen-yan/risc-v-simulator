# Issue Tracker

This repo is primarily developed through local documentation, with GitHub Issues available as an optional shared tracker.

## Default Workflow

Do not create or modify GitHub issues by default.

For day-to-day local work:

- Use `tasks/` for task background, refactor notes, and phase records.
- Use `PLANS.md` plus `docs/exec-plans/{active,completed,blocked}/` for complex, multi-step work.
- Use the current conversation and local working tree as the source of truth for small direct changes.

## GitHub Issues

The repo remote is `https://github.com/jensen-yan/risc-v-simulator.git`.

Use GitHub Issues only when the user explicitly asks to publish, create, fetch, triage, or update an issue.

When explicitly requested:

- Create an issue: `gh issue create --title "..." --body "..."`
- Read an issue: `gh issue view <number> --comments`
- List issues: `gh issue list --state open`
- Comment on an issue: `gh issue comment <number> --body "..."`
- Apply or remove labels: `gh issue edit <number> --add-label "..."` / `--remove-label "..."`
- Close: `gh issue close <number> --comment "..."`
