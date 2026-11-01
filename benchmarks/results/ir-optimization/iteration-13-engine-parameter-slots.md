# IR optimization iteration 13

Iteration: 13

Hypothesis: Engine sessions can bind their declared numeric parameters once and make per-tick `cxpr_engine_set_param` calls avoid repeated hashing and context-map lookup.

Changed: `src/engine/internal.h` stores session-owned parameter slots; `src/engine/session.c` binds, validates, rebinds, and frees them; `tests/engine_layer.test.c` verifies updates and rebinding after parameter-map growth; `benchmarks/engine_bench.c` adds the focused `--param-update` consumer benchmark.

Benchmark command: Nine independent runs of `build/benchmarks/cxpr_bench_engine --param-update`, five million updates per path per trial. The baseline column invokes the exact context setter used by the previous engine implementation.

Environment: before `1ae1988`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor unchanged; `perf` unavailable.

Profit now: engine parameter-slot median 5.68 ns/update, p95 12.00 ns, 176.056 M updates/s.

Profit previous: direct context setter median 84.18 ns/update, p95 90.58 ns, 11.879 M updates/s.

Delta profit: -78.50 ns (-93.25%, 14.82x speedup); throughput +164.177 M updates/s (+1382%).

Other key metrics: The complete engine benchmark retained parity for both basic and lookback scenarios. One representative run measured 445.26 ns/bar for the watched basic engine and 532.37 ns/bar for the watched lookback engine.

Correctness: 138/138 Release tests pass (one documented skip). Focused ASan/UBSan passes for `engine_layer` with leak detection disabled due to host ptrace policy. Tests force parameter-map growth and verify that the declared session parameter rebinds correctly.

Assessment: keep. Declared numeric parameters use the fast path; undeclared or nonnumeric values retain the existing context setter fallback.

Next hypothesis: Profile model optimizer candidate setup separately; its sessions are short-lived, so slot binding may cost more than it saves and should not be assumed beneficial.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `engine_param_update` | 84.18 ns | 5.68 ns | -78.50 ns | -93.25% | 14.82x | keep |
