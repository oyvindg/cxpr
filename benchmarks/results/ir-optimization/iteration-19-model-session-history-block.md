# IR optimization iteration 19

Iteration: 19

Hypothesis: History ring metadata and values can share one aligned session-owned allocation, removing the remaining separate history allocation and release.

Changed: `src/model/internal.h` removes the separate history-value owner; `src/model/session.c` calculates an overflow-safe aligned layout for history entries and values, allocates it as one block, and releases it once.

Benchmark command: Nine independent confirmation runs of `build/benchmarks/cxpr_bench_model --history-session-setup`; each trial creates and destroys 20,000 sessions containing eight history rings with depths one through eight.

Environment: before `490e4c0`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor unchanged; `perf` unavailable.

Profit now: history-session setup median 635.33 ns, p95 663.06 ns, 1.574 M sessions/s.

Profit previous: history-session setup median 659.68 ns, p95 673.05 ns, 1.516 M sessions/s.

Delta profit: -24.35 ns (-3.69%, 1.038x speedup); throughput +0.058 M sessions/s (+3.8%).

Other key metrics: History metadata and values now require one allocation and release rather than two. The p95 improves by 9.99 ns (-1.48%).

Correctness: 138/138 Release tests pass (one documented skip). The full model suite passes under ASan/UBSan with leak detection disabled due to host ptrace policy.

Assessment: keep. The value region is explicitly aligned for `cxpr_value`, all size calculations reject overflow, and each contained typed value is still destroyed before the shared block is released.

Next hypothesis: Isolate context construction from the remaining session setup cost before attempting broader session allocation changes.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `history_session_setup` | 659.68 ns | 635.33 ns | -24.35 ns | -3.69% | 1.038x | keep |
