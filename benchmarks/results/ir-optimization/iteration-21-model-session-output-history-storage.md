# IR optimization iteration 21

Iteration: 21

Hypothesis: Output state and history metadata/value storage can share one aligned session-owned block, removing one allocation and release from history-bearing session setup.

Changed: `src/model/internal.h` adds one owner for combined output/history storage; `src/model/session.c` computes an overflow-safe aligned layout, initializes output and history views into it, and releases the block once.

Benchmark command: Nine independent runs of `build/benchmarks/cxpr_bench_model --history-session-setup`; each trial creates and destroys 20,000 sessions containing one output and eight history rings with depths one through eight.

Environment: before `d6e5420`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor unchanged; `perf` unavailable.

Profit now: history-session setup median 203.84 ns, p95 240.47 ns, 4.906 M sessions/s.

Profit previous: history-session setup median 240.02 ns, p95 257.35 ns, 4.166 M sessions/s.

Delta profit: -36.18 ns (-15.07%, 1.177x speedup); throughput +0.740 M sessions/s (+17.8%).

Other key metrics: Output/history storage now requires one allocation and release rather than two. The p95 improves by 16.88 ns (-6.56%).

Correctness: 138/138 Release tests pass (one documented skip). The full model suite passes under ASan/UBSan with leak detection disabled due to host ptrace policy.

Assessment: keep. Output state, history entries, and typed history values retain explicit alignment and overflow checks; contained typed values are destroyed before the shared block is released.

Next hypothesis: The standalone session object allocation is now a material fraction of setup; measure whether folding the fixed session header into the same allocation gives a stable gain without complicating failure cleanup.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `history_session_setup` | 240.02 ns | 203.84 ns | -36.18 ns | -15.07% | 1.177x | keep |
