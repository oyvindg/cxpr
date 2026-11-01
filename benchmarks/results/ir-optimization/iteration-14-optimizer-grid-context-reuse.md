# IR optimization iteration 14

Iteration: 14

Hypothesis: Reusing one seeded validation context across optimizer grid rows removes per-candidate context allocation and default seeding while preserving independent assertion evaluation.

Changed: `src/model/optimize_run.c` seeds one context per grid and clears cached structs/evaluation memo between candidates; `benchmarks/model_bench.c` adds focused `--candidate-setup` and `--grid-setup` profiling modes.

Benchmark command: Nine independent runs of `build/benchmarks/cxpr_bench_model --grid-setup`; each run prepares 100 grids of 1000 candidates with three dimensions and one constraint.

Environment: before `50c7d73`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor unchanged; `perf` unavailable.

Profit now: optimizer grid median 430.90 ns/candidate, p95 446.62 ns, 2.321 M candidates/s.

Profit previous: optimizer grid median 1149.77 ns/candidate, p95 1169.77 ns, 0.870 M candidates/s.

Delta profit: -718.87 ns (-62.52%, 2.668x speedup); throughput +1.451 M candidates/s (+166.8%).

Other key metrics: Exploratory candidate-session profiling measured three one-shot parameter setters at approximately 252 ns of a 3.66 us setup path. Automatic model-session slots were rejected because candidate sessions use each value once; context allocation/default seeding was the larger measured cost.

Correctness: 138/138 Release tests pass (one documented skip). All optimize/assert tests pass, including nested dimensions, pruning, hard invariant errors, state objectives, and backend integration. Focused ASan/UBSan passes for `optimize_assert` with leak detection disabled due to host ptrace policy.

Assessment: keep. Every candidate overwrites all optimize dimensions; immutable defaults remain seeded, while producer caches and evaluation memo are cleared before the next row.

Next hypothesis: Profile active-candidate session allocation in `cxpr_model_optimize`; unlike grid validation, replay state requires strict reset semantics before sessions can be pooled safely.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `optimizer_grid_setup` | 1149.77 ns | 430.90 ns | -718.87 ns | -62.52% | 2.668x | keep |
