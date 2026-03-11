# Branch Predictor Debug Log

## Goal
Fix or explain the regression where enabling the new tournament branch predictor lowers performance versus the simpler baseline in the OOO CPU.

## Roles
- Claude CLI: review, risk analysis, debugging plan, suspicious code paths
- Codex CLI: implement targeted code changes, run tests/benchmarks, report diffs/results
- Moss: orchestrate, keep this log updated, summarize progress to Jensen

## Current Findings
- Predictor was upgraded from simple BHT/BTB to Tournament (Local + GShare + Chooser) with speculative and committed histories.
- Data path is wired through fetch -> decode -> dynamic inst -> commit.
- Suspected regression causes include warmup cost, speculative history pollution, chooser initialization bias, and recovery/training ordering issues.
- Claude highlighted a likely high-value risk: coarse recovery of the entire speculative local-history table and possible inconsistency between committed local-history progression and training metadata.

## Next Steps
1. Claude produces a concrete debug plan with ranked hypotheses and exact instrumentation/tests.
2. Codex inspects current test/benchmark entry points and applies the smallest useful instrumentation or fix.
3. Run focused validation to compare branch-predictor-on vs baseline behavior.
4. Record outcome and next iteration here.

## Progress 2026-03-11 21:xx
- Collaboration file created and in use.
- Claude review completed once already; key concerns: speculative local-history recovery is too coarse, committed-history progression may diverge from training metadata, and warmup/chooser bias may hide gains.
- Codex verified usable validation entry points already exist:
  - `./build/tests/risc-v-tests --gtest_filter='BranchPredictorTest.*'`
  - `python3 run_tests.py -p 'rv32ui-p-beq' --ooo -w 1`
  - `./build/risc-v-sim --ooo --max-ooo-cycles=500000 -e -m 2164260864 ./riscv-tests/benchmarks/dhrystone.riscv`
- `dhrystone` runs successfully and is a practical primary reproduction target for predictor-related performance regressions.
- OOO control-flow gtests are only partially useful right now; one of them prints `unsupported syscall: 0`, so benchmark + ISA tests are currently better anchors than that suite alone.

## Immediate Plan
- Ask Claude for a ranked, concrete debug plan tied to files/counters/tests.
- Ask Codex to establish a baseline measurement path, then implement the smallest high-value fix or instrumentation.
- Compare behavior on `dhrystone` first, then confirm no obvious correctness regressions with focused tests.

## Progress 2026-03-11 21:20+
- Focused validation is runnable and currently green:
  - `./build/tests/risc-v-tests --gtest_filter='BranchPredictorTest.*'` -> PASS
  - `./build/tests/risc-v-tests --gtest_filter='OutOfOrderControlFlowPredictionTest.*'` -> PASS (but prints `unsupported syscall: 0`)
  - `python3 run_tests.py -p 'rv32ui-p-beq' --ooo -w 1` -> PASS
- Primary performance reproduction anchor confirmed:
  - `./build/risc-v-sim --ooo --max-ooo-cycles=500000 -e -m 2164260864 ./riscv-tests/benchmarks/dhrystone.riscv`
- Current `dhrystone` run reports approximately `267123` cycles, versus historical `259336` cycles in `dhry.out`.
  - Approx slowdown: `+7787 cycles` / `+3.00%`
  - Historical conditional branch mispredicts: `5450`
  - Current conditional branch mispredicts: `6666`
  - Approx increase: `+1216` / `+22.31%`
- This strongly suggests the regression is accuracy-related first, not a tiny bookkeeping-only slowdown.
- There is no CLI/runtime flag yet to switch predictor strategy. The OOO CPU still contains old compatibility stubs (`predict_branch()` always false, `update_branch_predictor()` no-op), but the active path is the fetch/commit-stage predictor integration.

## Working Hypothesis
- The tournament predictor is integrated, but current training/recovery behavior likely degrades accuracy on dhrystone enough to outweigh any speculative fetch benefit.
- First suspect remains speculative-history handling and/or chooser behavior, not gross functional breakage.

## Next Iteration
1. Get Claude's ranked debug plan finalized in the collaboration log.
2. Let Codex take the first low-risk step: add or use minimal instrumentation / a temporary comparison path to isolate whether chooser bias or speculative-history recovery is the bigger factor.
3. Re-run `dhrystone` and compare cycle + mispredict deltas before any larger code changes.


## Progress 2026-03-11 23:00+
- Focus shifted from tournament to gshare-first tuning.
- Findings so far:
  - `simple` remains strongest current baseline on dhrystone: `260819 cycles / 5450 mispredicts`.
  - Original `gshare` path: `263713 / 6235`.
  - Removing backward-branch short-history heuristic made gshare slightly worse (`263789 / 6245`), so that heuristic is retained.
  - Reducing global history length helped materially: with `kGhrBits=10` and `kShortGhrBits=6`, gshare improved to `262812 / 5977`.
  - Tournament tuning helped but still lags tuned gshare.
- Current objective: continue gshare parameter search for 1-2 hours and see whether it can approach or beat `simple` on dhrystone while keeping focused tests green.
