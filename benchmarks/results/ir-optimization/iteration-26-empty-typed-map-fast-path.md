# IR optimization iteration 26

Iteration: 26

Hypothesis: Numeric batch updates should skip per-name type-conflict lookups when every alternate typed map is empty, which is the common steady-state numeric workload.

Changed: `src/context/values.c` adds an empty-typed-map fast path to variable and parameter batch updates while retaining the existing conflict-removal path whenever any typed map is populated. `tests/context_values.test.c` adds batch replacement coverage for boolean, int64, string, array, and struct bindings.

Benchmark command: Nine independent runs each of `build/benchmarks/cxpr_bench_ir --context-update-paths` and `build/benchmarks/cxpr_bench_ir --param-update-paths`; every measured trial performs 500,000 batch updates.

Environment: before `5c67f4d`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor unchanged; `perf` unavailable.

Profit now: context base median 242.25 ns and mutating median 168.49 ns. Parameter base median 154.34 ns and mutating median 157.53 ns.

Profit previous: context base median 376.61 ns and mutating median 244.50 ns. Parameter base median 223.31 ns and mutating median 224.49 ns.

Delta profit: context base -134.36 ns (-35.68%, 1.555x speedup) and mutating -76.01 ns (-31.09%, 1.451x). Parameter base -68.97 ns (-30.89%, 1.447x) and mutating -66.96 ns (-29.83%, 1.425x).

Other key metrics: Throughput improves by 55.5%, 45.1%, 44.7%, and 42.5% respectively. Context base/mutating p95 improve by 36.92%/35.19%; parameter base/mutating p95 improve by 30.82%/30.16%. The nonempty typed-map branch has explicit replacement coverage for all five alternate value kinds.

Correctness: 138/138 Release tests pass (one documented skip). Focused context-value and evaluator tests pass under ASan/UBSan with leak detection disabled due to host ptrace policy.

Assessment: keep. The fast path is a count-only guard, while contexts containing any potentially conflicting typed binding continue through the complete per-name cleanup path.

Next hypothesis: Profile the remaining numeric batch time to separate name hashing from hashmap update cost before changing the batch API or storage representation.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `base_array` | 376.61 ns | 242.25 ns | -134.36 ns | -35.68% | 1.555x | keep |
| `mutate_array` | 244.50 ns | 168.49 ns | -76.01 ns | -31.09% | 1.451x | keep |
| `base_param_array` | 223.31 ns | 154.34 ns | -68.97 ns | -30.89% | 1.447x | keep |
| `mutate_param_array` | 224.49 ns | 157.53 ns | -66.96 ns | -29.83% | 1.425x | keep |
