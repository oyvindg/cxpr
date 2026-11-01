# IR optimization iteration 09

Iteration: 9

Hypothesis: Resolving statically named context struct fields into evaluator-owned guarded pointers removes repeated struct and field-name searches from scalarized field arithmetic.

Changed: `src/ir/bindings.c`, `src/ir/internal.h`, `src/ir/exec/fast.c`, `src/ir/exec/typed.c`, and `src/context/structs.c`; `tests/ir_exec_api.test.c` verifies rebinding after struct replacement.

Benchmark command: Nine independent runs of `build/benchmarks/cxpr_bench_ir --case <case>` at before/after commits.

Environment: before `3d80cbd`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor unchanged; `perf` unavailable.

Before: scalar/struct median 83.41 ns, p95 84.48 ns; struct/struct median 147.25 ns, p95 154.64 ns.

After: scalar/struct median 32.47 ns, p95 72.04 ns (one isolated outlier; eight trials were 32.00–34.15 ns); struct/struct median 41.79 ns, p95 45.72 ns.

Delta: scalar/struct median -50.94 ns (-61.07%, 2.569x), throughput +157%; struct/struct median -105.46 ns (-71.62%, 3.524x), throughput +252%.

Correctness: 138/138 Release tests pass (one documented skip). Replacement, context lifecycle, typed/fast execution, and struct-operation tests pass. Focused ASan/UBSan passes with leak detection disabled due to host ptrace policy. AST/IR/C totals match.

Assessment: keep. Direct field pointers live only in evaluator-owned binding state and are refreshed when the context version or storage base changes; shared compiled programs remain context-independent.

Next hypothesis: Stop core optimization at the Phase 5 boundary and reassess remaining dispatch cost before profile-gated register IR/superinstructions.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `struct_scalar_mul_all` | 83.41 ns | 32.47 ns | -50.94 ns | -61.07% | 2.569x | keep |
| `struct_struct_mul_all` | 147.25 ns | 41.79 ns | -105.46 ns | -71.62% | 3.524x | keep |
