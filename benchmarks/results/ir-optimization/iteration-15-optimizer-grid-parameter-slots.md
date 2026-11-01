# IR optimization iteration 15

Iteration: 15

Hypothesis: Once optimizer grid validation reuses a context, binding optimize dimensions to numeric parameter slots removes repeated name hashing and map lookup from every candidate row.

Changed: `src/model/optimize_run.c` allocates evaluator-local dimension slots, binds existing flat parameters once, and lazily binds nested/flattened parameters after their first compatible fallback write.

Benchmark command: Nine independent runs of `build/benchmarks/cxpr_bench_model --grid-setup`; each run prepares 100 grids of 1000 candidates with three dimensions and one constraint.

Environment: before `2a67b5d`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor unchanged; `perf` unavailable.

Profit now: optimizer grid median 157.54 ns/candidate, p95 159.20 ns, 6.348 M candidates/s.

Profit previous: optimizer grid median 430.90 ns/candidate, p95 446.62 ns, 2.321 M candidates/s.

Delta profit: -273.36 ns (-63.44%, 2.735x speedup); throughput +4.027 M candidates/s (+173.5%).

Other key metrics: Combined with iteration 14's context reuse, the original 1149.77 ns/candidate median is now 157.54 ns/candidate, an 86.30% reduction and 7.30x cumulative speedup.

Correctness: 138/138 Release tests pass (one documented skip). Focused ASan/UBSan passes for `optimize_assert` with leak detection disabled due to host ptrace policy. Nested-record optimize dimensions exercise the lazy fallback-and-bind path.

Assessment: keep. Slot validity is checked before every direct write, preserving correctness if a fallback insertion moves parameter-map storage.

Next hypothesis: Profile active-candidate replay session allocation; any pooling must reset state, history, outputs, children, fused slots, and pending values explicitly.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `optimizer_grid_parameter_slots` | 430.90 ns | 157.54 ns | -273.36 ns | -63.44% | 2.735x | keep |
