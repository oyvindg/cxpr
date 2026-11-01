# IR optimization iteration 29

Iteration: 29

Hypothesis: Ordinary numeric setters can avoid hashing names on a valid pointer-cache hit, closing the remaining gap to the prehashed path without changing miss behavior.

Changed: `src/context/values.c` routes ordinary context and parameter setters through the lazy-hash cached setter. The helper performs one pointer-cache lookup, then computes the hash and performs hash-cache/insertion fallback directly on a miss.

Benchmark command: Nine independent runs each of `build/benchmarks/cxpr_bench_ir --context-update-paths` and `build/benchmarks/cxpr_bench_ir --param-update-paths`; every measured trial performs 500,000 update groups.

Environment: before `d927b1e`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor unchanged; `perf` unavailable.

Profit now: context base setter median 318.21 ns and mutating setter 105.73 ns. Parameter base setter median 132.68 ns and mutating setter 131.05 ns.

Profit previous: context base setter median 353.94 ns and mutating setter 122.81 ns. Parameter base setter median 143.15 ns and mutating setter 138.87 ns.

Delta profit: context base -35.73 ns (-10.09%, 1.112x) and mutating -17.08 ns (-13.91%, 1.162x). Parameter base -10.47 ns (-7.31%, 1.079x) and mutating -7.82 ns (-5.63%, 1.060x).

Other key metrics: Throughput improves by 11.2%, 16.2%, 7.9%, and 6.0% respectively. P95 improves by 5.56%, 22.66%, 1.29%, and 4.83%. Prehashed callers retain their existing direct hashed path.

Correctness: 138/138 Release tests pass (one documented skip). Focused context-value and evaluator tests pass under ASan/UBSan with leak detection disabled due to host ptrace policy.

Assessment: keep. Cache misses retain the same hashed lookup, insertion, version update, and cache refresh sequence; hits avoid both hashing and duplicate pointer lookup.

Next hypothesis: Skip alternate-type cleanup entirely in individual numeric setters when every conflicting typed map is empty, mirroring the proven batch fast path.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| context `base set` | 353.94 ns | 318.21 ns | -35.73 ns | -10.09% | 1.112x | keep |
| context `mutate set` | 122.81 ns | 105.73 ns | -17.08 ns | -13.91% | 1.162x | keep |
| parameter `base set` | 143.15 ns | 132.68 ns | -10.47 ns | -7.31% | 1.079x | keep |
| parameter `mutate set` | 138.87 ns | 131.05 ns | -7.82 ns | -5.63% | 1.060x | keep |
