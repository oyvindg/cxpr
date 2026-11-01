# IR optimization iteration 08

Iteration: 8

Hypothesis: Enabling the existing guarded direct input slots for standalone compiled evaluation removes repeated hashmap lookup while retaining version/base-pointer rebinding on structural context changes.

Changed: `src/ir/exec/api.c` enables evaluator-owned direct slots; `tests/ir_exec_api.test.c` verifies value/parameter updates and map growth/reallocation.

Benchmark command: Nine independent runs of `build/benchmarks/cxpr_bench_ir --case <case>` at before/after commits.

Environment: before `63e59b7`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor was not changed. Hardware counters are unavailable because `perf` is not installed.

Before: `large_arith` median 418.66 ns, p95 433.74 ns; `large_branch` median 156.62 ns, p95 161.09 ns; `mixed_expr` median 44.68 ns, p95 45.44 ns.

After: `large_arith` median 210.60 ns, p95 215.09 ns; `large_branch` median 136.58 ns, p95 145.11 ns; `mixed_expr` median 36.59 ns, p95 37.08 ns. Instruction and fallback counts are unchanged.

Delta: `large_arith` -208.06 ns (-49.70%, 1.988x); `large_branch` -20.04 ns (-12.80%, 1.147x); `mixed_expr` -8.09 ns (-18.11%, 1.221x). Median throughput improves by 98.8%, 14.7%, and 22.1%, respectively.

Correctness: 138/138 Release tests pass (one documented skip). Context lifecycle/slot/value tests pass. A new regression grows the context map, changes variable and parameter values, then verifies rebinding. Focused ASan/UBSan passes with leak detection disabled due to host ptrace policy. AST/IR/C totals match.

Assessment: keep. The same guarded input-slot design now benefits both engine sessions and standalone compiled evaluators without storing context pointers in shared programs.

Next hypothesis: Reassess remaining post-Phase-5 profiles before authorizing any profile-gated register IR or superinstruction work.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `large_arith` | 418.66 ns | 210.60 ns | -208.06 ns | -49.70% | 1.988x | keep |
| `large_branch` | 156.62 ns | 136.58 ns | -20.04 ns | -12.80% | 1.147x | keep |
| `mixed_expr` | 44.68 ns | 36.59 ns | -8.09 ns | -18.11% | 1.221x | keep |
