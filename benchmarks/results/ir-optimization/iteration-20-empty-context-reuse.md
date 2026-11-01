# IR optimization iteration 20

Iteration: 20

Hypothesis: Reusing empty base contexts in the existing bounded thread-local context pool avoids repeatedly allocating and zeroing the large fixed lookup caches during optimizer session churn.

Changed: `src/context/lifecycle.c` lets both base and overlay contexts reuse the existing thread-local pool when empty; `tests/context_lifecycle.test.c` verifies that an overlay reused as a base context does not retain its parent; `benchmarks/model_bench.c` adds focused `--context-setup` measurement.

Benchmark command: Nine independent runs each of `build/benchmarks/cxpr_bench_model --context-setup` and `build/benchmarks/cxpr_bench_model --history-session-setup`; each trial creates and destroys 20,000 objects.

Environment: before `46de73e`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor unchanged; `perf` unavailable.

Profit now: history-session setup median 240.02 ns, p95 257.35 ns, 4.166 M sessions/s. Empty-context setup median 7.82 ns, p95 8.69 ns, 127.877 M contexts/s.

Profit previous: history-session setup median 635.33 ns, p95 663.06 ns, 1.574 M sessions/s. Empty-context setup median 332.95 ns, p95 339.58 ns, 3.004 M contexts/s.

Delta profit: history-session -395.31 ns (-62.22%, 2.647x speedup), throughput +2.592 M sessions/s (+164.7%). Empty-context setup -325.13 ns (-97.65%, 42.577x speedup), throughput +124.873 M contexts/s (+4157.7%).

Other key metrics: History-session p95 improves by 405.71 ns (-61.19%). Empty-context p95 improves by 330.89 ns (-97.44%). The pool remains thread-local and bounded to 64 contexts per thread.

Correctness: 138/138 Release tests pass (one documented skip). Context lifecycle and full model suites pass under ASan/UBSan with leak detection disabled due to host ptrace policy.

Assessment: keep. Non-empty base contexts retain their existing destruction path. Reused contexts reset parent, expression scope, versions, evaluation depth, and pool linkage before being returned.

Next hypothesis: Measure whether the remaining history-session cost is dominated by the session/output/history allocation group before coalescing more storage.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `context_setup` | 332.95 ns | 7.82 ns | -325.13 ns | -97.65% | 42.577x | keep |
| `history_session_setup` | 635.33 ns | 240.02 ns | -395.31 ns | -62.22% | 2.647x | keep |
