# IR optimization iteration 32

Iteration: 32

Hypothesis: Zero-argument column lookbacks should read immutable column storage directly instead of paying per-tick source-memo lookup and insertion costs.

Changed: `src/engine/source.c` bypasses source memoization for zero-argument column reads while preserving memo behavior for pull/view sources and calls with arguments. `benchmarks/engine_bench.c` adds `--lookback` to isolate the complete representative lookback scenarios.

Benchmark command: Nine independent runs of `build/benchmarks/cxpr_bench_engine --lookback`; each trial evaluates 12,288,000 bars per variant and verifies signal parity.

Environment: before `f968080`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor unchanged; `perf` unavailable.

Profit now: watched lookback engine median 452.92 ns/bar, p95 459.88 ns, 2.208 M bars/s. No-watch lookback engine median 447.17 ns/bar, p95 450.40 ns, 2.236 M bars/s.

Profit previous: watched lookback engine median 514.16 ns/bar, p95 520.02 ns, 1.945 M bars/s. No-watch lookback engine median 515.64 ns/bar, p95 517.43 ns, 1.939 M bars/s.

Delta profit: watched -61.24 ns/bar (-11.91%, 1.135x speedup), throughput +0.263 M bars/s (+13.5%). No-watch -68.47 ns/bar (-13.28%, 1.153x), throughput +0.297 M bars/s (+15.3%).

Other key metrics: Watched p95 improves by 60.14 ns (-11.56%); no-watch p95 improves by 67.03 ns (-12.95%). All 18 before/after variants reported parity OK. The low-level evaluator remained in the same 278-289 ns/bar range, isolating the gain to engine source resolution.

Correctness: 139/139 Release tests pass (one documented skip). Engine-layer, context-value, and evaluator tests pass under ASan/UBSan with leak detection disabled due to host ptrace policy.

Assessment: keep. A zero-argument column lookup is a deterministic bounds-checked array read; memoization costs more than repeating it and provides no observable deduplication benefit.

Next hypothesis: Resolve engine-owned column lookback targets to a session source index once, avoiding repeated AST classification and linear source-name lookup on every lookback.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `lookback.engine_watch` | 514.16 ns | 452.92 ns | -61.24 ns | -11.91% | 1.135x | keep |
| `lookback.engine_nowatch` | 515.64 ns | 447.17 ns | -68.47 ns | -13.28% | 1.153x | keep |
