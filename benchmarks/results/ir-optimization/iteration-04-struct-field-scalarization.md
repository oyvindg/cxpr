# IR optimization iteration 04

Iteration: 4

Hypothesis: Scalarizing field selection through statically recognizable record arithmetic avoids temporary struct allocation, field-name lookup after each arithmetic operation, and generic typed dispatch.

Changed: `src/ir/compile/node.c`, `src/ir/compile/infer.c`, `src/ir/exec/fast.c`, `src/ir/validate.c`, and `tests/ir_compile.test.c`. Known identifier/record arithmetic fields lower to numeric field loads and scalar arithmetic; dynamic shapes retain the generic path.

Benchmark command: Nine independent runs of `build/benchmarks/cxpr_bench_ir --case <case>` in the Phase 2 baseline worktree and current worktree.

Environment: before `c0a9a3e2ba82b57b35cd54a5bc324a611179cb2f`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor was not changed. `perf` counters are unavailable because `perf` is not installed.

Before: `struct_scalar_mul_all` median 943.61 ns, p95 960.49 ns, 1.060 M eval/s; `struct_struct_mul_all` median 1510.62 ns, p95 1577.31 ns, 0.662 M eval/s.

After: `struct_scalar_mul_all` median 79.85 ns, p95 83.47 ns, 12.524 M eval/s; `struct_struct_mul_all` median 143.76 ns, p95 146.77 ns, 6.956 M eval/s. Both cases emit 12 instructions with zero AST fallback and use the number-fast path.

Delta: scalar/struct median -863.76 ns (-91.54%, 11.82x), p95 -877.02 ns (-91.31%), throughput +11.464 M eval/s (+1081.5%). Struct/struct median -1366.86 ns (-90.48%, 10.51x), p95 -1430.54 ns (-90.69%), throughput +6.294 M eval/s (+950.8%).

Correctness: 138/138 Release tests pass (one documented skip); focused `ir_compile`, `ir_exec_fast`, `struct_ops`, and `context_structs` tests pass. Focused ASan/UBSan tests pass with leak detection disabled because LSAN cannot run under the host ptrace policy. AST/IR/C benchmark totals match.

Assessment: keep. Both representative typed-struct cases exceed the Phase 3 order-of-magnitude gate without changing dynamic struct behavior.

Next hypothesis: Resolve producer schemas and cache equal calls per evaluation so repeated producer fields require one producer invocation and no result struct reconstruction.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `struct_scalar_mul_all` | 943.61 ns | 79.85 ns | -863.76 ns | -91.54% | 11.82x | keep |
| `struct_struct_mul_all` | 1510.62 ns | 143.76 ns | -1366.86 ns | -90.48% | 10.51x | keep |
