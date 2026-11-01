# IR optimization iteration 30

Iteration: 30

Hypothesis: Individual numeric and prehashed setters should skip alternate-type cleanup when every conflicting typed map is empty, matching the proven numeric batch fast path.

Changed: `src/context/values.c` centralizes the typed-variable and typed-parameter presence checks and gates cleanup in all numeric setter paths. Nonempty typed maps retain the complete per-name replacement behavior.

Benchmark command: Nine independent runs each of `build/benchmarks/cxpr_bench_ir --context-update-paths` and `build/benchmarks/cxpr_bench_ir --param-update-paths`; every measured trial performs 500,000 update groups.

Environment: before `493fe35`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor unchanged; `perf` unavailable.

Profit now: context base setter median 173.94 ns, mutating setter 73.11 ns, and prehashed setter 76.79 ns. Parameter base setter median 81.88 ns, mutating setter 81.74 ns, and prehashed setter 69.96 ns.

Profit previous: context base setter median 318.21 ns, mutating setter 105.73 ns, and prehashed setter 109.41 ns. Parameter base setter median 132.68 ns, mutating setter 131.05 ns, and prehashed setter 126.24 ns.

Delta profit: context base -144.27 ns (-45.34%, 1.829x), mutating -32.62 ns (-30.85%, 1.446x), and prehashed -32.62 ns (-29.81%, 1.425x). Parameter base -50.80 ns (-38.29%, 1.620x), mutating -49.31 ns (-37.63%, 1.603x), and prehashed -56.28 ns (-44.58%, 1.804x).

Other key metrics: Throughput improves between 42.5% and 82.9% across the six paths. P95 latency improves between 29.08% and 46.55%. Existing type-replacement tests cover all numeric/typed transitions, including explicit batch replacement for bool, int64, string, array, and struct values.

Correctness: 138/138 Release tests pass (one documented skip). Focused context-value and evaluator tests pass under ASan/UBSan with leak detection disabled due to host ptrace policy.

Assessment: keep. The guard reads only map counts; any possibly conflicting typed binding selects the unchanged complete cleanup path.

Next hypothesis: Measure whether the remaining ordinary-setter overhead is dominated by typed-map count checks or direct-map cache lookup before adding further specialization.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| context `base set` | 318.21 ns | 173.94 ns | -144.27 ns | -45.34% | 1.829x | keep |
| context `mutate set` | 105.73 ns | 73.11 ns | -32.62 ns | -30.85% | 1.446x | keep |
| context `mutate prehashed` | 109.41 ns | 76.79 ns | -32.62 ns | -29.81% | 1.425x | keep |
| parameter `base set` | 132.68 ns | 81.88 ns | -50.80 ns | -38.29% | 1.620x | keep |
| parameter `mutate set` | 131.05 ns | 81.74 ns | -49.31 ns | -37.63% | 1.603x | keep |
| parameter `mutate prehashed` | 126.24 ns | 69.96 ns | -56.28 ns | -44.58% | 1.804x | keep |
