# IR optimization iteration 05

Iteration: 5

Hypothesis: Resolving a constant producer field to its schema index during compilation removes repeated string-based field lookup while retaining one cached producer invocation per unique constant argument set.

Changed: `src/ir/compile/node.c`, `src/ir/exec/calls.c`, `src/ir/internal.h`, `benchmarks/ir_bench.c`, and `benchmarks/fixtures/ir_producer_fields.cxpr`. The constant-producer payload now carries the validated field offset; the dynamic producer fallback is unchanged.

Benchmark command: Nine independent runs of `build/benchmarks/cxpr_bench_ir --case producer_fields` at `bc5b11f` and in the changed worktree.

Environment: before `bc5b11f`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor was not changed. Hardware counters are unavailable because `perf` is not installed.

Before: median 150.89 ns, p95 157.96 ns, 6.627 M eval/s. The producer was invoked once, but each field was resolved by name.

After: median 138.88 ns, p95 143.50 ns, 7.201 M eval/s. Three fields share exactly one producer invocation and use compile-time field offsets; six IR instructions and zero AST fallbacks.

Delta: median -12.01 ns (-7.96%, 1.086x), p95 -14.46 ns (-9.15%), throughput +0.574 M eval/s (+8.67%).

Correctness: 138/138 Release tests pass (one documented skip). Focused `eval_calls`, `ir_exec_calls`, `ir_compile`, and `ir_exec_typed` tests pass. Focused ASan/UBSan tests pass with leak detection disabled due to the host ptrace policy. AST and IR benchmark totals match.

Assessment: keep. The Phase 3 repeated-field acceptance case now explicitly checks one producer call, no AST fallback, and indexed access for known constant schemas.

Next hypothesis: Run classic post-validation optimizer passes to fold constants, eliminate dead branches, and deduplicate safe repeated loads.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `producer_fields` | 150.89 ns | 138.88 ns | -12.01 ns | -7.96% | 1.086x | keep |
