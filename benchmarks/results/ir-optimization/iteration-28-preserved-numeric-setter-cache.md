# IR optimization iteration 28

Iteration: 28

Hypothesis: Individual numeric setters should preserve lookup caches when `KEEP_NUMBER` cleanup removes only alternate typed bindings and leaves numeric hashmap storage unchanged.

Changed: `src/context/values.c` now clears variable or parameter numeric caches only when cleanup can remove a numeric binding. Numeric and prehashed numeric setters therefore retain valid pointer-cache entries across steady-state updates.

Benchmark command: Nine independent runs each of `build/benchmarks/cxpr_bench_ir --context-update-paths` and `build/benchmarks/cxpr_bench_ir --param-update-paths`; every measured trial performs 500,000 update groups.

Environment: before `8e9d404`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor unchanged; `perf` unavailable.

Profit now: context base setter median 353.94 ns, mutating setter 122.81 ns, and mutating prehashed setter 110.05 ns. Parameter base setter median 143.15 ns, mutating setter 138.87 ns, and mutating prehashed setter 122.58 ns.

Profit previous: context base setter median 1774.38 ns, mutating setter 718.62 ns, and mutating prehashed setter 707.14 ns. Parameter base setter median 738.04 ns, mutating setter 734.13 ns, and mutating prehashed setter 715.71 ns.

Delta profit: context base -1420.44 ns (-80.05%, 5.013x), mutating -595.81 ns (-82.91%, 5.851x), and prehashed -597.09 ns (-84.44%, 6.426x). Parameter base -594.89 ns (-80.60%, 5.156x), mutating -595.26 ns (-81.08%, 5.286x), and prehashed -593.13 ns (-82.87%, 5.839x).

Other key metrics: Throughput improves between 401.3% and 542.6% across the six measured paths. P95 latency improves between 79.89% and 84.91%. Alternate typed values are still removed before every numeric update; only caches over the unchanged numeric hashmap are retained.

Correctness: 138/138 Release tests pass (one documented skip). Focused context-value and evaluator tests pass under ASan/UBSan with leak detection disabled due to host ptrace policy.

Assessment: keep. Cache entries already validate their hashmap storage base, and all operations that remove numeric entries continue to clear both caches.

Next hypothesis: Make the ordinary numeric setters hash lazily after a pointer-cache miss, targeting the remaining gap between ordinary and prehashed update paths.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| context `base set` | 1774.38 ns | 353.94 ns | -1420.44 ns | -80.05% | 5.013x | keep |
| context `mutate set` | 718.62 ns | 122.81 ns | -595.81 ns | -82.91% | 5.851x | keep |
| context `mutate prehashed` | 707.14 ns | 110.05 ns | -597.09 ns | -84.44% | 6.426x | keep |
| parameter `base set` | 738.04 ns | 143.15 ns | -594.89 ns | -80.60% | 5.156x | keep |
| parameter `mutate set` | 734.13 ns | 138.87 ns | -595.26 ns | -81.08% | 5.286x | keep |
| parameter `mutate prehashed` | 715.71 ns | 122.58 ns | -593.13 ns | -82.87% | 5.839x | keep |
