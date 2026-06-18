# Use ExecPlans for long-running investigations

Complex simulator work uses local ExecPlans under `docs/exec-plans/{active,completed,blocked}/` to preserve goal, evidence, hypotheses, next actions, and verification criteria across sessions. This repo has long-running debug, performance, and OOO architecture work where chat history alone is too fragile, while full PRD or issue workflows are often heavier than the local work requires.

## Considered Options

- Keep all investigation state in chat. This is fast, but loses precise evidence and handoff context.
- Put every investigation into issues or PRDs. This is shareable, but too much ceremony for local experiments and short-lived hypotheses.
- Use ExecPlans as local living documents. This keeps the evidence trail close to the code and can be promoted later when sharing is needed.

## Consequences

ExecPlans are expected for multi-step analysis, performance work, cross-module refactors, and investigations with unresolved hypotheses. Small fixes and simple documentation edits should not grow an ExecPlan just to follow the pattern.
