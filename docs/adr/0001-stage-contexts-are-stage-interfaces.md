# Stage contexts are stage interfaces

The out-of-order pipeline uses stage-specific context objects as the execution interface for each stage instead of a shared `execute(CPUState&)` surface. This keeps each stage's caller-facing interface narrower than the whole OOO CPU state, makes behavior tests possible at the stage seam, and leaves cross-cutting rules to deeper modules instead of expanding every stage's access to `CPUState`.

## Considered Options

- Keep a common `execute(CPUState&)` method for every stage. This was simple, but made each stage shallow and allowed accidental coupling to unrelated CPU state.
- Use stage-specific contexts. This adds small adapter code in `OutOfOrderCPU::step()`, but gives each stage a clear boundary.

## Consequences

`PipelineStage` remains only a stable observation surface. If a stage context starts carrying too many responsibilities, the preferred next move is to extract a deeper domain module rather than widening the context.
