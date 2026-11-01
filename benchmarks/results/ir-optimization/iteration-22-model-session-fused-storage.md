# IR optimization iteration 22

Iteration: 22

Hypothesis: Fused slots, context-slot bindings, bound flags, and pending commit values can share one aligned session-owned block, reducing allocator overhead in optimizer candidate setup.

Changed: `src/model/internal.h` adds one fused-storage owner; `src/model/session.c` adds overflow-safe aligned layout construction, replaces up to nine fused-state allocations with one allocation, and releases the block once.

Benchmark command: Nine independent runs of `build/benchmarks/cxpr_bench_model --candidate-setup`; each trial creates and destroys 20,000 RSI strategy sessions both without and with three parameter assignments.

Environment: before `b9c8b1b`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor unchanged; `perf` unavailable.

Profit now: candidate base setup median 3000.34 ns, p95 3085.09 ns, 0.333 M sessions/s. Setup with three parameters median 3211.74 ns, p95 3245.55 ns, 0.311 M sessions/s.

Profit previous: candidate base setup median 3028.51 ns, p95 3144.38 ns, 0.330 M sessions/s. Setup with three parameters median 3295.03 ns, p95 3353.23 ns, 0.303 M sessions/s.

Delta profit: candidate base -28.17 ns (-0.93%, 1.009x speedup), throughput +0.003 M sessions/s (+0.9%). With three parameters -83.29 ns (-2.53%, 1.026x speedup), throughput +0.008 M sessions/s (+2.6%).

Other key metrics: Median parameter-only delta improves from 241.79 to 225.23 ns, -16.56 ns (-6.85%). Base p95 improves 59.29 ns (-1.89%); three-parameter p95 improves 107.68 ns (-3.21%).

Correctness: 138/138 Release tests pass (one documented skip). The full model suite passes under ASan/UBSan with leak detection disabled due to host ptrace policy.

Assessment: keep. All typed regions retain explicit alignment and checked size arithmetic. Models without fused IR keep their existing allocation path.

Next hypothesis: Consolidate the paired pending typed-value/index arrays, gated on candidate setup rather than the already-plateaued history-session microbenchmark.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `candidate_setup base` | 3028.51 ns | 3000.34 ns | -28.17 ns | -0.93% | 1.009x | keep |
| `candidate_setup with_3_params` | 3295.03 ns | 3211.74 ns | -83.29 ns | -2.53% | 1.026x | keep |
