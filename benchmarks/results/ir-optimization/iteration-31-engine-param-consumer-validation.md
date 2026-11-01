# IR optimization iteration 31

Iteration: 31

Hypothesis: The numeric context-setter optimizations from iterations 24-30 should materially improve the public parameter-update path used as the engine fallback, not only isolated multi-value microbenchmarks.

Changed: No runtime code. Added a nine-trial representative consumer validation using the existing engine parameter-update benchmark.

Benchmark command: Nine independent runs of `build/benchmarks/cxpr_bench_engine --param-update`; each trial performs five million direct context updates and five million engine-slot updates.

Environment: `59fe2bf`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor unchanged; `perf` unavailable. Historical comparison uses the same benchmark and workload recorded before iteration 13.

Profit now: direct public context setter median 9.17 ns/update, p95 9.87 ns, 109.051 M updates/s. Engine parameter slot median 5.89 ns/update, p95 6.30 ns, 169.779 M updates/s.

Profit previous: direct public context setter median 84.18 ns/update, p95 90.58 ns, 11.879 M updates/s in the iteration-13 baseline.

Delta profit: direct context setter -75.01 ns (-89.11%, 9.180x speedup), throughput +97.172 M updates/s (+818.0%).

Other key metrics: The optimized public setter is now only 1.56x slower than the session-bound engine slot at the respective medians, down from 14.82x in the original comparison. Engine-slot median remains within normal run variance of the earlier 5.68 ns measurement, while its p95 improves from 12.00 to 6.30 ns.

Correctness: 138/138 Release tests pass (one documented skip). Focused context-value and evaluator tests pass under ASan/UBSan with leak detection disabled due to host ptrace policy. The benchmark completed successfully in all nine trials.

Assessment: keep the preceding setter optimizations. The gain transfers to the named engine consumer and substantially accelerates the fallback path for undeclared or dynamically addressed parameters.

Next hypothesis: Run the complete basic and lookback engine benchmark to determine whether context update cost is still material at whole-tick scope before making further setter changes.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `context_set_param` | 84.18 ns | 9.17 ns | -75.01 ns | -89.11% | 9.180x | keep |
