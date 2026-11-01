# IR optimization iteration 12

Iteration: 12

Hypothesis: Reusing the existing guarded numeric slot mechanism for parameters removes eight hashes and map lookups from each hot-loop parameter update batch.

Changed: `include/cxpr/context.h` exposes additive parameter-slot bind/validation APIs; `src/context/internal.h` and `src/context/slot.c` bind numeric parameter storage; `tests/context_slot.test.c` covers access, misses, map growth, and rebinding; `benchmarks/ir_bench.c` adds `mutate_param_slot`.

Benchmark command: Nine independent full runs of `build/benchmarks/cxpr_bench_ir`, comparing `mutate_param_slot`'s normal setter and slot columns. One operation updates eight numeric parameters.

Environment: before `c910262`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor unchanged; `perf` unavailable.

Profit now: parameter-slot median 9.15 ns per eight-parameter batch, p95 10.69 ns, 109.290 M batches/s.

Profit previous: normal parameter setters median 771.15 ns per batch, p95 785.12 ns, 1.297 M batches/s.

Delta profit: -762.00 ns (-98.81%, 84.28x speedup); throughput +107.993 M batches/s (+8328%).

Other key metrics: The full IR benchmark suite retained semantic parity. Slots are invalidated when parameter-map storage moves; callers can validate and rebind without hashing in the steady-state loop.

Correctness: 138/138 Release tests pass (one documented skip). Focused ASan/UBSan passes for `context_slot` with leak detection disabled due to host ptrace policy.

Assessment: keep. The implementation is additive, shares the established variable-slot storage contract, and does not change existing setter behavior.

Next hypothesis: Measure a real model/session consumer that mutates parameters per tick before wiring parameter slots into a higher-level runtime automatically.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `mutate_param_slot` (8 params) | 771.15 ns | 9.15 ns | -762.00 ns | -98.81% | 84.28x | keep |
