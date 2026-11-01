# IR optimization iteration 18

Iteration: 18

Hypothesis: History rings can share one contiguous session-owned value allocation, removing one allocation and release per ring without changing value ownership semantics.

Changed: `src/model/internal.h` adds the shared history-value owner; `src/model/session.c` sizes and allocates one contiguous block for all history rings, assigns each ring a slice, and keeps state-buffer storage independently owned.

Benchmark command: Nine independent runs of `build/benchmarks/cxpr_bench_model --history-session-setup`; each trial creates and destroys 20,000 sessions containing eight history rings with depths one through eight.

Environment: before `0c55ea8`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor unchanged; `perf` unavailable.

Profit now: history-session setup median 659.68 ns, p95 673.05 ns, 1.516 M sessions/s.

Profit previous: history-session setup median 820.96 ns, p95 832.67 ns, 1.218 M sessions/s.

Delta profit: -161.28 ns (-19.65%, 1.244x speedup); throughput +0.298 M sessions/s (+24.5%).

Other key metrics: Eight ring allocations and releases become one allocation and release per benchmark session. The p95 improves by 159.62 ns (-19.17%).

Correctness: 138/138 Release tests pass (one documented skip). The full model suite passes under ASan/UBSan with leak detection disabled due to host ptrace policy.

Assessment: keep. Each ring still destroys its contained typed values, while only the shared backing allocation is released once. State buffers retain their existing independent ownership.

Next hypothesis: Measure the remaining history-session setup cost before changing more ownership; context creation or history-entry metadata allocation may now dominate.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `history_session_setup` | 820.96 ns | 659.68 ns | -161.28 ns | -19.65% | 1.244x | keep |
