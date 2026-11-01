# IR optimization iteration 16

Iteration: 16

Hypothesis: Model sessions can borrow immutable output names from their compiled program, removing per-candidate string allocation and destruction without requiring session pooling or reset semantics.

Changed: `src/model/internal.h` marks session output names as borrowed const data; `src/model/session.c` assigns program-owned names directly and no longer duplicates/frees them.

Benchmark command: Nine independent runs of `build/benchmarks/cxpr_bench_model --candidate-setup`; each trial creates and destroys 20,000 RSI sessions and applies three optimizer parameters.

Environment: before `3ecd0c7`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor unchanged; `perf` unavailable.

Profit now: candidate setup median 3345.44 ns, p95 3375.88 ns, 0.299 M setups/s.

Profit previous: candidate setup median 3521.03 ns, p95 3567.32 ns, 0.284 M setups/s.

Delta profit: -175.59 ns (-4.99%, 1.052x speedup); throughput +0.015 M setups/s (+5.25%).

Other key metrics: The RSI fixture has five outputs, eliminating five `strdup` calls and five matching frees per candidate session. Parameter application remains unchanged.

Correctness: 138/138 Release tests pass (one documented skip). The full model suite passes under ASan/UBSan with leak detection disabled due to host ptrace policy.

Assessment: keep. Sessions already retain a compiled-program pointer and cannot outlive that program, so borrowing output names follows existing lifetime ownership.

Next hypothesis: Audit history names for the same ownership pattern, but keep the change only if history-heavy session setup shows a representative improvement.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `optimizer_candidate_setup` | 3521.03 ns | 3345.44 ns | -175.59 ns | -4.99% | 1.052x | keep |
