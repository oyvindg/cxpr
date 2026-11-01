# IR optimization iteration 33

Iteration: 33

Hypothesis: Engine lookback sessions can resolve each stable identifier AST target to its session-owned source once, avoiding repeated AST classification and linear source-name lookup on every bar.

Changed: `src/engine/internal.h` adds an eight-entry direct-mapped, session-owned target-to-source cache. `src/engine/lookback.c` uses the cached source for repeated identifier lookbacks and populates it after the normal first resolution.

Benchmark command: Nine independent confirmation runs of `build/benchmarks/cxpr_bench_engine --lookback`; each trial evaluates 12,288,000 bars per variant and verifies signal parity.

Environment: before `a1acb6b`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor unchanged; `perf` unavailable.

Profit now: watched lookback engine median 440.51 ns/bar, p95 449.41 ns, 2.270 M bars/s. No-watch lookback engine median 427.36 ns/bar, p95 456.68 ns, 2.340 M bars/s.

Profit previous: watched lookback engine median 452.92 ns/bar, p95 459.88 ns, 2.208 M bars/s. No-watch lookback engine median 447.17 ns/bar, p95 450.40 ns, 2.236 M bars/s.

Delta profit: watched -12.41 ns/bar (-2.74%, 1.028x speedup), throughput +0.062 M bars/s (+2.8%). No-watch -19.81 ns/bar (-4.43%, 1.046x), throughput +0.104 M bars/s (+4.6%).

Other key metrics: Watched p95 improves by 10.47 ns (-2.28%). No-watch p95 has one system outlier and regresses by 6.28 ns (+1.39%), while eight of nine no-watch trials are at or below 435.21 ns/bar. Every trial reports parity OK.

Correctness: 139/139 Release tests pass (one documented skip). Engine-layer, context-value, and evaluator tests pass under ASan/UBSan with leak detection disabled due to host ptrace policy.

Assessment: keep. The representative watched path improves in both median and p95. Cache entries are session-owned, source storage is stable for the session lifetime, and collisions safely fall back to normal resolution.

Next hypothesis: Add a resolved engine-source lookback callback that accepts the already-constant offset directly, avoiding per-evaluation synthetic index AST validation while retaining the generic resolver fallback.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `lookback.engine_watch` | 452.92 ns | 440.51 ns | -12.41 ns | -2.74% | 1.028x | keep |
| `lookback.engine_nowatch` | 447.17 ns | 427.36 ns | -19.81 ns | -4.43% | 1.046x | keep |
