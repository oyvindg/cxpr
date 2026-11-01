# IR optimization iteration 27

Iteration: 27

Hypothesis: Numeric batch updates can preserve their numeric-map caches because removing conflicting typed bindings does not mutate numeric hashmap storage; a pointer-cache hit can also avoid hashing the name.

Changed: `src/context/values.c` adds a lazy-hash cached numeric setter for batch updates and stops clearing numeric caches that remain valid across alternate-type cleanup.

Benchmark command: Nine independent runs each of `build/benchmarks/cxpr_bench_ir --context-update-paths` and `build/benchmarks/cxpr_bench_ir --param-update-paths`; every measured trial performs 500,000 batch updates. The parameter confirmation series was repeated after a system-wide outlier in the first series.

Environment: before `34a8b7f`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor unchanged; `perf` unavailable.

Profit now: context base median 115.16 ns and mutating median 38.58 ns. Parameter base median 89.47 ns and mutating median 88.35 ns.

Profit previous: context base median 242.25 ns and mutating median 168.49 ns. Parameter base median 154.34 ns and mutating median 157.53 ns.

Delta profit: context base -127.09 ns (-52.46%, 2.104x speedup) and mutating -129.91 ns (-77.10%, 4.367x). Parameter base -64.87 ns (-42.03%, 1.725x) and mutating -69.18 ns (-43.92%, 1.783x).

Other key metrics: Throughput improves by 110.4%, 336.7%, 72.5%, and 78.3% respectively. Context base/mutating p95 improve by 51.78%/77.15%; parameter base/mutating p95 improve by 12.39%/41.86%. Pointer-cache entries retain map-base validation, so hashmap growth still forces a safe miss and refresh.

Correctness: 138/138 Release tests pass (one documented skip). Focused context-value and evaluator tests pass under ASan/UBSan with leak detection disabled due to host ptrace policy.

Assessment: keep. Alternate typed-map removal cannot invalidate numeric hashmap entries, and numeric insertion/reallocation continues through the existing checked hashed setter.

Next hypothesis: Preserve numeric caches in the individual numeric setter when `KEEP_NUMBER` cleanup leaves numeric hashmap storage unchanged, targeting the remaining `set ns/op` cost.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `base_array` | 242.25 ns | 115.16 ns | -127.09 ns | -52.46% | 2.104x | keep |
| `mutate_array` | 168.49 ns | 38.58 ns | -129.91 ns | -77.10% | 4.367x | keep |
| `base_param_array` | 154.34 ns | 89.47 ns | -64.87 ns | -42.03% | 1.725x | keep |
| `mutate_param_array` | 157.53 ns | 88.35 ns | -69.18 ns | -43.92% | 1.783x | keep |
