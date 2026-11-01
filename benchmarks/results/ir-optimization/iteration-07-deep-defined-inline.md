# IR optimization iteration 07

Iteration: 7

Hypothesis: Raising the bounded scalar defined-function inline depth from 8 to 16 removes the remaining recursive `CALL_DEFINED` boundary in realistic nested helper chains.

Changed: `src/limits.h`, `benchmarks/ir_bench.c`, and `tests/ir_compile.test.c`. The scalar-only inline limit is 16; struct-returning and dynamic handler calls retain their existing call paths.

Benchmark command: Nine independent runs of `build/benchmarks/cxpr_bench_ir --case defined_call` in the exact before worktree and current worktree.

Environment: before `afe8a62`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor was not changed. Hardware counters are unavailable because `perf` is not installed.

Before: median 1147.75 ns, p95 1186.46 ns, 0.871 M eval/s; one residual `CALL_DEFINED` boundary; 11 instructions and zero AST fallbacks.

After: median 72.07 ns, p95 73.53 ns, 13.875 M eval/s; no `CALL_DEFINED`; 11 fully scalar instructions and zero AST fallbacks. Representative compile time remained approximately 50–52 us.

Delta: median -1075.68 ns (-93.72%, 15.93x), p95 -1112.93 ns (-93.80%), throughput +13.004 M eval/s (+1493%).

Correctness: 138/138 Release tests pass (one documented skip). The new regression test verifies absence of `CALL_DEFINED` and successful numeric execution. Focused ASan/UBSan passes with leak detection disabled due to the host ptrace policy. AST/IR benchmark totals match.

Assessment: keep. This removes the measured nested-call bottleneck with bounded compile growth and no expansion of dynamic or struct semantics.

Next hypothesis: Add explicit optimizer diagnostics and target branch/control-flow simplification without introducing profile-gated superinstructions.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `defined_call` | 1147.75 ns | 72.07 ns | -1075.68 ns | -93.72% | 15.93x | keep |
